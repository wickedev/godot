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
#include "core/math/vector3.h"
#include "core/templates/local_vector.h"

class ArrayMesh;

// Offline cluster DAG produced by NaniteDAGBuilder (Nanite stage S1).
//
// PROVISIONAL FORMAT. The on-disk and GPU-side layouts are deliberately not
// frozen: they converge with the L1/L3 geometry pool contract at gate G2
// (BDA + acceleration-structure build usage flags). Only the in-memory shape
// below is relied upon by the builder, its validator and its tests, so that
// the eventual layout decision does not invalidate the build loop.
//
// Structure
// ---------
// Simplification is non-destructive, so every level indexes into one shared
// vertex buffer; a cluster is just a range of `indices`.
//
// A *group* is a set of clusters at level L that were simplified together to
// produce the clusters at level L + 1. Both LOD quantities are therefore group
// properties, which is what forces every cluster of a group to take the same
// LOD decision at runtime -- the property that makes the cut crack-free:
//
//   Cluster::error / lod_bounds        -- from the group that PRODUCED it.
//   Cluster::parent_error / parent_lod_bounds -- from the group that CONSUMES it.
//
// The runtime cut at screen-space threshold `t` is
//   draw(c)  <=>  project(c.error) <= t < project(c.parent_error)
// which tiles [0, inf) without gaps or overlap along every root-to-leaf chain,
// because error is monotonically non-decreasing towards the root.
class NaniteDAG : public Resource {
	GDCLASS(NaniteDAG, Resource);

public:
	static constexpr uint32_t NO_GROUP = 0xFFFFFFFFu;

	struct Sphere {
		Vector3 center;
		float radius = 0.0f;

		// Conservative containment test used by the monotonicity validator.
		bool contains(const Sphere &p_other, float p_epsilon) const {
			return (float)center.distance_to(p_other.center) + p_other.radius <= radius + p_epsilon;
		}
	};

	struct Cluster {
		uint32_t index_offset = 0;
		uint32_t index_count = 0;
		uint32_t level = 0;

		// Group at `level` that simplifies this cluster away. NO_GROUP marks a
		// DAG root, in which case `parent_error` stays infinite so the cut
		// always terminates here.
		uint32_t parent_group = NO_GROUP;
		// Group at `level - 1` this cluster was produced from. NO_GROUP at
		// level 0, where the geometry is the unsimplified input.
		uint32_t source_group = NO_GROUP;

		// Absolute error, in mesh units, of the simplification that produced
		// this cluster, plus the sphere it is projected from.
		float error = 0.0f;
		Sphere lod_bounds;

		float parent_error = INFINITY;
		Sphere parent_lod_bounds;

		// Geometric bounds of this cluster's own triangles (culling).
		Sphere bounds;
	};

	struct Group {
		uint32_t level = 0;
		float error = 0.0f;
		Sphere lod_bounds;
		LocalVector<uint32_t> children; // Clusters at `level`, simplified by this group.
		LocalVector<uint32_t> produced; // Clusters at `level + 1`, the result.
	};

	// Shared vertex buffer, 3 floats per vertex.
	LocalVector<float> positions;
	uint32_t vertex_count = 0;

	// Cluster index data; a cluster owns [index_offset, index_offset + index_count).
	LocalVector<uint32_t> indices;
	LocalVector<Cluster> clusters;
	LocalVector<Group> groups;

	// Clusters of level L occupy [level_offsets[L], level_offsets[L + 1]).
	LocalVector<uint32_t> level_offsets;

	uint32_t get_level_count() const;
	uint32_t get_cluster_count() const { return clusters.size(); }
	uint32_t get_group_count() const { return groups.size(); }
	uint32_t get_triangle_count() const { return indices.size() / 3; }
	uint32_t get_level_cluster_count(uint32_t p_level) const;
	uint32_t get_level_triangle_count(uint32_t p_level) const;
	float get_level_max_error(uint32_t p_level) const;

	// Distinct vertices a single cluster references. Bounded by the builder's
	// max_cluster_vertices, so it fits an 8-bit local index.
	uint32_t get_cluster_vertex_count(uint32_t p_cluster) const;
	// Summed over a level: what a per-cluster vertex slice layout would cost.
	uint32_t get_level_vertex_slice_total(uint32_t p_level) const;
	// Distinct vertices the level touches: what a shared vertex buffer costs.
	// The ratio of the two is the duplication a slice layout pays for locality.
	uint32_t get_level_distinct_vertex_count(uint32_t p_level) const;

	// Every violated DAG invariant, most importantly error monotonicity. Empty
	// means the DAG is sound. Always compiled: this is the G3 quality gate's
	// pass/fail criterion, not a debug-only luxury.
	Vector<String> validate() const;

	// Clusters drawn at absolute error threshold `p_threshold`, i.e. the DAG
	// cut. `p_threshold` is in mesh units; the runtime projects it per-view.
	LocalVector<uint32_t> select_cut(float p_threshold) const;

	// Per-level cluster/triangle/error table, for judging DAG quality.
	String get_report() const;

	// Every cluster of one level, colored per cluster.
	Ref<ArrayMesh> create_level_debug_mesh(uint32_t p_level) const;
	// The cut at `p_threshold`, colored per cluster.
	Ref<ArrayMesh> create_cut_debug_mesh(float p_threshold) const;

	// Bumped whenever the serialized layout changes. This is a provisional
	// format: it is expected to be replaced wholesale once the geometry pool
	// contract is settled. Loading a mismatched version fails loudly rather
	// than yielding a silently empty DAG.
	static constexpr uint32_t FORMAT_VERSION = 1;

protected:
	static void _bind_methods();

	// Storage goes through one opaque blob rather than a property per array, so
	// that changing the layout is a version bump and not a scene-format break.
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

	PackedByteArray _serialize() const;
	bool _deserialize(const PackedByteArray &p_data);

	// Set when loading failed. ResourceLoader ignores a false return from
	// _set(), so a rejected payload would otherwise surface as a perfectly
	// valid resource that happens to contain nothing. validate() reports this
	// so the failure cannot be mistaken for an empty mesh.
	String load_error;

private:
	float _compute_epsilon() const;
	Ref<ArrayMesh> _create_debug_mesh(const LocalVector<uint32_t> &p_clusters) const;

	// Script-facing wrappers.
	PackedStringArray _validate_bind() const;
	PackedInt32Array _select_cut_bind(float p_threshold) const;
	Dictionary _get_statistics_bind() const;
};
