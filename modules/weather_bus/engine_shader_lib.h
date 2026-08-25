/**************************************************************************/
/*  engine_shader_lib.h                                                   */
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
#include "core/templates/hash_map.h"
#include "scene/resources/shader_include.h"

// Publishes shader includes that ship with the engine under `engine://shaderlib/<name>.gdshaderinc`,
// so any shader in any project can `#include` them without the project vendoring a copy.
//
// No new ResourceFormatLoader is needed. The shader preprocessor resolves includes through
// ResourceLoader::exists() / ::load() (shader_preprocessor.cpp), and both consult ResourceCache
// first (resource_loader.cpp) -- so a ShaderInclude that already carries the path is simply found.
// `engine://` survives path normalization because String::is_absolute_path() treats anything
// containing ":/" as absolute, which keeps ResourceLoader from rewriting it to `res://`.
//
// Shared across lanes: several subsystems are expected to publish libraries here. Entries are
// therefore owned individually. There is deliberately no "remove everything" call, because one
// module tearing down must not take another module's includes with it.
class EngineShaderLib {
	static HashMap<String, Ref<ShaderInclude>> published;

public:
	static const char *PATH_PREFIX;

	// Returns the full `engine://` path on success, or an empty string on failure -- an invalid
	// name, or a name already published by someone else.
	static String publish(const String &p_name, const String &p_code);

	// Removes a single entry. Only the publisher should call this, and only for its own names.
	static void unpublish(const String &p_name);

	static bool is_published(const String &p_name);

	// Full path for a name, whether or not it is published. Useful for error messages and tests.
	static String make_path(const String &p_name);
};
