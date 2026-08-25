/**************************************************************************/
/*  test_temp_path_isolation.cpp                                          */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

TEST_FORCE_LINK(test_temp_path_isolation)

namespace TestTempPathIsolation {

// The run-root acquisition in TestUtils::get_temp_path() adopts a candidate
// only when DirAccess::make_dir_absolute() reports it created the directory
// itself (OK). These cases pin that adoption rule: anything already occupying
// the target name — directory, file or symlink — must NOT be adopted.

TEST_CASE("[TempPathIsolation] Existing paths are not adopted by exclusive creation") {
	const String base = TestUtils::get_temp_path("exclusive_create_rules");
	REQUIRE(DirAccess::make_dir_recursive_absolute(base) == OK);

	SUBCASE("A pre-existing directory reports ERR_ALREADY_EXISTS") {
		const String taken = base.path_join("taken_dir");
		REQUIRE(DirAccess::make_dir_absolute(taken) == OK);
		CHECK(DirAccess::make_dir_absolute(taken) == ERR_ALREADY_EXISTS);
	}

	SUBCASE("A pre-existing regular file is not adopted") {
		const String file_path = base.path_join("taken_file");
		{
			Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
			REQUIRE(f.is_valid());
			f->store_8(0);
		}
		CHECK(DirAccess::make_dir_absolute(file_path) != OK);
	}

#ifndef WINDOWS_ENABLED
	SUBCASE("A pre-existing symlink is not adopted") {
		const String target = base.path_join("symlink_target");
		REQUIRE(DirAccess::make_dir_absolute(target) == OK);
		const String link_path = base.path_join("taken_link");
		Ref<DirAccess> da = DirAccess::open(base);
		REQUIRE(da.is_valid());
		REQUIRE(da->create_link(target, link_path) == OK);
		// mkdir on an existing symlink fails with EEXIST regardless of target.
		CHECK(DirAccess::make_dir_absolute(link_path) != OK);
	}
#endif

	SUBCASE("A fresh name is created and owned") {
		const String fresh = base.path_join("fresh_dir");
		CHECK(DirAccess::make_dir_absolute(fresh) == OK);
		CHECK(DirAccess::dir_exists_absolute(fresh));
	}
}

TEST_CASE("[TempPathIsolation] The run root is a stable per-process unique path") {
	const String a = TestUtils::get_temp_path("");
	const String b = TestUtils::get_temp_path("");
	// Immutable per-process cache: never re-derived.
	CHECK(a == b);
	CHECK(a.is_absolute_path());
	// Namespaced under the shared base, with the per-run unique prefix.
	const String base = OS::get_singleton()->get_cache_path().path_join("godot_test");
	CHECK(a.begins_with(base));
	// path_join("") leaves a trailing separator; strip it to inspect the leaf.
	const String root = a.trim_suffix("/");
	CHECK(root.get_file().begins_with("run_"));
	// The exclusively-created root exists.
	CHECK(DirAccess::dir_exists_absolute(root));
}

} // namespace TestTempPathIsolation
