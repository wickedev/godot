/**************************************************************************/
/*  test_utils.h                                                          */
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

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace TestUtils {

String get_data_path(const String &p_file);
String get_executable_dir();
String get_temp_path(const String &p_suffix);
// A token unique to this test-run process, derived from the same exclusively
// created run root as get_temp_path(). Use it to make a fixed shared name
// unique when the state does NOT go through get_temp_path() -- see the
// user:// logs fixture in test_logger.cpp.
String get_run_id();
// The run-root acquisition primitive, exposed so tests can pin the adoption
// rule with injected candidates: returns the first candidate under `p_base`
// that make_dir_absolute() reports as created by US (OK) — anything already
// occupying a candidate name (directory, file, symlink) is skipped, and an
// empty String is returned when every candidate is taken.
String acquire_exclusive_subdir(const String &p_base, const Vector<String> &p_candidates);
} // namespace TestUtils

// FIXME: This was originally constrained to `tests/core/config/test_project_settings.h`, but that
//  file is no longer a header. Some other tests relied on the ability to override the resource
//  path, so relocating the accessor is the least-intrusive workaround.
class TestProjectSettingsInternalsAccessor {
public:
	static String &resource_path();
};
