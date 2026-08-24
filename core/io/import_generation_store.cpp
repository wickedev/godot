/**************************************************************************/
/*  import_generation_store.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including  */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "import_generation_store.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant_parser.h"

#ifdef UNIX_ENABLED
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif

namespace {

static constexpr int64_t FORMAT_VERSION = 1;
static constexpr int64_t MAX_UID_CLAIMS = 100'000;
static constexpr int64_t MAX_TOTAL_UID_CLAIMS = 1'000'000;
static constexpr uint64_t MAX_EVENT_FILE_SIZE = 64 * 1024 * 1024;
static constexpr int MAX_RESOURCE_PATH_LENGTH = 4096;

struct UIDClaimSort {
	bool operator()(const ImportGenerationStore::UIDClaim &p_left, const ImportGenerationStore::UIDClaim &p_right) const {
		if (p_left.uid == p_right.uid) {
			return p_left.epoch < p_right.epoch;
		}
		return p_left.uid < p_right.uid;
	}
};

class PublishLockCleanup {
	String lock_path;

public:
	PublishLockCleanup(const String &p_lock_path) : lock_path(p_lock_path) {}
	~PublishLockCleanup() {
		DirAccess::remove_absolute(lock_path);
	}
};

static String _variant_to_string(const Variant &p_value) {
	String value;
	VariantWriter::write_to_string(p_value, value);
	return value;
}

static bool _is_valid_transaction_id(const String &p_transaction_id) {
	if (p_transaction_id.is_empty() || p_transaction_id.length() > 64 || p_transaction_id == "." || p_transaction_id == "..") {
		return false;
	}
	for (int i = 0; i < p_transaction_id.length(); i++) {
		const char32_t c = p_transaction_id[i];
		if (!is_ascii_alphanumeric_char(c) && c != '-' && c != '_') {
			return false;
		}
	}
	return true;
}

static bool _is_valid_hash(const String &p_hash) {
	if (p_hash.length() != 64 || p_hash != p_hash.to_lower()) {
		return false;
	}
	for (int i = 0; i < p_hash.length(); i++) {
		if (!is_hex_digit(p_hash[i])) {
			return false;
		}
	}
	return true;
}

static bool _is_valid_resource_path(const String &p_path) {
	return p_path.length() > 6 && p_path.length() <= MAX_RESOURCE_PATH_LENGTH && p_path.is_resource_file() &&
			!p_path.contains("/../") && !p_path.ends_with("/..") && p_path.simplify_path() == p_path;
}

static Error _sync_file(const String &p_path) {
#ifdef UNIX_ENABLED
	const int fd = ::open(p_path.utf8().get_data(), O_RDONLY);
	if (fd < 0) {
		return ERR_CANT_OPEN;
	}
	const int sync_error = ::fsync(fd);
	::close(fd);
	return sync_error == 0 ? OK : ERR_FILE_CANT_WRITE;
#elif defined(WINDOWS_ENABLED)
	const HANDLE file = CreateFileW((LPCWSTR)p_path.utf16().get_data(), GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return ERR_CANT_OPEN;
	}
	const bool synced = FlushFileBuffers(file);
	CloseHandle(file);
	return synced ? OK : ERR_FILE_CANT_WRITE;
#else
	return OK;
#endif
}

static Error _sync_directory(const String &p_path) {
#ifdef UNIX_ENABLED
	const int fd = ::open(p_path.utf8().get_data(), O_RDONLY);
	if (fd < 0) {
		return ERR_CANT_OPEN;
	}
	const int sync_error = ::fsync(fd);
	::close(fd);
	return sync_error == 0 ? OK : ERR_FILE_CANT_WRITE;
#elif defined(WINDOWS_ENABLED)
	// FlushFileBuffers does not support directory handles. Flushing the newly
	// created file persists its data and metadata on Windows.
	return OK;
#else
	return OK;
#endif
}

static Error _compare_existing_immutable(const String &p_path, const String &p_contents) {
	Error read_error = OK;
	const String existing = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		return read_error;
	}
	if (existing != p_contents) {
		return ERR_ALREADY_EXISTS;
	}
	const Error sync_error = _sync_file(p_path);
	return sync_error == OK ? _sync_directory(p_path.get_base_dir()) : sync_error;
}

