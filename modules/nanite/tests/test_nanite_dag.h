/**************************************************************************/
/*  test_nanite_dag.h                                                     */
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

#include "../nanite_dag.h"
#include "../nanite_dag_builder.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/math/face3.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/mesh.h"

#ifdef TOOLS_ENABLED
#include "../editor/nanite_import_plugin.h"
#endif
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include <cfloat>
#include <cmath>
#include <functional>

namespace TestNaniteDAG {

// A displaced grid: enough triangles for several DAG levels, and enough
// curvature that simplification produces a genuinely non-zero QEM error, which
// is what the monotonicity checks need in order to mean anything.
struct TestMesh {
	LocalVector<float> positions;
	LocalVector<float> normals;
	LocalVector<float> uvs;
	LocalVector<uint32_t> indices;
	uint32_t vertex_count = 0;
};

struct PositionKey {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	uint32_t index = 0;

	bool operator<(const PositionKey &p_other) const {
		if (x != p_other.x) {
			return x < p_other.x;
		}
		if (y != p_other.y) {
			return y < p_other.y;
		}
		if (z != p_other.z) {
			return z < p_other.z;
		}
		return index < p_other.index;
	}
	bool same_position(const PositionKey &p_other) const {
		return x == p_other.x && y == p_other.y && z == p_other.z;
	}
};

inline TestMesh make_displaced_grid(uint32_t p_resolution) {
	TestMesh mesh;
	const uint32_t side = p_resolution + 1;
	mesh.vertex_count = side * side;
	mesh.positions.resize((uint64_t)mesh.vertex_count * 3);
	mesh.normals.resize((uint64_t)mesh.vertex_count * 3);
	mesh.uvs.resize((uint64_t)mesh.vertex_count * 2);

	for (uint32_t y = 0; y < side; y++) {
		for (uint32_t x = 0; x < side; x++) {
			const uint32_t v = y * side + x;
			const float fx = (float)x / (float)p_resolution;
			const float fy = (float)y / (float)p_resolution;
			const float height = 0.25f * Math::sin(fx * 8.0f) * Math::cos(fy * 6.0f);
			mesh.positions[v * 3 + 0] = fx * 4.0f - 2.0f;
			mesh.positions[v * 3 + 1] = height;
			mesh.positions[v * 3 + 2] = fy * 4.0f - 2.0f;

			// A cheap analytic normal; exact values do not matter, only that
			// the attribute metric has something to chew on.
			const Vector3 normal = Vector3(-2.0f * Math::cos(fx * 8.0f), 1.0f, 1.5f * Math::sin(fy * 6.0f)).normalized();
			mesh.normals[v * 3 + 0] = (float)normal.x;
			mesh.normals[v * 3 + 1] = (float)normal.y;
			mesh.normals[v * 3 + 2] = (float)normal.z;
			mesh.uvs[v * 2 + 0] = fx;
			mesh.uvs[v * 2 + 1] = fy;
		}
	}

	for (uint32_t y = 0; y < p_resolution; y++) {
		for (uint32_t x = 0; x < p_resolution; x++) {
			const uint32_t v = y * side + x;
			mesh.indices.push_back(v);
			mesh.indices.push_back(v + side);
			mesh.indices.push_back(v + 1);
			mesh.indices.push_back(v + 1);
			mesh.indices.push_back(v + side);
			mesh.indices.push_back(v + side + 1);
		}
	}
	return mesh;
}

// A UV sphere built as a full quad grid, which leaves a fan of zero-area
// triangles at each pole. This is what a naive sphere generator produces and
// what real assets often contain; measured, it is also what drives a group to
// fail to simplify.
inline TestMesh make_pole_sphere(uint32_t p_segments, uint32_t p_rings) {
	TestMesh mesh;
	const uint32_t stride = p_segments + 1;
	mesh.vertex_count = stride * (p_rings + 1);
	mesh.positions.resize((uint64_t)mesh.vertex_count * 3);
	mesh.normals.resize((uint64_t)mesh.vertex_count * 3);
	mesh.uvs.resize((uint64_t)mesh.vertex_count * 2);

	for (uint32_t r = 0; r <= p_rings; r++) {
		const float phi = (float)r / (float)p_rings * (float)Math::PI;
		for (uint32_t sgm = 0; sgm <= p_segments; sgm++) {
			const float theta = (float)sgm / (float)p_segments * 2.0f * (float)Math::PI;
			const uint32_t v = r * stride + sgm;
			const Vector3 position = Vector3(Math::sin(phi) * Math::cos(theta), Math::cos(phi), Math::sin(phi) * Math::sin(theta));
			for (uint32_t axis = 0; axis < 3; axis++) {
				mesh.positions[v * 3 + axis] = (float)position[axis];
				mesh.normals[v * 3 + axis] = (float)position[axis];
			}
			mesh.uvs[v * 2 + 0] = (float)sgm / (float)p_segments;
			mesh.uvs[v * 2 + 1] = (float)r / (float)p_rings;
		}
	}
	for (uint32_t r = 0; r < p_rings; r++) {
		for (uint32_t sgm = 0; sgm < p_segments; sgm++) {
			const uint32_t a = r * stride + sgm;
			const uint32_t b = a + stride;
			mesh.indices.push_back(a);
			mesh.indices.push_back(b);
			mesh.indices.push_back(a + 1);
			mesh.indices.push_back(a + 1);
			mesh.indices.push_back(b);
			mesh.indices.push_back(b + 1);
		}
	}
	return mesh;
}

// A closed sphere with proper triangle fans at the poles, so it has no
// degenerate triangles and no boundary edges. Every edge is shared by exactly
// two triangles, which is what makes it usable as a watertightness fixture.
inline TestMesh make_closed_sphere(uint32_t p_segments, uint32_t p_rings) {
	TestMesh mesh;
	const uint32_t stride = p_segments + 1;
	mesh.vertex_count = stride * (p_rings + 1);
	mesh.positions.resize((uint64_t)mesh.vertex_count * 3);
	mesh.normals.resize((uint64_t)mesh.vertex_count * 3);
	mesh.uvs.resize((uint64_t)mesh.vertex_count * 2);

	for (uint32_t r = 0; r <= p_rings; r++) {
		const float phi = (float)r / (float)p_rings * (float)Math::PI;
		for (uint32_t sgm = 0; sgm <= p_segments; sgm++) {
			const float theta = (float)sgm / (float)p_segments * 2.0f * (float)Math::PI;
			const uint32_t v = r * stride + sgm;
			// Snap the poles. sin(PI) is ~1e-7 in float rather than 0, so
			// computing them would scatter the pole across a ring of distinct
			// positions, and the fan around it would never close.
			Vector3 position;
			if (r == 0) {
				position = Vector3(0.0f, 1.0f, 0.0f);
			} else if (r == p_rings) {
				position = Vector3(0.0f, -1.0f, 0.0f);
			} else if (sgm == p_segments) {
				// Same reason as the poles: sin(2*PI) is not 0 in float, so the
				// wrap-around column has to reuse column 0 verbatim to weld.
				const uint32_t first = r * stride;
				position = Vector3(mesh.positions[first * 3], mesh.positions[first * 3 + 1], mesh.positions[first * 3 + 2]);
			} else {
				position = Vector3(Math::sin(phi) * Math::cos(theta), Math::cos(phi), Math::sin(phi) * Math::sin(theta));
			}
			for (uint32_t axis = 0; axis < 3; axis++) {
				mesh.positions[v * 3 + axis] = (float)position[axis];
				mesh.normals[v * 3 + axis] = (float)position[axis];
			}
			mesh.uvs[v * 2 + 0] = (float)sgm / (float)p_segments;
			mesh.uvs[v * 2 + 1] = (float)r / (float)p_rings;
		}
	}
	for (uint32_t r = 0; r < p_rings; r++) {
		for (uint32_t sgm = 0; sgm < p_segments; sgm++) {
			const uint32_t a = r * stride + sgm;
			const uint32_t b = a + stride;
			if (r > 0) {
				mesh.indices.push_back(a);
				mesh.indices.push_back(b);
				mesh.indices.push_back(a + 1);
			}
			if (r + 1 < p_rings) {
				mesh.indices.push_back(a + 1);
				mesh.indices.push_back(b);
				mesh.indices.push_back(b + 1);
			}
		}
	}
	return mesh;
}

// Maps every vertex to the first vertex sharing its position. Seam columns and
// pole fans duplicate positions for attribute reasons, so edges have to be
// compared in position space or a closed surface looks torn.
inline LocalVector<uint32_t> weld_by_position(const NaniteDAG &p_dag) {
	const float *positions = p_dag.positions.ptr();
	LocalVector<PositionKey> keys;
	keys.resize(p_dag.vertex_count);
	for (uint32_t i = 0; i < p_dag.vertex_count; i++) {
		keys[i] = PositionKey{ positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2], i };
	}
	keys.sort();

