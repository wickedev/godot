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
	ERR_FAIL_COND_V(TestUtils::make_temp_dir(path) != OK, String());
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

static ImportGenerationStore::ResourceGeneration _resource_generation(const String &p_source_path, const String &p_generation_id, int64_t p_epoch) {
	ImportGenerationStore::ResourceGeneration resource_generation;
	resource_generation.source_path = p_source_path;
	resource_generation.resource_key = ImportGenerationStore::get_resource_key(p_source_path);
	resource_generation.generation_id = p_generation_id;
	resource_generation.epoch = p_epoch;
	return resource_generation;
}

static Error _write_staged_file(const String &p_staging_path, const String &p_file_name, const String &p_contents) {
	const Error dir_error = TestUtils::make_temp_dir(p_staging_path);
	if (dir_error != OK) {
		return dir_error;
	}
	Ref<FileAccess> file = FileAccess::open(p_staging_path.path_join(p_file_name), FileAccess::WRITE);
	if (file.is_null()) {
		return ERR_CANT_CREATE;
	}
	file->store_string(p_contents);
	return OK;
}

TEST_CASE("[ImportGenerationStore] Resource keys are stable per source path") {
	const String key = ImportGenerationStore::get_resource_key("res://characters/noelle.glb");
	CHECK(key == ImportGenerationStore::get_resource_key("res://characters/noelle.glb"));
	CHECK(key != ImportGenerationStore::get_resource_key("res://characters/yanfei.glb"));
	CHECK(key.begins_with("noelle.glb-"));
}

TEST_CASE("[ImportGenerationStore] Selectors round-trip and can be cleared") {
	const String project_data_path = _project_data_path("import-generation-store-selector");
	REQUIRE_FALSE(project_data_path.is_empty());

	ImportGenerationStore::Selector selector;
	selector.resource_key = ImportGenerationStore::get_resource_key("res://selector.glb");
	selector.generation_id = "gen-1";
	selector.epoch = 5;
	selector.file_names.push_back("b.scn");
	selector.file_names.push_back("a.md5");

	CHECK(ImportGenerationStore::write_selector(selector, project_data_path) == OK);

	ImportGenerationStore::Selector read;
	REQUIRE(ImportGenerationStore::read_selector(selector.resource_key, read, project_data_path));
	CHECK(read.generation_id == "gen-1");
	CHECK(read.epoch == 5);
	REQUIRE(read.file_names.size() == 2);
	CHECK(read.file_names[0] == "a.md5"); // Sorted, so the file is byte-identical for equal input.
	CHECK(read.file_names[1] == "b.scn");

	CHECK(ImportGenerationStore::clear_selector(selector.resource_key, project_data_path) == OK);
	CHECK_FALSE(ImportGenerationStore::read_selector(selector.resource_key, read, project_data_path));
	CHECK(ImportGenerationStore::clear_selector(selector.resource_key, project_data_path) == OK);
}

TEST_CASE("[ImportGenerationStore] Publishing moves staged output into an immutable generation") {
	const String project_data_path = _project_data_path("import-generation-store-publish");
	REQUIRE_FALSE(project_data_path.is_empty());

	const String source_path = "res://publish.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);
	const String staging_path = project_data_path.path_join("staging").path_join(resource_key);
	REQUIRE(_write_staged_file(staging_path, resource_key + ".scn", "scene") == OK);
	REQUIRE(_write_staged_file(staging_path, resource_key + ".md5", "md5") == OK);

	Vector<String> published_files;
	CHECK(ImportGenerationStore::publish_generation(resource_key, "gen-a", staging_path, &published_files, project_data_path) == OK);
	CHECK(published_files.size() == 2);
	CHECK_FALSE(DirAccess::dir_exists_absolute(staging_path));

	const String generation_path = ImportGenerationStore::get_generation_path(resource_key, "gen-a", project_data_path);
	CHECK(FileAccess::get_file_as_string(generation_path.path_join(resource_key + ".scn")) == "scene");

	// Publishing the same generation again is a no-op rather than a second move.
	CHECK(ImportGenerationStore::publish_generation(resource_key, "gen-a", staging_path, nullptr, project_data_path) == OK);
	CHECK(FileAccess::get_file_as_string(generation_path.path_join(resource_key + ".scn")) == "scene");
}

