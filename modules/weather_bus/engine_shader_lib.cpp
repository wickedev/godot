/**************************************************************************/
/*  engine_shader_lib.cpp                                                 */
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

#include "engine_shader_lib.h"

#include "core/io/resource_loader.h"

HashMap<String, Ref<ShaderInclude>> EngineShaderLib::published;

const char *EngineShaderLib::PATH_PREFIX = "engine://shaderlib/";

String EngineShaderLib::make_path(const String &p_name) {
	return String(PATH_PREFIX) + p_name + ".gdshaderinc";
}

String EngineShaderLib::publish(const String &p_name, const String &p_code) {
	// A name with a separator in it would land somewhere other than the caller thinks, and an empty
	// one would publish the prefix itself.
	ERR_FAIL_COND_V_MSG(p_name.is_empty(), String(), "Engine shader library name cannot be empty.");
	ERR_FAIL_COND_V_MSG(p_name != p_name.validate_filename() || p_name.contains("/") || p_name.contains("\\"),
			String(), vformat("Invalid engine shader library name: '%s'.", p_name));

	const String path = make_path(p_name);

	ERR_FAIL_COND_V_MSG(published.has(path), String(),
			vformat("Engine shader library '%s' is already published.", path));
	// Something else already owns this path -- a project resource, or another publisher that did
	// not go through here. Overwriting it would silently change what every shader including it
	// gets, so refuse instead.
	ERR_FAIL_COND_V_MSG(ResourceLoader::exists(path), String(),
			vformat("Something is already registered at '%s'.", path));

	Ref<ShaderInclude> include;
	include.instantiate();
	// Path before code: relative `#include` directives inside p_code resolve against the including
	// resource's path, so setting the code first would have them resolved against res://.
	include->set_path(path);
	ERR_FAIL_COND_V_MSG(include->get_path() != path, String(),
			vformat("Could not register an engine shader library at '%s'.", path));
	include->set_code(p_code);

	// Held for the lifetime of the entry: ShaderInclude is reference-counted, and dropping the last
	// reference would evict it from ResourceCache and break every shader including it.
	published[path] = include;

	return path;
}

void EngineShaderLib::unpublish(const String &p_name) {
	const String path = make_path(p_name);
	HashMap<String, Ref<ShaderInclude>>::Iterator entry = published.find(path);
	if (!entry) {
		return;
	}

	// Dropping our reference is not enough to free the path. A Shader that already includes this
	// library holds a reference of its own, so the resource outlives us and stays in ResourceCache
	// -- which would make a later publish() of the same name collide with a stale copy, and leave
	// shaders compiled afterwards reading the old code. Clearing the path detaches it from the
	// cache immediately (Resource::set_path erases the old entry), leaving existing holders with a
	// pathless resource rather than a squatted name.
	if (entry->value.is_valid()) {
		entry->value->set_path("");
	}
	published.remove(entry);
}

bool EngineShaderLib::is_published(const String &p_name) {
	return published.has(make_path(p_name));
}
