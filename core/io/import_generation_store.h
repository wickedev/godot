/**************************************************************************/
/*  import_generation_store.h                                             */
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

#pragma once

#include "core/io/resource_uid.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

class ImportGenerationStore {
public:
	struct UIDClaim {
		ResourceUID::ID uid = ResourceUID::INVALID_ID;
		String path;
		String previous_path;
		int64_t epoch = 0;
	};

	struct Selector {
		String resource_key;
		String generation_id;
		int64_t epoch = 0;
		Vector<String> file_names;
	};

	struct ResourceGeneration {
		String source_path;
		String resource_key;
		String generation_id;
		int64_t epoch = 0;
	};

	struct PreparedManifest {
		String transaction_id;
		Vector<UIDClaim> uid_claims;
		Vector<ResourceGeneration> resource_generations;
	};

	// Canonical, shared by every editor on the project root.
	static String get_transactions_path(const String &p_project_data_path = String());
	static String get_events_path(const String &p_project_data_path = String());
	static String get_generations_path(const String &p_project_data_path = String());
	static String get_resource_locks_path(const String &p_project_data_path = String());

	// A resource key is the same path-addressed identity the legacy import cache uses,
	// so a key maps one-to-one to a `res://` source file.
	static String get_resource_key(const String &p_source_file);
	static String get_resource_generations_path(const String &p_resource_key, const String &p_project_data_path = String());
	static String get_generation_path(const String &p_resource_key, const String &p_generation_id, const String &p_project_data_path = String());
	static String get_selector_path(const String &p_resource_key, const String &p_project_data_path = String());

	static String serialize_prepared_manifest(const PreparedManifest &p_manifest);
	static String serialize_commit_event(const String &p_transaction_id, const String &p_manifest_sha256);
	static String serialize_selector(const Selector &p_selector);

	static Error write_prepared_manifest(const String &p_project_data_path, const PreparedManifest &p_manifest, String *r_manifest_sha256 = nullptr);
	static Error commit_prepared_manifest(const String &p_project_data_path, const String &p_transaction_id);
	static Error replay_uid_events(const String &p_project_data_path = String(), bool p_update_cache = true);

	// Selector: the single mutable pointer that makes a published generation visible.
	static bool read_selector(const String &p_resource_key, Selector &r_selector, const String &p_project_data_path = String());
	static Error write_selector(const Selector &p_selector, const String &p_project_data_path = String());
	static Error clear_selector(const String &p_resource_key, const String &p_project_data_path = String());

	// Publish a staged directory as an immutable generation. Does not flip the selector.
	static Error publish_generation(const String &p_resource_key, const String &p_generation_id, const String &p_staging_path, Vector<String> *r_published_files = nullptr, const String &p_project_data_path = String());
	// Make every committed generation visible; used after a crash between commit and selector flip.
	static Error repair_selectors(const String &p_project_data_path = String());

	// Reads: map a logical `res://.godot/imported/...` path to the physical file backing it.
	static String resolve_artifact_path(const String &p_source_file, const String &p_logical_path);
	// Same, for callers that only hold the artifact path. Builds a reverse index over every
	// published selector, so prefer the two-argument form when the source is known.
	static String resolve_artifact_path(const String &p_logical_path);
	static void invalidate_resolution_cache();

	// UID claim bookkeeping, so successive claims for one UID form a valid chain.
	static UIDClaim make_uid_claim(ResourceUID::ID p_uid, const String &p_path);
	static void note_uid_claims(const Vector<UIDClaim> &p_claims);

	// Resource ownership leases. A lease only suppresses a peer's automatic reimport of the
	// same resource; unrelated resources are never blocked.
	static bool acquire_resource_lease(const String &p_resource_key, const String &p_owner, const String &p_project_data_path = String());
	static void release_resource_lease(const String &p_resource_key, const String &p_owner, const String &p_project_data_path = String());
	static void release_all_resource_leases();
	static bool is_leased_by_peer(const String &p_resource_key, const String &p_owner, const String &p_project_data_path = String());
	static void refresh_resource_leases();

	// Cross-editor notification.
	static Vector<String> collect_new_commit_events(HashSet<String> &r_seen_transactions, const String &p_project_data_path = String());
	static Vector<ResourceGeneration> read_committed_resources(const String &p_transaction_id, const String &p_project_data_path = String());

	// Reclamation. Both steps only ever touch state older than a grace period, which is what
	// makes them safe to run while other editors are reading the same project.
	struct ReclaimSettings {
		int64_t grace_seconds = 600;
		int keep_per_resource = 1;
		int64_t storage_limit_bytes = 0; // 0 disables the quota.
		int64_t min_events_to_compact = 256;
	};

	static String get_checkpoints_path(const String &p_project_data_path = String());
	static String get_resource_source_path(const String &p_resource_key, const String &p_project_data_path = String());
	static Error record_resource_source(const String &p_resource_key, const String &p_source_path, const String &p_project_data_path = String());
	static String read_resource_source(const String &p_resource_key, const String &p_project_data_path = String());

	static Error collect_generation_garbage(const ReclaimSettings &p_settings, const String &p_project_data_path = String());
	static Error compact_event_journal(const ReclaimSettings &p_settings, const String &p_project_data_path = String());
	static Error clean_abandoned_staging(const ReclaimSettings &p_settings, const String &p_session_data_root, const String &p_current_session_id);

private:
	// The end state a replay produced: the last claim per UID and the newest generation per resource.
	struct ReplayState {
		HashMap<ResourceUID::ID, UIDClaim> final_claims;
		HashMap<String, ResourceGeneration> final_generations;
		Vector<String> replayed_transaction_ids;
	};

	static Error _replay(const String &p_project_data_path, ReplayState &r_state, int64_t p_max_commit_time = 0, Vector<String> *r_eligible_transaction_ids = nullptr);

	struct ResolutionEntry {
		uint64_t selector_modified_time = 0;
		String generation_path;
		HashSet<String> file_names;
		bool valid = false;
	};

	struct OwnedLease {
		String owner;
		String token;
	};

	static Mutex state_mutex;
	static HashMap<String, ResolutionEntry> resolution_cache;
	static HashMap<String, String> artifact_reverse_index;
	static bool artifact_reverse_index_built;
	static HashMap<ResourceUID::ID, UIDClaim> last_uid_claims;
	static HashMap<String, OwnedLease> owned_resource_leases;
};
