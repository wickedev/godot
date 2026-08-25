/**************************************************************************/
/*  nanite_dag.h                                                          */
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

#include "core/io/resource.h"
#include "core/math/math_funcs.h"
#include "core/math/vector3.h"
#include "core/templates/local_vector.h"

class ArrayMesh;

// Offline cluster DAG produced by NaniteDAGBuilder (Nanite stage S1).
//
// Layout follows the geometry pool contract: one shared vertex buffer, a
// per-cluster slice of global vertex ids, and 8-bit indices local to that
// slice. Measured on a 51k-triangle mesh, the slice costs 2.6x the vertices of
// a shared buffer yet the whole thing still comes out 37% smaller than global
// 32-bit indices, because three bytes per triangle beats twelve.
//
// Structure
// ---------
// A *group* is the set of clusters at level L that were simplified together to
// produce level L + 1. Error and LOD bounds are properties of the group, and
// are stored once on it rather than copied onto each of its clusters, so every
// cluster of a group necessarily takes the same LOD decision -- which is what
// keeps a cut free of cracks.
//
//   error / lod_bounds        <- the group that PRODUCED the cluster
//   parent_error / parent_lod <- the group that CONSUMES it
//
// The runtime cut at screen-space threshold `t` is
//   draw(c)  <=>  project(error(c)) <= t < project(parent_error(c))
// which tiles [0, inf) without gaps or overlap along every root-to-leaf chain,
// because error never decreases towards the root.
class NaniteDAG : public Resource {
	GDCLASS(NaniteDAG, Resource);

public:
	static constexpr uint32_t NO_GROUP = 0xFFFFFFFFu;

	// On-disk layout. Bumped when the serialized structure changes.
	static constexpr uint32_t FORMAT_VERSION = 3;
	// The algorithm. Bumped when the same input would now produce a different
	// DAG -- a change that leaves the layout untouched but makes every stored
	// artifact stale. Keeping this separate from the format version is what
	// stops an algorithm improvement from silently serving old results.
	static constexpr uint32_t BUILDER_VERSION = 1;

	// Per-cluster ceilings, frozen by the geometry pool contract: 7 bits of
	// triangle index in the visibility buffer, 8 bits of local vertex index.
	static constexpr uint32_t MAX_CLUSTER_TRIANGLES = 128;
	static constexpr uint32_t MAX_CLUSTER_VERTICES = 255;

	struct Sphere {
		Vector3 center;
		float radius = 0.0f;

		bool contains(const Sphere &p_other, float p_epsilon) const {
			return (float)center.distance_to(p_other.center) + p_other.radius <= radius + p_epsilon;
		}
	};

	struct Cluster {
		uint32_t vertex_offset = 0; // Into cluster_vertices.
		uint32_t triangle_offset = 0; // Into cluster_indices, 3 entries per triangle.
		uint32_t vertex_count = 0; // <= MAX_CLUSTER_VERTICES.
		uint32_t triangle_count = 0; // <= MAX_CLUSTER_TRIANGLES.
		uint32_t level = 0;

		// Group at `level` that simplifies this cluster away; NO_GROUP marks a
		// DAG root, whose parent error is infinite so the cut terminates here.
		uint32_t parent_group = NO_GROUP;
		// Group at `level - 1` this cluster was produced from; NO_GROUP at
		// level 0, where the geometry is the unsimplified input.
		uint32_t source_group = NO_GROUP;

		// Culling volumes for this cluster's own triangles. The cone is what
		// lets a cluster be rejected when every triangle in it faces away; the
		// apex is needed for the perspective form of that test, so it is kept
		// rather than only axis and cutoff.
		Sphere bounds;
		Vector3 cone_apex;
		Vector3 cone_axis;
		float cone_cutoff = 1.0f;
	};

	struct Group {
		uint32_t level = 0;
		float error = 0.0f; // Absolute, in mesh units.
		Sphere lod_bounds;
		LocalVector<uint32_t> children; // Clusters at `level`.
		LocalVector<uint32_t> produced; // Clusters at `level + 1`.
	};

