/**************************************************************************/
/*  test_resource_uid.cpp                                                 */
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
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
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

TEST_FORCE_LINK(test_resource_uid)

#include "core/io/file_access.h"
#include "core/io/resource_uid.h"
#include "tests/test_utils.h"

namespace TestResourceUID {

TEST_CASE("[ResourceUID] Must encode/decode maximum/minimum UID correctly") {
	CHECK_MESSAGE(ResourceUID::get_singleton()->id_to_text(0x7fffffffffffffff) == "uid://d4n4ub6itg400", "Maximum UID must encode correctly.");
	CHECK_MESSAGE(ResourceUID::get_singleton()->text_to_id("uid://d4n4ub6itg400") == 0x7fffffffffffffff, "Maximum UID must decode correctly.");

	CHECK_MESSAGE(ResourceUID::get_singleton()->id_to_text(0) == "uid://a", "Minimum UID must encode correctly.");
	CHECK_MESSAGE(ResourceUID::get_singleton()->text_to_id("uid://a") == 0, "Minimum UID must decode correctly.");
}

TEST_CASE("[ResourceUID] Must encode and decode invalid UIDs correctly") {
	ResourceUID *rid = ResourceUID::get_singleton();
	CHECK_MESSAGE(rid->id_to_text(-1) == "uid://<invalid>", "Invalid UID -1 must encode correctly.");
	CHECK_MESSAGE(rid->text_to_id("uid://<invalid>") == -1, "Invalid UID -1 must decode correctly.");

	CHECK_MESSAGE(rid->id_to_text(-2) == rid->id_to_text(-1), "Invalid UID -2 must encode to the same as -1.");

	CHECK_MESSAGE(rid->text_to_id("dm3rdgs30kfci") == -1, "UID without scheme must decode correctly.");
}

TEST_CASE("[ResourceUID] Must encode and decode various UIDs correctly") {
	ResourceUID *rid = ResourceUID::get_singleton();
	CHECK_MESSAGE(rid->id_to_text(1) == "uid://b", "UID 1 must encode correctly.");
	CHECK_MESSAGE(rid->text_to_id("uid://b") == 1, "UID 1 must decode correctly.");

	CHECK_MESSAGE(rid->id_to_text(8060368642360689600) == "uid://dm3rdgs30kfci", "A normal UID must encode correctly.");
	CHECK_MESSAGE(rid->text_to_id("uid://dm3rdgs30kfci") == 8060368642360689600, "A normal UID must decode correctly.");
}

TEST_CASE("[ResourceUID] Replacing an aliased UID preserves the other reverse mapping") {
	ResourceUID *rid = ResourceUID::get_singleton();
	rid->enable_reverse_cache();
	const ResourceUID::ID first_uid = rid->create_id();
	const ResourceUID::ID second_uid = rid->create_id();
	REQUIRE(first_uid != second_uid);
	rid->add_id(first_uid, "res://shared.tres");
	rid->add_id(second_uid, "res://shared.tres");

	rid->set_id(first_uid, "res://moved.tres");
	CHECK(rid->get_path_id("res://shared.tres") == second_uid);
	CHECK(rid->get_path_id("res://moved.tres") == first_uid);

	rid->remove_id(first_uid);
	rid->remove_id(second_uid);
}

TEST_CASE("[ResourceUID] Loading duplicate cache records removes stale reverse mappings") {
	ResourceUID *rid = ResourceUID::get_singleton();
	rid->enable_reverse_cache();
	const ResourceUID::ID uid = rid->create_id();
	Vector<Pair<ResourceUID::ID, String>> entries;
	entries.push_back(Pair<ResourceUID::ID, String>(uid, "res://old.tres"));
	entries.push_back(Pair<ResourceUID::ID, String>(uid, "res://new.tres"));
	const Vector<uint8_t> cache_data = ResourceUID::encode_binary_cache(entries);
	const String cache_path = TestUtils::get_temp_path("resource_uid_duplicate_cache.bin");
	Ref<FileAccess> cache_file = FileAccess::open(cache_path, FileAccess::WRITE);
	REQUIRE_OR_RETURN(cache_file.is_valid());
	CHECK(cache_file->store_buffer(cache_data.ptr(), cache_data.size()));
	cache_file.unref();

	CHECK(rid->load_from_cache(false, cache_path) == OK);
	CHECK(rid->get_path_id("res://old.tres") == ResourceUID::INVALID_ID);
	CHECK(rid->get_path_id("res://new.tres") == uid);

	rid->remove_id(uid);
}

} // namespace TestResourceUID
