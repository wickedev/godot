/**************************************************************************/
/*  nanite_dag_builder.cpp                                                */
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

#include "nanite_dag_builder.h"

#include "core/math/face3.h"
#include "core/templates/hash_set.h"
#include "scene/resources/mesh.h"

#include <thirdparty/meshoptimizer/meshoptimizer.h>

#include <float.h>

namespace {

constexpr size_t POSITION_STRIDE = sizeof(float) * 3;

Vector3 get_position(const float *p_positions, uint32_t p_vertex) {
	return Vector3(p_positions[p_vertex * 3 + 0], p_positions[p_vertex * 3 + 1], p_positions[p_vertex * 3 + 2]);
}

// Enclosing sphere over the referenced vertices, centered on their bounding
// box. Not minimal, but it contains them and -- unlike averaging the index
// stream -- it does not shift toward whichever vertices happen to be
// referenced most often. That bias matters: a UV sphere's pole is shared by a
// whole triangle fan, which dragged the center off-origin and inflated the
// radius by ~16%, both loosening culling bounds and weakening the error
// sanity check that uses this extent.
NaniteDAG::Sphere sphere_from_indices(const float *p_positions, const uint32_t *p_indices, uint32_t p_index_count) {
	NaniteDAG::Sphere sphere;
	if (p_index_count == 0) {
		return sphere;
	}
	Vector3 min_corner = get_position(p_positions, p_indices[0]);
	Vector3 max_corner = min_corner;
	for (uint32_t i = 1; i < p_index_count; i++) {
		const Vector3 position = get_position(p_positions, p_indices[i]);
		min_corner = min_corner.min(position);
		max_corner = max_corner.max(position);
	}
	const Vector3 center = (min_corner + max_corner) * 0.5f;

	float radius = 0.0f;
	for (uint32_t i = 0; i < p_index_count; i++) {
		radius = MAX(radius, (float)center.distance_to(get_position(p_positions, p_indices[i])));
	}
	sphere.center = center;
	sphere.radius = radius;
	return sphere;
}

// Sphere enclosing a set of spheres. Used for a group's LOD bounds so that it
// provably contains every child's, keeping the projected error monotonic.
NaniteDAG::Sphere enclose_spheres(const LocalVector<NaniteDAG::Sphere> &p_spheres) {
	NaniteDAG::Sphere sphere;
	if (p_spheres.is_empty()) {
		return sphere;
	}
	Vector3 center;
	for (const NaniteDAG::Sphere &s : p_spheres) {
		center += s.center;
	}
	center /= (real_t)p_spheres.size();
	float radius = 0.0f;
	for (const NaniteDAG::Sphere &s : p_spheres) {
		radius = MAX(radius, (float)center.distance_to(s.center) + s.radius);
	}
	sphere.center = center;
	sphere.radius = radius;
	return sphere;
}

// How far the surface actually moved, measured in geometry alone.
//
// meshoptimizer's reported error cannot answer this. With attribute weights it
// returns a combined position-and-attribute quadric (`simplifier.cpp` keeps the
// positional part in `vertex_error` and returns the combined `result_error`),
// so a crease whose normals swing hard reads as a large distance even when the
// surface barely moved. That value is right for ordering collapses and wrong
// for projecting to screen space, which is what the LOD cut and the GBuffer
// contract's motion residual bound both do with it.
//
// Simplification only removes vertices, so survivors lie exactly on the result
// and only the removed ones can have moved away from it.
float measure_geometric_deviation(const float *p_positions, const LocalVector<uint32_t> &p_before,
		const uint32_t *p_after, size_t p_after_count) {
	HashSet<uint32_t> survivors;
	for (size_t i = 0; i < p_after_count; i++) {
		survivors.insert(p_after[i]);
	}

	// Per-triangle bounding spheres, so most triangles can be rejected without
	// a point-triangle test.
	const size_t triangle_count = p_after_count / 3;
	LocalVector<Vector3> centroids;
	LocalVector<float> radii;
	centroids.resize(triangle_count);
	radii.resize(triangle_count);
	for (size_t t = 0; t < triangle_count; t++) {
		const Vector3 a = get_position(p_positions, p_after[t * 3 + 0]);
		const Vector3 b = get_position(p_positions, p_after[t * 3 + 1]);
		const Vector3 c = get_position(p_positions, p_after[t * 3 + 2]);
		centroids[t] = (a + b + c) / 3.0f;
		radii[t] = MAX(MAX((float)centroids[t].distance_to(a), (float)centroids[t].distance_to(b)),
				(float)centroids[t].distance_to(c));
	}

	HashSet<uint32_t> measured;
	float worst = 0.0f;
	for (const uint32_t vertex : p_before) {
		if (survivors.has(vertex) || measured.has(vertex)) {
			continue;
		}
		measured.insert(vertex);

		const Vector3 point = get_position(p_positions, vertex);
		float nearest = FLT_MAX;
		for (size_t t = 0; t < triangle_count; t++) {
			if ((float)point.distance_to(centroids[t]) - radii[t] >= nearest) {
				continue;
			}
			const Face3 face(get_position(p_positions, p_after[t * 3 + 0]),
					get_position(p_positions, p_after[t * 3 + 1]),
					get_position(p_positions, p_after[t * 3 + 2]));
			nearest = MIN(nearest, (float)point.distance_to(face.get_closest_point_to(point)));
		}
		if (nearest != FLT_MAX) {
			worst = MAX(worst, nearest);
		}
	}
	return worst;
}

struct BuildContext {
	Ref<NaniteDAG> dag;
	const NaniteDAGBuilder::Settings *settings = nullptr;
	const float *positions = nullptr;
	uint32_t vertex_count = 0;

