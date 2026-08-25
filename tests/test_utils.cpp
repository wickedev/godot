/**************************************************************************/
/*  test_utils.cpp                                                        */
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

#include "tests/test_utils.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"

String TestUtils::get_data_path(const String &p_file) {
	String data_path = "../tests/data";
	return get_executable_dir().path_join(data_path.path_join(p_file));
}

String TestUtils::get_executable_dir() {
	return OS::get_singleton()->get_executable_path().get_base_dir();
}

static const String &_get_run_root() {
	// Each run gets a unique root: leftovers from a previous run (or a crashed run
	// that never reached cleanup) must never be observable, and two test binaries
	// running concurrently must not share directories. Every run leaks its root by
	// design — there is no in-process deletion of it at all, not even a final
	// non-recursive rmdir: any path-based operation on the root can be redirected
	// by concurrently swapping one of its ancestors for a symlink/junction, so the
	// only deletion-free-of-that-risk is no deletion. Reclamation is left to an
	// owner-aware janitor outside this binary (CI reclaims via workspace disposal).
	// Note this only isolates state that goes through this helper; tests touching
	// user:// or other fixed paths are not covered.
	// Platform caveat: on Android, make_dir_absolute is dir_exists + mkdirs and is
	// therefore not exclusive under a race; the exclusivity guarantee below is not
	// supported there (the test runner is not a shipping Android target).
	static String run_root;
	if (run_root.is_empty()) {
		const String temp_base = OS::get_singleton()->get_cache_path().path_join("godot_test");
		CRASH_COND_MSG(!temp_base.is_absolute_path(), "Test temp base is not absolute; refusing to run tests.");
		CRASH_COND_MSG(DirAccess::make_dir_recursive_absolute(temp_base) != OK && !DirAccess::dir_exists_absolute(temp_base), "Could not create the test temp base directory.");
		// Exclusive creation: make_dir_absolute fails with ERR_ALREADY_EXISTS if
		// another process raced us to the same name, in which case we retry with
		// a fresh nonce instead of silently adopting (and later deleting) a
		// directory we do not own.
		for (int attempt = 0; attempt < 16 && run_root.is_empty(); attempt++) {
			const String candidate = temp_base.path_join(
					"run_" + itos(OS::get_singleton()->get_process_id()) + "_" + itos(OS::get_singleton()->get_ticks_usec()) + "_" + itos(attempt));
			if (DirAccess::make_dir_absolute(candidate) == OK) {
				run_root = candidate;
			}
		}
		// A run without an exclusive root must not proceed: later cleanup would
		// otherwise operate on an empty or shared path, which is how recursive
		// deletion reaches directories we do not own.
		CRASH_COND_MSG(run_root.is_empty() || !run_root.is_absolute_path(), "Could not create an exclusive temp root for this test run.");
	}
	// No reacquisition: the exclusively-created root is never re-created or even
	// re-checked. If something deletes or replaces it mid-run, recreating (or
	// adopting) a path at the same name could hand tests a directory we do not
	// own; instead the affected tests simply fail on their own file operations.
	return run_root;
}

String TestUtils::get_temp_path(const String &p_suffix) {
	return _get_run_root().path_join(p_suffix);
}

Error TestUtils::make_temp_dir(const String &p_absolute_path) {
	const String &run_root = _get_run_root();
	const String rel_path = p_absolute_path.trim_prefix(run_root.path_join(""));
	ERR_FAIL_COND_V_MSG(rel_path == p_absolute_path || rel_path.is_empty() || rel_path.is_absolute_path() || rel_path.contains(".."), ERR_INVALID_PARAMETER,
			"Test directories must be created under the run temp root: " + p_absolute_path);
	// Open the existing root instead of creating paths absolutely: if the root has
	// been deleted or replaced mid-run this fails right here rather than silently
	// recreating the chain (see the no-reacquisition rule above).
	Ref<DirAccess> da = DirAccess::open(run_root);
	ERR_FAIL_COND_V_MSG(da.is_null(), ERR_CANT_OPEN, "The run temp root no longer exists; refusing to recreate it.");
	const Error err = da->make_dir_recursive(rel_path);
	return (err == OK || err == ERR_ALREADY_EXISTS) ? OK : err;
}

String &TestProjectSettingsInternalsAccessor::resource_path() {
	return ProjectSettings::get_singleton()->resource_path;
}