	LocalVector<uint32_t> weld;
	weld.resize(p_dag.vertex_count);
	uint32_t canonical = keys.is_empty() ? 0 : keys[0].index;
	for (uint32_t i = 0; i < keys.size(); i++) {
		if (i > 0 && !keys[i].same_position(keys[i - 1])) {
			canonical = keys[i].index;
		}
		weld[keys[i].index] = canonical;
	}
	return weld;
}

// PackedByteArray's script-side encode_u32 is not on the C++ Vector<uint8_t>.
inline uint32_t peek_u32(const PackedByteArray &p_data, int p_offset) {
	const uint8_t *r = p_data.ptr();
	return (uint32_t)r[p_offset] | ((uint32_t)r[p_offset + 1] << 8) |
			((uint32_t)r[p_offset + 2] << 16) | ((uint32_t)r[p_offset + 3] << 24);
}

inline void poke_u32(PackedByteArray &r_data, int p_offset, uint32_t p_value) {
	uint8_t *w = r_data.ptrw();
	w[p_offset + 0] = (uint8_t)(p_value & 0xFF);
	w[p_offset + 1] = (uint8_t)((p_value >> 8) & 0xFF);
	w[p_offset + 2] = (uint8_t)((p_value >> 16) & 0xFF);
	w[p_offset + 3] = (uint8_t)((p_value >> 24) & 0xFF);
}

// Every threshold at which the cut can change: the cut only moves when it
// crosses a group's error, so this enumerates every distinct cut the DAG can
// produce rather than sampling a handful of arbitrary values.
// A grid cut into independent blocks, so vertices along every block boundary
// are duplicated rather than shared. This is what material boundaries, UV
// seams and normal creases do to a real asset, and it is the axis the size
// budget is most sensitive to: vertex data is charged per vertex, so the
// vertex-to-triangle ratio drives bytes per triangle directly. The synthetic
// grid and sphere both sit near 0.5; production geometry does not.
inline TestMesh make_seamed_grid(uint32_t p_resolution, uint32_t p_block) {
	TestMesh mesh;
	const uint32_t blocks = p_resolution / p_block;
	const uint32_t block_side = p_block + 1;

	for (uint32_t by = 0; by < blocks; by++) {
		for (uint32_t bx = 0; bx < blocks; bx++) {
			const uint32_t base = mesh.vertex_count;
			for (uint32_t y = 0; y <= p_block; y++) {
				for (uint32_t x = 0; x <= p_block; x++) {
					const float fx = (float)(bx * p_block + x) / (float)p_resolution;
					const float fy = (float)(by * p_block + y) / (float)p_resolution;
					const float height = 0.25f * Math::sin(fx * 8.0f) * Math::cos(fy * 6.0f);
					mesh.positions.push_back(fx * 4.0f - 2.0f);
					mesh.positions.push_back(height);
					mesh.positions.push_back(fy * 4.0f - 2.0f);
					const Vector3 normal = Vector3(-2.0f * Math::cos(fx * 8.0f), 1.0f, 1.5f * Math::sin(fy * 6.0f)).normalized();
					mesh.normals.push_back((float)normal.x);
					mesh.normals.push_back((float)normal.y);
					mesh.normals.push_back((float)normal.z);
					mesh.uvs.push_back(fx);
					mesh.uvs.push_back(fy);
					mesh.vertex_count++;
				}
			}
			for (uint32_t y = 0; y < p_block; y++) {
				for (uint32_t x = 0; x < p_block; x++) {
					const uint32_t v = base + y * block_side + x;
					mesh.indices.push_back(v);
					mesh.indices.push_back(v + block_side);
					mesh.indices.push_back(v + 1);
					mesh.indices.push_back(v + 1);
					mesh.indices.push_back(v + block_side);
					mesh.indices.push_back(v + block_side + 1);
				}
			}
		}
	}
	return mesh;
}

inline LocalVector<float> every_distinct_threshold(const NaniteDAG &p_dag) {
	LocalVector<float> errors;
	errors.push_back(0.0f);
	for (const NaniteDAG::Group &group : p_dag.groups) {
		errors.push_back(group.error);
	}
	errors.sort();

	LocalVector<float> thresholds;
	for (uint32_t i = 0; i < errors.size(); i++) {
		if (i == 0 || errors[i] != errors[i - 1]) {
			thresholds.push_back(errors[i]);
			// Just past the boundary, where the cut has actually moved on.
			thresholds.push_back(nextafterf(errors[i], 3.4e38f));
		}
	}
	return thresholds;
}

inline Ref<NaniteDAG> build_test_dag(const TestMesh &p_mesh, const NaniteDAGBuilder::Settings &p_settings) {
	return NaniteDAGBuilder::build(p_mesh.positions, p_mesh.normals, p_mesh.uvs,
			p_mesh.vertex_count, p_mesh.indices, p_settings);
}

