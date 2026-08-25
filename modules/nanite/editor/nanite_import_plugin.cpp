/**************************************************************************/
/*  nanite_import_plugin.cpp                                              */
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

#include "nanite_import_plugin.h"

#include "../nanite_dag.h"
#include "../nanite_dag_builder.h"

#include "core/io/resource_importer.h"
#include "core/string/print_string.h"
#include "scene/resources/3d/importer_mesh.h"

const char *NaniteImportPlugin::METADATA_PREFIX = "nanite_dag_";

void NaniteImportPlugin::get_internal_import_options(InternalImportCategory p_category, List<ResourceImporter::ImportOption> *r_options) {
	if (p_category != INTERNAL_IMPORT_CATEGORY_MESH) {
		return;
	}
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "nanite/enabled"), false));
	// Capped at 128, not at meshoptimizer's 512: the S3 visibility buffer
	// spends only 7 bits on the triangle index (30b depth + 27b cluster +
	// 7b triangle, nanite-impl research doc section 1.4), so anything above
	// 128 produces assets the rasterizer cannot address. The builder itself
	// still accepts up to 512 for offline experiments.
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "nanite/max_cluster_triangles", PROPERTY_HINT_RANGE, "16,128,1"), 128));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "nanite/group_size", PROPERTY_HINT_RANGE, "2,32,1"), 8));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "nanite/simplify_ratio", PROPERTY_HINT_RANGE, "0.1,0.9,0.05"), 0.5f));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "nanite/spatial_clustering"), false));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "nanite/print_report"), false));
}

Variant NaniteImportPlugin::get_internal_option_visibility(InternalImportCategory p_category, const String &p_scene_import_type, const String &p_option, const HashMap<StringName, Variant> &p_options) const {
	if (p_category != INTERNAL_IMPORT_CATEGORY_MESH || !p_option.begins_with("nanite/") || p_option == "nanite/enabled") {
		return Variant();
	}
	// Everything else only matters once the mesh actually opts in.
	const Variant *enabled = p_options.getptr("nanite/enabled");
	return enabled ? (bool)*enabled : false;
}

void NaniteImportPlugin::internal_process(InternalImportCategory p_category, Node *p_base_scene, Node *p_node, Ref<Resource> p_resource, const Dictionary &p_options) {
	if (p_category != INTERNAL_IMPORT_CATEGORY_MESH) {
		return;
	}
	if (!p_options.has("nanite/enabled") || !(bool)p_options["nanite/enabled"]) {
		return;
	}
	Ref<ImporterMesh> mesh = p_resource;
	if (mesh.is_null()) {
		return;
	}

	// Drop anything a previous import left behind before deciding whether to
	// build again. Otherwise turning the option off, or a surface that now
	// fails to build, silently keeps serving the old DAG.
	List<StringName> existing_meta;
	mesh->get_meta_list(&existing_meta);
	for (const StringName &key : existing_meta) {
		if (String(key).begins_with(METADATA_PREFIX)) {
			mesh->remove_meta(key);
		}
	}

	NaniteDAGBuilder::Settings settings;
	if (p_options.has("nanite/max_cluster_triangles")) {
		settings.max_cluster_triangles = (uint32_t)(int)p_options["nanite/max_cluster_triangles"];
	}
	if (p_options.has("nanite/group_size")) {
		settings.group_size = (uint32_t)(int)p_options["nanite/group_size"];
	}
	if (p_options.has("nanite/simplify_ratio")) {
		settings.simplify_ratio = (float)p_options["nanite/simplify_ratio"];
	}
	if (p_options.has("nanite/spatial_clustering")) {
		settings.spatial_clustering = (bool)p_options["nanite/spatial_clustering"];
	}
	const bool print_report = p_options.has("nanite/print_report") && (bool)p_options["nanite/print_report"];

	const String mesh_name = mesh->get_name().is_empty() ? String("<unnamed>") : mesh->get_name();

	// Blend shapes move vertices at runtime, so the offline cluster bounds and
	// LOD errors would describe a pose the mesh is never actually in. Skinned
	// surfaces are refused by the builder for the same reason.
	if (mesh->get_blend_shape_count() > 0) {
		WARN_PRINT(vformat("Nanite: skipping '%s', stage S1 supports static geometry only and this mesh has blend shapes.", mesh_name));
		return;
	}

	// With several surfaces the open border of each one is a seam shared with
	// another surface that simplifies independently.
	settings.lock_mesh_border = mesh->get_surface_count() > 1;

	// This hook is the only one the scene importer offers for a mesh, and it
	// runs before generate_lods, create_shadow_mesh, optimize_indices and any
	// lightmap unwrap. The DAG is therefore a self-contained snapshot of the
	// surface as it stands here: it carries its own positions and indices and
	// does not reference the ArrayMesh that those later steps go on to reorder.
	// Lightmap unwrapping is the one that actually diverges, since it splits
	// vertices and adds UV2 the snapshot will not have.
	if (p_options.has("generate/lightmap_uv") && (int)p_options["generate/lightmap_uv"] == 1) {
		WARN_PRINT(vformat("Nanite: '%s' has lightmap unwrapping enabled. The DAG is built before unwrapping, so it does not carry UV2 and its vertex layout differs from the saved mesh.", mesh_name));
	}

	for (int surface = 0; surface < mesh->get_surface_count(); surface++) {
		if (mesh->get_surface_primitive_type(surface) != Mesh::PRIMITIVE_TRIANGLES) {
			WARN_PRINT(vformat("Nanite: skipping surface %d of '%s', only triangle surfaces can be clustered.", surface, mesh_name));
			continue;
		}

		String error;
		const Ref<NaniteDAG> dag = NaniteDAGBuilder::build_from_surface(mesh->get_surface_arrays(surface), settings, &error);
		if (dag.is_null()) {
			// The builder already reported the specifics; keep the import
			// going so one bad surface does not abort the whole scene.
			WARN_PRINT(vformat("Nanite: could not build a DAG for surface %d of '%s': %s", surface, mesh_name, error));
			continue;
		}

		mesh->set_meta(String(METADATA_PREFIX) + itos(surface), dag);

		if (print_report) {
			print_line(vformat("Nanite DAG for surface %d of '%s':\n%s", surface, mesh_name, dag->get_report()));
		}
	}
}