static Error _write_immutable_file(const String &p_path, const String &p_contents) {
	const Error dir_error = DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	if (dir_error != OK && dir_error != ERR_ALREADY_EXISTS) {
		return dir_error;
	}

	const String lock_path = p_path + ".publish-lock";
	const Error lock_error = DirAccess::make_dir_absolute(lock_path);
	if (lock_error != OK) {
		if (FileAccess::exists(p_path)) {
			return _compare_existing_immutable(p_path, p_contents);
		}
		return DirAccess::dir_exists_absolute(lock_path) ? ERR_BUSY : lock_error;
	}
	PublishLockCleanup lock_cleanup(lock_path);

	if (FileAccess::exists(p_path)) {
		return _compare_existing_immutable(p_path, p_contents);
	}

	const String temporary_path = p_path + vformat(".%d-%d.tmp", OS::get_singleton()->get_process_id(), OS::get_singleton()->get_ticks_usec());
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(temporary_path, FileAccess::WRITE, &open_error);
	if (file.is_null()) {
		return open_error;
	}
	if (!file->store_string(p_contents)) {
		file.unref();
		DirAccess::remove_absolute(temporary_path);
		return ERR_FILE_CANT_WRITE;
	}
	file->flush();
	const Error write_error = file->get_error();
	file.unref();
	if (write_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		return write_error;
	}

	const Error temporary_sync_error = _sync_file(temporary_path);
	if (temporary_sync_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		return temporary_sync_error;
	}
	if (FileAccess::exists(p_path)) {
		DirAccess::remove_absolute(temporary_path);
		return _compare_existing_immutable(p_path, p_contents);
	}

	const Error rename_error = DirAccess::rename_absolute(temporary_path, p_path);
	if (rename_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		return rename_error;
	}
	const Error final_sync_error = _sync_file(p_path);
	return final_sync_error == OK ? _sync_directory(p_path.get_base_dir()) : final_sync_error;
}

static Error _parse_prepared_manifest(const String &p_contents, const String &p_expected_transaction_id, ImportGenerationStore::PreparedManifest &r_manifest) {
	ConfigFile config;
	const Error parse_error = config.parse(p_contents);
	if (parse_error != OK) {
		return parse_error;
	}

	const Variant version_value = config.get_value("manifest", "format_version");
	const Variant transaction_value = config.get_value("manifest", "transaction_id");
	const Variant count_value = config.get_value("manifest", "uid_claim_count");
	if (version_value.get_type() != Variant::INT || int64_t(version_value) != FORMAT_VERSION ||
			transaction_value.get_type() != Variant::STRING || count_value.get_type() != Variant::INT) {
		return ERR_FILE_CORRUPT;
	}

	r_manifest.transaction_id = transaction_value;
	if (!_is_valid_transaction_id(r_manifest.transaction_id) ||
			(!p_expected_transaction_id.is_empty() && r_manifest.transaction_id != p_expected_transaction_id)) {
		return ERR_FILE_CORRUPT;
	}

	const int64_t claim_count = count_value;
	if (claim_count < 0 || claim_count > MAX_UID_CLAIMS) {
		return ERR_FILE_CORRUPT;
	}

	r_manifest.uid_claims.clear();
	r_manifest.uid_claims.reserve(claim_count);
	HashSet<ResourceUID::ID> manifest_uids;
	for (int64_t i = 0; i < claim_count; i++) {
		const String section = "uid_" + itos(i);
		const Variant uid_value = config.get_value(section, "uid");
		const Variant path_value = config.get_value(section, "path");
		const Variant previous_path_value = config.get_value(section, "previous_path");
		const Variant epoch_value = config.get_value(section, "epoch");
		if (uid_value.get_type() != Variant::INT || path_value.get_type() != Variant::STRING ||
				previous_path_value.get_type() != Variant::STRING || epoch_value.get_type() != Variant::INT) {
			return ERR_FILE_CORRUPT;
		}

		ImportGenerationStore::UIDClaim claim;
		claim.uid = uid_value;
		claim.path = path_value;
		claim.previous_path = previous_path_value;
		claim.epoch = epoch_value;
		if (claim.uid == ResourceUID::INVALID_ID || claim.uid < 0 || claim.epoch <= 0 ||
				!_is_valid_resource_path(claim.path) ||
				(!claim.previous_path.is_empty() && !_is_valid_resource_path(claim.previous_path)) ||
				manifest_uids.has(claim.uid)) {
			return ERR_FILE_CORRUPT;
		}
		manifest_uids.insert(claim.uid);
		r_manifest.uid_claims.push_back(claim);
	}

	if (ImportGenerationStore::serialize_prepared_manifest(r_manifest) != p_contents) {
		return ERR_FILE_CORRUPT;
	}
	return OK;
}