TEST_CASE("[Nanite] DAG builder produces a valid multi-level DAG") {
	const TestMesh mesh = make_displaced_grid(64);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);

	REQUIRE(dag.is_valid());
	CHECK_MESSAGE(dag->validate().is_empty(), "A freshly built DAG must satisfy every invariant.");
	CHECK_MESSAGE(dag->get_level_count() >= 2, "A 8192-triangle grid must produce more than one level.");

	// Level 0 must reproduce the input geometry exactly.
	CHECK(dag->get_level_triangle_count(0) == mesh.indices.size() / 3);

	// Cluster count has to shrink strictly, which is what guarantees the loop
	// terminates rather than grinding on a level that makes no progress.
	for (uint32_t level = 1; level < dag->get_level_count(); level++) {
		CHECK_MESSAGE(dag->get_level_cluster_count(level) < dag->get_level_cluster_count(level - 1),
				vformat("Level %d did not reduce the cluster count.", level));
	}

	// Every cluster must respect the per-cluster triangle budget.
	for (const NaniteDAG::Cluster &cluster : dag->clusters) {
		CHECK(cluster.triangle_count > 0);
		CHECK(cluster.triangle_count <= settings.max_cluster_triangles);
		CHECK(cluster.vertex_count <= settings.max_cluster_vertices);
	}
}

TEST_CASE("[Nanite] DAG error is monotonic towards the root") {
	const TestMesh mesh = make_displaced_grid(48);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	REQUIRE(dag.is_valid());

	// The property the runtime cut depends on: a cluster is never coarser than
	// the group that replaces it, and the replacement's LOD sphere encloses it.
	// Without both, two levels can be selected at once and the seam cracks.
	for (uint32_t i = 0; i < dag->get_cluster_count(); i++) {
		if (dag->clusters[i].parent_group == NaniteDAG::NO_GROUP) {
			CHECK(Math::is_inf(dag->get_cluster_parent_error(i)));
			continue;
		}
		CHECK_MESSAGE(dag->get_cluster_parent_error(i) >= dag->get_cluster_error(i),
				"Parent error dropped below its child's error.");
		CHECK_MESSAGE(dag->get_cluster_parent_lod_bounds(i).contains(dag->get_cluster_lod_bounds(i), 1e-3f),
				"Parent LOD sphere does not enclose its child's LOD sphere.");
	}

	// Simplification only ever adds error, so it must not fall level to level.
	float previous = -1.0f;
	for (uint32_t level = 0; level < dag->get_level_count(); level++) {
		const float error = dag->get_level_max_error(level);
		CHECK_MESSAGE(error >= previous, vformat("Level %d has a lower maximum error than the level below it.", level));
		previous = error;
	}
	CHECK_MESSAGE(dag->get_level_max_error(dag->get_level_count() - 1) > 0.0f,
			"A curved surface must accumulate a non-zero error by the top level.");
}

TEST_CASE("[Nanite] DAG cut is the frontier of a top-down descent") {
	const TestMesh mesh = make_displaced_grid(48);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	REQUIRE(dag.is_valid());

	const float max_error = dag->get_level_max_error(dag->get_level_count() - 1);
	const float thresholds[] = { 0.0f, max_error * 0.1f, max_error * 0.5f, max_error, max_error * 2.0f };

	uint32_t previous_triangles = UINT32_MAX;
	for (const float threshold : thresholds) {
		const LocalVector<uint32_t> cut = dag->select_cut(threshold);
		CHECK_MESSAGE(!cut.is_empty(), "The cut must never be empty.");

		// Independently walk the DAG from its roots, descending wherever a
		// cluster is too coarse. Reaching the same set proves the threshold
		// rule selects exactly one cluster along every root-to-leaf chain --
		// no gap (a hole in the surface) and no overlap (z-fighting).
		HashSet<uint32_t> descended;
		LocalVector<uint32_t> worklist;
		for (uint32_t i = 0; i < dag->clusters.size(); i++) {
			if (dag->clusters[i].parent_group == NaniteDAG::NO_GROUP) {
				worklist.push_back(i);
			}
		}
		HashSet<uint32_t> visited;
		while (!worklist.is_empty()) {
			const uint32_t cluster_id = worklist[worklist.size() - 1];
			worklist.remove_at(worklist.size() - 1);
			if (visited.has(cluster_id)) {
				continue;
			}
			visited.insert(cluster_id);

			if (dag->get_cluster_error(cluster_id) <= threshold) {
				descended.insert(cluster_id);
			} else {
				REQUIRE_MESSAGE(dag->clusters[cluster_id].source_group != NaniteDAG::NO_GROUP,
						"A level 0 cluster has non-zero error, so the descent cannot terminate.");
				for (const uint32_t child : dag->groups[dag->clusters[cluster_id].source_group].children) {
					worklist.push_back(child);
				}
			}
		}

		CHECK_MESSAGE(descended.size() == cut.size(), vformat("Descent and threshold selection disagree at %f.", threshold));
		for (const uint32_t cluster_id : cut) {
			CHECK(descended.has(cluster_id));
		}

		uint32_t triangles = 0;
		for (const uint32_t cluster_id : cut) {
			triangles += dag->clusters[cluster_id].triangle_count;
		}
		CHECK_MESSAGE(triangles <= previous_triangles, "A coarser threshold selected more triangles than a finer one.");
		previous_triangles = triangles;
	}

	// At threshold zero nothing may be simplified away -- unless some group
	// happened to simplify at exactly zero error, in which case its output
	// legitimately supersedes its input even at the finest threshold.
	const LocalVector<uint32_t> exact_cut = dag->select_cut(0.0f);
	uint32_t exact_triangles = 0;
	for (const uint32_t cluster_id : exact_cut) {
		exact_triangles += dag->clusters[cluster_id].triangle_count;
	}
	bool any_lossless_group = false;
	for (const NaniteDAG::Group &group : dag->groups) {
		any_lossless_group = any_lossless_group || group.error <= 0.0f;
	}
	if (any_lossless_group) {
		CHECK(exact_triangles <= mesh.indices.size() / 3);
	} else {
		CHECK_MESSAGE(exact_triangles == mesh.indices.size() / 3, "The zero-error cut must reproduce the input triangle count.");
	}
}

TEST_CASE("[Nanite] DAG debug output covers every level") {
	const TestMesh mesh = make_displaced_grid(32);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	REQUIRE(dag.is_valid());

	// The per-level table is the artifact DAG quality is judged from, so it
	// has to list every level rather than silently stopping short.
	const String report = dag->get_report();
	CHECK(report.contains("clusters"));
	for (uint32_t level = 0; level < dag->get_level_count(); level++) {
		CHECK_MESSAGE(report.contains(vformat("%5d |", level)), vformat("Report is missing level %d.", level));
	}

	for (uint32_t level = 0; level < dag->get_level_count(); level++) {
		const Ref<ArrayMesh> debug_mesh = dag->create_level_debug_mesh(level);
		REQUIRE_MESSAGE(debug_mesh.is_valid(), vformat("Level %d produced no debug mesh.", level));
		REQUIRE(debug_mesh->get_surface_count() == 1);

		// Triangles are expanded so each one can carry its cluster's color.
		const Array arrays = debug_mesh->surface_get_arrays(0);
		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
		CHECK(vertices.size() == (int)dag->get_level_triangle_count(level) * 3);
		CHECK(colors.size() == vertices.size());
	}

	const Ref<ArrayMesh> cut_mesh = dag->create_cut_debug_mesh(dag->get_level_max_error(dag->get_level_count() - 1) * 0.5f);
	REQUIRE(cut_mesh.is_valid());
	CHECK(cut_mesh->get_surface_count() == 1);
}

