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

uint32_t NaniteDAG::get_cluster_vertex_count(uint32_t p_cluster) const {
	ERR_FAIL_COND_V(p_cluster >= clusters.size(), 0);
	const Cluster &cluster = clusters[p_cluster];

	// At most max_cluster_vertices entries, so sorting a scratch copy beats
	// standing up a hash set.
	LocalVector<uint32_t> referenced;
	referenced.resize(cluster.index_count);
	for (uint32_t i = 0; i < cluster.index_count; i++) {
		referenced[i] = indices[cluster.index_offset + i];
	}
	referenced.sort();

	uint32_t distinct = 0;
	for (uint32_t i = 0; i < referenced.size(); i++) {
		if (i == 0 || referenced[i] != referenced[i - 1]) {
			distinct++;
		}
	}
	return distinct;
}

uint32_t NaniteDAG::get_level_vertex_slice_total(uint32_t p_level) const {
	ERR_FAIL_COND_V(p_level >= get_level_count(), 0);
	uint32_t total = 0;
	for (uint32_t i = level_offsets[p_level]; i < level_offsets[p_level + 1]; i++) {
		total += get_cluster_vertex_count(i);
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
		for (uint32_t k = 0; k < cluster.index_count; k++) {
			const uint32_t vertex = indices[cluster.index_offset + k];
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
	if (vertex_count == 0) {
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
		// A simplification cannot displace the surface further than the extent
		// of the geometry it covers. An error beyond that is not a distance at
		// all, which is how a stalled simplifier reports failure.
		if (g.error > 2.0f * g.lod_bounds.radius + epsilon) {
			errors.push_back(at + vformat("error %f exceeds the diameter %f of its own LOD bounds, so it is not a geometric distance.", g.error, 2.0f * g.lod_bounds.radius));
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

	// Pool sizing table: how full clusters actually are, and what a per-cluster
	// vertex slice costs over a shared vertex buffer.
	report += "  level | tri/cl avg | tri/cl max | vtx/cl avg | vtx/cl max | slice vtx | shared vtx | dup\n";
	report += "  ------+------------+------------+------------+------------+-----------+------------+------\n";
	for (uint32_t level = 0; level < get_level_count(); level++) {
		const uint32_t cluster_count = get_level_cluster_count(level);
		uint32_t max_triangles = 0;
		uint32_t max_vertices = 0;
		for (uint32_t i = level_offsets[level]; i < level_offsets[level + 1]; i++) {
			max_triangles = MAX(max_triangles, clusters[i].index_count / 3);
			max_vertices = MAX(max_vertices, get_cluster_vertex_count(i));
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
		// Golden-ratio hue stepping keeps neighboring cluster ids visually apart.
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

namespace {

void write_u32(PackedByteArray &r_data, uint32_t p_value) {
	r_data.append_array(PackedByteArray{ (uint8_t)(p_value & 0xFF), (uint8_t)((p_value >> 8) & 0xFF),
			(uint8_t)((p_value >> 16) & 0xFF), (uint8_t)((p_value >> 24) & 0xFF) });
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

	// A count read from the payload is used to size an allocation, so it has
	// to be checked against what is actually left rather than trusted. A
	// truncated or hostile blob would otherwise ask for gigabytes.
	uint32_t count(size_t p_element_bytes) {
		const uint32_t value = u32();
		if (overrun || (uint64_t)value * p_element_bytes > (uint64_t)(size - offset)) {
			overrun = true;
			return 0;
		}
		return value;
	}

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
};

} // namespace

PackedByteArray NaniteDAG::_serialize() const {
	PackedByteArray data;
	write_u32(data, FORMAT_VERSION);
	write_u32(data, vertex_count);

	write_u32(data, positions.size());
	for (const float position : positions) {
		write_f32(data, position);
	}
	write_u32(data, indices.size());
	for (const uint32_t index : indices) {
		write_u32(data, index);
	}
	write_u32(data, level_offsets.size());
	for (const uint32_t offset : level_offsets) {
		write_u32(data, offset);
	}

	write_u32(data, clusters.size());
	for (const Cluster &cluster : clusters) {
		write_u32(data, cluster.index_offset);
		write_u32(data, cluster.index_count);
		write_u32(data, cluster.level);
		write_u32(data, cluster.parent_group);
		write_u32(data, cluster.source_group);
		write_f32(data, cluster.error);
		write_sphere(data, cluster.lod_bounds);
		// Infinity survives the float round-trip, which is what keeps a root
		// cluster drawable at every threshold after a reload.
		write_f32(data, cluster.parent_error);
		write_sphere(data, cluster.parent_lod_bounds);
		write_sphere(data, cluster.bounds);
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
	positions.clear();
	indices.clear();
	clusters.clear();
	groups.clear();
	level_offsets.clear();
	vertex_count = 0;

	if (p_data.is_empty()) {
		return true;
	}

	Reader reader;
	reader.data = p_data.ptr();
	reader.size = p_data.size();

	const uint32_t version = reader.u32();
	if (version != FORMAT_VERSION) {
		load_error = vformat("NaniteDAG was saved with format version %d but this build reads version %d. Reimport the source mesh.", version, FORMAT_VERSION);
		ERR_FAIL_V_MSG(false, load_error);
	}

	vertex_count = reader.u32();

	positions.resize(reader.count(sizeof(float)));
	for (uint32_t i = 0; i < positions.size(); i++) {
		positions[i] = reader.f32();
	}
	indices.resize(reader.count(sizeof(uint32_t)));
	for (uint32_t i = 0; i < indices.size(); i++) {
		indices[i] = reader.u32();
	}
	level_offsets.resize(reader.count(sizeof(uint32_t)));
	for (uint32_t i = 0; i < level_offsets.size(); i++) {
		level_offsets[i] = reader.u32();
	}

	// 15 words per cluster; see the writer.
	clusters.resize(reader.count(sizeof(uint32_t) * 15));
	for (uint32_t i = 0; i < clusters.size(); i++) {
		Cluster &cluster = clusters[i];
		cluster.index_offset = reader.u32();
		cluster.index_count = reader.u32();
		cluster.level = reader.u32();
		cluster.parent_group = reader.u32();
		cluster.source_group = reader.u32();
		cluster.error = reader.f32();
		cluster.lod_bounds = reader.sphere();
		cluster.parent_error = reader.f32();
		cluster.parent_lod_bounds = reader.sphere();
		cluster.bounds = reader.sphere();
	}

	// A group is at least 8 words, so that is the floor for the count check.
	groups.resize(reader.count(sizeof(uint32_t) * 8));
	for (uint32_t i = 0; i < groups.size(); i++) {
		Group &group = groups[i];
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
	return true;
}

bool NaniteDAG::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name == SNAME("data")) {
		load_error = String();
		if (!_deserialize(p_value)) {
			// Leave nothing behind that could be mistaken for real geometry.
			positions.clear();
			indices.clear();
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

void NaniteDAG::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_level_count"), &NaniteDAG::get_level_count);
	ClassDB::bind_method(D_METHOD("get_cluster_count"), &NaniteDAG::get_cluster_count);
	ClassDB::bind_method(D_METHOD("get_group_count"), &NaniteDAG::get_group_count);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &NaniteDAG::get_triangle_count);
	ClassDB::bind_method(D_METHOD("get_level_cluster_count", "level"), &NaniteDAG::get_level_cluster_count);
	ClassDB::bind_method(D_METHOD("get_level_triangle_count", "level"), &NaniteDAG::get_level_triangle_count);
	ClassDB::bind_method(D_METHOD("get_level_max_error", "level"), &NaniteDAG::get_level_max_error);
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