static Error _parse_commit_event(const String &p_contents, const String &p_expected_transaction_id, String &r_manifest_sha256) {
	ConfigFile config;
	const Error parse_error = config.parse(p_contents);
	if (parse_error != OK) {
		return parse_error;
	}

	const Variant version_value = config.get_value("commit", "format_version");
	const Variant transaction_value = config.get_value("commit", "transaction_id");
	const Variant hash_value = config.get_value("commit", "manifest_sha256");
	if (version_value.get_type() != Variant::INT || int64_t(version_value) != FORMAT_VERSION ||
			transaction_value.get_type() != Variant::STRING || hash_value.get_type() != Variant::STRING) {
		return ERR_FILE_CORRUPT;
	}

	const String transaction_id = transaction_value;
	r_manifest_sha256 = hash_value;
	if (!_is_valid_transaction_id(transaction_id) || transaction_id != p_expected_transaction_id || !_is_valid_hash(r_manifest_sha256) ||
			ImportGenerationStore::serialize_commit_event(transaction_id, r_manifest_sha256) != p_contents) {
		return ERR_FILE_CORRUPT;
	}
	return OK;
}

} // namespace

String ImportGenerationStore::get_transactions_path(const String &p_project_data_path) {
	const String project_data_path = p_project_data_path.is_empty() ? ProjectSettings::get_singleton()->get_project_data_path() : p_project_data_path;
	return project_data_path.path_join("transactions");
}

String ImportGenerationStore::get_events_path(const String &p_project_data_path) {
	const String project_data_path = p_project_data_path.is_empty() ? ProjectSettings::get_singleton()->get_project_data_path() : p_project_data_path;
	return project_data_path.path_join("events");
}

String ImportGenerationStore::serialize_prepared_manifest(const PreparedManifest &p_manifest) {
	Vector<UIDClaim> claims = p_manifest.uid_claims;
	claims.sort_custom<UIDClaimSort>();

	String contents;
	contents += "[manifest]\n\n";
	contents += "format_version=" + itos(FORMAT_VERSION) + "\n";
	contents += "transaction_id=" + _variant_to_string(p_manifest.transaction_id) + "\n";
	contents += "uid_claim_count=" + itos(claims.size()) + "\n";
	for (int i = 0; i < claims.size(); i++) {
		const UIDClaim &claim = claims[i];
		contents += "\n[uid_" + itos(i) + "]\n\n";
		contents += "uid=" + itos(claim.uid) + "\n";
		contents += "path=" + _variant_to_string(claim.path) + "\n";
		contents += "previous_path=" + _variant_to_string(claim.previous_path) + "\n";
		contents += "epoch=" + itos(claim.epoch) + "\n";
	}
	return contents;
}

String ImportGenerationStore::serialize_commit_event(const String &p_transaction_id, const String &p_manifest_sha256) {
	String contents;
	contents += "[commit]\n\n";
	contents += "format_version=" + itos(FORMAT_VERSION) + "\n";
	contents += "transaction_id=" + _variant_to_string(p_transaction_id) + "\n";
	contents += "manifest_sha256=" + _variant_to_string(p_manifest_sha256) + "\n";
	return contents;
}

Error ImportGenerationStore::write_prepared_manifest(const String &p_project_data_path, const PreparedManifest &p_manifest, String *r_manifest_sha256) {
	if (!_is_valid_transaction_id(p_manifest.transaction_id)) {
		return ERR_INVALID_PARAMETER;
	}

	const String contents = serialize_prepared_manifest(p_manifest);
	PreparedManifest validated_manifest;
	const Error validation_error = _parse_prepared_manifest(contents, p_manifest.transaction_id, validated_manifest);
	if (validation_error != OK) {
		return validation_error;
	}

	const String path = get_transactions_path(p_project_data_path).path_join(p_manifest.transaction_id + ".prepared");
	const Error write_error = _write_immutable_file(path, contents);
	if (write_error != OK) {
		return write_error;
	}
	if (r_manifest_sha256) {
		*r_manifest_sha256 = contents.sha256_text();
	}
	return OK;
}

Error ImportGenerationStore::commit_prepared_manifest(const String &p_project_data_path, const String &p_transaction_id) {
	if (!_is_valid_transaction_id(p_transaction_id)) {
		return ERR_INVALID_PARAMETER;
	}

	const String prepared_path = get_transactions_path(p_project_data_path).path_join(p_transaction_id + ".prepared");
	if (!FileAccess::exists(prepared_path)) {
		return ERR_FILE_NOT_FOUND;
	}

	Error read_error = OK;
	const String prepared_contents = FileAccess::get_file_as_string(prepared_path, &read_error);
	if (read_error != OK) {
		return read_error;
	}
	PreparedManifest manifest;
	const Error parse_error = _parse_prepared_manifest(prepared_contents, p_transaction_id, manifest);
	if (parse_error != OK) {
		return parse_error;
	}

	const String manifest_sha256 = prepared_contents.sha256_text();
	if (!_is_valid_hash(manifest_sha256)) {
		return ERR_FILE_CORRUPT;
	}
	const String event_path = get_events_path(p_project_data_path).path_join(p_transaction_id + ".commit");
	return _write_immutable_file(event_path, serialize_commit_event(p_transaction_id, manifest_sha256));
}

