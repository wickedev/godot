/**************************************************************************/
/*  nanite_import_plugin.h                                                */
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

#include "editor/import/3d/resource_importer_scene.h"

// Builds a Nanite cluster DAG for every imported mesh surface that opts in.
//
// The DAG is attached to the ImporterMesh as metadata under a `nanite_dag_`
// prefix. That is deliberately a provisional carrier, not a format decision:
// where the DAG ultimately lives is settled with the L1/L3 geometry pool
// contract at gate G2.
class NaniteImportPlugin : public EditorScenePostImportPlugin {
	GDCLASS(NaniteImportPlugin, EditorScenePostImportPlugin);

public:
	// Metadata key prefix, one entry per surface: `nanite_dag_0`, `nanite_dag_1`, ...
	static const char *METADATA_PREFIX;

private:
	// internal_process() is handed only the per-mesh subresource options, so
	// anything set at scene scope has to be captured on the way past. The
	// plugin instance is registered once and shared, while EditorFileSystem
	// reimports files across a WorkerThreadPool, so this state is per-thread
	// rather than per-instance. pre_process and the mesh loop for one file run
	// on the same thread.
	static inline thread_local int scene_light_bake_mode = 0;
	static inline thread_local String scene_root_name;

public:
	virtual void pre_process(Node *p_scene, const HashMap<StringName, Variant> &p_options) override;
	virtual void get_internal_import_options(InternalImportCategory p_category, List<ResourceImporter::ImportOption> *r_options) override;
	virtual Variant get_internal_option_visibility(InternalImportCategory p_category, const String &p_scene_import_type, const String &p_option, const HashMap<StringName, Variant> &p_options) const override;
	virtual void internal_process(InternalImportCategory p_category, Node *p_base_scene, Node *p_node, Ref<Resource> p_resource, const Dictionary &p_options) override;
};
