/**************************************************************************/
/*  nanite_dag.cpp                                                        */
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

#include "nanite_dag.h"

#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "scene/resources/mesh.h"

uint32_t NaniteDAG::get_level_count() const {
	return level_offsets.is_empty() ? 0 : level_offsets.size() - 1;
}

uint32_t NaniteDAG::get_level_cluster_count(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), 0);
	return level_offsets[p_level + 1] - level_offsets[p_level];
}

uint32_t NaniteDAG::get_level_triangle_count(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), 0);
	uint32_t total = 0;
	for (uint32_t i = level_offsets[p_level]; i < level_offsets[p_level + 1]; i++) {
		total += clusters[i].index_count / 3;
	}
	return total;
}

float NaniteDAG::get_level_max_error(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), 0.0f);
	float max_error = 0.0f;
	for (uint32_t i = level_offsets[p_level]; i < level_offsets[p_level + 1]; i++) {
		max_error = MAX(max_error, clusters[i].error);
	}
	return max_error;
}

// Tolerance for the monotonicity comparisons, scaled by the mesh so that it
// stays meaningful for both centimetre-scale props and kilometre-scale terrain.
float NaniteDAG::_compute_epsilon() const {
	float extent = 0.0f;
	for (uint32_t i = 0; i < vertex_count; i++) {
		extent = MAX(extent, Math::abs(positions[i * 3 + 0]));
		extent = MAX(extent, Math::abs(positions[i * 3 + 1]));
		extent = MAX(extent, Math::abs(positions[i * 3 + 2]));
	}
	return 1e-4f * MAX(1.0f, extent);
}

Vector<String> NaniteDAG::validate() const {
	Vector<String> errors;
	const float epsilon = _compute_epsilon();

	if (level_offsets.is_empty() || level_offsets[level_offsets.size() - 1] != clusters.size()) {
		errors.push_back("Level offset table does not span the cluster array.");
		return errors;
	}

	for (uint32_t i = 0; i < clusters.size(); i++) {
		const Cluster &c = clusters[i];
		const String at = vformat("Cluster %d (level %d): ", i, c.level);

		if (c.index_count == 0 || c.index_count % 3 != 0) {
			errors.push_back(at + vformat("index count %d is not a positive multiple of 3.", c.index_count));
		}
		if ((uint64_t)c.index_offset + c.index_count > indices.size()) {
			errors.push_back(at + "index range runs past the end of the index buffer.");
			continue;
		}
		for (uint32_t k = 0; k < c.index_count; k++) {
			if (indices[c.index_offset + k] >= vertex_count) {
				errors.push_back(at + "references a vertex outside the shared vertex buffer.");
				break;
			}
		}
		if (!Math::is_finite(c.error) || c.error < 0.0f) {
			errors.push_back(at + vformat("error %f is negative or not finite.", c.error));
		}
		if (c.lod_bounds.radius < 0.0f || c.bounds.radius < 0.0f) {
			errors.push_back(at + "has a negative bounding sphere radius.");
		}

		// The cluster was produced by simplifying `source_group`, so it must
		// carry that group's LOD decision verbatim. This is what makes every
		// cluster of a group flip at the same threshold, i.e. crack-free.
		if (c.source_group == NO_GROUP) {
			if (c.level != 0) {
				errors.push_back(at + "has no source group but is not on level 0.");
			}
			if (c.error != 0.0f) {
				errors.push_back(at + "is an unsimplified level 0 cluster but has non-zero error.");
			}
		} else if (c.source_group >= groups.size()) {
			errors.push_back(at + "source group index is out of range.");
		} else {
			const Group &g = groups[c.source_group];
			if (g.level + 1 != c.level) {
				errors.push_back(at + vformat("source group is on level %d, expected %d.", g.level, c.level - 1));
			}
			if (c.error != g.error) {
				errors.push_back(at + "error differs from its source group's error (LOD decision is not uniform across the group).");
			}
			if (c.lod_bounds.center != g.lod_bounds.center || c.lod_bounds.radius != g.lod_bounds.radius) {
				errors.push_back(at + "LOD bounds differ from its source group's bounds (LOD decision is not uniform across the group).");
			}
		}

		// MONOTONICITY. The group that simplifies this cluster away must have
		// an error at least as large, otherwise the cut intervals overlap and
		// a cluster can be drawn together with its own coarser replacement.
		if (c.parent_group == NO_GROUP) {
			if (!Math::is_inf(c.parent_error)) {
				errors.push_back(at + "is a DAG root but its parent error is finite.");
			}
		} else if (c.parent_group >= groups.size()) {
			errors.push_back(at + "parent group index is out of range.");
		} else {
			const Group &g = groups[c.parent_group];
			if (g.level != c.level) {
				errors.push_back(at + vformat("parent group is on level %d, expected %d.", g.level, c.level));
			}
			if (g.error + epsilon < c.error) {
				errors.push_back(at + vformat("MONOTONICITY VIOLATED: parent error %f is below the cluster's own error %f.", g.error, c.error));
			}
			if (!g.lod_bounds.contains(c.lod_bounds, epsilon)) {
				errors.push_back(at + "MONOTONICITY VIOLATED: parent LOD sphere does not enclose the cluster's LOD sphere.");
			}
			if (c.parent_error != g.error) {
				errors.push_back(at + "cached parent error does not match its parent group.");
			}
			if (c.parent_lod_bounds.center != g.lod_bounds.center || c.parent_lod_bounds.radius != g.lod_bounds.radius) {
				errors.push_back(at + "cached parent LOD bounds do not match its parent group.");
			}
		}
	}

	for (uint32_t i = 0; i < groups.size(); i++) {
		const Group &g = groups[i];
		const String at = vformat("Group %d (level %d): ", i, g.level);

		if (g.children.is_empty()) {
			errors.push_back(at + "simplifies no clusters.");
		}
		if (g.produced.is_empty()) {
			errors.push_back(at + "produced no clusters, so the cut has a hole above it.");
		}
		if (!Math::is_finite(g.error) || g.error < 0.0f) {
			errors.push_back(at + vformat("error %f is negative or not finite.", g.error));
		}
		for (uint32_t child : g.children) {
			if (child >= clusters.size()) {
				errors.push_back(at + "child index is out of range.");
				continue;
			}
			if (clusters[child].parent_group != i) {
				errors.push_back(at + vformat("child %d does not point back at this group.", child));
			}
		}
		for (uint32_t produced : g.produced) {
			if (produced >= clusters.size()) {
				errors.push_back(at + "produced index is out of range.");
				continue;
			}
			if (clusters[produced].source_group != i) {
				errors.push_back(at + vformat("produced cluster %d does not point back at this group.", produced));
			}
		}
	}

	return errors;
}