TEST_CASE("[Nanite] LOD spheres are real volumes that grow towards the root") {
	const TestMesh mesh = make_displaced_grid(48);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	REQUIRE(dag.is_valid());

	// Regression: level 0 was left with a default-constructed LOD sphere, so
	// every sphere in the hierarchy collapsed to a zero-radius point at the
	// origin. Containment held trivially and the checks all passed, but the
	// runtime had nothing to project an error from.
	for (uint32_t i = dag->level_offsets[0]; i < dag->level_offsets[1]; i++) {
		CHECK_MESSAGE(dag->get_cluster_lod_bounds(i).radius > 0.0f, "A level 0 cluster has a degenerate LOD sphere.");
		CHECK(dag->get_cluster_lod_bounds(i).radius == dag->clusters[i].bounds.radius);
	}

	for (const NaniteDAG::Group &group : dag->groups) {
		CHECK_MESSAGE(group.lod_bounds.radius > 0.0f, "A group has a degenerate LOD sphere.");
	}

	// A cluster's own geometry must sit inside the sphere its error is
	// projected from, or the projection understates the error on screen.
	//
	// Note this is a statement about the vertices, not about nesting the
	// cluster's bounding sphere inside its LOD sphere: neither sphere is
	// minimal, so a loose one can poke outside its parent while every point it
	// covers is still comfortably within.
	uint32_t outside = 0;
	for (uint32_t i = 0; i < dag->get_cluster_count(); i++) {
		const NaniteDAG::Sphere lod = dag->get_cluster_lod_bounds(i);
		for (uint32_t k = 0; k < dag->clusters[i].triangle_count * 3; k++) {
			const uint32_t v = dag->get_cluster_vertex(i, k);
			const Vector3 position = Vector3(dag->positions[v * 3 + 0], dag->positions[v * 3 + 1], dag->positions[v * 3 + 2]);
			if ((float)lod.center.distance_to(position) > lod.radius + 1e-3f) {
				outside++;
			}
		}
	}
	CHECK_MESSAGE(outside == 0, vformat("%d vertices fall outside the LOD sphere their error is projected from.", outside));
}

TEST_CASE("[Nanite] Degenerate triangles cannot inject a non-geometric error") {
	// Zero-area triangles are common in real assets (a UV sphere built as a
	// full quad grid has a fan of them at each pole). meshoptimizer stops short
	// on them and the error it reports is then not a distance: measured, a step
	// that removed one triangle reported half the mesh radius. Accumulated, it
	// corrupts the LOD error of every level above.
	TestMesh mesh = make_displaced_grid(48);
	const uint32_t side = 49;
	for (uint32_t i = 0; i < side - 1; i++) {
		mesh.indices.push_back(i);
		mesh.indices.push_back(i);
		mesh.indices.push_back(i + 1);
	}

	NaniteDAGBuilder::Settings settings;
	ERR_PRINT_OFF;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	ERR_PRINT_ON;
	REQUIRE_MESSAGE(dag.is_valid(), "Degenerate input must still produce a DAG, not a failure.");
	CHECK(dag->validate().is_empty());

	// The bound is physical: a simplification cannot displace the surface
	// further than the extent of the geometry it was handed.
	for (const NaniteDAG::Group &group : dag->groups) {
		CHECK_MESSAGE(group.error <= 2.0f * group.lod_bounds.radius,
				vformat("Group error %f exceeds its own LOD diameter %f.", group.error, 2.0f * group.lod_bounds.radius));
	}
}

TEST_CASE("[Nanite] Spatial clustering stays inside its meshlet buffers") {
	// meshopt_buildMeshletsSpatial emits clusters as small as min_triangles, so
	// its worst-case count must be bounded with min_triangles rather than max.
	// Sizing the buffers from max_triangles under-allocates by that ratio and
	// lets the builder write past them. Nothing exercised this path before,
	// because spatial clustering is off by default.
	const TestMesh mesh = make_displaced_grid(64);
	NaniteDAGBuilder::Settings settings;
	settings.spatial_clustering = true;

	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	REQUIRE(dag.is_valid());
	CHECK(dag->validate().is_empty());
	CHECK(dag->get_level_count() >= 2);

	for (const NaniteDAG::Cluster &cluster : dag->clusters) {
		CHECK(cluster.triangle_count <= settings.max_cluster_triangles);
		CHECK(cluster.triangle_count > 0);
		CHECK(cluster.vertex_count <= NaniteDAG::MAX_CLUSTER_VERTICES);
	}
}

TEST_CASE("[Nanite] DAG survives a save and reload") {
	const TestMesh mesh = make_displaced_grid(32);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> original = build_test_dag(mesh, settings);
	REQUIRE(original.is_valid());

	// Round-trip through the storage property the same way saving a scene does.
	const Variant stored = original->get("data");
	REQUIRE(stored.get_type() == Variant::PACKED_BYTE_ARRAY);
	REQUIRE(!((PackedByteArray)stored).is_empty());

	Ref<NaniteDAG> reloaded;
	reloaded.instantiate();
	reloaded->set("data", stored);

	// A DAG that reloads empty is worse than one that fails to load: every
	// consumer would read zero clusters and render nothing, silently.
	REQUIRE(reloaded->get_cluster_count() == original->get_cluster_count());
	REQUIRE(reloaded->get_group_count() == original->get_group_count());
	REQUIRE(reloaded->get_level_count() == original->get_level_count());
	CHECK(reloaded->validate().is_empty());

	CHECK(reloaded->geometry_hash == original->geometry_hash);
	CHECK(reloaded->settings_hash == original->settings_hash);
	// The stored error is a measured maximum, not a proven bound, so the
	// density it was measured at has to survive with it or the number cannot
	// be interpreted on the other side.
	CHECK(original->deviation_samples_per_triangle > 0);
	CHECK(reloaded->deviation_samples_per_triangle == original->deviation_samples_per_triangle);
	for (uint32_t i = 0; i < original->get_group_count(); i++) {
		CHECK(reloaded->groups[i].analytic_error == original->groups[i].analytic_error);
		CHECK(reloaded->groups[i].analytic_error >= reloaded->groups[i].error);
	}
	CHECK(reloaded->normals.size() == original->normals.size());
	CHECK(reloaded->uvs.size() == original->uvs.size());
	for (uint32_t i = 0; i < original->get_cluster_count(); i++) {
		CHECK(original->clusters[i].vertex_offset == reloaded->clusters[i].vertex_offset);
		CHECK(original->clusters[i].triangle_count == reloaded->clusters[i].triangle_count);
		CHECK(original->get_cluster_error(i) == reloaded->get_cluster_error(i));
		// Roots carry an infinite parent error; if that does not survive the
		// round-trip they stop being drawn at coarse thresholds.
		CHECK(Math::is_inf(original->get_cluster_parent_error(i)) == Math::is_inf(reloaded->get_cluster_parent_error(i)));
	}
	CHECK(reloaded->select_cut(0.0f).size() == original->select_cut(0.0f).size());
}

