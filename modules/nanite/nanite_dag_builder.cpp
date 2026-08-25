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
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/hashfuncs.h"
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
uint64_t hash_buffer_64(const void *p_data, int p_length, uint64_t p_carry) {
	const uint32_t low = hash_murmur3_buffer(p_data, p_length, (uint32_t)(p_carry & 0xFFFFFFFF));
	const uint32_t high = hash_murmur3_buffer(p_data, p_length, (uint32_t)(p_carry >> 32) ^ 0x9E3779B9u);
	return (uint64_t)low | ((uint64_t)high << 32);
}

// Only the settings that change the output. A diagnostic toggle must not
// invalidate a stored artifact, and anything that reshapes the DAG must.
uint64_t hash_settings(const NaniteDAGBuilder::Settings &p_settings) {
	const float values[] = {
		(float)p_settings.max_cluster_vertices, (float)p_settings.max_cluster_triangles,
		p_settings.cone_weight, (float)p_settings.group_size, p_settings.simplify_ratio,
		p_settings.normal_weight, p_settings.uv_weight, p_settings.min_progress_ratio,
		p_settings.spatial_clustering ? 1.0f : 0.0f, p_settings.lock_mesh_border ? 1.0f : 0.0f,
		(float)p_settings.max_levels
	};
	return hash_buffer_64(values, (int)sizeof(values), 0x1234567890ABCDEFull);
}

// What the builder actually consumed, which is not the same thing as the source
// file: scene import options reshape this geometry without changing a byte on
// disk, and hashing the file would leave a stale artifact alive in that case.
uint64_t hash_geometry(const LocalVector<float> &p_positions, const LocalVector<float> &p_normals,
		const LocalVector<float> &p_uvs, const LocalVector<uint32_t> &p_indices) {
	uint64_t carry = 0xC0FFEE0000000001ull;
	carry = hash_buffer_64(p_positions.ptr(), (int)(p_positions.size() * sizeof(float)), carry);
	if (!p_normals.is_empty()) {
		carry = hash_buffer_64(p_normals.ptr(), (int)(p_normals.size() * sizeof(float)), carry);
	}
	if (!p_uvs.is_empty()) {
		carry = hash_buffer_64(p_uvs.ptr(), (int)(p_uvs.size() * sizeof(float)), carry);
	}
	carry = hash_buffer_64(p_indices.ptr(), (int)(p_indices.size() * sizeof(uint32_t)), carry);
	return carry;
}

// The largest distance from any point of a triangle to its nearest corner.
// For an acute triangle that is the circumradius; once a triangle is right or
// obtuse the circumcenter leaves it and the worst point is the midpoint of the
// longest edge. Both cases are exact, and the degenerate one falls out of the
// second without dividing by a zero area.
float max_gap_from_corners(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c) {
	const float ab = (float)p_a.distance_to(p_b);
	const float bc = (float)p_b.distance_to(p_c);
	const float ca = (float)p_c.distance_to(p_a);
	const float longest = MAX(ab, MAX(bc, ca));
	const float other_a = (longest == ab) ? bc : ((longest == bc) ? ca : ab);
	const float other_b = (longest == ab) ? ca : ((longest == bc) ? ab : bc);

	if (longest * longest >= other_a * other_a + other_b * other_b) {
		return longest * 0.5f;
	}
	const float area = (float)(p_b - p_a).cross(p_c - p_a).length() * 0.5f;
	if (area <= 0.0f) {
		return longest * 0.5f;
	}
	return (ab * bc * ca) / (4.0f * area);
}

// Centroid plus the three edge midpoints. Recorded in the artifact, because
// the stored error is the largest deviation seen at these samples rather than a
// proven bound, and a consumer that does not know the density cannot say what
// the number means.
constexpr uint32_t DEVIATION_SAMPLES_PER_TRIANGLE = 4;