LocalVector<uint32_t> NaniteDAG::select_cut(float p_threshold) const {
	LocalVector<uint32_t> cut;
	for (uint32_t i = 0; i < clusters.size(); i++) {
		const Cluster &c = clusters[i];
		if (c.error <= p_threshold && p_threshold < c.parent_error) {
			cut.push_back(i);
		}
	}
	return cut;
}

String NaniteDAG::get_report() const {
	String report = vformat("Nanite DAG: %d levels, %d clusters, %d groups, %d triangles, %d vertices\n",
			get_level_count(), get_cluster_count(), get_group_count(), get_triangle_count(), vertex_count);
	report += "  level | clusters | triangles | max error\n";
	report += "  ------+----------+-----------+----------\n";
	for (uint32_t level = 0; level < get_level_count(); level++) {
		report += vformat("  %5d | %8d | %9d | %.6f\n",
				level, get_level_cluster_count(level), get_level_triangle_count(level), get_level_max_error(level));
	}
	return report;
}

Ref<ArrayMesh> NaniteDAG::create_level_debug_mesh(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), Ref<ArrayMesh>());
	LocalVector<uint32_t> level_clusters;
	for (uint32_t i = level_offsets[p_level]; i < level_offsets[p_level + 1]; i++) {
		level_clusters.push_back(i);
	}
	return _create_debug_mesh(level_clusters);
}

Ref<ArrayMesh> NaniteDAG::create_cut_debug_mesh(float p_threshold) const {
	return _create_debug_mesh(select_cut(p_threshold));
}