	// Splits an index buffer into clusters, appending them to the DAG.
	// Returns the ids of the clusters it created.
	void append_clusters(const uint32_t *p_indices, size_t p_index_count, uint32_t p_level, LocalVector<uint32_t> &r_created) {
		if (p_index_count < 3) {
			return;
		}
		const size_t max_vertices = settings->max_cluster_vertices;
		const size_t max_triangles = settings->max_cluster_triangles;
		// The spatial builder may emit clusters as small as min_triangles, so
		// it can return far more of them than the max-triangle bound predicts.
		// meshoptimizer is explicit that the bound must be computed with
		// min_triangles; using max_triangles under-allocates by the ratio
		// between them and lets the builder write past the arrays.
		const size_t min_triangles = settings->spatial_clustering ? MAX(max_triangles / 2, (size_t)1) : max_triangles;
		const size_t max_meshlets = meshopt_buildMeshletsBound(p_index_count, max_vertices, min_triangles);

		LocalVector<meshopt_Meshlet> meshlets;
		meshlets.resize(max_meshlets);
		LocalVector<uint32_t> meshlet_vertices;
		meshlet_vertices.resize(MAX(max_meshlets * max_vertices, p_index_count));
		LocalVector<uint8_t> meshlet_triangles;
		meshlet_triangles.resize(MAX(max_meshlets * max_triangles * 3, p_index_count));

		size_t meshlet_count;
		if (settings->spatial_clustering) {
			meshlet_count = meshopt_buildMeshletsSpatial(meshlets.ptr(), meshlet_vertices.ptr(), meshlet_triangles.ptr(),
					p_indices, p_index_count, positions, vertex_count, POSITION_STRIDE,
					max_vertices, min_triangles, max_triangles, 0.5f);
		} else {
			meshlet_count = meshopt_buildMeshlets(meshlets.ptr(), meshlet_vertices.ptr(), meshlet_triangles.ptr(),
					p_indices, p_index_count, positions, vertex_count, POSITION_STRIDE,
					max_vertices, max_triangles, settings->cone_weight);
		}

		for (size_t i = 0; i < meshlet_count; i++) {
			const meshopt_Meshlet &meshlet = meshlets[i];
			if (meshlet.triangle_count == 0) {
				continue;
			}
			NaniteDAG::Cluster cluster;
			cluster.level = p_level;
			cluster.index_offset = dag->indices.size();
			cluster.index_count = meshlet.triangle_count * 3;
			for (uint32_t k = 0; k < cluster.index_count; k++) {
				// Meshlet locals index into the meshlet's vertex slice, which
				// in turn holds indices into the shared vertex buffer.
				const uint8_t local = meshlet_triangles[meshlet.triangle_offset + k];
				dag->indices.push_back(meshlet_vertices[meshlet.vertex_offset + local]);
			}
			cluster.bounds = sphere_from_indices(positions, &dag->indices[cluster.index_offset], cluster.index_count);
			// Level 0 projects its (zero) error from its own geometry. Clusters
			// produced by a simplification overwrite this with their group's
			// sphere, so that a whole group shares one LOD decision.
			cluster.lod_bounds = cluster.bounds;
			r_created.push_back(dag->clusters.size());
			dag->clusters.push_back(cluster);
		}
	}
};

} // namespace

