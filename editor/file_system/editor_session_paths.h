/**************************************************************************/
/*  editor_session_paths.h                                                */
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
#include "core/typedefs.h"

class EditorSessionPaths {
	static EditorSessionPaths *singleton;

	bool lease_owner = false;
	uint64_t last_heartbeat_usec = 0;
	String session_data_dir;
	String editor_data_dir;
	String shader_cache_dir;
	String staging_dir;
	String lease_file;

	void _bootstrap_file(const String &p_file_name);
	void _write_lease(uint64_t p_ticks_usec);

public:
	static void create(bool p_lease_owner);
	static void free();
	static EditorSessionPaths *get_singleton() { return singleton; }

	void heartbeat(uint64_t p_ticks_usec);

	String get_session_data_dir() const { return session_data_dir; }
	String get_editor_data_dir() const { return editor_data_dir; }
	String get_shader_cache_dir() const { return shader_cache_dir; }
	String get_staging_dir() const { return staging_dir; }
	String get_lease_file() const { return lease_file; }

	EditorSessionPaths(bool p_lease_owner);
	~EditorSessionPaths();
};