TEST_CASE("[Nanite] Skinned surfaces are refused") {
	// The DAG is built from rest-pose positions. A skinned surface moves at
	// runtime, so its cluster bounds and LOD errors would describe a pose the
	// mesh is never in. S1 is static-only and must say so rather than produce a
	// confidently wrong DAG.
	const TestMesh mesh = make_displaced_grid(8);

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	PackedVector3Array vertices;
	PackedInt32Array indices;
	for (uint32_t i = 0; i < mesh.vertex_count; i++) {
		vertices.push_back(Vector3(mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]));
	}
	for (uint32_t index : mesh.indices) {
		indices.push_back((int32_t)index);
	}
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_INDEX] = indices;

	NaniteDAGBuilder::Settings settings;
	ERR_PRINT_OFF;
	CHECK_MESSAGE(NaniteDAGBuilder::build_from_surface(arrays, settings).is_valid(), "A static surface must still build.");

	PackedInt32Array bones;
	PackedFloat32Array weights;
	for (uint32_t i = 0; i < mesh.vertex_count * 4; i++) {
		bones.push_back(0);
		weights.push_back(i % 4 == 0 ? 1.0f : 0.0f);
	}
	arrays[Mesh::ARRAY_BONES] = bones;
	arrays[Mesh::ARRAY_WEIGHTS] = weights;
	CHECK_MESSAGE(NaniteDAGBuilder::build_from_surface(arrays, settings).is_null(), "A skinned surface must be refused.");
	ERR_PRINT_ON;
}

TEST_CASE("[Nanite] Every cut of a closed mesh is watertight") {
	// This is the property the whole S1 design exists to guarantee, stated
	// directly rather than through its ingredients: pick any threshold, take
	// the clusters it selects, and the surface they form must have no holes.
	//
	// A crack shows up as an edge used by one triangle where a closed surface
	// requires two -- exactly what happens if two sides of a seam end up at
	// different LODs, or if a cluster left behind by a failed group keeps a
	// seam that the other side has since simplified away.
	const TestMesh mesh = make_closed_sphere(96, 48);
	NaniteDAGBuilder::Settings settings;
	settings.group_size = 2; // Small groups strand more clusters, which is the interesting case.

	ERR_PRINT_OFF;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	ERR_PRINT_ON;
	REQUIRE(dag.is_valid());
	CHECK(dag->validate().is_empty());

	const LocalVector<uint32_t> weld = weld_by_position(**dag);
	// Every cut the DAG can produce, not a sample of them: a hole that only
	// opens between two of the thresholds picked by hand would go unseen.
	const LocalVector<float> thresholds = every_distinct_threshold(**dag);
	CHECK(thresholds.size() > 10);

	for (const float threshold : thresholds) {
		const LocalVector<uint32_t> cut = dag->select_cut(threshold);
		REQUIRE(!cut.is_empty());

		HashMap<uint64_t, uint32_t> edge_use;
		for (const uint32_t cluster_id : cut) {
			const NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
			for (uint32_t t = 0; t < cluster.triangle_count * 3; t += 3) {
				for (uint32_t e = 0; e < 3; e++) {
					const uint32_t a = weld[dag->get_cluster_vertex(cluster_id, t + e)];
					const uint32_t b = weld[dag->get_cluster_vertex(cluster_id, t + (e + 1) % 3)];
					if (a == b) {
						continue; // Degenerate edge, carries no surface.
					}
					const uint64_t key = ((uint64_t)MIN(a, b) << 32) | (uint64_t)MAX(a, b);
					edge_use[key] = edge_use.has(key) ? edge_use[key] + 1 : 1;
				}
			}
		}

		uint32_t open_edges = 0;
		for (const KeyValue<uint64_t, uint32_t> &edge : edge_use) {
			if (edge.value != 2) {
				open_edges++;
			}
		}
		CHECK_MESSAGE(open_edges == 0,
				vformat("Cut at threshold %f has %d edges not shared by exactly two triangles, out of %d edges across %d clusters.",
						threshold, open_edges, edge_use.size(), cut.size()));
	}
}

TEST_CASE("[Nanite] DAG survives a real save and load") {
	// The in-memory property round-trip is not the same thing as going through
	// ResourceSaver and ResourceLoader, which is how a DAG actually reaches a
	// later session.
	const TestMesh mesh = make_displaced_grid(32);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> original = build_test_dag(mesh, settings);
	REQUIRE(original.is_valid());

	const String path = TestUtils::get_temp_path("nanite_dag_roundtrip.res");
	REQUIRE(ResourceSaver::save(original, path) == OK);

	const Ref<NaniteDAG> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());
	CHECK(loaded->validate().is_empty());
	CHECK(loaded->get_cluster_count() == original->get_cluster_count());
	CHECK(loaded->get_group_count() == original->get_group_count());
	CHECK(loaded->get_level_count() == original->get_level_count());
	CHECK(loaded->get_triangle_count() == original->get_triangle_count());
	CHECK(loaded->select_cut(0.0f).size() == original->select_cut(0.0f).size());

	for (uint32_t i = 0; i < original->get_cluster_count(); i++) {
		CHECK(loaded->get_cluster_error(i) == original->get_cluster_error(i));
		CHECK(Math::is_inf(loaded->get_cluster_parent_error(i)) == Math::is_inf(original->get_cluster_parent_error(i)));
	}
}

TEST_CASE("[Nanite] A payload that cannot be read is not mistaken for an empty DAG") {
	const TestMesh mesh = make_displaced_grid(16);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> original = build_test_dag(mesh, settings);
	REQUIRE(original.is_valid());
	const PackedByteArray good = original->get("data");
	REQUIRE(good.size() > 64);

	ERR_PRINT_OFF;

	SUBCASE("a newer format version") {
		// ResourceLoader discards a false return from _set(), so refusing the
		// payload cannot stop the load. The resource has to carry the failure
		// itself or it looks like a mesh that simply has no clusters.
		PackedByteArray future = good.duplicate();
		poke_u32(future, 0, NaniteDAG::FORMAT_VERSION + 1);

		Ref<NaniteDAG> dag;
		dag.instantiate();
		dag->set("data", future);
		CHECK(dag->get_cluster_count() == 0);
		CHECK_MESSAGE(!dag->validate().is_empty(), "A version mismatch must be reported, not silently yield an empty DAG.");
	}

	SUBCASE("a truncated payload") {
		PackedByteArray truncated = good.duplicate();
		truncated.resize(good.size() / 3);

		Ref<NaniteDAG> dag;
		dag.instantiate();
		dag->set("data", truncated);
		CHECK(!dag->validate().is_empty());
	}

	SUBCASE("a count larger than the payload") {
		// Sizing an allocation from a count without checking it against the
		// bytes remaining is how a small file asks for gigabytes.
		//
		// The offset is asserted before it is used. A field was added to the
		// header once already and moved this by four bytes, which left the test
		// corrupting the vertex count instead -- still failing, but no longer
		// exercising the allocation guard it was written for. A green test that
		// has quietly stopped watching its target is worse than no test.
		PackedByteArray hostile = good.duplicate();
		const int position_count_offset = 40;
		REQUIRE_MESSAGE(peek_u32(hostile, position_count_offset) == (uint32_t)original->positions.size(),
				"Header layout moved: this offset no longer holds the position count.");
		poke_u32(hostile, position_count_offset, 0xFFFFFF00);

		Ref<NaniteDAG> dag;
		dag.instantiate();
		dag->set("data", hostile);
		CHECK(dag->get_cluster_count() == 0);
		CHECK(!dag->validate().is_empty());
	}

	ERR_PRINT_ON;
}