// Reports a build failure through both channels: the optional out-parameter,
// so the importer can surface it per surface, and the engine error stream.
#define FAIL(m_message) \
	{ \
		const String _nanite_message = String(m_message); \
		if (r_error) { \
			*r_error = _nanite_message; \
		} \
		ERR_FAIL_V_MSG(Ref<NaniteDAG>(), "Nanite DAG builder: " + _nanite_message); \
	}

Ref<NaniteDAG> NaniteDAGBuilder::build(const LocalVector<float> &p_positions, uint32_t p_vertex_count,
		const LocalVector<float> &p_attributes, uint32_t p_attribute_count,
		const LocalVector<float> &p_attribute_weights, const LocalVector<uint32_t> &p_indices,
		const Settings &p_settings, String *r_error) {
	if (p_vertex_count < 3) {
		FAIL("mesh has fewer than 3 vertices.");
	}
	if (p_positions.size() != (uint64_t)p_vertex_count * 3) {
		FAIL("position buffer size does not match the vertex count.");
	}
	if (p_indices.is_empty() || p_indices.size() % 3 != 0) {
		FAIL("index count is not a positive multiple of 3.");
	}
	if (p_attribute_count > 0 && p_attributes.size() != (uint64_t)p_vertex_count * p_attribute_count) {
		FAIL("attribute buffer size does not match the vertex count.");
	}
	if (p_attribute_count > 0 && p_attribute_weights.size() != p_attribute_count) {
		FAIL("attribute weight count does not match the attribute count.");
	}
	if (p_attribute_count > 32) {
		FAIL("attribute count exceeds the 32 attributes meshoptimizer supports.");
	}
	if (p_settings.max_cluster_vertices < 3 || p_settings.max_cluster_vertices > 256) {
		FAIL("max_cluster_vertices must be in [3, 256].");
	}
	if (p_settings.max_cluster_triangles < 1 || p_settings.max_cluster_triangles > 512) {
		FAIL("max_cluster_triangles must be in [1, 512].");
	}
	if (p_settings.group_size < 2) {
		FAIL("group_size must be at least 2.");
	}
	// Range tests alone would let NaN through, since every comparison against
	// it is false.
	if (!Math::is_finite(p_settings.simplify_ratio) || p_settings.simplify_ratio <= 0.0f || p_settings.simplify_ratio >= 1.0f) {
		FAIL("simplify_ratio must be a finite value in (0, 1).");
	}
	if (!Math::is_finite(p_settings.min_progress_ratio) || p_settings.min_progress_ratio <= 0.0f || p_settings.min_progress_ratio > 1.0f) {
		FAIL("min_progress_ratio must be a finite value in (0, 1].");
	}
	if (!Math::is_finite(p_settings.cone_weight) || p_settings.cone_weight < 0.0f || p_settings.cone_weight > 1.0f) {
		FAIL("cone_weight must be a finite value in [0, 1].");
	}
	if (!Math::is_finite(p_settings.normal_weight) || p_settings.normal_weight < 0.0f ||
			!Math::is_finite(p_settings.uv_weight) || p_settings.uv_weight < 0.0f) {
		FAIL("attribute weights must be finite and non-negative.");
	}
	for (uint32_t i = 0; i < p_attribute_count; i++) {
		if (!Math::is_finite(p_attribute_weights[i]) || p_attribute_weights[i] < 0.0f) {
			FAIL("attribute weights must be finite and non-negative.");
		}
	}
	// A single NaN position propagates into every bound and error in the DAG,
	// and meshoptimizer's behavior on one is undefined.
	for (uint32_t i = 0; i < p_positions.size(); i++) {
		if (!Math::is_finite(p_positions[i])) {
			FAIL("position buffer contains a non-finite value.");
		}
	}
	for (uint32_t i = 0; i < p_attributes.size(); i++) {
		if (!Math::is_finite(p_attributes[i])) {
			FAIL("attribute buffer contains a non-finite value.");
		}
	}
	for (uint32_t index : p_indices) {
		if (index >= p_vertex_count) {
			FAIL("index buffer references a vertex outside the vertex buffer.");
		}
	}

	BuildContext ctx;
	ctx.dag.instantiate();
	ctx.settings = &p_settings;
	ctx.dag->positions = p_positions;
	ctx.dag->vertex_count = p_vertex_count;
	ctx.positions = ctx.dag->positions.ptr();
	ctx.vertex_count = p_vertex_count;

	const Ref<NaniteDAG> dag = ctx.dag;

	// Position-only weld map. Vertices split for UV/normal seams sit at the
	// same point in space, so the group boundary has to be reasoned about in
	// position space; locking only one of a seam's copies would let the other
	// move and open a crack.
	LocalVector<uint32_t> weld;
	weld.resize(p_vertex_count);
	meshopt_generateVertexRemap(weld.ptr(), nullptr, p_vertex_count, ctx.positions, p_vertex_count, POSITION_STRIDE);

	// Positions that may never move again. A group that fails to simplify
	// leaves its clusters as permanent roots, drawn at every threshold, still
	// carrying their original seam. Later levels no longer see those clusters,
	// so without this they would happily simplify the shared seam away on the
	// other side and crack against geometry that is still on screen.
	LocalVector<uint8_t> permanently_locked;
	permanently_locked.resize(p_vertex_count);
	for (uint32_t i = 0; i < p_vertex_count; i++) {
		permanently_locked[i] = 0;
	}

	// Level 0: the input geometry, split into clusters, error zero.
	LocalVector<uint32_t> current;
	dag->level_offsets.push_back(0);
	ctx.append_clusters(p_indices.ptr(), p_indices.size(), 0, current);
	if (current.is_empty()) {
		FAIL("clustering produced no clusters for the input mesh.");
	}
	dag->level_offsets.push_back(dag->clusters.size());

	for (uint32_t level = 0; level + 1 < p_settings.max_levels; level++) {
		if (current.size() <= 1) {
			break; // Single root reached.
		}

		// 1. Group clusters. Positions are passed so the partitioner is
		//    spatially aware rather than falling back to shared-vertex only.
		LocalVector<uint32_t> cluster_indices;
		LocalVector<uint32_t> cluster_index_counts;
		for (uint32_t cluster_id : current) {
			const NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
			for (uint32_t k = 0; k < cluster.index_count; k++) {
				cluster_indices.push_back(dag->indices[cluster.index_offset + k]);
			}
			cluster_index_counts.push_back(cluster.index_count);
		}

		LocalVector<uint32_t> partition;
		partition.resize(current.size());
		const size_t group_count = meshopt_partitionClusters(partition.ptr(), cluster_indices.ptr(), cluster_indices.size(),
				cluster_index_counts.ptr(), current.size(), ctx.positions, p_vertex_count, POSITION_STRIDE,
				p_settings.group_size);

		if (group_count == 0 || group_count >= current.size()) {
			break; // Nothing merged; the current level is the root level.
		}

		LocalVector<LocalVector<uint32_t>> members;
		members.resize(group_count);
		for (uint32_t i = 0; i < current.size(); i++) {
			members[partition[i]].push_back(current[i]);
		}

		// 2. Lock every vertex shared between two groups. Computed once for
		//    the whole level: a vertex on the seam between group A and B has
		//    to be immovable in both simplifications, otherwise the two sides
		//    drift apart and crack.
		LocalVector<uint8_t> locked_position;
		locked_position.resize(p_vertex_count);
		LocalVector<int64_t> position_owner;
		position_owner.resize(p_vertex_count);
		for (uint32_t i = 0; i < p_vertex_count; i++) {
			locked_position[i] = 0;
			position_owner[i] = -1;
		}
		for (uint32_t i = 0; i < current.size(); i++) {
			const int64_t group_id = (int64_t)partition[i];
			const NaniteDAG::Cluster &cluster = dag->clusters[current[i]];
			for (uint32_t k = 0; k < cluster.index_count; k++) {
				const uint32_t welded = weld[dag->indices[cluster.index_offset + k]];
				if (position_owner[welded] < 0) {
					position_owner[welded] = group_id;
				} else if (position_owner[welded] != group_id) {
					locked_position[welded] = 1;
				}
			}
		}
		unsigned int simplify_options = meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute;
		LocalVector<uint8_t> vertex_lock;
		vertex_lock.resize(p_vertex_count);
		for (uint32_t i = 0; i < p_vertex_count; i++) {
			const uint32_t welded = weld[i];
			const bool locked = locked_position[welded] || permanently_locked[welded];
			vertex_lock[i] = locked ? (uint8_t)meshopt_SimplifyVertex_Lock : (uint8_t)0;
		}
		if (p_settings.lock_mesh_border) {
			simplify_options |= meshopt_SimplifyLockBorder;
		}

		// 3. Simplify each group and re-cluster the result. Everything lands
		//    in the DAG immediately but is rolled back below if the level as a
		//    whole made no progress.
		const uint32_t restore_indices = dag->indices.size();
		const uint32_t restore_clusters = dag->clusters.size();
		const uint32_t restore_groups = dag->groups.size();

		LocalVector<uint32_t> next;
		LocalVector<uint32_t> created_groups;

		for (uint32_t g = 0; g < group_count; g++) {
			const LocalVector<uint32_t> &group_members = members[g];
			if (group_members.is_empty()) {
				continue;
			}

			LocalVector<uint32_t> merged;
			LocalVector<NaniteDAG::Sphere> child_bounds;
			float max_child_error = 0.0f;
			for (uint32_t cluster_id : group_members) {
				const NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
				for (uint32_t k = 0; k < cluster.index_count; k++) {
					merged.push_back(dag->indices[cluster.index_offset + k]);
				}
				child_bounds.push_back(cluster.lod_bounds);
				max_child_error = MAX(max_child_error, cluster.error);
			}

			uint32_t target_index_count = (uint32_t)(merged.size() * p_settings.simplify_ratio);
			target_index_count -= target_index_count % 3;
			target_index_count = MAX(target_index_count, 3u);

			LocalVector<uint32_t> simplified;
			simplified.resize(merged.size());
			float simplify_error = 0.0f;
			// Sparse: the group is a small subset of the mesh. ErrorAbsolute:
			// the reported error is in mesh units, so it can be compared and
			// accumulated across levels without a scale conversion.
			const size_t simplified_count = meshopt_simplifyWithAttributes(simplified.ptr(), merged.ptr(), merged.size(),
					ctx.positions, p_vertex_count, POSITION_STRIDE,
					p_attribute_count > 0 ? p_attributes.ptr() : nullptr, sizeof(float) * p_attribute_count,
					p_attribute_count > 0 ? p_attribute_weights.ptr() : nullptr, p_attribute_count,
					vertex_lock.ptr(), target_index_count, FLT_MAX, simplify_options, &simplify_error);

			// Two ways a group can fail, both ending with its members staying
			// roots rather than gaining a parent that misrepresents them.
			//
			// The first is simply not shrinking. The second is subtler and was
			// found by measurement: when meshoptimizer stops short on topology
			// constraints, the error it reports is not a geometric distance at
			// all. A UV sphere step that removed one triangle reported an error
			// of half the mesh radius, and another that cut 27% of triangles
			// reported more than the sphere's diameter. Accumulating either
			// would corrupt the LOD error of every level above -- the very
			// quantity the runtime projects to pick a cut, and that the GBuffer
			// contract uses to bound motion residual across an LOD switch.
			//
			// The bound is physical rather than tuned: a simplification cannot
			// displace the surface further than the extent of the geometry it
			// was given.
			// `simplify_error` ordered the collapses; it is not a distance, so
			// it stops here. What the DAG stores is measured.
			const float step_error = simplified_count >= 3
					? measure_geometric_deviation(ctx.positions, merged, simplified.ptr(), simplified_count)
					: 0.0f;

			const uint32_t progress_ceiling = (uint32_t)(merged.size() * MIN(p_settings.min_progress_ratio, 1.0f));
			const NaniteDAG::Sphere merged_bounds = sphere_from_indices(ctx.positions, merged.ptr(), merged.size());
			const bool stalled = simplified_count < 3 || simplified_count > progress_ceiling;
			const bool error_is_not_a_distance = !(step_error <= 2.0f * merged_bounds.radius);

			if (stalled || error_is_not_a_distance) {
				if (error_is_not_a_distance && !stalled) {
					WARN_PRINT(vformat("Nanite: discarding a simplification of %d triangles whose measured deviation %f exceeds its own extent %f. The group's clusters stay DAG roots.",
							merged.size() / 3, step_error, 2.0f * merged_bounds.radius));
				}
				// Whatever the reason, these clusters are now permanent roots, so
				// their geometry -- and therefore every seam they share with the
				// rest of the mesh -- must survive untouched through every level
				// above.
				for (uint32_t cluster_id : group_members) {
					const NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
					for (uint32_t k = 0; k < cluster.index_count; k++) {
						permanently_locked[weld[dag->indices[cluster.index_offset + k]]] = 1;
					}
				}
				continue;
			}

			NaniteDAG::Group group;
			group.level = level;
			// Monotonic by construction: a group's error is its worst child's
			// error plus the distance this simplification moved the surface.
			group.error = max_child_error + MAX(step_error, 0.0f);
			group.lod_bounds = enclose_spheres(child_bounds);
			group.children = group_members;

			const uint32_t group_id = dag->groups.size();
			dag->groups.push_back(group);

			LocalVector<uint32_t> produced;
			ctx.append_clusters(simplified.ptr(), simplified_count, level + 1, produced);
			if (produced.is_empty()) {
				dag->groups.resize(group_id); // Undo, nothing was produced.
				continue;
			}

			for (uint32_t cluster_id : produced) {
				NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
				cluster.source_group = group_id;
				cluster.error = group.error;
				cluster.lod_bounds = group.lod_bounds;
				next.push_back(cluster_id);
			}
			dag->groups[group_id].produced = produced;
			created_groups.push_back(group_id);
		}

		// 4. Commit only on strict progress. Cluster count is a positive
		//    integer, so strict reduction is what guarantees termination.
		if (next.is_empty() || next.size() >= current.size()) {
			dag->indices.resize(restore_indices);
			dag->clusters.resize(restore_clusters);
			dag->groups.resize(restore_groups);
			break;
		}

		for (uint32_t group_id : created_groups) {
			const NaniteDAG::Group &group = dag->groups[group_id];
			for (uint32_t cluster_id : group.children) {
				NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
				cluster.parent_group = group_id;
				cluster.parent_error = group.error;
				cluster.parent_lod_bounds = group.lod_bounds;
			}
		}

		dag->level_offsets.push_back(dag->clusters.size());
		current = next;
	}

	{
		const Vector<String> errors = dag->validate();
		if (!errors.is_empty()) {
			String message = vformat("DAG invariants violated (%d):", errors.size());
			for (int i = 0; i < MIN(errors.size(), 10); i++) {
				message += "\n  " + errors[i];
			}
			if (errors.size() > 10) {
				message += vformat("\n  ... and %d more.", errors.size() - 10);
			}
			FAIL(message);
		}
	}

	return dag;
}