TEST_CASE("[ImportGenerationStore] A committed transaction survives a missing selector") {
	const String project_data_path = _project_data_path("import-generation-store-repair");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	const String source_path = "res://repair.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);
	const String staging_path = project_data_path.path_join("staging").path_join(resource_key);
	REQUIRE(_write_staged_file(staging_path, resource_key + ".scn", "committed") == OK);

	ImportGenerationStore::PreparedManifest manifest = _manifest("repair-1", uid, source_path, "", 1);
	manifest.resource_generations.push_back(_resource_generation(source_path, "repair-1", 1));
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, manifest) == OK);
	CHECK(ImportGenerationStore::publish_generation(resource_key, "repair-1", staging_path, nullptr, project_data_path) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "repair-1") == OK);

	// The editor died here, before the selector was replaced.
	ImportGenerationStore::Selector selector;
	CHECK_FALSE(ImportGenerationStore::read_selector(resource_key, selector, project_data_path));

	CHECK(ImportGenerationStore::repair_selectors(project_data_path) == OK);
	REQUIRE(ImportGenerationStore::read_selector(resource_key, selector, project_data_path));
	CHECK(selector.generation_id == "repair-1");
	CHECK(selector.file_names.size() == 1);

	// Repair is idempotent and never moves a selector backwards.
	CHECK(ImportGenerationStore::repair_selectors(project_data_path) == OK);
	REQUIRE(ImportGenerationStore::read_selector(resource_key, selector, project_data_path));
	CHECK(selector.epoch == 1);
}

TEST_CASE("[ImportGenerationStore] A generation committed but never published keeps the old one visible") {
	const String project_data_path = _project_data_path("import-generation-store-unpublished");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	const String source_path = "res://unpublished.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);

	ImportGenerationStore::Selector old_selector;
	old_selector.resource_key = resource_key;
	old_selector.generation_id = "old-gen";
	old_selector.epoch = 1;
	old_selector.file_names.push_back(resource_key + ".scn");
	REQUIRE(ImportGenerationStore::write_selector(old_selector, project_data_path) == OK);

	ImportGenerationStore::PreparedManifest manifest = _manifest("never-published", uid, source_path, "", 2);
	manifest.resource_generations.push_back(_resource_generation(source_path, "never-published", 2));
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, manifest) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "never-published") == OK);

	CHECK(ImportGenerationStore::repair_selectors(project_data_path) == OK);
	ImportGenerationStore::Selector selector;
	REQUIRE(ImportGenerationStore::read_selector(resource_key, selector, project_data_path));
	CHECK(selector.generation_id == "old-gen");
}

TEST_CASE("[ImportGenerationStore] Commit events are reported once per transaction") {
	const String project_data_path = _project_data_path("import-generation-store-events");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	const String source_path = "res://events.glb";
	ImportGenerationStore::PreparedManifest manifest = _manifest("event-1", uid, source_path, "", 1);
	manifest.resource_generations.push_back(_resource_generation(source_path, "event-1", 1));
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, manifest) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "event-1") == OK);

	HashSet<String> seen;
	Vector<String> observed = ImportGenerationStore::collect_new_commit_events(seen, project_data_path);
	REQUIRE(observed.size() == 1);
	CHECK(observed[0] == "event-1");
	CHECK(ImportGenerationStore::collect_new_commit_events(seen, project_data_path).is_empty());

	const Vector<ImportGenerationStore::ResourceGeneration> resources = ImportGenerationStore::read_committed_resources("event-1", project_data_path);
	REQUIRE(resources.size() == 1);
	CHECK(resources[0].source_path == source_path);
	CHECK(resources[0].generation_id == "event-1");
	CHECK(ImportGenerationStore::read_committed_resources("never-committed", project_data_path).is_empty());
}

