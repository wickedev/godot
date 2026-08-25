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
#include "core/string/string_name.h"
#include "scene/resources/mesh.h"

uint32_t NaniteDAG::encode_normal(const Vector3 &p_normal) {
	Vector3 n = p_normal;
	const real_t length = n.length();
	n = length > 0.0 ? n / length : Vector3(0, 0, 1);

	const real_t l1 = Math::abs(n.x) + Math::abs(n.y) + Math::abs(n.z);
	real_t x = n.x / l1;
	real_t y = n.y / l1;
	if (n.z < 0.0) {
		const real_t fold_x = (1.0 - Math::abs(y)) * (x >= 0.0 ? 1.0 : -1.0);
		const real_t fold_y = (1.0 - Math::abs(x)) * (y >= 0.0 ? 1.0 : -1.0);
		x = fold_x;
		y = fold_y;
	}
	const uint32_t qx = (uint32_t)CLAMP(Math::round((x * 0.5 + 0.5) * 65535.0), 0.0, 65535.0);
	const uint32_t qy = (uint32_t)CLAMP(Math::round((y * 0.5 + 0.5) * 65535.0), 0.0, 65535.0);
	return qx | (qy << 16);
}

Vector3 NaniteDAG::decode_normal(uint32_t p_encoded) {
	const real_t x = ((real_t)(p_encoded & 0xFFFF) / 65535.0) * 2.0 - 1.0;
	const real_t y = ((real_t)(p_encoded >> 16) / 65535.0) * 2.0 - 1.0;
	Vector3 n(x, y, 1.0 - Math::abs(x) - Math::abs(y));
	if (n.z < 0.0) {
		const real_t fold_x = (1.0 - Math::abs(n.y)) * (n.x >= 0.0 ? 1.0 : -1.0);
		const real_t fold_y = (1.0 - Math::abs(n.x)) * (n.y >= 0.0 ? 1.0 : -1.0);
		n.x = fold_x;
		n.y = fold_y;
	}
	return n.normalized();
}

uint32_t NaniteDAG::encode_uv(float p_u, float p_v) {
	return (uint32_t)Math::make_half_float(p_u) | ((uint32_t)Math::make_half_float(p_v) << 16);
}

void NaniteDAG::decode_uv(uint32_t p_encoded, float &r_u, float &r_v) {
	r_u = Math::half_to_float((uint16_t)(p_encoded & 0xFFFF));
	r_v = Math::half_to_float((uint16_t)(p_encoded >> 16));
}

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
		total += clusters[i].triangle_count;
	}
	return total;
}

float NaniteDAG::get_cluster_error(uint32_t p_cluster) const {
	ERR_FAIL_COND_V(p_cluster >= clusters.size(), 0.0f);
	const uint32_t source = clusters[p_cluster].source_group;
	// Level 0 is the input geometry, so it carries no error at all.
	return source == NO_GROUP ? 0.0f : groups[source].error;
}

float NaniteDAG::get_cluster_parent_error(uint32_t p_cluster) const {
	ERR_FAIL_COND_V(p_cluster >= clusters.size(), 0.0f);
	const uint32_t parent = clusters[p_cluster].parent_group;
	// A root has nothing coarser to defer to, so it is drawn at any threshold.
	return parent == NO_GROUP ? INFINITY : groups[parent].error;
}

NaniteDAG::Sphere NaniteDAG::get_cluster_lod_bounds(uint32_t p_cluster) const {
	ERR_FAIL_COND_V(p_cluster >= clusters.size(), Sphere());
	const uint32_t source = clusters[p_cluster].source_group;
	return source == NO_GROUP ? clusters[p_cluster].bounds : groups[source].lod_bounds;
}

NaniteDAG::Sphere NaniteDAG::get_cluster_parent_lod_bounds(uint32_t p_cluster) const {
	ERR_FAIL_COND_V(p_cluster >= clusters.size(), Sphere());
	const uint32_t parent = clusters[p_cluster].parent_group;
	return parent == NO_GROUP ? get_cluster_lod_bounds(p_cluster) : groups[parent].lod_bounds;
}