TEST_CASE("[Nanite] Cuts stay watertight across a surface boundary") {
	// Surfaces simplify independently, so the seam between two of them is not
	// a group boundary and nothing inside one surface knows the other exists.
	// Locking each surface's open border is what holds them together.
	const TestMesh sphere = make_closed_sphere(64, 32);

	// Split the sphere into two surfaces at the equator, sharing that ring.
	const uint32_t stride = 65;
	TestMesh top = sphere;
	TestMesh bottom = sphere;
	top.indices.clear();
	bottom.indices.clear();
	for (uint32_t t = 0; t < sphere.indices.size(); t += 3) {
		const uint32_t ring = sphere.indices[t] / stride;
		LocalVector<uint32_t> &target = ring < 16 ? top.indices : bottom.indices;
		for (uint32_t e = 0; e < 3; e++) {
			target.push_back(sphere.indices[t + e]);
		}
	}
	REQUIRE(!top.indices.is_empty());
	REQUIRE(!bottom.indices.is_empty());

	NaniteDAGBuilder::Settings settings;
	settings.lock_mesh_border = true;

	ERR_PRINT_OFF;
	const Ref<NaniteDAG> top_dag = build_test_dag(top, settings);
	const Ref<NaniteDAG> bottom_dag = build_test_dag(bottom, settings);
	ERR_PRINT_ON;
	REQUIRE(top_dag.is_valid());
	REQUIRE(bottom_dag.is_valid());

	const LocalVector<uint32_t> weld = weld_by_position(**top_dag);
	// Both surfaces change cut at their own group errors, so the union changes
	// at the merge of the two sets.
	LocalVector<float> thresholds = every_distinct_threshold(**top_dag);
	for (const float threshold : every_distinct_threshold(**bottom_dag)) {
		thresholds.push_back(threshold);
	}
	thresholds.sort();

	for (const float threshold : thresholds) {
		HashMap<uint64_t, uint32_t> edge_use;
		const Ref<NaniteDAG> parts[2] = { top_dag, bottom_dag };
		for (const Ref<NaniteDAG> &part : parts) {
			for (const uint32_t cluster_id : part->select_cut(threshold)) {
				const NaniteDAG::Cluster &cluster = part->clusters[cluster_id];
				for (uint32_t t = 0; t < cluster.triangle_count * 3; t += 3) {
					for (uint32_t e = 0; e < 3; e++) {
						const uint32_t a = weld[part->get_cluster_vertex(cluster_id, t + e)];
						const uint32_t b = weld[part->get_cluster_vertex(cluster_id, t + (e + 1) % 3)];
						if (a == b) {
							continue;
						}
						const uint64_t key = ((uint64_t)MIN(a, b) << 32) | (uint64_t)MAX(a, b);
						edge_use[key] = edge_use.has(key) ? edge_use[key] + 1 : 1;
					}
				}
			}
		}
		// A hole and a doubled sheet are different faults and only the first is
		// a crack. Measured on this fixture, the two halves keep every one of
		// the 64 shared equator edges at every threshold, so there are no
		// holes; but at the coarsest levels each half flattens onto that locked
		// boundary plane, which leaves a few edges carrying two sheets. That is
		// inherent to simplifying surfaces independently -- which is forced,
		// since a surface is a material -- so it is measured, not forbidden.
		uint32_t holes = 0;
		uint32_t doubled = 0;
		for (const KeyValue<uint64_t, uint32_t> &edge : edge_use) {
			if (edge.value == 1) {
				holes++;
			} else if (edge.value > 2) {
				doubled++;
			}
		}
		CHECK_MESSAGE(holes == 0,
				vformat("Two-surface cut at threshold %f has %d edges belonging to a single triangle, so the surfaces have torn apart.", threshold, holes));

		// A closed surface is manifold: every edge belongs to exactly two
		// triangles. Holes and doubled sheets are both departures from that,
		// and both are now required to be absent rather than merely rare.
		CHECK_MESSAGE(doubled == 0,
				vformat("Two-surface cut at threshold %f has %d edges carrying more than two triangles.", threshold, doubled));
	}
}

TEST_CASE("[Nanite] Artifact identity tracks what actually changes the DAG") {
	// The identity tuple is what decides whether a stored artifact is stale.
	// If a setting that reshapes the DAG leaves the hash alone, a rebuild
	// silently keeps serving the old one.
	const TestMesh mesh = make_displaced_grid(24);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> base = build_test_dag(mesh, settings);
	REQUIRE(base.is_valid());
	CHECK(base->geometry_hash != 0);
	CHECK(base->settings_hash != 0);

	SUBCASE("the same input twice agrees") {
		const Ref<NaniteDAG> again = build_test_dag(mesh, settings);
		REQUIRE(again.is_valid());
		CHECK(again->settings_hash == base->settings_hash);
		CHECK(again->geometry_hash == base->geometry_hash);
	}

	SUBCASE("a setting that changes the output changes the settings hash") {
		NaniteDAGBuilder::Settings other = settings;
		other.group_size = settings.group_size + 1;
		const Ref<NaniteDAG> changed = build_test_dag(mesh, other);
		REQUIRE(changed.is_valid());
		CHECK(changed->settings_hash != base->settings_hash);
		CHECK_MESSAGE(changed->geometry_hash == base->geometry_hash, "Geometry did not change, so its hash must not either.");
	}

	SUBCASE("moving a single vertex changes the geometry hash") {
		TestMesh moved = mesh;
		moved.positions[0] += 0.001f;
		const Ref<NaniteDAG> changed = build_test_dag(moved, settings);
		REQUIRE(changed.is_valid());
		CHECK(changed->geometry_hash != base->geometry_hash);
		CHECK_MESSAGE(changed->settings_hash == base->settings_hash, "Settings did not change, so their hash must not either.");
	}

	SUBCASE("changing only a UV still changes the geometry hash") {
		// UVs feed the simplifier's metric and are part of the stored vertex
		// record, so they are part of what the artifact describes.
		TestMesh moved = mesh;
		moved.uvs[0] += 0.25f;
		const Ref<NaniteDAG> changed = build_test_dag(moved, settings);
		REQUIRE(changed.is_valid());
		CHECK(changed->geometry_hash != base->geometry_hash);
	}
}