TEST_CASE("[ImportGenerationStore] A live lease blocks only the same resource") {
	const String project_data_path = _project_data_path("import-generation-store-lease");
	REQUIRE_FALSE(project_data_path.is_empty());

	const String held_key = ImportGenerationStore::get_resource_key("res://held.glb");
	const String other_key = ImportGenerationStore::get_resource_key("res://other.glb");

	CHECK(ImportGenerationStore::acquire_resource_lease(held_key, "session-a", project_data_path));
	CHECK_FALSE(ImportGenerationStore::is_leased_by_peer(held_key, "session-a", project_data_path));
	CHECK(ImportGenerationStore::is_leased_by_peer(held_key, "session-b", project_data_path));
	CHECK_FALSE(ImportGenerationStore::is_leased_by_peer(other_key, "session-b", project_data_path));

	// Re-acquiring a lease this session already owns stays successful.
	CHECK(ImportGenerationStore::acquire_resource_lease(held_key, "session-a", project_data_path));

	ImportGenerationStore::release_resource_lease(held_key, "session-a", project_data_path);
	CHECK_FALSE(ImportGenerationStore::is_leased_by_peer(held_key, "session-b", project_data_path));
	CHECK(ImportGenerationStore::acquire_resource_lease(held_key, "session-b", project_data_path));
	ImportGenerationStore::release_all_resource_leases();
}

TEST_CASE("[ImportGenerationStore] UID claims continue the chain replay expects") {
	const String project_data_path = _project_data_path("import-generation-store-claims");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	const String source_path = "res://chain.glb";
	ImportGenerationStore::UIDClaim first = ImportGenerationStore::make_uid_claim(uid, source_path);
	CHECK(first.previous_path.is_empty());
	Vector<ImportGenerationStore::UIDClaim> claims;
	claims.push_back(first);
	ImportGenerationStore::note_uid_claims(claims);

	const ImportGenerationStore::UIDClaim second = ImportGenerationStore::make_uid_claim(uid, source_path);
	CHECK(second.previous_path == source_path);
	CHECK(second.epoch > first.epoch);

	ImportGenerationStore::PreparedManifest first_manifest;
	first_manifest.transaction_id = "chain-1";
	first_manifest.uid_claims.push_back(first);
	ImportGenerationStore::PreparedManifest second_manifest;
	second_manifest.transaction_id = "chain-2";
	second_manifest.uid_claims.push_back(second);

	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, first_manifest) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "chain-1") == OK);
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, second_manifest) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "chain-2") == OK);

	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == OK);
	CHECK(resource_uid->get_id_path(uid) == source_path);

	resource_uid->remove_id(uid);
}

static void _publish_test_generation(const String &p_project_data_path, const String &p_source_path, const String &p_generation_id, int64_t p_epoch, const String &p_contents, bool p_select) {
	const String resource_key = ImportGenerationStore::get_resource_key(p_source_path);
	const String staging_path = p_project_data_path.path_join("staging").path_join(p_generation_id);
	REQUIRE(_write_staged_file(staging_path, resource_key + ".scn", p_contents) == OK);
	REQUIRE(ImportGenerationStore::publish_generation(resource_key, p_generation_id, staging_path, nullptr, p_project_data_path) == OK);
	if (p_select) {
		ImportGenerationStore::Selector selector;
		selector.resource_key = resource_key;
		selector.generation_id = p_generation_id;
		selector.epoch = p_epoch;
		selector.file_names.push_back(resource_key + ".scn");
		REQUIRE(ImportGenerationStore::write_selector(selector, p_project_data_path) == OK);
	}
}

TEST_CASE("[ImportGenerationStore] Reclamation keeps the selected generation") {
	const String project_data_path = _project_data_path("import-generation-store-gc");
	REQUIRE_FALSE(project_data_path.is_empty());

	const String source_path = "res://gc.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);
	_publish_test_generation(project_data_path, source_path, "gc-1", 1, "one", false);
	_publish_test_generation(project_data_path, source_path, "gc-2", 2, "two", false);
	_publish_test_generation(project_data_path, source_path, "gc-3", 3, "three", true);

	ImportGenerationStore::ReclaimSettings settings;
	settings.grace_seconds = 0;
	settings.keep_per_resource = 0;
	CHECK(ImportGenerationStore::collect_generation_garbage(settings, project_data_path) == OK);

	CHECK(DirAccess::dir_exists_absolute(ImportGenerationStore::get_generation_path(resource_key, "gc-3", project_data_path)));
	CHECK_FALSE(DirAccess::dir_exists_absolute(ImportGenerationStore::get_generation_path(resource_key, "gc-1", project_data_path)));
	CHECK_FALSE(DirAccess::dir_exists_absolute(ImportGenerationStore::get_generation_path(resource_key, "gc-2", project_data_path)));

	ImportGenerationStore::Selector selector;
	REQUIRE(ImportGenerationStore::read_selector(resource_key, selector, project_data_path));
	CHECK(selector.generation_id == "gc-3");
}