float NaniteDAG::get_level_max_error(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), 0.0f);
	float max_error = 0.0f;
	for (uint32_t i = level_offsets[p_level]; i < level_offsets[p_level + 1]; i++) {
		max_error = MAX(max_error, get_cluster_error(i));
	}
	return max_error;
}

uint32_t NaniteDAG::get_cluster_vertex(uint32_t p_cluster, uint32_t p_corner) const {
	const Cluster &cluster = clusters[p_cluster];
	const uint8_t local = cluster_indices[cluster.triangle_offset + p_corner];
	return cluster_vertices[cluster.vertex_offset + local];
}

uint32_t NaniteDAG::get_cluster_vertex_count(uint32_t p_cluster) const {
	ERR_FAIL_COND_V(p_cluster >= clusters.size(), 0);
	return clusters[p_cluster].vertex_count;
}

uint32_t NaniteDAG::get_level_vertex_slice_total(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), 0);
	uint32_t total = 0;
	for (uint32_t i = level_offsets[p_level]; i < level_offsets[p_level + 1]; i++) {
		total += clusters[i].vertex_count;
	}
	return total;
}

uint32_t NaniteDAG::get_level_distinct_vertex_count(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), 0);
	LocalVector<uint8_t> seen;
	seen.resize(vertex_count);
	for (uint32_t i = 0; i < vertex_count; i++) {
		seen[i] = 0;
	}
	uint32_t distinct = 0;
	for (uint32_t i = level_offsets[p_level]; i < level_offsets[p_level + 1]; i++) {
		const Cluster &cluster = clusters[i];
		for (uint32_t k = 0; k < cluster.vertex_count; k++) {
			const uint32_t vertex = cluster_vertices[cluster.vertex_offset + k];
			if (!seen[vertex]) {
				seen[vertex] = 1;
				distinct++;
			}
		}
	}
	return distinct;
}

// Tolerance for the monotonicity comparisons, scaled by the mesh so that it
// stays meaningful for both centimetre-scale props and kilometre-scale terrain.
float NaniteDAG::_compute_epsilon() const {
	// The mesh's own extent, not its distance from the origin: a small object
	// far from the origin would otherwise get a wildly oversized tolerance.
	if (vertex_count == 0 || positions.size() < 3) {
		return 1e-4f;
	}
	float min_corner[3] = { positions[0], positions[1], positions[2] };
	float max_corner[3] = { positions[0], positions[1], positions[2] };
	for (uint32_t i = 1; i < vertex_count; i++) {
		for (uint32_t axis = 0; axis < 3; axis++) {
			min_corner[axis] = MIN(min_corner[axis], positions[i * 3 + axis]);
			max_corner[axis] = MAX(max_corner[axis], positions[i * 3 + axis]);
		}
	}
	float extent = 0.0f;
	for (uint32_t axis = 0; axis < 3; axis++) {
		extent = MAX(extent, max_corner[axis] - min_corner[axis]);
	}
	return 1e-4f * MAX(1.0f, extent);
}