// Furthest any interior sample of one triangle set sits from the other set.
// Used in both directions so that the recorded density means the same thing
// whichever way the surfaces are compared.
float sample_triangles_against(const float *p_positions, const uint32_t *p_from, size_t p_from_count,
		const uint32_t *p_to, size_t p_to_count) {
	const size_t to_triangles = p_to_count / 3;
	if (to_triangles == 0) {
		return 0.0f;
	}

	LocalVector<Vector3> centroids;
	LocalVector<float> radii;
	centroids.resize(to_triangles);
	radii.resize(to_triangles);
	for (size_t t = 0; t < to_triangles; t++) {
		const Vector3 a = get_position(p_positions, p_to[t * 3 + 0]);
		const Vector3 b = get_position(p_positions, p_to[t * 3 + 1]);
		const Vector3 c = get_position(p_positions, p_to[t * 3 + 2]);
		centroids[t] = (a + b + c) / 3.0f;
		radii[t] = MAX(MAX((float)centroids[t].distance_to(a), (float)centroids[t].distance_to(b)),
				(float)centroids[t].distance_to(c));
	}

	float worst = 0.0f;
	for (size_t f = 0; f + 2 < p_from_count; f += 3) {
		const Vector3 a = get_position(p_positions, p_from[f + 0]);
		const Vector3 b = get_position(p_positions, p_from[f + 1]);
		const Vector3 c = get_position(p_positions, p_from[f + 2]);
		const Vector3 samples[DEVIATION_SAMPLES_PER_TRIANGLE] = {
			(a + b + c) / 3.0f, (a + b) * 0.5f, (b + c) * 0.5f, (c + a) * 0.5f
		};

		for (const Vector3 &sample : samples) {
			float nearest = FLT_MAX;
			for (size_t t = 0; t < to_triangles; t++) {
				// Bounding-sphere reject, so most triangles never reach the
				// point-triangle test.
				if ((float)sample.distance_to(centroids[t]) - radii[t] >= nearest) {
					continue;
				}
				const Face3 face(get_position(p_positions, p_to[t * 3 + 0]),
						get_position(p_positions, p_to[t * 3 + 1]),
						get_position(p_positions, p_to[t * 3 + 2]));
				nearest = MIN(nearest, (float)sample.distance_to(face.get_closest_point_to(sample)));
			}
			if (nearest != FLT_MAX) {
				worst = MAX(worst, nearest);
			}
		}
	}
	return worst;
}

struct DeviationBound {
	// Sampled interior deviation of the simplified surface from the original.
	// Not a bound -- it is what the analytic term assumes the worst about --
	// but it says how much of that assumption the geometry actually uses.
	float sampled = 0.0f;
	// Furthest any original vertex ended up from the simplified surface. This
	// is what the previous revision stored, and on its own it is an estimate
	// rather than a bound.
	float vertex_measure = 0.0f;
	// Holds unconditionally, from the Lipschitz argument below.
	float analytic = 0.0f;
	// Both directions by measurement rather than worst case.
	float measured = 0.0f;
};