Ref<ArrayMesh> NaniteDAG::_create_debug_mesh(const LocalVector<uint32_t> &p_clusters) const {
	Ref<ArrayMesh> mesh;
	ERR_FAIL_COND_V(p_clusters.is_empty(), mesh);

	uint32_t triangle_count = 0;
	for (uint32_t cluster : p_clusters) {
		triangle_count += clusters[cluster].index_count / 3;
	}

	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors;
	vertices.resize(triangle_count * 3);
	normals.resize(triangle_count * 3);
	colors.resize(triangle_count * 3);
	Vector3 *vertices_w = vertices.ptrw();
	Vector3 *normals_w = normals.ptrw();
	Color *colors_w = colors.ptrw();

	uint32_t out = 0;
	for (uint32_t cluster : p_clusters) {
		const Cluster &c = clusters[cluster];
		// Golden-ratio hue stepping keeps neighbouring cluster ids visually apart.
		const Color color = Color::from_hsv(Math::fmod(cluster * 0.618033988f, 1.0f), 0.65f, 0.95f);

		for (uint32_t t = 0; t < c.index_count; t += 3) {
			Vector3 p[3];
			for (uint32_t k = 0; k < 3; k++) {
				const uint32_t v = indices[c.index_offset + t + k];
				p[k] = Vector3(positions[v * 3 + 0], positions[v * 3 + 1], positions[v * 3 + 2]);
			}
			// Flat shading makes individual clusters readable in the viewport.
			const Vector3 normal = (p[1] - p[0]).cross(p[2] - p[0]).normalized();
			for (uint32_t k = 0; k < 3; k++) {
				vertices_w[out] = p[k];
				normals_w[out] = normal;
				colors_w[out] = color;
				out++;
			}
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;

	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

PackedStringArray NaniteDAG::_validate_bind() const {
	PackedStringArray out;
	for (const String &error : validate()) {
		out.push_back(error);
	}
	return out;
}

PackedInt32Array NaniteDAG::_select_cut_bind(float p_threshold) const {
	PackedInt32Array out;
	for (uint32_t cluster : select_cut(p_threshold)) {
		out.push_back((int32_t)cluster);
	}
	return out;
}

Dictionary NaniteDAG::_get_statistics_bind() const {
	Dictionary stats;
	stats["level_count"] = get_level_count();
	stats["cluster_count"] = get_cluster_count();
	stats["group_count"] = get_group_count();
	stats["triangle_count"] = get_triangle_count();
	stats["vertex_count"] = vertex_count;

	Array levels;
	for (uint32_t level = 0; level < get_level_count(); level++) {
		Dictionary entry;
		entry["clusters"] = get_level_cluster_count(level);
		entry["triangles"] = get_level_triangle_count(level);
		entry["max_error"] = get_level_max_error(level);
		levels.push_back(entry);
	}
	stats["levels"] = levels;
	return stats;
}

void NaniteDAG::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_level_count"), &NaniteDAG::get_level_count);
	ClassDB::bind_method(D_METHOD("get_cluster_count"), &NaniteDAG::get_cluster_count);
	ClassDB::bind_method(D_METHOD("get_group_count"), &NaniteDAG::get_group_count);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &NaniteDAG::get_triangle_count);
	ClassDB::bind_method(D_METHOD("get_level_cluster_count", "level"), &NaniteDAG::get_level_cluster_count);
	ClassDB::bind_method(D_METHOD("get_level_triangle_count", "level"), &NaniteDAG::get_level_triangle_count);
	ClassDB::bind_method(D_METHOD("get_level_max_error", "level"), &NaniteDAG::get_level_max_error);
	ClassDB::bind_method(D_METHOD("get_statistics"), &NaniteDAG::_get_statistics_bind);
	ClassDB::bind_method(D_METHOD("get_report"), &NaniteDAG::get_report);
	ClassDB::bind_method(D_METHOD("validate"), &NaniteDAG::_validate_bind);
	ClassDB::bind_method(D_METHOD("select_cut", "threshold"), &NaniteDAG::_select_cut_bind);
	ClassDB::bind_method(D_METHOD("create_level_debug_mesh", "level"), &NaniteDAG::create_level_debug_mesh);
	ClassDB::bind_method(D_METHOD("create_cut_debug_mesh", "threshold"), &NaniteDAG::create_cut_debug_mesh);
}
