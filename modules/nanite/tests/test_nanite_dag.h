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

#include "core/templates/hash_map.h"
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

// A UV sphere built as a full quad grid, which leaves a fan of zero-area
// triangles at each pole. This is what a naive sphere generator produces and
// what real assets often contain; measured, it is also what drives a group to
// fail to simplify.
inline TestMesh make_pole_sphere(uint32_t p_segments, uint32_t p_rings) {
	TestMesh mesh;
	const uint32_t stride = p_segments + 1;
	mesh.vertex_count = stride * (p_rings + 1);
	mesh.positions.resize((uint64_t)mesh.vertex_count * 3);
	mesh.attributes.resize((uint64_t)mesh.vertex_count * 3);

	for (uint32_t r = 0; r <= p_rings; r++) {
		const float phi = (float)r / (float)p_rings * (float)Math::PI;
		for (uint32_t sgm = 0; sgm <= p_segments; sgm++) {
			const float theta = (float)sgm / (float)p_segments * 2.0f * (float)Math::PI;
			const uint32_t v = r * stride + sgm;
			const Vector3 position = Vector3(Math::sin(phi) * Math::cos(theta), Math::cos(phi), Math::sin(phi) * Math::sin(theta));
			for (uint32_t axis = 0; axis < 3; axis++) {
				mesh.positions[v * 3 + axis] = (float)position[axis];
				mesh.attributes[v * 3 + axis] = (float)position[axis];
			}
		}
	}
	for (uint32_t i = 0; i < 3; i++) {
		mesh.attribute_weights.push_back(0.5f);
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
	mesh.attributes.resize((uint64_t)mesh.vertex_count * 3);

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
				mesh.attributes[v * 3 + axis] = (float)position[axis];
			}
		}
	}
	for (uint32_t i = 0; i < 3; i++) {
		mesh.attribute_weights.push_back(0.5f);
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
		const NaniteDAG::Cluster &cluster = dag->clusters[i];
		CHECK_MESSAGE(cluster.lod_bounds.radius > 0.0f, "A level 0 cluster has a degenerate LOD sphere.");
		CHECK(cluster.lod_bounds.radius == cluster.bounds.radius);
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
	for (const NaniteDAG::Cluster &cluster : dag->clusters) {
		for (uint32_t k = 0; k < cluster.index_count; k++) {
			const uint32_t v = dag->indices[cluster.index_offset + k];
			const Vector3 position = Vector3(dag->positions[v * 3 + 0], dag->positions[v * 3 + 1], dag->positions[v * 3 + 2]);
			if ((float)cluster.lod_bounds.center.distance_to(position) > cluster.lod_bounds.radius + 1e-3f) {
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
		CHECK(cluster.index_count / 3 <= settings.max_cluster_triangles);
		CHECK(cluster.index_count > 0);
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

	for (uint32_t i = 0; i < original->get_cluster_count(); i++) {
		const NaniteDAG::Cluster &a = original->clusters[i];
		const NaniteDAG::Cluster &b = reloaded->clusters[i];
		CHECK(a.index_offset == b.index_offset);
		CHECK(a.index_count == b.index_count);
		CHECK(a.error == b.error);
		CHECK(a.lod_bounds.radius == b.lod_bounds.radius);
		// Roots carry an infinite parent error; if that does not survive the
		// round-trip they stop being drawn at coarse thresholds.
		CHECK(Math::is_inf(a.parent_error) == Math::is_inf(b.parent_error));
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
	const float top_error = dag->get_level_max_error(dag->get_level_count() - 1);
	const float thresholds[] = { 0.0f, top_error * 0.05f, top_error * 0.25f, top_error * 0.5f, top_error, top_error * 4.0f };

	for (const float threshold : thresholds) {
		const LocalVector<uint32_t> cut = dag->select_cut(threshold);
		REQUIRE(!cut.is_empty());

		HashMap<uint64_t, uint32_t> edge_use;
		for (const uint32_t cluster_id : cut) {
			const NaniteDAG::Cluster &cluster = dag->clusters[cluster_id];
			for (uint32_t t = 0; t < cluster.index_count; t += 3) {
				for (uint32_t e = 0; e < 3; e++) {
					const uint32_t a = weld[dag->indices[cluster.index_offset + t + e]];
					const uint32_t b = weld[dag->indices[cluster.index_offset + t + (e + 1) % 3]];
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
