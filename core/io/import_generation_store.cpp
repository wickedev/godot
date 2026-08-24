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
#include <signal.h>
#include <unistd.h>
#endif

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif

namespace {

static constexpr int64_t FORMAT_VERSION = 2;
static constexpr int64_t MAX_RESOURCE_GENERATIONS = 100'000;
static constexpr int64_t LEASE_TIMEOUT_SECONDS = 60;
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

struct ResourceGenerationSort {
	bool operator()(const ImportGenerationStore::ResourceGeneration &p_left, const ImportGenerationStore::ResourceGeneration &p_right) const {
		if (p_left.resource_key == p_right.resource_key) {
			return p_left.epoch < p_right.epoch;
		}
		return p_left.resource_key < p_right.resource_key;
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

static bool _is_valid_name_component(const String &p_name) {
	if (p_name.is_empty() || p_name.length() > 255 || p_name == "." || p_name == "..") {
		return false;
	}
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t c = p_name[i];
		if (c == '/' || c == '\\' || c == ':' || c < 32) {
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

static String _globalize(const String &p_path) {
	// The raw sync calls below go straight to the OS, so `res://` has to be resolved first.
	return ProjectSettings::get_singleton() ? ProjectSettings::get_singleton()->globalize_path(p_path) : p_path;
}

static Error _sync_file(const String &p_path) {
#ifdef UNIX_ENABLED
	const int fd = ::open(_globalize(p_path).utf8().get_data(), O_RDONLY);
	if (fd < 0) {
		return ERR_CANT_OPEN;
	}
	const int sync_error = ::fsync(fd);
	::close(fd);
	return sync_error == 0 ? OK : ERR_FILE_CANT_WRITE;
#elif defined(WINDOWS_ENABLED)
	const HANDLE file = CreateFileW((LPCWSTR)_globalize(p_path).utf16().get_data(), GENERIC_READ | GENERIC_WRITE,
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
	const int fd = ::open(_globalize(p_path).utf8().get_data(), O_RDONLY);
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
	const Variant resource_count_value = config.get_value("manifest", "resource_generation_count");
	if (version_value.get_type() != Variant::INT) {
		return ERR_FILE_CORRUPT;
	}
	if (int64_t(version_value) > 0 && int64_t(version_value) < FORMAT_VERSION) {
		// Written by an older build of this fork. Skipping is safe: the canonical `.import`
		// sidecars still carry the UIDs, so a rescan rebuilds whatever the event would have replayed.
		return ERR_SKIP;
	}
	if (int64_t(version_value) != FORMAT_VERSION || transaction_value.get_type() != Variant::STRING ||
			count_value.get_type() != Variant::INT || resource_count_value.get_type() != Variant::INT) {
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

	const int64_t resource_generation_count = resource_count_value;
	if (resource_generation_count < 0 || resource_generation_count > MAX_RESOURCE_GENERATIONS) {
		return ERR_FILE_CORRUPT;
	}

	r_manifest.resource_generations.clear();
	r_manifest.resource_generations.reserve(resource_generation_count);
	HashSet<String> manifest_resource_keys;
	for (int64_t i = 0; i < resource_generation_count; i++) {
		const String section = "resource_" + itos(i);
		const Variant source_value = config.get_value(section, "source_path");
		const Variant key_value = config.get_value(section, "resource_key");
		const Variant generation_value = config.get_value(section, "generation_id");
		const Variant epoch_value = config.get_value(section, "epoch");
		if (source_value.get_type() != Variant::STRING || key_value.get_type() != Variant::STRING ||
				generation_value.get_type() != Variant::STRING || epoch_value.get_type() != Variant::INT) {
			return ERR_FILE_CORRUPT;
		}

		ImportGenerationStore::ResourceGeneration resource_generation;
		resource_generation.source_path = source_value;
		resource_generation.resource_key = key_value;
		resource_generation.generation_id = generation_value;
		resource_generation.epoch = epoch_value;
		if (!_is_valid_resource_path(resource_generation.source_path) ||
				!_is_valid_name_component(resource_generation.resource_key) ||
				!_is_valid_transaction_id(resource_generation.generation_id) ||
				resource_generation.epoch <= 0 ||
				resource_generation.resource_key != ImportGenerationStore::get_resource_key(resource_generation.source_path) ||
				manifest_resource_keys.has(resource_generation.resource_key)) {
			return ERR_FILE_CORRUPT;
		}
		manifest_resource_keys.insert(resource_generation.resource_key);
		r_manifest.resource_generations.push_back(resource_generation);
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
	if (version_value.get_type() != Variant::INT) {
		return ERR_FILE_CORRUPT;
	}
	if (int64_t(version_value) > 0 && int64_t(version_value) < FORMAT_VERSION) {
		return ERR_SKIP;
	}
	if (int64_t(version_value) != FORMAT_VERSION || transaction_value.get_type() != Variant::STRING ||
			hash_value.get_type() != Variant::STRING) {
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
	Vector<ResourceGeneration> resource_generations = p_manifest.resource_generations;
	resource_generations.sort_custom<ResourceGenerationSort>();

	String contents;
	contents += "[manifest]\n\n";
	contents += "format_version=" + itos(FORMAT_VERSION) + "\n";
	contents += "transaction_id=" + _variant_to_string(p_manifest.transaction_id) + "\n";
	contents += "uid_claim_count=" + itos(claims.size()) + "\n";
	contents += "resource_generation_count=" + itos(resource_generations.size()) + "\n";
	for (int i = 0; i < claims.size(); i++) {
		const UIDClaim &claim = claims[i];
		contents += "\n[uid_" + itos(i) + "]\n\n";
		contents += "uid=" + itos(claim.uid) + "\n";
		contents += "path=" + _variant_to_string(claim.path) + "\n";
		contents += "previous_path=" + _variant_to_string(claim.previous_path) + "\n";
		contents += "epoch=" + itos(claim.epoch) + "\n";
	}
	for (int i = 0; i < resource_generations.size(); i++) {
		const ResourceGeneration &resource_generation = resource_generations[i];
		contents += "\n[resource_" + itos(i) + "]\n\n";
		contents += "source_path=" + _variant_to_string(resource_generation.source_path) + "\n";
		contents += "resource_key=" + _variant_to_string(resource_generation.resource_key) + "\n";
		contents += "generation_id=" + _variant_to_string(resource_generation.generation_id) + "\n";
		contents += "epoch=" + itos(resource_generation.epoch) + "\n";
	}
	return contents;
}

String ImportGenerationStore::serialize_selector(const Selector &p_selector) {
	Vector<String> file_names = p_selector.file_names;
	file_names.sort();

	Array names;
	for (const String &file_name : file_names) {
		names.push_back(file_name);
	}

	String contents;
	contents += "[selector]\n\n";
	contents += "format_version=" + itos(FORMAT_VERSION) + "\n";
	contents += "resource_key=" + _variant_to_string(p_selector.resource_key) + "\n";
	contents += "generation_id=" + _variant_to_string(p_selector.generation_id) + "\n";
	contents += "epoch=" + itos(p_selector.epoch) + "\n";
	contents += "file_names=" + _variant_to_string(names) + "\n";
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
		if (event_error == ERR_SKIP) {
			continue;
		}
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
		if (manifest_error == ERR_SKIP) {
			continue;
		}
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
	HashMap<ResourceUID::ID, UIDClaim> final_claims;
	for (KeyValue<ResourceUID::ID, Vector<UIDClaim>> &E : claims_by_uid) {
		Vector<UIDClaim> &claims = E.value;
		claims.sort_custom<UIDClaimSort>();
		for (int i = 1; i < claims.size(); i++) {
			if (claims[i].path == claims[i - 1].path && claims[i].previous_path == claims[i - 1].previous_path) {
				// Two editor sessions reimported the same resource and claimed the same UID for
				// the same path. The outcome is identical either way, so ordering is irrelevant.
				continue;
			}
			if (claims[i].epoch <= claims[i - 1].epoch || claims[i].previous_path != claims[i - 1].path) {
				return ERR_FILE_CORRUPT;
			}
		}
		final_mappings[E.key] = claims[claims.size() - 1].path;
		final_claims[E.key] = claims[claims.size() - 1];
	}

	// Remember where each UID chain ended, so a claim made by this process continues it
	// instead of starting a second chain that replay would reject as corrupt.
	{
		MutexLock lock(state_mutex);
		for (const KeyValue<ResourceUID::ID, UIDClaim> &E : final_claims) {
			const UIDClaim *known = last_uid_claims.getptr(E.key);
			if (!known || known->epoch < E.value.epoch) {
				last_uid_claims[E.key] = E.value;
			}
		}
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

Mutex ImportGenerationStore::state_mutex;
HashMap<String, ImportGenerationStore::ResolutionEntry> ImportGenerationStore::resolution_cache;
HashMap<ResourceUID::ID, ImportGenerationStore::UIDClaim> ImportGenerationStore::last_uid_claims;
HashMap<String, ImportGenerationStore::OwnedLease> ImportGenerationStore::owned_resource_leases;

String ImportGenerationStore::get_generations_path(const String &p_project_data_path) {
	const String project_data_path = p_project_data_path.is_empty() ? ProjectSettings::get_singleton()->get_project_data_path() : p_project_data_path;
	return project_data_path.path_join("generations");
}

String ImportGenerationStore::get_resource_locks_path(const String &p_project_data_path) {
	const String project_data_path = p_project_data_path.is_empty() ? ProjectSettings::get_singleton()->get_project_data_path() : p_project_data_path;
	return project_data_path.path_join("locks").path_join("resources");
}

String ImportGenerationStore::get_resource_key(const String &p_source_file) {
	// Same identity the legacy import cache uses, so one key means one `res://` source file.
	return p_source_file.get_file() + "-" + p_source_file.md5_text();
}

String ImportGenerationStore::get_resource_generations_path(const String &p_resource_key, const String &p_project_data_path) {
	return get_generations_path(p_project_data_path).path_join(p_resource_key);
}

String ImportGenerationStore::get_generation_path(const String &p_resource_key, const String &p_generation_id, const String &p_project_data_path) {
	return get_resource_generations_path(p_resource_key, p_project_data_path).path_join(p_generation_id);
}

String ImportGenerationStore::get_selector_path(const String &p_resource_key, const String &p_project_data_path) {
	return get_resource_generations_path(p_resource_key, p_project_data_path).path_join("current.cfg");
}

static Error _parse_selector(const String &p_contents, ImportGenerationStore::Selector &r_selector) {
	ConfigFile config;
	const Error parse_error = config.parse(p_contents);
	if (parse_error != OK) {
		return parse_error;
	}

	const Variant version_value = config.get_value("selector", "format_version");
	const Variant key_value = config.get_value("selector", "resource_key");
	const Variant generation_value = config.get_value("selector", "generation_id");
	const Variant epoch_value = config.get_value("selector", "epoch");
	const Variant names_value = config.get_value("selector", "file_names");
	if (version_value.get_type() != Variant::INT) {
		return ERR_FILE_CORRUPT;
	}
	if (int64_t(version_value) > 0 && int64_t(version_value) < FORMAT_VERSION) {
		return ERR_SKIP;
	}
	if (int64_t(version_value) != FORMAT_VERSION || key_value.get_type() != Variant::STRING ||
			generation_value.get_type() != Variant::STRING || epoch_value.get_type() != Variant::INT ||
			names_value.get_type() != Variant::ARRAY) {
		return ERR_FILE_CORRUPT;
	}

	r_selector.resource_key = key_value;
	r_selector.generation_id = generation_value;
	r_selector.epoch = epoch_value;
	r_selector.file_names.clear();
	if (!_is_valid_name_component(r_selector.resource_key) || !_is_valid_transaction_id(r_selector.generation_id) || r_selector.epoch <= 0) {
		return ERR_FILE_CORRUPT;
	}

	const Array names = names_value;
	for (const Variant &name_value : names) {
		if (name_value.get_type() != Variant::STRING) {
			return ERR_FILE_CORRUPT;
		}
		const String name = name_value;
		if (!_is_valid_name_component(name)) {
			return ERR_FILE_CORRUPT;
		}
		r_selector.file_names.push_back(name);
	}
	return OK;
}

bool ImportGenerationStore::read_selector(const String &p_resource_key, Selector &r_selector, const String &p_project_data_path) {
	const String selector_path = get_selector_path(p_resource_key, p_project_data_path);
	if (!FileAccess::exists(selector_path)) {
		return false;
	}
	Error read_error = OK;
	const String contents = FileAccess::get_file_as_string(selector_path, &read_error);
	if (read_error != OK) {
		return false;
	}
	return _parse_selector(contents, r_selector) == OK && r_selector.resource_key == p_resource_key;
}

Error ImportGenerationStore::write_selector(const Selector &p_selector, const String &p_project_data_path) {
	if (!_is_valid_name_component(p_selector.resource_key) || !_is_valid_transaction_id(p_selector.generation_id) || p_selector.epoch <= 0) {
		return ERR_INVALID_PARAMETER;
	}

	const String selector_path = get_selector_path(p_selector.resource_key, p_project_data_path);
	const Error dir_error = DirAccess::make_dir_recursive_absolute(selector_path.get_base_dir());
	if (dir_error != OK && dir_error != ERR_ALREADY_EXISTS) {
		return dir_error;
	}

	const String contents = serialize_selector(p_selector);
	Selector validated;
	const Error validation_error = _parse_selector(contents, validated);
	if (validation_error != OK) {
		return validation_error;
	}

	// The selector is the only mutable file in the canonical tree, so it is replaced by
	// rename: a concurrent reader sees either the previous or the next selector, never a
	// partially written one.
	const String temporary_path = selector_path + vformat(".%d-%d.tmp", OS::get_singleton()->get_process_id(), OS::get_singleton()->get_ticks_usec());
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(temporary_path, FileAccess::WRITE, &open_error);
	if (file.is_null()) {
		return open_error;
	}
	if (!file->store_string(contents)) {
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
	const Error sync_error = _sync_file(temporary_path);
	if (sync_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		return sync_error;
	}

	const Error rename_error = DirAccess::rename_absolute(temporary_path, selector_path);
	if (rename_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		return rename_error;
	}
	_sync_directory(selector_path.get_base_dir());
	invalidate_resolution_cache();
	return OK;
}

Error ImportGenerationStore::clear_selector(const String &p_resource_key, const String &p_project_data_path) {
	const String selector_path = get_selector_path(p_resource_key, p_project_data_path);
	if (!FileAccess::exists(selector_path)) {
		return OK;
	}
	const Error remove_error = DirAccess::remove_absolute(selector_path);
	invalidate_resolution_cache();
	return remove_error;
}

Error ImportGenerationStore::publish_generation(const String &p_resource_key, const String &p_generation_id, const String &p_staging_path, Vector<String> *r_published_files, const String &p_project_data_path) {
	if (!_is_valid_name_component(p_resource_key) || !_is_valid_transaction_id(p_generation_id)) {
		return ERR_INVALID_PARAMETER;
	}

	const String generation_path = get_generation_path(p_resource_key, p_generation_id, p_project_data_path);
	if (DirAccess::dir_exists_absolute(generation_path)) {
		// Already published, so the staged copy is redundant. A generation directory never
		// changes once it exists, which is what lets other editors read it without locking.
		Ref<DirAccess> staging = DirAccess::open(p_staging_path);
		if (staging.is_valid()) {
			staging->erase_contents_recursive();
			DirAccess::remove_absolute(p_staging_path);
		}
	} else {
		if (!DirAccess::dir_exists_absolute(p_staging_path)) {
			return ERR_FILE_NOT_FOUND;
		}
		const Error dir_error = DirAccess::make_dir_recursive_absolute(generation_path.get_base_dir());
		if (dir_error != OK && dir_error != ERR_ALREADY_EXISTS) {
			return dir_error;
		}
		// A generation directory is immutable once it exists, so moving the staged
		// directory into place is the whole publish step.
		const Error rename_error = DirAccess::rename_absolute(p_staging_path, generation_path);
		if (rename_error != OK) {
			return rename_error;
		}
		_sync_directory(generation_path);
		_sync_directory(generation_path.get_base_dir());
	}

	if (r_published_files) {
		r_published_files->clear();
		for (const String &file_name : DirAccess::get_files_at(generation_path)) {
			r_published_files->push_back(file_name);
		}
	}
	return OK;
}

Error ImportGenerationStore::repair_selectors(const String &p_project_data_path) {
	const String events_path = get_events_path(p_project_data_path);
	if (!DirAccess::dir_exists_absolute(events_path)) {
		return OK;
	}

	Vector<String> event_files;
	for (const String &event_file : DirAccess::get_files_at(events_path)) {
		if (event_file.ends_with(".commit")) {
			event_files.push_back(event_file);
		}
	}
	event_files.sort();

	HashMap<String, ResourceGeneration> newest_by_key;
	for (const String &event_file : event_files) {
		const String transaction_id = event_file.left(event_file.length() - String(".commit").length());
		for (const ResourceGeneration &resource_generation : read_committed_resources(transaction_id, p_project_data_path)) {
			const ResourceGeneration *known = newest_by_key.getptr(resource_generation.resource_key);
			if (!known || known->epoch < resource_generation.epoch) {
				newest_by_key[resource_generation.resource_key] = resource_generation;
			}
		}
	}

	for (const KeyValue<String, ResourceGeneration> &E : newest_by_key) {
		const ResourceGeneration &resource_generation = E.value;
		const String generation_path = get_generation_path(resource_generation.resource_key, resource_generation.generation_id, p_project_data_path);
		if (!DirAccess::dir_exists_absolute(generation_path)) {
			// Committed but never published: the importer died between the two steps.
			// Leaving the selector alone keeps the previous completed generation visible.
			continue;
		}

		Selector current;
		if (read_selector(resource_generation.resource_key, current, p_project_data_path) && current.epoch >= resource_generation.epoch) {
			continue;
		}

		Selector selector;
		selector.resource_key = resource_generation.resource_key;
		selector.generation_id = resource_generation.generation_id;
		selector.epoch = resource_generation.epoch;
		for (const String &file_name : DirAccess::get_files_at(generation_path)) {
			selector.file_names.push_back(file_name);
		}
		const Error write_error = write_selector(selector, p_project_data_path);
		if (write_error != OK) {
			ERR_PRINT(vformat("Could not publish import generation '%s' for '%s'.", resource_generation.generation_id, resource_generation.source_path));
		}
	}
	return OK;
}

String ImportGenerationStore::resolve_artifact_path(const String &p_source_file, const String &p_logical_path) {
	if (p_logical_path.is_empty() || p_source_file.is_empty() || !ProjectSettings::get_singleton()) {
		return p_logical_path;
	}

	const String resource_key = get_resource_key(p_source_file);
	const String selector_path = get_selector_path(resource_key);

	MutexLock lock(state_mutex);
	ResolutionEntry *entry = resolution_cache.getptr(resource_key);
	const bool selector_exists = FileAccess::exists(selector_path);
	const uint64_t modified_time = selector_exists ? FileAccess::get_modified_time(selector_path) : 0;
	if (!entry || entry->selector_modified_time != modified_time) {
		ResolutionEntry fresh;
		fresh.selector_modified_time = modified_time;
		Selector selector;
		if (selector_exists && read_selector(resource_key, selector)) {
			fresh.valid = true;
			fresh.generation_path = get_generation_path(resource_key, selector.generation_id);
			for (const String &file_name : selector.file_names) {
				fresh.file_names.insert(file_name);
			}
		}
		resolution_cache[resource_key] = fresh;
		entry = resolution_cache.getptr(resource_key);
	}

	if (!entry->valid) {
		return p_logical_path;
	}
	const String file_name = p_logical_path.get_file();
	if (!entry->file_names.has(file_name)) {
		return p_logical_path;
	}
	return entry->generation_path.path_join(file_name);
}

void ImportGenerationStore::invalidate_resolution_cache() {
	MutexLock lock(state_mutex);
	resolution_cache.clear();
}

ImportGenerationStore::UIDClaim ImportGenerationStore::make_uid_claim(ResourceUID::ID p_uid, const String &p_path) {
	MutexLock lock(state_mutex);
	UIDClaim claim;
	claim.uid = p_uid;
	claim.path = p_path;

	// Sub-second entropy keeps two editors that commit within the same second from minting
	// the same epoch for the same UID.
	const int64_t now = int64_t(OS::get_singleton()->get_unix_time()) * 1'000'000 + int64_t(OS::get_singleton()->get_ticks_usec() % 1'000'000);
	const UIDClaim *previous = last_uid_claims.getptr(p_uid);
	if (previous) {
		claim.previous_path = previous->path;
		claim.epoch = MAX(now, previous->epoch + 1);
	} else {
		claim.epoch = MAX(now, int64_t(1));
	}
	return claim;
}

void ImportGenerationStore::note_uid_claims(const Vector<UIDClaim> &p_claims) {
	MutexLock lock(state_mutex);
	for (const UIDClaim &claim : p_claims) {
		const UIDClaim *previous = last_uid_claims.getptr(claim.uid);
		if (!previous || previous->epoch < claim.epoch) {
			last_uid_claims[claim.uid] = claim;
		}
	}
}

static bool _is_process_alive(int64_t p_process_id) {
	// `OS::is_process_running()` only answers for processes this one spawned, and the editor
	// holding a lease is a sibling, so probe the process directly.
	if (p_process_id <= 0) {
		return true; // Unknown owner: fall back to the heartbeat timeout alone.
	}
#ifdef UNIX_ENABLED
	return ::kill((pid_t)p_process_id, 0) == 0 || errno == EPERM;
#elif defined(WINDOWS_ENABLED)
	const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)p_process_id);
	if (!process) {
		return false;
	}
	const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
	CloseHandle(process);
	return alive;
#else
	return true;
#endif
}

static String _resource_lease_path(const String &p_resource_key, const String &p_project_data_path) {
	return ImportGenerationStore::get_resource_locks_path(p_project_data_path).path_join(p_resource_key + ".lock");
}

static String _serialize_lease(const String &p_owner, const String &p_token) {
	String contents;
	contents += "[lease]\n\n";
	contents += "format_version=" + itos(FORMAT_VERSION) + "\n";
	contents += "owner=" + _variant_to_string(p_owner) + "\n";
	contents += "token=" + _variant_to_string(p_token) + "\n";
	contents += "process_id=" + itos(OS::get_singleton()->get_process_id()) + "\n";
	contents += "heartbeat_unix=" + itos(int64_t(OS::get_singleton()->get_unix_time())) + "\n";
	return contents;
}

static bool _read_lease(const String &p_lease_path, String &r_owner, String &r_token, int64_t &r_heartbeat_unix, int64_t &r_process_id) {
	const String owner_path = p_lease_path.path_join("owner.cfg");
	if (!FileAccess::exists(owner_path)) {
		return false;
	}
	Error read_error = OK;
	const String contents = FileAccess::get_file_as_string(owner_path, &read_error);
	if (read_error != OK) {
		return false;
	}
	ConfigFile config;
	if (config.parse(contents) != OK) {
		return false;
	}
	const Variant owner_value = config.get_value("lease", "owner");
	const Variant token_value = config.get_value("lease", "token");
	const Variant heartbeat_value = config.get_value("lease", "heartbeat_unix");
	const Variant process_value = config.get_value("lease", "process_id");
	if (owner_value.get_type() != Variant::STRING || token_value.get_type() != Variant::STRING || heartbeat_value.get_type() != Variant::INT) {
		return false;
	}
	r_owner = owner_value;
	r_token = token_value;
	r_heartbeat_unix = heartbeat_value;
	r_process_id = process_value.get_type() == Variant::INT ? int64_t(process_value) : 0;
	return true;
}

// A lease only holds off a peer while the editor that took it is demonstrably alive.
static bool _is_lease_live(int64_t p_heartbeat_unix, int64_t p_process_id) {
	const int64_t now = int64_t(OS::get_singleton()->get_unix_time());
	return now - p_heartbeat_unix < LEASE_TIMEOUT_SECONDS && _is_process_alive(p_process_id);
}

static Error _write_lease(const String &p_lease_path, const String &p_owner, const String &p_token) {
	const String owner_path = p_lease_path.path_join("owner.cfg");
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(owner_path, FileAccess::WRITE, &open_error);
	if (file.is_null()) {
		return open_error;
	}
	file->store_string(_serialize_lease(p_owner, p_token));
	file->flush();
	const Error write_error = file->get_error();
	file.unref();
	return write_error;
}

bool ImportGenerationStore::acquire_resource_lease(const String &p_resource_key, const String &p_owner, const String &p_project_data_path) {
	if (!_is_valid_name_component(p_resource_key) || p_owner.is_empty()) {
		return false;
	}

	{
		MutexLock lock(state_mutex);
		const OwnedLease *owned = owned_resource_leases.getptr(p_resource_key);
		if (owned && owned->owner == p_owner) {
			return true;
		}
	}

	const String lease_path = _resource_lease_path(p_resource_key, p_project_data_path);
	const Error dir_error = DirAccess::make_dir_recursive_absolute(get_resource_locks_path(p_project_data_path));
	if (dir_error != OK && dir_error != ERR_ALREADY_EXISTS) {
		return false;
	}

	const String token = p_owner + "-" + itos(OS::get_singleton()->get_process_id()) + "-" + itos(OS::get_singleton()->get_ticks_usec());
	const Error create_error = DirAccess::make_dir_absolute(lease_path);
	if (create_error != OK) {
		String owner;
		String existing_token;
		int64_t heartbeat_unix = 0;
		int64_t process_id = 0;
		const bool readable = _read_lease(lease_path, owner, existing_token, heartbeat_unix, process_id);
		if (readable && owner != p_owner && _is_lease_live(heartbeat_unix, process_id)) {
			return false;
		}
		// Either ours from a previous run, unreadable, or abandoned by a dead editor.
	}

	if (_write_lease(lease_path, p_owner, token) != OK) {
		return false;
	}
	// Confirm the write survived: two editors can decide to take over a stale lease at
	// the same time, and only the last writer actually owns it.
	String owner;
	String stored_token;
	int64_t heartbeat_unix = 0;
	int64_t process_id = 0;
	if (!_read_lease(lease_path, owner, stored_token, heartbeat_unix, process_id) || owner != p_owner || stored_token != token) {
		return false;
	}

	MutexLock lock(state_mutex);
	OwnedLease owned;
	owned.owner = p_owner;
	owned.token = token;
	owned_resource_leases[p_resource_key] = owned;
	return true;
}

void ImportGenerationStore::release_resource_lease(const String &p_resource_key, const String &p_owner, const String &p_project_data_path) {
	{
		MutexLock lock(state_mutex);
		const OwnedLease *owned = owned_resource_leases.getptr(p_resource_key);
		if (!owned || owned->owner != p_owner) {
			return;
		}
		owned_resource_leases.erase(p_resource_key);
	}

	const String lease_path = _resource_lease_path(p_resource_key, p_project_data_path);
	DirAccess::remove_absolute(lease_path.path_join("owner.cfg"));
	DirAccess::remove_absolute(lease_path);
}

void ImportGenerationStore::release_all_resource_leases() {
	Vector<String> keys;
	{
		MutexLock lock(state_mutex);
		for (const KeyValue<String, OwnedLease> &E : owned_resource_leases) {
			keys.push_back(E.key);
		}
	}
	for (const String &key : keys) {
		const String lease_path = _resource_lease_path(key, String());
		{
			MutexLock lock(state_mutex);
			owned_resource_leases.erase(key);
		}
		DirAccess::remove_absolute(lease_path.path_join("owner.cfg"));
		DirAccess::remove_absolute(lease_path);
	}
}

bool ImportGenerationStore::is_leased_by_peer(const String &p_resource_key, const String &p_owner, const String &p_project_data_path) {
	{
		MutexLock lock(state_mutex);
		const OwnedLease *owned = owned_resource_leases.getptr(p_resource_key);
		if (owned && owned->owner == p_owner) {
			return false;
		}
	}

	const String lease_path = _resource_lease_path(p_resource_key, p_project_data_path);
	if (!DirAccess::dir_exists_absolute(lease_path)) {
		return false;
	}
	String owner;
	String token;
	int64_t heartbeat_unix = 0;
	int64_t process_id = 0;
	if (!_read_lease(lease_path, owner, token, heartbeat_unix, process_id)) {
		return false;
	}
	return owner != p_owner && _is_lease_live(heartbeat_unix, process_id);
}

void ImportGenerationStore::refresh_resource_leases() {
	HashMap<String, OwnedLease> owned;
	{
		MutexLock lock(state_mutex);
		owned = owned_resource_leases;
	}
	for (const KeyValue<String, OwnedLease> &E : owned) {
		_write_lease(_resource_lease_path(E.key, String()), E.value.owner, E.value.token);
	}
}

Vector<String> ImportGenerationStore::collect_new_commit_events(HashSet<String> &r_seen_transactions, const String &p_project_data_path) {
	Vector<String> new_transactions;
	const String events_path = get_events_path(p_project_data_path);
	if (!DirAccess::dir_exists_absolute(events_path)) {
		return new_transactions;
	}

	for (const String &event_file : DirAccess::get_files_at(events_path)) {
		if (!event_file.ends_with(".commit")) {
			continue;
		}
		const String transaction_id = event_file.left(event_file.length() - String(".commit").length());
		if (!_is_valid_transaction_id(transaction_id) || r_seen_transactions.has(transaction_id)) {
			continue;
		}
		r_seen_transactions.insert(transaction_id);
		new_transactions.push_back(transaction_id);
	}
	new_transactions.sort();
	return new_transactions;
}

Vector<ImportGenerationStore::ResourceGeneration> ImportGenerationStore::read_committed_resources(const String &p_transaction_id, const String &p_project_data_path) {
	Vector<ResourceGeneration> resources;
	if (!_is_valid_transaction_id(p_transaction_id)) {
		return resources;
	}

	const String event_path = get_events_path(p_project_data_path).path_join(p_transaction_id + ".commit");
	if (!FileAccess::exists(event_path) || FileAccess::get_size(event_path) > MAX_EVENT_FILE_SIZE) {
		return resources;
	}
	Error read_error = OK;
	const String event_contents = FileAccess::get_file_as_string(event_path, &read_error);
	if (read_error != OK) {
		return resources;
	}
	String expected_manifest_sha256;
	if (_parse_commit_event(event_contents, p_transaction_id, expected_manifest_sha256) != OK) {
		return resources;
	}

	const String prepared_path = get_transactions_path(p_project_data_path).path_join(p_transaction_id + ".prepared");
	if (!FileAccess::exists(prepared_path) || FileAccess::get_size(prepared_path) > MAX_EVENT_FILE_SIZE) {
		return resources;
	}
	const String prepared_contents = FileAccess::get_file_as_string(prepared_path, &read_error);
	if (read_error != OK || prepared_contents.sha256_text() != expected_manifest_sha256) {
		return resources;
	}
	PreparedManifest manifest;
	if (_parse_prepared_manifest(prepared_contents, p_transaction_id, manifest) != OK) {
		return resources;
	}
	return manifest.resource_generations;
}
