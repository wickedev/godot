/**************************************************************************/
/*  test_import_generation_store.cpp                                      */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_import_generation_store)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/import_generation_store.h"
#include "tests/test_utils.h"

namespace TestImportGenerationStore {

static String _project_data_path(const String &p_name) {
	const String path = TestUtils::get_temp_path(p_name);
	const Error error = DirAccess::make_dir_recursive_absolute(path);
	ERR_FAIL_COND_V(error != OK && error != ERR_ALREADY_EXISTS, String());
	return path;
}

static ImportGenerationStore::PreparedManifest _manifest(const String &p_transaction_id, ResourceUID::ID p_uid, const String &p_path, const String &p_previous_path, int64_t p_epoch) {
	ImportGenerationStore::PreparedManifest manifest;
	manifest.transaction_id = p_transaction_id;
	ImportGenerationStore::UIDClaim claim;
	claim.uid = p_uid;
	claim.path = p_path;
	claim.previous_path = p_previous_path;
	claim.epoch = p_epoch;
	manifest.uid_claims.push_back(claim);
	return manifest;
}

TEST_CASE("[ImportGenerationStore] Prepared manifests serialize deterministically") {
	ImportGenerationStore::PreparedManifest manifest;
	manifest.transaction_id = "deterministic";
	manifest.uid_claims.push_back(_manifest("unused", 42, "res://second.tres", "", 1).uid_claims[0]);
	manifest.uid_claims.push_back(_manifest("unused", 7, "res://first.tres", "", 1).uid_claims[0]);

	const String serialized = ImportGenerationStore::serialize_prepared_manifest(manifest);
	CHECK(serialized.find("uid=7") < serialized.find("uid=42"));
	CHECK(serialized == ImportGenerationStore::serialize_prepared_manifest(manifest));
}

TEST_CASE("[ImportGenerationStore] Missing event directory is a no-op") {
	const String project_data_path = _project_data_path("import-generation-store-missing");
	REQUIRE_FALSE(project_data_path.is_empty());
	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == OK);
}

TEST_CASE("[ImportGenerationStore] Replays UID claims by per-UID epoch") {
	const String project_data_path = _project_data_path("import-generation-store-replay");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID new_uid = resource_uid->create_id();
	const ResourceUID::ID legacy_uid = resource_uid->create_id();
	REQUIRE(new_uid != legacy_uid);
	resource_uid->add_id(legacy_uid, "res://legacy-old.tres");

	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("new-1", new_uid, "res://new-one.tres", "", 1)) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "new-1") == OK);
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("legacy-1", legacy_uid, "res://legacy-new.tres", "res://legacy-old.tres", 1)) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "legacy-1") == OK);
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("new-2", new_uid, "res://new-two.tres", "res://new-one.tres", 2)) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "new-2") == OK);

	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == OK);
	CHECK(resource_uid->get_id_path(new_uid) == "res://new-two.tres");
	CHECK(resource_uid->get_id_path(legacy_uid) == "res://legacy-new.tres");
	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == OK);

	resource_uid->remove_id(new_uid);
	resource_uid->remove_id(legacy_uid);
}

TEST_CASE("[ImportGenerationStore] Rejects a changed prepared manifest") {
	const String project_data_path = _project_data_path("import-generation-store-changed");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("changed", uid, "res://safe.tres", "", 1)) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "changed") == OK);
	const String prepared_path = ImportGenerationStore::get_transactions_path(project_data_path).path_join("changed.prepared");
	Ref<FileAccess> prepared_file = FileAccess::open(prepared_path, FileAccess::WRITE);
	REQUIRE(prepared_file.is_valid());
	prepared_file->store_string("changed after commit\n");
	prepared_file.unref();

	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == ERR_FILE_CORRUPT);
	CHECK_FALSE(resource_uid->has_id(uid));
}

TEST_CASE("[ImportGenerationStore] Immutable history repairs a missing UID cache baseline") {
	const String project_data_path = _project_data_path("import-generation-store-missing-baseline");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("repair", uid, "res://new.tres", "res://old.tres", 1)) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "repair") == OK);
	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == OK);
	CHECK(resource_uid->get_id_path(uid) == "res://new.tres");

	resource_uid->remove_id(uid);
}

TEST_CASE("[ImportGenerationStore] Rejects conflicting immutable contents") {
	const String project_data_path = _project_data_path("import-generation-store-conflict");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID first_uid = resource_uid->create_id();
	const ResourceUID::ID second_uid = resource_uid->create_id();
	REQUIRE(first_uid != second_uid);

	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("same-id", first_uid, "res://first.tres", "", 1)) == OK);
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("same-id", second_uid, "res://second.tres", "", 1)) == ERR_ALREADY_EXISTS);
}

TEST_CASE("[ImportGenerationStore] Rejects duplicate final UID paths") {
	const String project_data_path = _project_data_path("import-generation-store-duplicate-path");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID existing_uid = resource_uid->create_id();
	const ResourceUID::ID claimed_uid = resource_uid->create_id();
	REQUIRE(existing_uid != claimed_uid);
	resource_uid->add_id(existing_uid, "res://same.tres");

	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("duplicate", claimed_uid, "res://same.tres", "", 1)) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "duplicate") == OK);
	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == ERR_FILE_CORRUPT);
	CHECK_FALSE(resource_uid->has_id(claimed_uid));

	resource_uid->remove_id(existing_uid);
}

TEST_CASE("[ImportGenerationStore] Rejects resource paths outside the project") {
	const String project_data_path = _project_data_path("import-generation-store-path-validation");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("escape", uid, "res://../outside.tres", "", 1)) == ERR_FILE_CORRUPT);
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, _manifest("subresource", uid, "res://file.tres::1", "", 1)) == ERR_FILE_CORRUPT);
}

} // namespace TestImportGenerationStore
