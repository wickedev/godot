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

// These cases exercise the ACTUAL acquisition primitive used by
// TestUtils::get_temp_path() — TestUtils::acquire_exclusive_subdir() — with
// injected candidates, pinning the adoption rule end to end: anything already
// occupying a candidate name (directory, file, symlink) is skipped, only a
// name this process created itself is adopted, and exhaustion yields empty.

TEST_CASE("[TempPathIsolation] Acquisition skips occupied candidates and adopts only fresh ones") {
	const String base = TestUtils::get_temp_path("exclusive_create_rules");
	REQUIRE(DirAccess::make_dir_recursive_absolute(base) == OK);

	// Occupy candidate names ahead of the acquisition call. The setup runs once
	// per subcase, so every step tolerates its own leftovers.
	const Error taken_dir_err = DirAccess::make_dir_absolute(base.path_join("taken_dir"));
	REQUIRE((taken_dir_err == OK || taken_dir_err == ERR_ALREADY_EXISTS));
	{
		Ref<FileAccess> f = FileAccess::open(base.path_join("taken_file"), FileAccess::WRITE);
		REQUIRE(f.is_valid());
		f->store_8(0);
	}
	Vector<String> candidates;
	candidates.push_back("taken_dir");
	candidates.push_back("taken_file");
#ifndef WINDOWS_ENABLED
	{
		Ref<DirAccess> da = DirAccess::open(base);
		REQUIRE(da.is_valid());
		if (!da->is_link(base.path_join("taken_link"))) {
			REQUIRE(da->create_link(base.path_join("taken_dir"), base.path_join("taken_link")) == OK);
		}
	}
	candidates.push_back("taken_link");
#endif
	candidates.push_back("fresh_dir");

	SUBCASE("Occupied names are skipped; the first fresh name is adopted") {
		Vector<String> with_fresh = candidates;
		with_fresh.remove_at(with_fresh.size() - 1);
		with_fresh.push_back("fresh_adopt");
		const String acquired = TestUtils::acquire_exclusive_subdir(base, with_fresh);
		CHECK(acquired == base.path_join("fresh_adopt"));
		CHECK(DirAccess::dir_exists_absolute(acquired));
	}

	SUBCASE("Exhaustion returns empty instead of adopting an occupied name") {
		Vector<String> all_taken = candidates;
		all_taken.remove_at(all_taken.size() - 1); // Drop the fresh one.
		const String acquired = TestUtils::acquire_exclusive_subdir(base, all_taken);
		CHECK(acquired.is_empty());
	}

	SUBCASE("A second acquisition of the same fresh name is refused (self-race)") {
		Vector<String> fresh_only;
		fresh_only.push_back("fresh_dir_2");
		CHECK(TestUtils::acquire_exclusive_subdir(base, fresh_only) == base.path_join("fresh_dir_2"));
		CHECK(TestUtils::acquire_exclusive_subdir(base, fresh_only).is_empty());
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