	// Shared vertex buffer. Decoded guarantees are float3 position, octahedral
	// RG16 normal and half2 UV; tangents are deliberately absent, since a
	// visibility-buffer resolve can derive the frame from the triangle.
	LocalVector<float> positions; // 3 floats per vertex.
	LocalVector<uint32_t> normals; // Octahedral, two 16-bit unorms packed.
	LocalVector<uint32_t> uvs; // Two halves packed.
	uint32_t vertex_count = 0;

	LocalVector<uint32_t> cluster_vertices; // Global vertex ids, sliced per cluster.
	LocalVector<uint8_t> cluster_indices; // Local to the owning cluster's slice.
	LocalVector<Cluster> clusters;
	LocalVector<Group> groups;

	// Clusters of level L occupy [level_offsets[L], level_offsets[L + 1]).
	LocalVector<uint32_t> level_offsets;

	// Artifact identity. Invalidation rests entirely on these plus the two
	// version constants; the source asset is not part of it, because an import
	// plugin is never told which file it is processing.
	uint32_t surface_index = 0;
	uint64_t settings_hash = 0;
	uint64_t geometry_hash = 0;
	String source_hint; // Non-authoritative, for humans reading a stray file.

	// How densely each simplified triangle was sampled when measuring how far
	// the surface moved. The stored error is the largest deviation found at
	// those samples, not a proven analytic bound, so a consumer cannot tell
	// what the number means without knowing this. Zero means unrecorded, which
	// only happens for a DAG built before the field existed.
	uint32_t deviation_samples_per_triangle = 0;

	static uint32_t encode_normal(const Vector3 &p_normal);
	static Vector3 decode_normal(uint32_t p_encoded);
	static uint32_t encode_uv(float p_u, float p_v);
	static void decode_uv(uint32_t p_encoded, float &r_u, float &r_v);

	uint32_t get_level_count() const;
	uint32_t get_cluster_count() const { return clusters.size(); }
	uint32_t get_group_count() const { return groups.size(); }
	uint32_t get_triangle_count() const { return cluster_indices.size() / 3; }
	uint32_t get_level_cluster_count(uint32_t p_level) const;
	uint32_t get_level_triangle_count(uint32_t p_level) const;
	float get_level_max_error(uint32_t p_level) const;

	// LOD quantities resolved through the owning groups.
	float get_cluster_error(uint32_t p_cluster) const;
	float get_cluster_parent_error(uint32_t p_cluster) const;
	Sphere get_cluster_lod_bounds(uint32_t p_cluster) const;
	Sphere get_cluster_parent_lod_bounds(uint32_t p_cluster) const;

	// Global vertex id behind corner `p_corner` of the cluster's triangles.
	uint32_t get_cluster_vertex(uint32_t p_cluster, uint32_t p_corner) const;
	uint32_t get_cluster_vertex_count(uint32_t p_cluster) const;
	uint32_t get_level_vertex_slice_total(uint32_t p_level) const;
	uint32_t get_level_distinct_vertex_count(uint32_t p_level) const;

	// Every violated invariant, most importantly error monotonicity. Empty
	// means sound. Always compiled: this is the DAG quality gate's criterion,
	// not a debug-only luxury.
	Vector<String> validate() const;

	// Clusters drawn at absolute error threshold `p_threshold`, in mesh units.
	LocalVector<uint32_t> select_cut(float p_threshold) const;

	String get_report() const;
	Ref<ArrayMesh> create_level_debug_mesh(uint32_t p_level) const;
	Ref<ArrayMesh> create_cut_debug_mesh(float p_threshold) const;

protected:
	static void _bind_methods();

	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

	PackedByteArray _serialize() const;
	bool _deserialize(const PackedByteArray &p_data);

	// Set when loading failed. ResourceLoader ignores a false return from
	// _set(), so a rejected payload would otherwise surface as a valid resource
	// that happens to contain nothing. validate() reports this.
	String load_error;

private:
	float _compute_epsilon() const;
	Ref<ArrayMesh> _create_debug_mesh(const LocalVector<uint32_t> &p_clusters) const;

	PackedStringArray _validate_bind() const;
	PackedInt32Array _select_cut_bind(float p_threshold) const;
	Dictionary _get_statistics_bind() const;
};
