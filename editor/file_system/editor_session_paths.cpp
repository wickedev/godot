/**************************************************************************/
/*  editor_session_paths.cpp                                              */
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

#include "editor_session_paths.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

EditorSessionPaths *EditorSessionPaths::singleton = nullptr;

void EditorSessionPaths::create(bool p_lease_owner) {
	ERR_FAIL_COND(singleton != nullptr);
	memnew(EditorSessionPaths(p_lease_owner));
}

void EditorSessionPaths::free() {
	if (!singleton) {
		return;
	}
	memdelete(singleton);
	singleton = nullptr;
}

void EditorSessionPaths::_bootstrap_file(const String &p_file_name) {
	const String source = ProjectSettings::get_singleton()->get_project_data_path().path_join(p_file_name);
	const String destination = session_data_dir.path_join(p_file_name);
	if (!FileAccess::exists(destination) && FileAccess::exists(source)) {
		const Error err = DirAccess::copy_absolute(source, destination);
		ERR_FAIL_COND_MSG(err != OK, vformat("Could not initialize editor session file '%s'.", destination));
	}
}

void EditorSessionPaths::_write_lease(uint64_t p_ticks_usec) {
	if (!lease_owner) {
		return;
	}

	Ref<FileAccess> lease = FileAccess::open(lease_file, FileAccess::WRITE);
	if (lease.is_null()) {
		ERR_PRINT("Could not write editor session lease: " + lease_file);
		return;
	}

	lease->store_line("[lease]");
	lease->store_line("session_id=\"" + ProjectSettings::get_singleton()->get_editor_session_id() + "\"");
	lease->store_line("process_id=" + itos(OS::get_singleton()->get_process_id()));
	lease->store_line("heartbeat_usec=" + uitos(p_ticks_usec));
	last_heartbeat_usec = p_ticks_usec;
}

void EditorSessionPaths::heartbeat(uint64_t p_ticks_usec) {
	static constexpr uint64_t HEARTBEAT_INTERVAL_USEC = 2'000'000;
	if (lease_owner && p_ticks_usec - last_heartbeat_usec >= HEARTBEAT_INTERVAL_USEC) {
		_write_lease(p_ticks_usec);
	}
}

EditorSessionPaths::EditorSessionPaths(bool p_lease_owner) {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
	lease_owner = p_lease_owner;

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	ERR_FAIL_COND(!project_settings->has_editor_session());

	session_data_dir = project_settings->get_project_session_data_path();
	editor_data_dir = session_data_dir.path_join("editor");
	shader_cache_dir = session_data_dir.path_join("shader_cache");
	staging_dir = session_data_dir.path_join("staging");
	lease_file = session_data_dir.path_join("lease.cfg");

	const String directories[] = {
		project_settings->get_project_data_path(),
		session_data_dir,
		editor_data_dir,
		shader_cache_dir,
		staging_dir,
	};
	for (const String &directory : directories) {
		const Error err = DirAccess::make_dir_recursive_absolute(directory);
		ERR_CONTINUE_MSG(err != OK && err != ERR_ALREADY_EXISTS, vformat("Could not create editor session directory '%s'.", directory));
	}

	const String ignored_directories[] = {
		project_settings->get_project_data_path(),
		session_data_dir,
	};
	for (const String &ignored_directory : ignored_directories) {
		const String gdignore = ignored_directory.path_join(".gdignore");
		if (!FileAccess::exists(gdignore)) {
			Ref<FileAccess> file = FileAccess::open(gdignore, FileAccess::WRITE);
			if (file.is_valid()) {
				file->store_line("");
			}
		}
	}

	_bootstrap_file("extension_list.cfg");
	_bootstrap_file("global_script_class_cache.cfg");
	_bootstrap_file("scene_groups_cache.cfg");
	_bootstrap_file("uid_cache.bin");

	if (lease_owner) {
		_write_lease(OS::get_singleton()->get_ticks_usec());
	}
}

EditorSessionPaths::~EditorSessionPaths() {
	if (lease_owner) {
		DirAccess::remove_absolute(lease_file);
	}
}
