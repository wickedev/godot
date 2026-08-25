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
	// Test state namespace isolation. This guarantees exactly two things:
	//   1. At startup, a unique run root is created EXCLUSIVELY (or the run
	//      aborts fatally) — so leftovers from previous/crashed runs are never
	//      observable and concurrent test binaries never share directories.
	//   2. The isolation lifecycle code itself performs no startup sweep and no
	//      run-root cleanup — it never deletes anything. (Tests and the
	//      production code they exercise remain free to create and delete
	//      entries under the root through the ordinary DirAccess contract.)
	// It is not a filesystem-safety API: creation of suffix paths under the
	// root (here, in tests, or in production code they exercise) follows the
	// ordinary DirAccess contract with no symlink-swap guarantees — including
	// recursive creation that may re-materialize the same pathname chain. What
	// is guaranteed is narrower: the CACHED PATHNAME is never re-selected; this
	// function derives the root once and returns the same string forever.
	// Every run (crashed or normal) leaks its root by design;
	// reclamation belongs to a trusted external janitor (CI reclaims via
	// workspace disposal).
	// Mixed-version caveat: pre-isolation binaries sweep the shared base
	// directory at startup and can delete a live run's root; the contract only
	// holds between binaries that include this change.
	// Platform scope: the exclusivity in (1) relies on mkdir's atomic
	// EEXIST-on-existing behavior (DirAccessUnix / DirAccessWindows). Android
	// routes absolute filesystem paths to DirAccessJAndroid
	// (os_android.cpp:135), whose make_dir is a non-atomic dir_exists check
	// (dir_access_jandroid.cpp:221) followed by Kotlin
	// `dirFile.isDirectory || dirFile.mkdirs()`
	// (FilesystemDirectoryAccess.kt:177) — which reports success for an
	// ALREADY-EXISTING directory, so it cannot be exclusive even without the
	// race. The guarantee is only claimed for desktop test runners.
	// Note this only isolates state that goes through this helper; tests
	// touching user:// or other fixed paths are not covered.
	static String run_root;
	if (run_root.is_empty()) {
		const String temp_base = OS::get_singleton()->get_cache_path().path_join("godot_test");
		CRASH_COND_MSG(!temp_base.is_absolute_path(), "Test temp base is not absolute; refusing to run tests.");
		CRASH_COND_MSG(DirAccess::make_dir_recursive_absolute(temp_base) != OK && !DirAccess::dir_exists_absolute(temp_base), "Could not create the test temp base directory.");
		// Exclusive creation via the shared primitive below: only a candidate
		// that mkdir reports as created by US is adopted; a name already taken
		// by anything (raced process, file, symlink) is skipped and retried
		// with a fresh nonce.
		Vector<String> candidates;
		for (int attempt = 0; attempt < 16; attempt++) {
			candidates.push_back(
					"run_" + itos(OS::get_singleton()->get_process_id()) + "_" + itos(OS::get_singleton()->get_ticks_usec()) + "_" + itos(attempt));
		}
		run_root = TestUtils::acquire_exclusive_subdir(temp_base, candidates);
		// A run without an exclusive root must not proceed: a shared or empty
		// root would let two runs observe each other's state.
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

String TestUtils::get_run_id() {
	return _get_run_root().trim_suffix("/").get_file();
}

String TestUtils::acquire_exclusive_subdir(const String &p_base, const Vector<String> &p_candidates) {
	for (const String &name : p_candidates) {
		const String candidate = p_base.path_join(name);
		// make_dir_absolute maps mkdir's EEXIST to ERR_ALREADY_EXISTS for ANY
		// occupant — directory, regular file or symlink — so only a name this
		// process created itself is ever adopted.
		if (DirAccess::make_dir_absolute(candidate) == OK) {
			return candidate;
		}
	}
	return String();
}

String &TestProjectSettingsInternalsAccessor::resource_path() {
	return ProjectSettings::get_singleton()->resource_path;
}