// A bound on how far the surface moved, rather than a sample of it.
//
// The previous revision measured removed vertices against the result and called
// the answer two-sided, reasoning that survivors sit exactly on the original so
// the reverse direction is zero. That is wrong: a surface is not its vertices.
// The interior of a simplified triangle can leave the original entirely -- that
// is precisely what flattening a bump does -- and no vertex is anywhere near
// the gap to witness it.
//
// Two Lipschitz steps close both directions. d(., X) is 1-Lipschitz, so:
//
//   original -> simplified: for a point p in an original triangle with corners
//     q_i, d(p, B) <= min_i (d(q_i, B) + |p - q_i|) <= V + gap(S), where V is
//     the vertex measure and gap(S) the furthest a point of S sits from its
//     own nearest corner.
//
//   simplified -> original: the corners of a simplified triangle are original
//     vertices, so they lie on A at distance 0, giving d(p, A) <= gap(T)
//     directly.
//
// Hence max(V + gap_A, gap_B) bounds the Hausdorff distance. It is loose where
// triangles are large, which is exactly where a flat triangle really can depart
// from a curved patch by about its own radius, so the looseness is honest
// rather than an artifact. Cost is one pass over each triangle set.
DeviationBound measure_deviation(const float *p_positions, const LocalVector<uint32_t> &p_before,
		const uint32_t *p_after, size_t p_after_count) {
	DeviationBound result;

	HashSet<uint32_t> survivors;
	for (size_t i = 0; i < p_after_count; i++) {
		survivors.insert(p_after[i]);
	}

	const size_t after_triangles = p_after_count / 3;
	LocalVector<Vector3> centroids;
	LocalVector<float> radii;
	centroids.resize(after_triangles);
	radii.resize(after_triangles);
	float gap_after = 0.0f;
	for (size_t t = 0; t < after_triangles; t++) {
		const Vector3 a = get_position(p_positions, p_after[t * 3 + 0]);
		const Vector3 b = get_position(p_positions, p_after[t * 3 + 1]);
		const Vector3 c = get_position(p_positions, p_after[t * 3 + 2]);
		centroids[t] = (a + b + c) / 3.0f;
		radii[t] = MAX(MAX((float)centroids[t].distance_to(a), (float)centroids[t].distance_to(b)),
				(float)centroids[t].distance_to(c));
		gap_after = MAX(gap_after, max_gap_from_corners(a, b, c));
	}

	float gap_before = 0.0f;
	for (uint32_t t = 0; t + 2 < p_before.size(); t += 3) {
		gap_before = MAX(gap_before, max_gap_from_corners(get_position(p_positions, p_before[t + 0]), get_position(p_positions, p_before[t + 1]), get_position(p_positions, p_before[t + 2])));
	}

	// Survivors sit on the result at distance zero, so only the removed
	// vertices can contribute to the vertex term.
	HashSet<uint32_t> measured;
	for (const uint32_t vertex : p_before) {
		if (survivors.has(vertex) || measured.has(vertex)) {
			continue;
		}
		measured.insert(vertex);

		const Vector3 point = get_position(p_positions, vertex);
		float nearest = FLT_MAX;
		for (size_t t = 0; t < after_triangles; t++) {
			// Bounding-sphere reject, so most triangles never reach the
			// point-triangle test.
			if ((float)point.distance_to(centroids[t]) - radii[t] >= nearest) {
				continue;
			}
			const Face3 face(get_position(p_positions, p_after[t * 3 + 0]),
					get_position(p_positions, p_after[t * 3 + 1]),
					get_position(p_positions, p_after[t * 3 + 2]));
			nearest = MIN(nearest, (float)point.distance_to(face.get_closest_point_to(point)));
		}
		if (nearest != FLT_MAX) {
			result.vertex_measure = MAX(result.vertex_measure, nearest);
		}
	}

	// Both directions, sampled the same way. Sampling only one and calling the
	// pair symmetric was the previous mistake: the original surface's triangle
	// interiors were never examined, so a bump that the simplified surface cut
	// straight through went unseen from that side.
	//
	// Each triangle contributes DEVIATION_SAMPLES_PER_TRIANGLE interior points,
	// and the original's vertices are measured on top, since a removed vertex
	// is where the surfaces most often part company.
	result.sampled = MAX(result.sampled,
			sample_triangles_against(p_positions, p_after, p_after_count, p_before.ptr(), p_before.size()));
	result.sampled = MAX(result.sampled,
			sample_triangles_against(p_positions, p_before.ptr(), p_before.size(), p_after, p_after_count));

	result.analytic = MAX(result.vertex_measure + gap_before, gap_after);
	result.measured = MAX(result.vertex_measure, result.sampled);
	return result;
}

// Whether a simplification produced geometry where some edge carries more than
// two triangles. A closed surface is manifold, so that is a fold: the surface
// has doubled back onto itself. Boundary edges carrying one triangle are
// expected and ignored -- the group is a patch, not a closed mesh.
//
// Edges are compared in position space, since a seam duplicates vertices at one
// point and index comparison would miss folds that meet across it.
bool folds_onto_itself(const uint32_t *p_indices, size_t p_index_count, const LocalVector<uint32_t> &p_weld) {
	HashMap<uint64_t, uint32_t> edge_use;
	for (size_t t = 0; t + 2 < p_index_count; t += 3) {
		for (uint32_t e = 0; e < 3; e++) {
			const uint32_t a = p_weld[p_indices[t + e]];
			const uint32_t b = p_weld[p_indices[t + (e + 1) % 3]];
			if (a == b) {
				continue;
			}
			const uint64_t key = ((uint64_t)MIN(a, b) << 32) | (uint64_t)MAX(a, b);
			uint32_t *existing = edge_use.getptr(key);
			if (existing) {
				if (++(*existing) > 2) {
					return true;
				}
			} else {
				edge_use.insert(key, 1);
			}
		}
	}
	return false;
}

