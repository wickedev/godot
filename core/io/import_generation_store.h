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
#include "core/templates/vector.h"

class ImportGenerationStore {
public:
	struct UIDClaim {
		ResourceUID::ID uid = ResourceUID::INVALID_ID;
		String path;
		String previous_path;
		int64_t epoch = 0;
	};

	struct PreparedManifest {
		String transaction_id;
		Vector<UIDClaim> uid_claims;
	};

	static String get_transactions_path(const String &p_project_data_path = String());
	static String get_events_path(const String &p_project_data_path = String());

	static String serialize_prepared_manifest(const PreparedManifest &p_manifest);
	static String serialize_commit_event(const String &p_transaction_id, const String &p_manifest_sha256);

	static Error write_prepared_manifest(const String &p_project_data_path, const PreparedManifest &p_manifest, String *r_manifest_sha256 = nullptr);
	static Error commit_prepared_manifest(const String &p_project_data_path, const String &p_transaction_id);
	static Error replay_uid_events(const String &p_project_data_path = String(), bool p_update_cache = true);
};