TEST_CASE("[ImportGenerationStore] Reclamation honours the retention count") {
	const String project_data_path = _project_data_path("import-generation-store-gc-keep");
	REQUIRE_FALSE(project_data_path.is_empty());

	const String source_path = "res://gc-keep.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);
	_publish_test_generation(project_data_path, source_path, "keep-1", 1, "one", false);
	_publish_test_generation(project_data_path, source_path, "keep-2", 2, "two", false);
	_publish_test_generation(project_data_path, source_path, "keep-3", 3, "three", true);

	ImportGenerationStore::ReclaimSettings settings;
	settings.grace_seconds = 0;
	settings.keep_per_resource = 1;
	CHECK(ImportGenerationStore::collect_generation_garbage(settings, project_data_path) == OK);

	const int surviving = DirAccess::get_directories_at(ImportGenerationStore::get_resource_generations_path(resource_key, project_data_path)).size();
	CHECK(surviving == 2); // The selected one plus one predecessor.
	CHECK(DirAccess::dir_exists_absolute(ImportGenerationStore::get_generation_path(resource_key, "keep-3", project_data_path)));
}

TEST_CASE("[ImportGenerationStore] A grace period protects recent generations") {
	const String project_data_path = _project_data_path("import-generation-store-gc-grace");
	REQUIRE_FALSE(project_data_path.is_empty());

	const String source_path = "res://gc-grace.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);
	_publish_test_generation(project_data_path, source_path, "grace-1", 1, "one", false);
	_publish_test_generation(project_data_path, source_path, "grace-2", 2, "two", true);

	ImportGenerationStore::ReclaimSettings settings;
	settings.grace_seconds = 3600;
	settings.keep_per_resource = 0;
	CHECK(ImportGenerationStore::collect_generation_garbage(settings, project_data_path) == OK);

	// Another editor could still be loading from it, so nothing this recent is reclaimed.
	CHECK(DirAccess::dir_exists_absolute(ImportGenerationStore::get_generation_path(resource_key, "grace-1", project_data_path)));
}

TEST_CASE("[ImportGenerationStore] A removed source takes its generations with it") {
	const String project_data_path = _project_data_path("import-generation-store-gc-orphan");
	REQUIRE_FALSE(project_data_path.is_empty());

	const String source_path = "res://gone/removed.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);
	_publish_test_generation(project_data_path, source_path, "orphan-1", 1, "one", true);
	CHECK(ImportGenerationStore::record_resource_source(resource_key, source_path, project_data_path) == OK);
	CHECK(ImportGenerationStore::read_resource_source(resource_key, project_data_path) == source_path);

	ImportGenerationStore::ReclaimSettings settings;
	settings.grace_seconds = 0;
	settings.keep_per_resource = 1;
	CHECK(ImportGenerationStore::collect_generation_garbage(settings, project_data_path) == OK);
	CHECK_FALSE(DirAccess::dir_exists_absolute(ImportGenerationStore::get_resource_generations_path(resource_key, project_data_path)));
}

