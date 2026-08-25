/**************************************************************************/
/*  nanite_dag_builder.h                                                  */
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

#include "nanite_dag.h"

// Offline Nanite S1 DAG builder.
//
// Runs the Karis loop on one triangle surface:
//   cluster -> group (spatially aware) -> simplify with the group boundary
//   locked -> re-cluster -> repeat until nothing merges any more.
//
// The whole loop stands on the bundled meshoptimizer 1.2; no METIS, and no
// renderer coupling whatsoever, which is why it can run ahead of gate G0.
class NaniteDAGBuilder {
public:
	struct Settings {
		// Cluster shape. 128 triangles is the Nanite/Bevy figure; 255 vertices
		// is the mesh-shading friendly ceiling that still fits 8-bit locals.
		// Both are frozen by the geometry pool contract and cannot simply be
		// raised: the visibility buffer spends 7 bits on the triangle index and
		// the pool spends 8 on the local vertex index.
		uint32_t max_cluster_vertices = NaniteDAG::MAX_CLUSTER_VERTICES;
		uint32_t max_cluster_triangles = NaniteDAG::MAX_CLUSTER_TRIANGLES;
		float cone_weight = 0.0f;

		// Clusters per group. Karis groups ~4-32; 8 balances edge-cut against
		// how much geometry a single LOD decision locks together.
		uint32_t group_size = 8;

		// Fraction of a group's triangles to keep per level.
		float simplify_ratio = 0.5f;

		// Attribute weights fed to the QEM metric, relative to position.
		float normal_weight = 0.5f;
		float uv_weight = 0.25f;

		// Optimize cluster subdivision for raytracing (meshopt_buildMeshletsSpatial).
		// Relevant to the BLAS convergence point, off until that is scoped.
		bool spatial_clustering = false;

		// A group must shrink to at most this fraction of its triangles for the
		// simplification to count. Guards against meshoptimizer stalling on
		// topology and reporting a non-geometric error alongside it.
		float min_progress_ratio = 0.95f;

		// Safety net; the loop terminates on its own via strict cluster-count
		// reduction, this only bounds pathological inputs.
		uint32_t max_levels = 32;

		// Lock the surface's open border. Required when a mesh has more than one
		// surface: the border is then a seam shared with another surface that
		// simplifies independently, and moving it cracks between submeshes.
		bool lock_mesh_border = false;
	};

	// The invariant checker always runs and a DAG that trips it is never
	// returned. There is deliberately no way to opt out: a DAG that violates
	// error monotonicity is not a faster DAG, it is a broken one.

	// Builds from a Mesh surface array set (Mesh::ARRAY_*). Triangles only.
	static Ref<NaniteDAG> build_from_surface(const Array &p_arrays, const Settings &p_settings, String *r_error = nullptr);

	// Builds from raw buffers. Positions hold 3 floats per vertex; normals hold
	// 3 and UVs 2, and either may be empty. The vertex record the pool contract
	// asks for is exactly these three, so they are named rather than passed as
	// an opaque attribute block: the same values drive the simplifier's
	// attribute metric and end up stored in the DAG.
	static Ref<NaniteDAG> build(const LocalVector<float> &p_positions, const LocalVector<float> &p_normals,
			const LocalVector<float> &p_uvs, uint32_t p_vertex_count, const LocalVector<uint32_t> &p_indices,
			const Settings &p_settings, String *r_error = nullptr);
};