Vector<String> NaniteDAG::validate() const {
	Vector<String> errors;
	if (!load_error.is_empty()) {
		errors.push_back(load_error);
		return errors;
	}

	// Structural checks first, and bail before anything indexes with a value
	// these would have rejected.
	if (positions.size() != (uint64_t)vertex_count * 3) {
		errors.push_back(vformat("Position buffer holds %d floats for %d vertices.", positions.size(), vertex_count));
	}
	if (normals.size() != vertex_count) {
		errors.push_back(vformat("Normal buffer holds %d entries for %d vertices.", normals.size(), vertex_count));
	}
	if (uvs.size() != vertex_count) {
		errors.push_back(vformat("UV buffer holds %d entries for %d vertices.", uvs.size(), vertex_count));
	}
	if (deviation_samples_per_triangle == 0 && !groups.is_empty()) {
		// Without it the stored error is an uninterpretable number: a consumer
		// cannot tell a densely measured deviation from a sparsely measured one.
		errors.push_back("Deviation sampling density is unrecorded, so the stored error cannot be interpreted.");
	}
	if (level_offsets.is_empty()) {
		errors.push_back("Level offset table is empty.");
	} else {
		if (level_offsets[0] != 0) {
			errors.push_back("Level offset table does not start at zero.");
		}
		for (uint32_t i = 1; i < level_offsets.size(); i++) {
			if (level_offsets[i] < level_offsets[i - 1]) {
				errors.push_back("Level offset table is not monotonic.");
				break;
			}
		}
		if (level_offsets[level_offsets.size() - 1] != clusters.size()) {
			errors.push_back("Level offset table does not span the cluster array.");
		}
	}
	for (uint32_t i = 0; i < positions.size(); i++) {
		if (!Math::is_finite(positions[i])) {
			errors.push_back("Position buffer contains a non-finite value.");
			break;
		}
	}
	if (!errors.is_empty()) {
		return errors;
	}

	const float epsilon = _compute_epsilon();

	for (uint32_t i = 0; i < clusters.size(); i++) {
		const Cluster &c = clusters[i];
		const String at = vformat("Cluster %d (level %d): ", i, c.level);

		if (c.triangle_count == 0 || c.triangle_count > MAX_CLUSTER_TRIANGLES) {
			errors.push_back(at + vformat("triangle count %d is outside [1, %d].", c.triangle_count, MAX_CLUSTER_TRIANGLES));
			continue;
		}
		if (c.vertex_count == 0 || c.vertex_count > MAX_CLUSTER_VERTICES) {
			errors.push_back(at + vformat("vertex count %d is outside [1, %d].", c.vertex_count, MAX_CLUSTER_VERTICES));
			continue;
		}
		if ((uint64_t)c.vertex_offset + c.vertex_count > cluster_vertices.size()) {
			errors.push_back(at + "vertex slice runs past the end of the slice table.");
			continue;
		}
		if ((uint64_t)c.triangle_offset + (uint64_t)c.triangle_count * 3 > cluster_indices.size()) {
			errors.push_back(at + "triangle range runs past the end of the index buffer.");
			continue;
		}
		bool slice_ok = true;
		for (uint32_t k = 0; k < c.vertex_count; k++) {
			if (cluster_vertices[c.vertex_offset + k] >= vertex_count) {
				errors.push_back(at + "slice references a vertex outside the shared buffer.");
				slice_ok = false;
				break;
			}
		}
		if (!slice_ok) {
			continue;
		}
		for (uint32_t k = 0; k < c.triangle_count * 3; k++) {
			if (cluster_indices[c.triangle_offset + k] >= c.vertex_count) {
				errors.push_back(at + "local index points outside the cluster's own slice.");
				break;
			}
		}
		if (!Math::is_finite(c.bounds.radius) || c.bounds.radius < 0.0f) {
			errors.push_back(at + "has a non-finite or negative bounding sphere.");
		}
		if (!Math::is_finite(c.cone_cutoff) || !c.cone_axis.is_finite() || !c.cone_apex.is_finite()) {
			errors.push_back(at + "has a non-finite normal cone.");
		}

		// Both group references are range-checked before anything reads
		// through them: validate() runs on freshly parsed data, where a
		// corrupted index would otherwise index the group array out of bounds.
		if (c.source_group != NO_GROUP && c.source_group >= groups.size()) {
			errors.push_back(at + "source group index is out of range.");
			continue;
		}
		if (c.parent_group != NO_GROUP && c.parent_group >= groups.size()) {
			errors.push_back(at + "parent group index is out of range.");
			continue;
		}

		const float error = get_cluster_error(i);
		if (!Math::is_finite(error) || error < 0.0f) {
			errors.push_back(at + vformat("error %f is negative or not finite.", error));
		}

		if (c.source_group == NO_GROUP) {
			if (c.level != 0) {
				errors.push_back(at + "has no source group but is not on level 0.");
			}
		} else if (groups[c.source_group].level + 1 != c.level) {
			errors.push_back(at + vformat("source group is on level %d, expected %d.", groups[c.source_group].level, c.level - 1));
		}

		// MONOTONICITY. The group that simplifies this cluster away must have
		// an error at least as large, otherwise the cut intervals overlap and a
		// cluster can be drawn together with its own coarser replacement.
		if (c.parent_group == NO_GROUP) {
			continue;
		}
		const Group &g = groups[c.parent_group];
		if (g.level != c.level) {
			errors.push_back(at + vformat("parent group is on level %d, expected %d.", g.level, c.level));
		}
		// Compared exactly, with no tolerance. The cut orders these values with
		// strict float comparisons, so letting a parent sit an epsilon below a
		// child here permits an inversion there -- the cluster and its own
		// coarser replacement both selected. Construction adds a non-negative
		// step to the child's error, so equality is the tightest this ever is
		// and no slack is needed. The negated form also catches NaN.
		if (!(g.error >= error)) {
			errors.push_back(at + vformat("MONOTONICITY VIOLATED: parent error %f is not at least the cluster's own error %f.", g.error, error));
		}
		if (!g.lod_bounds.contains(get_cluster_lod_bounds(i), epsilon)) {
			errors.push_back(at + "MONOTONICITY VIOLATED: parent LOD sphere does not enclose the cluster's LOD sphere.");
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
		if (!Math::is_finite(g.lod_bounds.radius) || g.lod_bounds.radius < 0.0f) {
			errors.push_back(at + "has a non-finite or negative LOD sphere.");
		}
		// A simplification cannot displace the surface further than the extent
		// of the geometry it covers. Beyond that it is not a distance at all,
		// which is how a stalled simplifier reports failure.
		if (g.error > 2.0f * g.lod_bounds.radius + epsilon) {
			errors.push_back(at + vformat("error %f exceeds the diameter %f of its own LOD bounds, so it is not a geometric distance.", g.error, 2.0f * g.lod_bounds.radius));
		}
		for (uint32_t child : g.children) {
			if (child >= clusters.size()) {
				errors.push_back(at + "child index is out of range.");
			} else if (clusters[child].parent_group != i) {
				errors.push_back(at + vformat("child %d does not point back at this group.", child));
			}
		}
		for (uint32_t produced : g.produced) {
			if (produced >= clusters.size()) {
				errors.push_back(at + "produced index is out of range.");
			} else if (clusters[produced].source_group != i) {
				errors.push_back(at + vformat("produced cluster %d does not point back at this group.", produced));
			}
		}
	}

	return errors;
}

LocalVector<uint32_t> NaniteDAG::select_cut(float p_threshold) const {
	LocalVector<uint32_t> cut;
	for (uint32_t i = 0; i < clusters.size(); i++) {
		if (get_cluster_error(i) <= p_threshold && p_threshold < get_cluster_parent_error(i)) {
			cut.push_back(i);
		}
	}
	return cut;
}

String NaniteDAG::get_report() const {
	String report = vformat("Nanite DAG: %d levels, %d clusters, %d groups, %d triangles, %d vertices (format %d, builder %d, %d deviation samples/triangle)\n",
			get_level_count(), get_cluster_count(), get_group_count(), get_triangle_count(), vertex_count,
			FORMAT_VERSION, BUILDER_VERSION, deviation_samples_per_triangle);
	report += "  level | clusters | triangles | max error\n";
	report += "  ------+----------+-----------+----------\n";
	for (uint32_t level = 0; level < get_level_count(); level++) {
		report += vformat("  %5d | %8d | %9d | %.6f\n",
				level, get_level_cluster_count(level), get_level_triangle_count(level), get_level_max_error(level));
	}

	report += "  level | tri/cl avg | tri/cl max | vtx/cl avg | vtx/cl max | slice vtx | shared vtx | dup\n";
	report += "  ------+------------+------------+------------+------------+-----------+------------+------\n";
	for (uint32_t level = 0; level < get_level_count(); level++) {
		const uint32_t cluster_count = get_level_cluster_count(level);
		uint32_t max_triangles = 0;
		uint32_t max_vertices = 0;
		for (uint32_t i = level_offsets[level]; i < level_offsets[level + 1]; i++) {
			max_triangles = MAX(max_triangles, clusters[i].triangle_count);
			max_vertices = MAX(max_vertices, clusters[i].vertex_count);
		}
		const uint32_t slice_total = get_level_vertex_slice_total(level);
		const uint32_t shared_total = get_level_distinct_vertex_count(level);
		report += vformat("  %5d | %10.1f | %10d | %10.1f | %10d | %9d | %10d | %.2fx\n",
				level,
				(double)get_level_triangle_count(level) / (double)cluster_count, max_triangles,
				(double)slice_total / (double)cluster_count, max_vertices,
				slice_total, shared_total,
				shared_total > 0 ? (double)slice_total / (double)shared_total : 0.0);
	}

	if (!groups.is_empty()) {
		uint32_t min_children = UINT32_MAX;
		uint32_t max_children = 0;
		uint32_t total_children = 0;
		for (const Group &group : groups) {
			min_children = MIN(min_children, group.children.size());
			max_children = MAX(max_children, group.children.size());
			total_children += group.children.size();
		}
		report += vformat("  groups: %d, clusters per group min %d / avg %.1f / max %d\n",
				groups.size(), min_children, (double)total_children / (double)groups.size(), max_children);
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
		triangle_count += clusters[cluster].triangle_count;
	}

	PackedVector3Array vertices;
	PackedVector3Array normals_out;
	PackedColorArray colors;
	vertices.resize(triangle_count * 3);
	normals_out.resize(triangle_count * 3);
	colors.resize(triangle_count * 3);
	Vector3 *vertices_w = vertices.ptrw();
	Vector3 *normals_w = normals_out.ptrw();
	Color *colors_w = colors.ptrw();

	uint32_t out = 0;
	for (uint32_t cluster_id : p_clusters) {
		const Cluster &c = clusters[cluster_id];
		// Golden-ratio hue stepping keeps neighboring cluster ids visually apart.
		const Color color = Color::from_hsv(Math::fmod(cluster_id * 0.618033988f, 1.0f), 0.65f, 0.95f);

		for (uint32_t t = 0; t < c.triangle_count; t++) {
			Vector3 p[3];
			for (uint32_t k = 0; k < 3; k++) {
				const uint32_t v = get_cluster_vertex(cluster_id, t * 3 + k);
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
	arrays[Mesh::ARRAY_NORMAL] = normals_out;
	arrays[Mesh::ARRAY_COLOR] = colors;

	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

namespace {

// Word counts per record, used to bound a count read from the payload before
// it is allowed to size an allocation.
constexpr size_t CLUSTER_WORDS = 18;
constexpr size_t GROUP_MIN_WORDS = 8;

void write_u32(PackedByteArray &r_data, uint32_t p_value) {
	r_data.append_array(PackedByteArray{ (uint8_t)(p_value & 0xFF), (uint8_t)((p_value >> 8) & 0xFF),
			(uint8_t)((p_value >> 16) & 0xFF), (uint8_t)((p_value >> 24) & 0xFF) });
}

void write_u64(PackedByteArray &r_data, uint64_t p_value) {
	write_u32(r_data, (uint32_t)(p_value & 0xFFFFFFFF));
	write_u32(r_data, (uint32_t)(p_value >> 32));
}

void write_f32(PackedByteArray &r_data, float p_value) {
	uint32_t bits;
	memcpy(&bits, &p_value, sizeof(bits));
	write_u32(r_data, bits);
}

void write_sphere(PackedByteArray &r_data, const NaniteDAG::Sphere &p_sphere) {
	write_f32(r_data, (float)p_sphere.center.x);
	write_f32(r_data, (float)p_sphere.center.y);
	write_f32(r_data, (float)p_sphere.center.z);
	write_f32(r_data, p_sphere.radius);
}

struct Reader {
	const uint8_t *data = nullptr;
	int64_t size = 0;
	int64_t offset = 0;
	bool overrun = false;

	uint32_t u32() {
		if (offset + 4 > size) {
			overrun = true;
			return 0;
		}
		const uint32_t value = (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
				((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
		offset += 4;
		return value;
	}

	uint8_t u8() {
		if (offset + 1 > size) {
			overrun = true;
			return 0;
		}
		return data[offset++];
	}

	uint64_t u64() {
		const uint64_t low = u32();
		return low | ((uint64_t)u32() << 32);
	}

	float f32() {
		const uint32_t bits = u32();
		float value;
		memcpy(&value, &bits, sizeof(value));
		return value;
	}

	NaniteDAG::Sphere sphere() {
		NaniteDAG::Sphere out;
		const float x = f32();
		const float y = f32();
		const float z = f32();
		out.center = Vector3(x, y, z);
		out.radius = f32();
		return out;
	}

	// A count read from the payload sizes an allocation, so it is checked
	// against the bytes actually remaining rather than trusted. Without this a
	// small malformed file can ask for gigabytes.
	uint32_t count(size_t p_element_bytes) {
		const uint32_t value = u32();
		if (overrun || (uint64_t)value * p_element_bytes > (uint64_t)(size - offset)) {
			overrun = true;
			return 0;
		}
		return value;
	}

	String utf8() {
		const uint32_t length = count(1);
		if (overrun) {
			return String();
		}
		Vector<uint8_t> bytes;
		bytes.resize(length + 1);
		for (uint32_t i = 0; i < length; i++) {
			bytes.write[i] = u8();
		}
		bytes.write[length] = 0;
		String out;
		out.append_utf8((const char *)bytes.ptr());
		return out;
	}
};

} // namespace

PackedByteArray NaniteDAG::_serialize() const {
	PackedByteArray data;
	write_u32(data, FORMAT_VERSION);
	write_u32(data, BUILDER_VERSION);
	write_u32(data, surface_index);
	write_u64(data, settings_hash);
	write_u64(data, geometry_hash);
	write_u32(data, deviation_samples_per_triangle);

	const CharString hint = source_hint.utf8();
	write_u32(data, hint.length());
	for (int i = 0; i < hint.length(); i++) {
		data.push_back((uint8_t)hint[i]);
	}

	write_u32(data, vertex_count);
	write_u32(data, positions.size());
	for (const float position : positions) {
		write_f32(data, position);
	}
	write_u32(data, normals.size());
	for (const uint32_t normal : normals) {
		write_u32(data, normal);
	}
	write_u32(data, uvs.size());
	for (const uint32_t uv : uvs) {
		write_u32(data, uv);
	}

	write_u32(data, cluster_vertices.size());
	for (const uint32_t vertex : cluster_vertices) {
		write_u32(data, vertex);
	}
	// Local indices are bytes and are stored as bytes. Writing them as words
	// would quadruple the largest array in the file and put the artifact at
	// roughly twice the size the pool contract budgets per triangle.
	write_u32(data, cluster_indices.size());
	{
		PackedByteArray raw;
		raw.resize(cluster_indices.size());
		memcpy(raw.ptrw(), cluster_indices.ptr(), cluster_indices.size());
		data.append_array(raw);
	}
	write_u32(data, level_offsets.size());
	for (const uint32_t offset : level_offsets) {
		write_u32(data, offset);
	}

	write_u32(data, clusters.size());
	for (const Cluster &cluster : clusters) {
		write_u32(data, cluster.vertex_offset);
		write_u32(data, cluster.triangle_offset);
		write_u32(data, cluster.vertex_count);
		write_u32(data, cluster.triangle_count);
		write_u32(data, cluster.level);
		write_u32(data, cluster.parent_group);
		write_u32(data, cluster.source_group);
		write_sphere(data, cluster.bounds);
		write_f32(data, (float)cluster.cone_apex.x);
		write_f32(data, (float)cluster.cone_apex.y);
		write_f32(data, (float)cluster.cone_apex.z);
		write_f32(data, (float)cluster.cone_axis.x);
		write_f32(data, (float)cluster.cone_axis.y);
		write_f32(data, (float)cluster.cone_axis.z);
		write_f32(data, cluster.cone_cutoff);
	}

	write_u32(data, groups.size());
	for (const Group &group : groups) {
		write_u32(data, group.level);
		write_f32(data, group.error);
		write_sphere(data, group.lod_bounds);
		write_u32(data, group.children.size());
		for (const uint32_t child : group.children) {
			write_u32(data, child);
		}
		write_u32(data, group.produced.size());
		for (const uint32_t produced : group.produced) {
			write_u32(data, produced);
		}
	}
	return data;
}

bool NaniteDAG::_deserialize(const PackedByteArray &p_data) {
	if (p_data.is_empty()) {
		return true; // A cleared resource, not a broken one.
	}

	// Everything is parsed into a scratch DAG and fully checked before any of
	// it is committed, so a payload that fails halfway cannot leave this object
	// holding a mixture of old and new state.
	Ref<NaniteDAG> parsed;
	parsed.instantiate();

	Reader reader;
	reader.data = p_data.ptr();
	reader.size = p_data.size();

	const uint32_t format_version = reader.u32();
	if (format_version != FORMAT_VERSION) {
		load_error = vformat("NaniteDAG was saved with format version %d but this build reads version %d. Reimport the source mesh.", format_version, FORMAT_VERSION);
		ERR_FAIL_V_MSG(false, load_error);
	}
	const uint32_t builder_version = reader.u32();
	if (builder_version != BUILDER_VERSION) {
		load_error = vformat("NaniteDAG was built by builder version %d but this build is version %d. The algorithm changed, so the stored DAG is stale. Reimport the source mesh.", builder_version, BUILDER_VERSION);
		ERR_FAIL_V_MSG(false, load_error);
	}

	parsed->surface_index = reader.u32();
	parsed->settings_hash = reader.u64();
	parsed->geometry_hash = reader.u64();
	parsed->deviation_samples_per_triangle = reader.u32();
	parsed->source_hint = reader.utf8();

	parsed->vertex_count = reader.u32();
	parsed->positions.resize(reader.count(sizeof(uint32_t)));
	for (uint32_t i = 0; i < parsed->positions.size(); i++) {
		parsed->positions[i] = reader.f32();
	}
	parsed->normals.resize(reader.count(sizeof(uint32_t)));
	for (uint32_t i = 0; i < parsed->normals.size(); i++) {
		parsed->normals[i] = reader.u32();
	}
	parsed->uvs.resize(reader.count(sizeof(uint32_t)));
	for (uint32_t i = 0; i < parsed->uvs.size(); i++) {
		parsed->uvs[i] = reader.u32();
	}

	parsed->cluster_vertices.resize(reader.count(sizeof(uint32_t)));
	for (uint32_t i = 0; i < parsed->cluster_vertices.size(); i++) {
		parsed->cluster_vertices[i] = reader.u32();
	}
	parsed->cluster_indices.resize(reader.count(sizeof(uint8_t)));
	for (uint32_t i = 0; i < parsed->cluster_indices.size(); i++) {
		parsed->cluster_indices[i] = reader.u8();
	}
	parsed->level_offsets.resize(reader.count(sizeof(uint32_t)));
	for (uint32_t i = 0; i < parsed->level_offsets.size(); i++) {
		parsed->level_offsets[i] = reader.u32();
	}

	parsed->clusters.resize(reader.count(sizeof(uint32_t) * CLUSTER_WORDS));
	for (uint32_t i = 0; i < parsed->clusters.size(); i++) {
		Cluster &cluster = parsed->clusters[i];
		cluster.vertex_offset = reader.u32();
		cluster.triangle_offset = reader.u32();
		cluster.vertex_count = reader.u32();
		cluster.triangle_count = reader.u32();
		cluster.level = reader.u32();
		cluster.parent_group = reader.u32();
		cluster.source_group = reader.u32();
		cluster.bounds = reader.sphere();
		const float apex_x = reader.f32();
		const float apex_y = reader.f32();
		const float apex_z = reader.f32();
		cluster.cone_apex = Vector3(apex_x, apex_y, apex_z);
		const float axis_x = reader.f32();
		const float axis_y = reader.f32();
		const float axis_z = reader.f32();
		cluster.cone_axis = Vector3(axis_x, axis_y, axis_z);
		cluster.cone_cutoff = reader.f32();
	}

	parsed->groups.resize(reader.count(sizeof(uint32_t) * GROUP_MIN_WORDS));
	for (uint32_t i = 0; i < parsed->groups.size(); i++) {
		Group &group = parsed->groups[i];
		group.level = reader.u32();
		group.error = reader.f32();
		group.lod_bounds = reader.sphere();
		group.children.resize(reader.count(sizeof(uint32_t)));
		for (uint32_t k = 0; k < group.children.size(); k++) {
			group.children[k] = reader.u32();
		}
		group.produced.resize(reader.count(sizeof(uint32_t)));
		for (uint32_t k = 0; k < group.produced.size(); k++) {
			group.produced[k] = reader.u32();
		}
		if (reader.overrun) {
			break;
		}
	}

	if (reader.overrun) {
		load_error = "NaniteDAG data is truncated or malformed.";
		ERR_FAIL_V_MSG(false, load_error);
	}
	if (parsed->clusters.is_empty()) {
		// A non-empty payload that yields nothing is a broken file, not a mesh
		// that happens to have no geometry.
		load_error = "NaniteDAG data decoded to zero clusters.";
		ERR_FAIL_V_MSG(false, load_error);
	}

	const Vector<String> structure = parsed->validate();
	if (!structure.is_empty()) {
		load_error = vformat("NaniteDAG data is structurally invalid: %s", structure[0]);
		ERR_FAIL_V_MSG(false, load_error);
	}

	surface_index = parsed->surface_index;
	settings_hash = parsed->settings_hash;
	geometry_hash = parsed->geometry_hash;
	deviation_samples_per_triangle = parsed->deviation_samples_per_triangle;
	source_hint = parsed->source_hint;
	vertex_count = parsed->vertex_count;
	positions = std::move(parsed->positions);
	normals = std::move(parsed->normals);
	uvs = std::move(parsed->uvs);
	cluster_vertices = std::move(parsed->cluster_vertices);
	cluster_indices = std::move(parsed->cluster_indices);
	level_offsets = std::move(parsed->level_offsets);
	clusters = std::move(parsed->clusters);
	groups = std::move(parsed->groups);
	return true;
}

bool NaniteDAG::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name == SNAME("data")) {
		load_error = String();
		if (!_deserialize(p_value)) {
			// Leave nothing behind that could be mistaken for real geometry.
			positions.clear();
			normals.clear();
			uvs.clear();
			cluster_vertices.clear();
			cluster_indices.clear();
			clusters.clear();
			groups.clear();
			level_offsets.clear();
			vertex_count = 0;
		}
		// True either way: ResourceLoader discards a false return, so refusing
		// here would change nothing. load_error is what carries the failure.
		return true;
	}
	return false;
}

bool NaniteDAG::_get(const StringName &p_name, Variant &r_ret) const {
	if (p_name == SNAME("data")) {
		// Serializes afresh on every read. That is a full copy of the DAG, but
		// the property exists for saving and inspection rather than for any hot
		// path, and caching it would mean tracking every mutation to know when
		// the cache went stale.
		r_ret = _serialize();
		return true;
	}
	return false;
}

void NaniteDAG::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR));
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
	stats["format_version"] = FORMAT_VERSION;
	stats["builder_version"] = BUILDER_VERSION;
	stats["level_count"] = get_level_count();
	stats["cluster_count"] = get_cluster_count();
	stats["group_count"] = get_group_count();
	stats["triangle_count"] = get_triangle_count();
	stats["vertex_count"] = vertex_count;
	stats["surface_index"] = surface_index;
	stats["deviation_samples_per_triangle"] = deviation_samples_per_triangle;

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
	ClassDB::bind_method(D_METHOD("get_cluster_error", "cluster"), &NaniteDAG::get_cluster_error);
	ClassDB::bind_method(D_METHOD("get_cluster_parent_error", "cluster"), &NaniteDAG::get_cluster_parent_error);
	ClassDB::bind_method(D_METHOD("get_cluster_vertex_count", "cluster"), &NaniteDAG::get_cluster_vertex_count);
	ClassDB::bind_method(D_METHOD("get_level_vertex_slice_total", "level"), &NaniteDAG::get_level_vertex_slice_total);
	ClassDB::bind_method(D_METHOD("get_level_distinct_vertex_count", "level"), &NaniteDAG::get_level_distinct_vertex_count);
	ClassDB::bind_method(D_METHOD("get_statistics"), &NaniteDAG::_get_statistics_bind);
	ClassDB::bind_method(D_METHOD("get_report"), &NaniteDAG::get_report);
	ClassDB::bind_method(D_METHOD("validate"), &NaniteDAG::_validate_bind);
	ClassDB::bind_method(D_METHOD("select_cut", "threshold"), &NaniteDAG::_select_cut_bind);
	ClassDB::bind_method(D_METHOD("create_level_debug_mesh", "level"), &NaniteDAG::create_level_debug_mesh);
	ClassDB::bind_method(D_METHOD("create_cut_debug_mesh", "threshold"), &NaniteDAG::create_cut_debug_mesh);
}