TEST_CASE("[Nanite] A structurally broken payload is refused") {
	// Corrupting the parsed structure rather than poking byte offsets, so the
	// test says what it means and does not drift when the layout moves.
	const TestMesh mesh = make_displaced_grid(16);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> good = build_test_dag(mesh, settings);
	REQUIRE(good.is_valid());

	ERR_PRINT_OFF;

	auto reload_corrupted = [&](const std::function<void(Ref<NaniteDAG> &)> &p_corrupt) {
		Ref<NaniteDAG> scratch;
		scratch.instantiate();
		scratch->set("data", good->get("data"));
		REQUIRE(scratch->validate().is_empty());
		p_corrupt(scratch);

		Ref<NaniteDAG> loaded;
		loaded.instantiate();
		loaded->set("data", scratch->get("data"));
		return loaded;
	};

	SUBCASE("level offsets that run backwards") {
		const Ref<NaniteDAG> loaded = reload_corrupted([](Ref<NaniteDAG> &d) {
			d->level_offsets[1] = d->level_offsets[0];
			d->level_offsets[0] = 7;
		});
		CHECK(!loaded->validate().is_empty());
		CHECK(loaded->get_cluster_count() == 0);
	}

	SUBCASE("a cluster slice past the end of the slice table") {
		const Ref<NaniteDAG> loaded = reload_corrupted([](Ref<NaniteDAG> &d) {
			d->clusters[0].vertex_offset = d->cluster_vertices.size();
		});
		CHECK(!loaded->validate().is_empty());
	}

	SUBCASE("a local index pointing outside its own slice") {
		const Ref<NaniteDAG> loaded = reload_corrupted([](Ref<NaniteDAG> &d) {
			d->cluster_indices[d->clusters[0].triangle_offset] = 254;
			d->clusters[0].vertex_count = 3;
		});
		CHECK(!loaded->validate().is_empty());
	}

	SUBCASE("an analytic edge that inverts") {
		const Ref<NaniteDAG> loaded = reload_corrupted([](Ref<NaniteDAG> &d) {
			for (uint32_t i = 0; i < d->groups.size(); i++) {
				if (d->groups[i].level > 0) {
					d->groups[i].analytic_error = 0.0f;
					break;
				}
			}
		});
		CHECK(!loaded->validate().is_empty());
	}

	SUBCASE("an error with no sampling density to interpret it by") {
		const Ref<NaniteDAG> loaded = reload_corrupted([](Ref<NaniteDAG> &d) {
			d->deviation_samples_per_triangle = 0;
		});
		CHECK(!loaded->validate().is_empty());
	}

	SUBCASE("a vertex record that does not match the vertex count") {
		const Ref<NaniteDAG> loaded = reload_corrupted([](Ref<NaniteDAG> &d) {
			d->normals.resize(d->normals.size() - 1);
		});
		CHECK(!loaded->validate().is_empty());
	}

	ERR_PRINT_ON;
}

#ifdef TOOLS_ENABLED
TEST_CASE("[Nanite] The importer stops serving a DAG once the option is off") {
	// The cleanup used to sit behind the enabled check, so turning the option
	// off left the previous DAG attached and every consumer kept reading it.
	const TestMesh mesh = make_displaced_grid(16);

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedInt32Array indices;
	for (uint32_t i = 0; i < mesh.vertex_count; i++) {
		vertices.push_back(Vector3(mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]));
		normals.push_back(Vector3(mesh.normals[i * 3], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]));
	}
	for (uint32_t index : mesh.indices) {
		indices.push_back((int32_t)index);
	}
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ImporterMesh> importer_mesh;
	importer_mesh.instantiate();
	importer_mesh->set_name("TestSurface");
	importer_mesh->add_surface(Mesh::PRIMITIVE_TRIANGLES, arrays);

	Ref<NaniteImportPlugin> plugin;
	plugin.instantiate();
	const String meta_key = String(NaniteImportPlugin::METADATA_PREFIX) + "0";

	Dictionary options;
	options["nanite/enabled"] = true;
	plugin->internal_process(EditorScenePostImportPlugin::INTERNAL_IMPORT_CATEGORY_MESH, nullptr, nullptr, importer_mesh, options);
	REQUIRE_MESSAGE(importer_mesh->has_meta(meta_key), "Enabling the option should attach a DAG.");

	const Ref<NaniteDAG> attached = importer_mesh->get_meta(meta_key);
	REQUIRE(attached.is_valid());
	CHECK(attached->surface_index == 0);
	CHECK(attached->validate().is_empty());

	options["nanite/enabled"] = false;
	plugin->internal_process(EditorScenePostImportPlugin::INTERNAL_IMPORT_CATEGORY_MESH, nullptr, nullptr, importer_mesh, options);
	CHECK_MESSAGE(!importer_mesh->has_meta(meta_key), "Disabling the option must remove the previous DAG, not leave it attached.");
}
#endif // TOOLS_ENABLED

