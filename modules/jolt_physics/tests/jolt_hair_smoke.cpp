/**************************************************************************/
/*  jolt_hair_smoke.cpp                                                   */
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

#include "jolt_hair_smoke.h"

#ifdef JPH_USE_CPU_COMPUTE

#include "../spaces/jolt_broad_phase_layer.h"
#include "../spaces/jolt_layers.h"

#include <Jolt/Jolt.h>

#include <Jolt/Compute/CPU/ComputeSystemCPU.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Hair/Hair.h>
#include <Jolt/Physics/Hair/HairSettings.h>
#include <Jolt/Physics/Hair/HairShaders.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Shaders/HairWrapper.h>

namespace JoltHairSmoke {

namespace {

constexpr uint32_t VERTICES_PER_STRAND = 8;
constexpr float STRAND_LENGTH = 0.4f;

// The smallest groom the solver accepts: one scalp triangle rigidly bound to a single joint, and
// one strand hanging off it whose root vertex is pinned (inverse mass 0).
JPH::Ref<JPH::HairSettings> make_minimal_groom(JPH::ComputeSystem *p_compute_system, bool p_collision) {
	JPH::Ref<JPH::HairSettings> settings = new JPH::HairSettings();

	settings->mScalpVertices = {
		JPH::Float3(-0.1f, 0.0f, -0.1f),
		JPH::Float3(0.1f, 0.0f, -0.1f),
		JPH::Float3(0.0f, 0.0f, 0.1f),
	};
	settings->mScalpTriangles = { JPH::IndexedTriangleNoMaterial(0, 1, 2) };
	settings->mScalpInverseBindPose = { JPH::Mat44::sIdentity() };

	settings->mScalpNumSkinWeightsPerVertex = 1;
	for (uint32_t i = 0; i < settings->mScalpVertices.size(); ++i) {
		JPH::HairSettings::SkinWeight weight;
		weight.mJointIdx = 0;
		weight.mWeight = 1.0f;
		settings->mScalpSkinWeights.push_back(weight);
	}

	JPH::Array<JPH::HairSettings::SVertex> vertices;
	for (uint32_t i = 0; i < VERTICES_PER_STRAND; ++i) {
		const float t = float(i) / float(VERTICES_PER_STRAND - 1);
		vertices.push_back(JPH::HairSettings::SVertex(
				JPH::Float3(0.0f, -t * STRAND_LENGTH, 0.0f), i == 0 ? 0.0f : 1.0f));
	}
	JPH::Array<JPH::HairSettings::SStrand> strands;
	strands.push_back(JPH::HairSettings::SStrand(0, VERTICES_PER_STRAND, 0));

	JPH::HairSettings::Material material;
	material.mEnableCollision = p_collision;
	// Defaults to 0.1, which would round a single-strand groom down to zero simulated strands.
	material.mSimulationStrandsFraction = 1.0f;
	settings->mMaterials.push_back(material);

	settings->mSimulationBoundsPadding = JPH::Vec3::sReplicate(0.5f);
	settings->mInitialGravity = JPH::Vec3(0.0f, -9.81f, 0.0f);
	settings->InitRenderAndSimulationStrands(vertices, strands);

	float max_dist_sq = 0.0f;
	settings->Init(max_dist_sq);
	settings->InitCompute(p_compute_system);

	return settings;
}

} // namespace

bool is_available() {
	return true;
}

Result run(bool p_with_collision) {
	Result result;

	JPH::ComputeSystemCPU compute_system;
	JPH::HairRegisterShaders(&compute_system);

	JPH::ComputeQueueResult queue_result = compute_system.CreateComputeQueue();
	if (!queue_result.IsValid()) {
		result.skip_reason = "Could not create a CPU compute queue.";
		return result;
	}
	JPH::Ref<JPH::ComputeQueue> compute_queue = queue_result.Get();

	JPH::HairShaders shaders;
	shaders.Init(&compute_system);
	// HairShaders::Init() swallows failures and leaves null refs behind, so check rather than trust.
	result.shaders_loaded = shaders.mIntegrateCS != nullptr && shaders.mGridAccumulateCS != nullptr;

	// RegisterHair() ran during module init; this is what proves it took effect.
	if (JPH::Factory::sInstance != nullptr) {
		JPH::HairSettings *from_factory =
				reinterpret_cast<JPH::HairSettings *>(JPH::Factory::sInstance->CreateObject("HairSettings"));
		result.factory_creates_hair_settings = from_factory != nullptr;
		if (from_factory != nullptr) {
			// CreateObject() hands back an unowned pointer; HairSettings is ref-counted, so adopt it
			// into a Ref and let that release it.
			JPH::Ref<JPH::HairSettings> adopted = from_factory;
		}
	}

	JoltLayers layers;
	// The hair has to sit in BODY_DYNAMIC. Its broadphase query runs through JoltLayers'
	// object-vs-broadphase matrix, and a BODY_STATIC querier is filtered out against static
	// geometry -- the collider would never be gathered and collision would silently do nothing.
	const JPH::ObjectLayer hair_layer =
			layers.to_object_layer(JoltBroadPhaseLayer::BODY_DYNAMIC, 1, 1);
	const JPH::ObjectLayer static_layer =
			layers.to_object_layer(JoltBroadPhaseLayer::BODY_STATIC, 1, 1);

	JPH::PhysicsSystem physics_system;
	physics_system.Init(2, 0, 2, 2, layers, layers, layers);
	physics_system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

	if (p_with_collision) {
		// The obstacle has to be a ConvexHullShape specifically -- the solver's collision path skips
		// every other subtype (`GetSubType() == EShapeSubType::ConvexHull` in Hair::Update), which
		// upstream lists as a known gap in Hair.h. A BoxShape is gathered by the broadphase and then
		// silently ignored.
		//
		// It also has to sit off to one side. A slab directly under a strand that hangs straight
		// down is degenerate: the contact normal is parallel to the strand, so pushing the vertices
		// out just fights the edge-length constraints and the strand stays where it was. Overlapping
		// the lower half from +X gives the contact something to deflect along.
		JPH::ConvexHullShapeSettings obstacle_settings;
		obstacle_settings.SetEmbedded();
		for (int i = 0; i < 8; ++i) {
			obstacle_settings.mPoints.push_back(JPH::Vec3(
					(i & 1) ? 0.2f : -0.2f,
					(i & 2) ? 0.1f : -0.1f,
					(i & 4) ? 0.2f : -0.2f));
		}
		JPH::ShapeSettings::ShapeResult obstacle_shape = obstacle_settings.Create();
		if (!obstacle_shape.IsValid()) {
			result.skip_reason = "Could not build the collision hull.";
			return result;
		}
		JPH::BodyCreationSettings obstacle(obstacle_shape.Get(),
				JPH::RVec3(0.18f, -0.6f * STRAND_LENGTH, 0.0f), JPH::Quat::sIdentity(),
				JPH::EMotionType::Static, static_layer);
		const JPH::BodyID id = physics_system.GetBodyInterface().CreateAndAddBody(
				obstacle, JPH::EActivation::DontActivate);
		if (id.IsInvalid()) {
			result.skip_reason = "Could not add the collision body.";
			return result;
		}
		result.collider_added = true;
		physics_system.OptimizeBroadPhase();
	}

	JPH::Ref<JPH::HairSettings> settings = make_minimal_groom(&compute_system, p_with_collision);
	if (settings->mSimStrands.size() != 1) {
		result.skip_reason = "Groom did not produce exactly one simulated strand.";
		return result;
	}
	const JPH::HairSettings::SStrand &strand = settings->mSimStrands[0];
	result.vertex_count = int(settings->mSimVertices.size());

	JPH::Hair hair(settings, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), hair_layer);
	hair.Init(&compute_system);

