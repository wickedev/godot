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

String TestUtils::get_temp_path(const String &p_suffix) {
	// Each run gets a unique root: leftovers from a previous run (or a crashed run
	// that never reached cleanup) must never be observable, and two test binaries
	// running concurrently must not share directories. The root is removed at the
	// end of the run in test_main.cpp. Note this only isolates state that goes
	// through this helper; tests touching user:// or other fixed paths are not
	// covered.
	static String run_root;
	if (run_root.is_empty()) {
		const String temp_base = OS::get_singleton()->get_cache_path().path_join("godot_test");
		DirAccess::make_dir_recursive_absolute(temp_base);
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
		ERR_FAIL_COND_V_MSG(run_root.is_empty(), String(), "Could not create an exclusive temp root for this test run.");
	}
	DirAccess::make_dir_recursive_absolute(run_root); // Ensure the directory still exists.
	return run_root.path_join(p_suffix);
}

String &TestProjectSettingsInternalsAccessor::resource_path() {
	return ProjectSettings::get_singleton()->resource_path;
}