TEST_CASE("[Nanite] Artifact size does not drift") {
	// Drift detection, not a contract check. The contract is that a pool
	// buffer's serialized byte count fits the device's storage buffer limit,
	// and bytes are exactly knowable when writing them -- there is nothing to
	// estimate. Per-triangle figures cannot stand in for that: bytes per
	// triangle is driven by the vertex-to-triangle ratio, which is a property
	// of the mesh, and admitting unreferenced vertices removes any finite bound
	// on it altogether. So each fixture gets a byte ceiling of its own, sized
	// just above what it measures, purely to notice when something doubles.
	struct Fixture {
		const char *name;
		int kind; // 0 grid, 1 sphere, 2 seamed grid.
		uint64_t ceiling_bytes;
	};
	const Fixture fixtures[] = {
		{ "seam-free grid", 0, 1'250'000 },
		{ "sphere", 1, 400'000 },
		{ "seam-heavy grid", 2, 2'250'000 },
	};

	for (const Fixture &fixture : fixtures) {
		TestMesh mesh;
		switch (fixture.kind) {
			case 0:
				mesh = make_displaced_grid(160);
				break;
			case 1:
				mesh = make_closed_sphere(128, 64);
				break;
			default:
				mesh = make_seamed_grid(160, 2);
				break;
		}

		NaniteDAGBuilder::Settings settings;
		ERR_PRINT_OFF;
		const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
		ERR_PRINT_ON;
		REQUIRE(dag.is_valid());

		const PackedByteArray serialized = dag->get("data");
		CHECK_MESSAGE((uint64_t)serialized.size() <= fixture.ceiling_bytes,
				vformat("%s serialized to %d bytes, past its %d ceiling.",
						fixture.name, serialized.size(), (int)fixture.ceiling_bytes));

		// The component accounting has to add up to the file, or a byte
		// ceiling is guarding something other than what gets written.
		const uint64_t vertex_bytes = (uint64_t)dag->vertex_count * (12 + 4 + 4);
		const uint64_t slice_bytes = (uint64_t)dag->cluster_vertices.size() * 4;
		const uint64_t local_bytes = (uint64_t)dag->cluster_indices.size();
		const uint64_t cluster_bytes = (uint64_t)dag->clusters.size() * 18 * 4;
		uint64_t group_bytes = 0;
		for (const NaniteDAG::Group &group : dag->groups) {
			group_bytes += 9 * 4 + (uint64_t)(group.children.size() + group.produced.size()) * 4;
		}
		const uint64_t components = vertex_bytes + slice_bytes + local_bytes + cluster_bytes + group_bytes;
		CHECK((uint64_t)serialized.size() >= components);
		CHECK((uint64_t)serialized.size() - components < 1024);
	}
}

TEST_CASE("[Nanite] An artifact built by a different algorithm is refused") {
	// The format version covers the layout; this one covers the algorithm. A
	// DAG whose errors were measured a different way is readable and wrong,
	// which is worse than unreadable: nothing about it looks broken.
	const TestMesh mesh = make_displaced_grid(16);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	REQUIRE(dag.is_valid());
	const PackedByteArray good = dag->get("data");

	ERR_PRINT_OFF;
	PackedByteArray stale = good.duplicate();
	poke_u32(stale, 4, NaniteDAG::BUILDER_VERSION - 1); // Builder version sits after the format version.

	Ref<NaniteDAG> loaded;
	loaded.instantiate();
	loaded->set("data", stale);
	ERR_PRINT_ON;

	CHECK_MESSAGE(loaded->get_cluster_count() == 0, "An artifact from an older builder must not load.");
	CHECK(!loaded->validate().is_empty());
}

TEST_CASE("[Nanite] Deviation is measured from both surfaces, not one") {
	// Reconstructs each group's input and output from the stored graph and
	// independently measures the direction the previous implementation missed:
	// how far the interior of a simplified triangle sits from the original
	// surface. The original was represented by its removed vertices alone, so
	// a bump the simplified surface cut straight through went unseen -- and no
	// test noticed, because the value it produced was still plausible.
	//
	// Verified to discriminate: removing the reverse-direction sampling from
	// the builder fails this on four of five groups. Measured on this fixture
	// the reverse direction is the larger of the two in four groups out of
	// five, so a one-sided implementation understates the error rather than
	// merely computing it differently.
	const TestMesh mesh = make_displaced_grid(32);
	NaniteDAGBuilder::Settings settings;
	const Ref<NaniteDAG> dag = build_test_dag(mesh, settings);
	REQUIRE(dag.is_valid());
	REQUIRE(dag->get_group_count() > 0);
	CHECK(dag->deviation_samples_per_triangle == 4);

	auto gather = [&](const LocalVector<uint32_t> &p_clusters) {
		LocalVector<Vector3> corners;
		for (const uint32_t cluster_id : p_clusters) {
			const NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
			for (uint32_t k = 0; k < cluster.triangle_count * 3; k++) {
				const uint32_t v = dag->get_cluster_vertex(cluster_id, k);
				corners.push_back(Vector3(dag->positions[v * 3], dag->positions[v * 3 + 1], dag->positions[v * 3 + 2]));
			}
		}
		return corners;
	};

	uint32_t checked = 0;
	for (uint32_t g = 0; g < dag->get_group_count() && checked < 6; g++) {
		const NaniteDAG::Group &group = dag->groups[g];
		const LocalVector<Vector3> before = gather(group.children);
		const LocalVector<Vector3> after = gather(group.produced);
		if (before.is_empty() || after.is_empty()) {
			continue;
		}
		checked++;

		float max_child = 0.0f;
		for (const uint32_t child : group.children) {
			max_child = MAX(max_child, dag->get_cluster_error(child));
		}
		const float step = group.error - max_child;

		// The missed direction: output triangle interiors against the input
		// surface, sampled exactly as the graph says it was.
		float reverse = 0.0f;
		for (uint32_t t = 0; t + 2 < after.size(); t += 3) {
			const Vector3 samples[4] = {
				(after[t] + after[t + 1] + after[t + 2]) / 3.0f,
				(after[t] + after[t + 1]) * 0.5f,
				(after[t + 1] + after[t + 2]) * 0.5f,
				(after[t + 2] + after[t]) * 0.5f
			};
			for (const Vector3 &sample : samples) {
				float nearest = FLT_MAX;
				for (uint32_t u = 0; u + 2 < before.size(); u += 3) {
					const Face3 face(before[u], before[u + 1], before[u + 2]);
					nearest = MIN(nearest, (float)sample.distance_to(face.get_closest_point_to(sample)));
				}
				reverse = MAX(reverse, nearest);
			}
		}

		CHECK_MESSAGE(step >= reverse - 1e-4f,
				vformat("Group %d stored a step of %f while its simplified surface strays %f from the original, so that direction was not measured.",
						g, step, reverse));
	}
	REQUIRE_MESSAGE(checked > 0, "No group had both an input and an output to compare.");
}

TEST_CASE("[Nanite] DAG builder is deterministic") {
	const TestMesh mesh = make_displaced_grid(32);
	NaniteDAGBuilder::Settings settings;

	const Ref<NaniteDAG> first = build_test_dag(mesh, settings);
	const Ref<NaniteDAG> second = build_test_dag(mesh, settings);
	REQUIRE(first.is_valid());
	REQUIRE(second.is_valid());

	// Reimporting the same asset must not reshuffle cluster ids, otherwise
	// nothing downstream of the importer can be cached.
	REQUIRE(first->get_cluster_count() == second->get_cluster_count());
	REQUIRE(first->cluster_indices.size() == second->cluster_indices.size());
	REQUIRE(first->cluster_vertices.size() == second->cluster_vertices.size());
	CHECK(first->geometry_hash == second->geometry_hash);
	CHECK(first->settings_hash == second->settings_hash);
	for (uint32_t i = 0; i < first->get_cluster_count(); i++) {
		CHECK(first->clusters[i].vertex_offset == second->clusters[i].vertex_offset);
		CHECK(first->clusters[i].triangle_count == second->clusters[i].triangle_count);
		CHECK(first->get_cluster_error(i) == second->get_cluster_error(i));
		CHECK(first->get_cluster_analytic_error(i) == second->get_cluster_analytic_error(i));
	}
	for (uint32_t i = 0; i < first->cluster_vertices.size(); i++) {
		CHECK(first->cluster_vertices[i] == second->cluster_vertices[i]);
	}
}

TEST_CASE("[Nanite] DAG builder rejects malformed input") {
	const TestMesh mesh = make_displaced_grid(8);
	NaniteDAGBuilder::Settings settings;

	ERR_PRINT_OFF;

	SUBCASE("index count is not a multiple of 3") {
		TestMesh broken = mesh;
		broken.indices.remove_at(0);
		CHECK(build_test_dag(broken, settings).is_null());
	}

	SUBCASE("index out of range") {
		TestMesh broken = mesh;
		broken.indices[0] = broken.vertex_count;
		CHECK(build_test_dag(broken, settings).is_null());
	}

	SUBCASE("normal buffer does not match the vertex count") {
		TestMesh broken = mesh;
		broken.normals.resize(broken.normals.size() - 3);
		CHECK(build_test_dag(broken, settings).is_null());
	}

	SUBCASE("cluster budget out of range") {
		NaniteDAGBuilder::Settings broken = settings;
		broken.max_cluster_triangles = 512;
		CHECK(build_test_dag(mesh, broken).is_null());
	}

	SUBCASE("simplify ratio out of range") {
		NaniteDAGBuilder::Settings broken = settings;
		broken.simplify_ratio = 1.0f;
		CHECK(build_test_dag(mesh, broken).is_null());
	}

	ERR_PRINT_ON;
}

} // namespace TestNaniteDAG