// Global index buffer for one cluster, which is what the partitioner and the
// simplifier both consume.
void append_cluster_indices(const NaniteDAG &p_dag, uint32_t p_cluster, LocalVector<uint32_t> &r_out) {
	const NaniteDAG::Cluster &cluster = p_dag.clusters[p_cluster];
	for (uint32_t k = 0; k < cluster.triangle_count * 3; k++) {
		const uint8_t local = p_dag.cluster_indices[cluster.triangle_offset + k];
		r_out.push_back(p_dag.cluster_vertices[cluster.vertex_offset + local]);
	}
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
			// meshoptimizer already emits a vertex slice plus 8-bit locals,
			// which is the pool layout, so it is kept rather than flattened.
			cluster.vertex_offset = dag->cluster_vertices.size();
			cluster.vertex_count = meshlet.vertex_count;
			cluster.triangle_offset = dag->cluster_indices.size();
			cluster.triangle_count = meshlet.triangle_count;

			for (uint32_t k = 0; k < meshlet.vertex_count; k++) {
				dag->cluster_vertices.push_back(meshlet_vertices[meshlet.vertex_offset + k]);
			}
			LocalVector<uint32_t> global;
			for (uint32_t k = 0; k < meshlet.triangle_count * 3; k++) {
				const uint8_t local = meshlet_triangles[meshlet.triangle_offset + k];
				dag->cluster_indices.push_back(local);
				global.push_back(meshlet_vertices[meshlet.vertex_offset + local]);
			}
			const meshopt_Bounds meshopt_bounds = meshopt_computeClusterBounds(global.ptr(), global.size(),
					positions, vertex_count, POSITION_STRIDE);
			cluster.bounds.center = Vector3(meshopt_bounds.center[0], meshopt_bounds.center[1], meshopt_bounds.center[2]);
			cluster.bounds.radius = meshopt_bounds.radius;
			cluster.cone_apex = Vector3(meshopt_bounds.cone_apex[0], meshopt_bounds.cone_apex[1], meshopt_bounds.cone_apex[2]);
			cluster.cone_axis = Vector3(meshopt_bounds.cone_axis[0], meshopt_bounds.cone_axis[1], meshopt_bounds.cone_axis[2]);
			cluster.cone_cutoff = meshopt_bounds.cone_cutoff;
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

Ref<NaniteDAG> NaniteDAGBuilder::build(const LocalVector<float> &p_positions, const LocalVector<float> &p_normals,
		const LocalVector<float> &p_uvs, uint32_t p_vertex_count, const LocalVector<uint32_t> &p_indices,
		const Settings &p_settings, String *r_error) {
	const bool has_normals = p_normals.size() == (uint64_t)p_vertex_count * 3;
	const bool has_uvs = p_uvs.size() == (uint64_t)p_vertex_count * 2;
	if (!p_normals.is_empty() && !has_normals) {
		FAIL("normal buffer size does not match the vertex count.");
	}
	if (!p_uvs.is_empty() && !has_uvs) {
		FAIL("UV buffer size does not match the vertex count.");
	}

	// The simplifier's attribute metric runs on the same values the DAG stores.
	const uint32_t p_attribute_count = (has_normals ? 3 : 0) + (has_uvs ? 2 : 0);
	LocalVector<float> p_attributes;
	LocalVector<float> p_attribute_weights;
	if (p_attribute_count > 0) {
		p_attributes.resize((uint64_t)p_vertex_count * p_attribute_count);
		for (uint32_t i = 0; i < p_vertex_count; i++) {
			uint32_t offset = i * p_attribute_count;
			if (has_normals) {
				for (uint32_t k = 0; k < 3; k++) {
					p_attributes[offset++] = p_normals[i * 3 + k];
				}
			}
			if (has_uvs) {
				for (uint32_t k = 0; k < 2; k++) {
					p_attributes[offset++] = p_uvs[i * 2 + k];
				}
			}
		}
		if (has_normals) {
			for (uint32_t i = 0; i < 3; i++) {
				p_attribute_weights.push_back(p_settings.normal_weight);
			}
		}
		if (has_uvs) {
			for (uint32_t i = 0; i < 2; i++) {
				p_attribute_weights.push_back(p_settings.uv_weight);
			}
		}
	}
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
	// Ceilings come from the geometry pool contract, not from meshoptimizer's
	// wider limits: exceeding them produces clusters the rasterizer cannot
	// address and a local index that does not fit a byte.
	if (p_settings.max_cluster_vertices < 3 || p_settings.max_cluster_vertices > NaniteDAG::MAX_CLUSTER_VERTICES) {
		FAIL(vformat("max_cluster_vertices must be in [3, %d].", NaniteDAG::MAX_CLUSTER_VERTICES));
	}
	if (p_settings.max_cluster_triangles < 1 || p_settings.max_cluster_triangles > NaniteDAG::MAX_CLUSTER_TRIANGLES) {
		FAIL(vformat("max_cluster_triangles must be in [1, %d].", NaniteDAG::MAX_CLUSTER_TRIANGLES));
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

	// The vertex record the pool contract asks for. Absent inputs get a
	// well-defined placeholder rather than an empty buffer, so the record is
	// always complete.
	dag->normals.resize(p_vertex_count);
	dag->uvs.resize(p_vertex_count);
	for (uint32_t i = 0; i < p_vertex_count; i++) {
		dag->normals[i] = NaniteDAG::encode_normal(has_normals
						? Vector3(p_normals[i * 3 + 0], p_normals[i * 3 + 1], p_normals[i * 3 + 2])
						: Vector3(0, 0, 1));
		dag->uvs[i] = has_uvs ? NaniteDAG::encode_uv(p_uvs[i * 2 + 0], p_uvs[i * 2 + 1]) : NaniteDAG::encode_uv(0.0f, 0.0f);
	}

	// Identity, per the artifact contract. The geometry hash covers what the
	// builder actually consumed rather than the source file, because scene
	// import options change this geometry without touching the file.
	dag->deviation_samples_per_triangle = DEVIATION_SAMPLES_PER_TRIANGLE;
	dag->settings_hash = hash_settings(p_settings);
	dag->geometry_hash = hash_geometry(p_positions, p_normals, p_uvs, p_indices);

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
			const uint32_t before = cluster_indices.size();
			append_cluster_indices(**dag, cluster_id, cluster_indices);
			cluster_index_counts.push_back(cluster_indices.size() - before);
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
			LocalVector<uint32_t> cluster_global;
			append_cluster_indices(**dag, current[i], cluster_global);
			for (const uint32_t index : cluster_global) {
				const uint32_t welded = weld[index];
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
		const uint32_t restore_vertices = dag->cluster_vertices.size();
		const uint32_t restore_indices = dag->cluster_indices.size();
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
			float max_child_analytic = 0.0f;
			for (uint32_t cluster_id : group_members) {
				append_cluster_indices(**dag, cluster_id, merged);
				child_bounds.push_back(dag->get_cluster_lod_bounds(cluster_id));
				max_child_error = MAX(max_child_error, dag->get_cluster_error(cluster_id));
				max_child_analytic = MAX(max_child_analytic, dag->get_cluster_analytic_error(cluster_id));
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
			const DeviationBound deviation = simplified_count >= 3
					? measure_deviation(ctx.positions, merged, simplified.ptr(), simplified_count)
					: DeviationBound();
			const float step_error = deviation.measured;
			if (p_settings.report_deviation_terms) {
				print_line(vformat("  [deviation] level %d group %d: vertex %.6f, sampled %.6f, bound %.6f",
						level, g, deviation.vertex_measure, deviation.sampled, deviation.analytic));
			}

			// Checked first, and fatally. Ordering matters: the "not a distance"
			// test below is written as a negated comparison, so a NaN or an
			// infinity satisfies it and would be quietly routed to the
			// discard-and-continue path -- which is a silent clamp wearing a
			// different hat. A deviation that is not a finite non-negative
			// number means the measurement is broken, and that is fatal.
			if (!Math::is_finite(step_error) || step_error < 0.0f) {
				FAIL(vformat("measured a deviation of %f, which is not a distance. The measurement is broken.", step_error));
			}
			if (!Math::is_finite(deviation.analytic) || deviation.analytic < step_error) {
				FAIL(vformat("the analytic bound %f does not bound the measured deviation %f.", deviation.analytic, step_error));
			}

			const uint32_t progress_ceiling = (uint32_t)(merged.size() * MIN(p_settings.min_progress_ratio, 1.0f));
			const NaniteDAG::Sphere merged_bounds = sphere_from_indices(ctx.positions, merged.ptr(), merged.size());
			const bool stalled = simplified_count < 3 || simplified_count > progress_ceiling;
			const bool error_is_not_a_distance = !(step_error <= 2.0f * merged_bounds.radius);
			// Extreme reduction can fold a patch onto itself, which leaves the
			// drawn surface non-manifold at whatever threshold selects it.
			// Cheaper to refuse the result than to hope no cut picks it.
			const bool folded = !stalled && folds_onto_itself(simplified.ptr(), simplified_count, weld);

			if (stalled || error_is_not_a_distance || folded) {
				if (folded && !error_is_not_a_distance) {
					WARN_PRINT(vformat("Nanite: discarding a simplification of %d triangles that folded onto itself. The group's clusters stay DAG roots.",
							merged.size() / 3));
				}
				if (error_is_not_a_distance && !stalled) {
					WARN_PRINT(vformat("Nanite: discarding a simplification of %d triangles whose measured deviation %f exceeds its own extent %f. The group's clusters stay DAG roots.",
							merged.size() / 3, step_error, 2.0f * merged_bounds.radius));
				}
				// Whatever the reason, these clusters are now permanent roots, so
				// their geometry -- and therefore every seam they share with the
				// rest of the mesh -- must survive untouched through every level
				// above.
				for (uint32_t cluster_id : group_members) {
					LocalVector<uint32_t> cluster_global;
					append_cluster_indices(**dag, cluster_id, cluster_global);
					for (const uint32_t index : cluster_global) {
						permanently_locked[weld[index]] = 1;
					}
				}
				continue;
			}

			NaniteDAG::Group group;
			group.level = level;
			// Monotonic by construction: a group's error is its worst child's
			// error plus the distance this simplification moved the surface.
			group.error = max_child_error + step_error;
			group.analytic_error = max_child_analytic + deviation.analytic;
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
				// Error and bounds are read back from the group, so there is
				// nothing to copy onto the cluster.
				dag->clusters[cluster_id].source_group = group_id;
				next.push_back(cluster_id);
			}
			dag->groups[group_id].produced = produced;
			created_groups.push_back(group_id);
		}

		// 4. Commit only on strict progress. Cluster count is a positive
		//    integer, so strict reduction is what guarantees termination.
		if (next.is_empty() || next.size() >= current.size()) {
			dag->cluster_vertices.resize(restore_vertices);
			dag->cluster_indices.resize(restore_indices);
			dag->clusters.resize(restore_clusters);
			dag->groups.resize(restore_groups);
			break;
		}

		for (uint32_t group_id : created_groups) {
			const NaniteDAG::Group &group = dag->groups[group_id];
			for (uint32_t cluster_id : group.children) {
				dag->clusters[cluster_id].parent_group = group_id;
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

	LocalVector<float> positions;
	positions.resize((uint64_t)vertex_count * 3);
	for (uint32_t i = 0; i < vertex_count; i++) {
		positions[i * 3 + 0] = (float)source_positions[i].x;
		positions[i * 3 + 1] = (float)source_positions[i].y;
		positions[i * 3 + 2] = (float)source_positions[i].z;
	}

	LocalVector<float> normals;
	if (source_normals.size() == (int)vertex_count) {
		normals.resize((uint64_t)vertex_count * 3);
		for (uint32_t i = 0; i < vertex_count; i++) {
			normals[i * 3 + 0] = (float)source_normals[i].x;
			normals[i * 3 + 1] = (float)source_normals[i].y;
			normals[i * 3 + 2] = (float)source_normals[i].z;
		}
	}

	LocalVector<float> uvs;
	if (source_uvs.size() == (int)vertex_count) {
		uvs.resize((uint64_t)vertex_count * 2);
		for (uint32_t i = 0; i < vertex_count; i++) {
			uvs[i * 2 + 0] = (float)source_uvs[i].x;
			uvs[i * 2 + 1] = (float)source_uvs[i].y;
		}
	}

	LocalVector<uint32_t> indices;
	const PackedInt32Array source_indices = p_arrays[Mesh::ARRAY_INDEX];
	if (source_indices.is_empty()) {
		// Unindexed surfaces are a valid import product; synthesize the trivial
		// index buffer rather than rejecting them.
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

	return build(positions, normals, uvs, vertex_count, indices, p_settings, r_error);
}

#undef FAIL
