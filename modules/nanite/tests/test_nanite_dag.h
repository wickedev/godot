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

#include "core/templates/hash_set.h"
#include "tests/test_macros.h"

namespace TestNaniteDAG {

// A displaced grid: enough triangles for several DAG levels, and enough
// curvature that simplification produces a genuinely non-zero QEM error, which
// is what the monotonicity checks need in order to mean anything.
struct TestMesh {
	LocalVector<float> positions;
	LocalVector<float> attributes;
	LocalVector<float> attribute_weights;
	LocalVector<uint32_t> indices;
	uint32_t vertex_count = 0;
	uint32_t attribute_count = 3;
};

inline TestMesh make_displaced_grid(uint32_t p_resolution) {
	TestMesh mesh;
	const uint32_t side = p_resolution + 1;
	mesh.vertex_count = side * side;
	mesh.positions.resize((uint64_t)mesh.vertex_count * 3);
	mesh.attributes.resize((uint64_t)mesh.vertex_count * 3);

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
			mesh.attributes[v * 3 + 0] = (float)normal.x;
			mesh.attributes[v * 3 + 1] = (float)normal.y;
			mesh.attributes[v * 3 + 2] = (float)normal.z;
		}
	}

	for (uint32_t i = 0; i < 3; i++) {
		mesh.attribute_weights.push_back(0.5f);
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

inline Ref<NaniteDAG> build_test_dag(const TestMesh &p_mesh, const NaniteDAGBuilder::Settings &p_settings) {
	return NaniteDAGBuilder::build(p_mesh.positions, p_mesh.vertex_count, p_mesh.attributes,
			p_mesh.attribute_count, p_mesh.attribute_weights, p_mesh.indices, p_settings);
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
		CHECK(cluster.index_count % 3 == 0);
		CHECK(cluster.index_count / 3 <= settings.max_cluster_triangles);
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
	for (const NaniteDAG::Cluster &cluster : dag->clusters) {
		if (cluster.parent_group == NaniteDAG::NO_GROUP) {
			CHECK(Math::is_inf(cluster.parent_error));
			continue;
		}
		const NaniteDAG::Group &group = dag->groups[cluster.parent_group];
		CHECK_MESSAGE(group.error >= cluster.error, "Parent error dropped below its child's error.");
		CHECK_MESSAGE(group.lod_bounds.contains(cluster.lod_bounds, 1e-3f),
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

			const NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
			if (cluster.error <= threshold) {
				descended.insert(cluster_id);
			} else {
				REQUIRE_MESSAGE(cluster.source_group != NaniteDAG::NO_GROUP,
						"A level 0 cluster has non-zero error, so the descent cannot terminate.");
				for (const uint32_t child : dag->groups[cluster.source_group].children) {
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
			triangles += dag->clusters[cluster_id].index_count / 3;
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
		exact_triangles += dag->clusters[cluster_id].index_count / 3;
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

	// The per-level table is the artefact DAG quality is judged from, so it
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

		// Triangles are expanded so each one can carry its cluster's colour.
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
	REQUIRE(first->indices.size() == second->indices.size());
	for (uint32_t i = 0; i < first->get_cluster_count(); i++) {
		CHECK(first->clusters[i].index_offset == second->clusters[i].index_offset);
		CHECK(first->clusters[i].index_count == second->clusters[i].index_count);
		CHECK(first->clusters[i].error == second->clusters[i].error);
	}
	for (uint32_t i = 0; i < first->indices.size(); i++) {
		CHECK(first->indices[i] == second->indices[i]);
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

	SUBCASE("attribute buffer does not match the vertex count") {
		TestMesh broken = mesh;
		broken.attributes.resize(broken.attributes.size() - 3);
		CHECK(build_test_dag(broken, settings).is_null());
	}

	SUBCASE("cluster budget out of range") {
		NaniteDAGBuilder::Settings broken = settings;
		broken.max_cluster_triangles = 1024;
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