Error ImportGenerationStore::replay_uid_events(const String &p_project_data_path, bool p_update_cache) {
	const String events_path = get_events_path(p_project_data_path);
	if (!DirAccess::dir_exists_absolute(events_path)) {
		return OK;
	}

	Error directory_error = OK;
	Ref<DirAccess> events_directory = DirAccess::open(events_path, &directory_error);
	if (events_directory.is_null()) {
		return directory_error;
	}
	const Error list_error = events_directory->list_dir_begin();
	if (list_error != OK) {
		return list_error;
	}
	Vector<String> event_files;
	for (String event_file = events_directory->get_next(); !event_file.is_empty(); event_file = events_directory->get_next()) {
		if (!events_directory->current_is_dir() && event_file.ends_with(".commit")) {
			event_files.push_back(event_file);
		}
	}
	events_directory->list_dir_end();
	event_files.sort();

	int64_t total_uid_claims = 0;
	HashMap<ResourceUID::ID, Vector<UIDClaim>> claims_by_uid;
	for (const String &event_file : event_files) {
		const String transaction_id = event_file.left(event_file.length() - String(".commit").length());
		if (!_is_valid_transaction_id(transaction_id)) {
			return ERR_FILE_CORRUPT;
		}

		const String event_path = events_path.path_join(event_file);
		if (FileAccess::get_size(event_path) > MAX_EVENT_FILE_SIZE) {
			return ERR_FILE_CORRUPT;
		}
		Error read_error = OK;
		const String event_contents = FileAccess::get_file_as_string(event_path, &read_error);
		if (read_error != OK) {
			return read_error;
		}
		String expected_manifest_sha256;
		const Error event_error = _parse_commit_event(event_contents, transaction_id, expected_manifest_sha256);
		if (event_error != OK) {
			return event_error;
		}

		const String prepared_path = get_transactions_path(p_project_data_path).path_join(transaction_id + ".prepared");
		if (!FileAccess::exists(prepared_path) || FileAccess::get_size(prepared_path) > MAX_EVENT_FILE_SIZE) {
			return ERR_FILE_CORRUPT;
		}
		const String prepared_contents = FileAccess::get_file_as_string(prepared_path, &read_error);
		if (read_error != OK) {
			return read_error;
		}
		if (prepared_contents.sha256_text() != expected_manifest_sha256) {
			return ERR_FILE_CORRUPT;
		}
		PreparedManifest manifest;
		const Error manifest_error = _parse_prepared_manifest(prepared_contents, transaction_id, manifest);
		if (manifest_error != OK) {
			return manifest_error;
		}
		total_uid_claims += manifest.uid_claims.size();
		if (total_uid_claims > MAX_TOTAL_UID_CLAIMS) {
			return ERR_OUT_OF_MEMORY;
		}
		for (const UIDClaim &claim : manifest.uid_claims) {
			claims_by_uid[claim.uid].push_back(claim);
		}
	}

	ResourceUID *resource_uid = ResourceUID::get_singleton();
	HashMap<ResourceUID::ID, String> final_mappings;
	for (KeyValue<ResourceUID::ID, Vector<UIDClaim>> &E : claims_by_uid) {
		Vector<UIDClaim> &claims = E.value;
		claims.sort_custom<UIDClaimSort>();
		for (int i = 1; i < claims.size(); i++) {
			if (claims[i].epoch <= claims[i - 1].epoch || claims[i].previous_path != claims[i - 1].path) {
				return ERR_FILE_CORRUPT;
			}
		}
		final_mappings[E.key] = claims[claims.size() - 1].path;
	}

	HashMap<String, ResourceUID::ID> final_path_owners;
	for (const KeyValue<ResourceUID::ID, String> &E : resource_uid->get_id_map()) {
		if (final_mappings.has(E.key)) {
			continue;
		}
		const ResourceUID::ID *owner = final_path_owners.getptr(E.value);
		if (owner && *owner != E.key) {
			return ERR_FILE_CORRUPT;
		}
		final_path_owners[E.value] = E.key;
	}
	for (const KeyValue<ResourceUID::ID, String> &E : final_mappings) {
		const ResourceUID::ID *owner = final_path_owners.getptr(E.value);
		if (owner && *owner != E.key) {
			return ERR_FILE_CORRUPT;
		}
		final_path_owners[E.value] = E.key;
	}

	for (const KeyValue<ResourceUID::ID, String> &E : final_mappings) {
		if (!resource_uid->has_id(E.key)) {
			resource_uid->add_id(E.key, E.value);
		} else if (resource_uid->get_id_path(E.key) != E.value) {
			resource_uid->set_id(E.key, E.value);
		}
	}

	return p_update_cache && !final_mappings.is_empty() ? resource_uid->update_cache() : OK;
}