TEST_CASE("[ImportGenerationStore] The storage limit reclaims the oldest unselected generations") {
	const String project_data_path = _project_data_path("import-generation-store-gc-quota");
	REQUIRE_FALSE(project_data_path.is_empty());

	const String source_path = "res://quota.glb";
	const String resource_key = ImportGenerationStore::get_resource_key(source_path);
	_publish_test_generation(project_data_path, source_path, "quota-1", 1, "aaaaaaaaaa", false);
	_publish_test_generation(project_data_path, source_path, "quota-2", 2, "bbbbbbbbbb", true);

	ImportGenerationStore::ReclaimSettings settings;
	settings.grace_seconds = 0;
	settings.keep_per_resource = 4; // Retention alone would keep both.
	settings.storage_limit_bytes = 15;
	CHECK(ImportGenerationStore::collect_generation_garbage(settings, project_data_path) == OK);

	CHECK(DirAccess::dir_exists_absolute(ImportGenerationStore::get_generation_path(resource_key, "quota-2", project_data_path)));
	CHECK_FALSE(DirAccess::dir_exists_absolute(ImportGenerationStore::get_generation_path(resource_key, "quota-1", project_data_path)));
}

TEST_CASE("[ImportGenerationStore] Compaction folds old transactions and preserves replay") {
	const String project_data_path = _project_data_path("import-generation-store-compact");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	const String source_path = "res://compact.glb";
	ImportGenerationStore::PreparedManifest first = _manifest("compact-1", uid, source_path, "", 1);
	first.resource_generations.push_back(_resource_generation(source_path, "compact-1", 1));
	ImportGenerationStore::PreparedManifest second = _manifest("compact-2", uid, source_path, source_path, 2);
	second.resource_generations.push_back(_resource_generation(source_path, "compact-2", 2));
	ImportGenerationStore::PreparedManifest third = _manifest("compact-3", uid, source_path, source_path, 3);
	third.resource_generations.push_back(_resource_generation(source_path, "compact-3", 3));

	for (const ImportGenerationStore::PreparedManifest &manifest : { first, second, third }) {
		CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, manifest) == OK);
		CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, manifest.transaction_id) == OK);
	}

	ImportGenerationStore::ReclaimSettings settings;
	settings.grace_seconds = 0;
	settings.min_events_to_compact = 2;
	CHECK(ImportGenerationStore::compact_event_journal(settings, project_data_path) == OK);

	CHECK(DirAccess::get_files_at(ImportGenerationStore::get_events_path(project_data_path)).is_empty());
	CHECK(DirAccess::get_files_at(ImportGenerationStore::get_checkpoints_path(project_data_path)).size() == 1);

	// The checkpoint has to replay to exactly what the folded transactions did.
	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == OK);
	CHECK(resource_uid->get_id_path(uid) == source_path);

	// Compacting again has nothing left to fold.
	CHECK(ImportGenerationStore::compact_event_journal(settings, project_data_path) == OK);
	CHECK(DirAccess::get_files_at(ImportGenerationStore::get_checkpoints_path(project_data_path)).size() == 1);

	resource_uid->remove_id(uid);
}

TEST_CASE("[ImportGenerationStore] Compaction leaves recent transactions replayable") {
	const String project_data_path = _project_data_path("import-generation-store-compact-recent");
	REQUIRE_FALSE(project_data_path.is_empty());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();

	const String source_path = "res://recent.glb";
	ImportGenerationStore::PreparedManifest manifest = _manifest("recent-1", uid, source_path, "", 1);
	manifest.resource_generations.push_back(_resource_generation(source_path, "recent-1", 1));
	CHECK(ImportGenerationStore::write_prepared_manifest(project_data_path, manifest) == OK);
	CHECK(ImportGenerationStore::commit_prepared_manifest(project_data_path, "recent-1") == OK);

	ImportGenerationStore::ReclaimSettings settings;
	settings.grace_seconds = 3600;
	settings.min_events_to_compact = 1;
	CHECK(ImportGenerationStore::compact_event_journal(settings, project_data_path) == OK);

	CHECK_FALSE(DirAccess::dir_exists_absolute(ImportGenerationStore::get_checkpoints_path(project_data_path)));
	CHECK(DirAccess::get_files_at(ImportGenerationStore::get_events_path(project_data_path)).size() == 1);
	CHECK(ImportGenerationStore::replay_uid_events(project_data_path, false) == OK);
	CHECK(resource_uid->get_id_path(uid) == source_path);

	resource_uid->remove_id(uid);
}

} // namespace TestImportGenerationStore