Ref<NaniteDAG> NaniteDAGBuilder::build_from_surface(const Array &p_arrays, const Settings &p_settings, String *r_error) {
	if (p_arrays.size() != Mesh::ARRAY_MAX) {
		FAIL("surface array set has an unexpected size.");
	}

	const PackedVector3Array source_positions = p_arrays[Mesh::ARRAY_VERTEX];
	if (source_positions.is_empty()) {
		FAIL("surface has no vertex array.");
	}
	const uint32_t vertex_count = source_positions.size();

	// S1 builds against rest-pose positions. A skinned surface deforms at
	// runtime, which moves cluster bounds and invalidates the LOD error the cut
	// is chosen from, so it must be refused rather than silently mis-clustered.
	const PackedInt32Array source_bones = p_arrays[Mesh::ARRAY_BONES];
	const PackedFloat32Array source_weights = p_arrays[Mesh::ARRAY_WEIGHTS];
	if (!source_bones.is_empty() || !source_weights.is_empty()) {
		FAIL("surface is skinned; stage S1 supports static geometry only.");
	}

	const PackedVector3Array source_normals = p_arrays[Mesh::ARRAY_NORMAL];
	const PackedVector2Array source_uvs = p_arrays[Mesh::ARRAY_TEX_UV];
	const bool has_normals = source_normals.size() == (int)vertex_count;
	const bool has_uvs = source_uvs.size() == (int)vertex_count;

	LocalVector<float> positions;
	positions.resize((uint64_t)vertex_count * 3);
	for (uint32_t i = 0; i < vertex_count; i++) {
		positions[i * 3 + 0] = (float)source_positions[i].x;
		positions[i * 3 + 1] = (float)source_positions[i].y;
		positions[i * 3 + 2] = (float)source_positions[i].z;
	}

	const uint32_t attribute_count = (has_normals ? 3 : 0) + (has_uvs ? 2 : 0);
	LocalVector<float> attributes;
	LocalVector<float> attribute_weights;
	if (attribute_count > 0) {
		attributes.resize((uint64_t)vertex_count * attribute_count);
		for (uint32_t i = 0; i < vertex_count; i++) {
			uint32_t offset = i * attribute_count;
			if (has_normals) {
				attributes[offset++] = (float)source_normals[i].x;
				attributes[offset++] = (float)source_normals[i].y;
				attributes[offset++] = (float)source_normals[i].z;
			}
			if (has_uvs) {
				attributes[offset++] = (float)source_uvs[i].x;
				attributes[offset++] = (float)source_uvs[i].y;
			}
		}
		if (has_normals) {
			for (uint32_t i = 0; i < 3; i++) {
				attribute_weights.push_back(p_settings.normal_weight);
			}
		}
		if (has_uvs) {
			for (uint32_t i = 0; i < 2; i++) {
				attribute_weights.push_back(p_settings.uv_weight);
			}
		}
	}

	LocalVector<uint32_t> indices;
	const PackedInt32Array source_indices = p_arrays[Mesh::ARRAY_INDEX];
	if (source_indices.is_empty()) {
		// Unindexed surfaces are a valid import product; synthesize the
		// trivial index buffer rather than rejecting them.
		indices.resize(vertex_count);
		for (uint32_t i = 0; i < vertex_count; i++) {
			indices[i] = i;
		}
	} else {
		indices.resize(source_indices.size());
		for (int i = 0; i < source_indices.size(); i++) {
			indices[i] = (uint32_t)source_indices[i];
		}
	}

	return build(positions, vertex_count, attributes, attribute_count, attribute_weights, indices, p_settings, r_error);
}

#undef FAIL