	const JPH::Mat44 joint_matrix = JPH::Mat44::sIdentity();
	auto step = [&](float p_delta) {
		hair.Update(p_delta, JPH::Mat44::sIdentity(), &joint_matrix, physics_system, shaders,
				&compute_system, compute_queue);
	};
	auto to_godot = [](const JPH::Float3 &p_v) { return Vector3(p_v.x, p_v.y, p_v.z); };

	// ReadBackGPUState() de-transposes the position buffer into a Float3 array indexed like
	// mSimVertices, so positions need no LockReadBackBuffers().
	step(0.0f);
	hair.ReadBackGPUState(compute_queue);
	result.root_before = to_godot(hair.GetPositions()[strand.mStartVtx]);
	result.tip_before = to_godot(hair.GetPositions()[strand.mEndVtx - 1]);

	for (int i = 0; i < 30; ++i) {
		step(1.0f / 60.0f);
	}
	hair.ReadBackGPUState(compute_queue);

	const JPH::Float3 *positions = hair.GetPositions();
	for (int i = 0; i < result.vertex_count; ++i) {
		if (!to_godot(positions[i]).is_finite()) {
			result.any_vertex_nonfinite = true;
		}
	}
	result.root_after = to_godot(positions[strand.mStartVtx]);
	result.tip_after = to_godot(positions[strand.mEndVtx - 1]);

	result.skipped = false;
	return result;
}

} // namespace JoltHairSmoke

#else // !JPH_USE_CPU_COMPUTE

namespace JoltHairSmoke {

bool is_available() {
	return false;
}

Result run(bool p_with_collision) {
	Result result;
	result.skip_reason = "Built without a Jolt hair compute backend (jolt_hair_compute=none).";
	return result;
}

} // namespace JoltHairSmoke

#endif // JPH_USE_CPU_COMPUTE
