/**************************************************************************/
/*  test_jolt_hair.h                                                      */
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

#include "jolt_hair_smoke.h"

#include "tests/test_macros.h"

namespace TestJoltHair {

// The only coverage the vendored Jolt/Compute + Jolt/Shaders + Jolt/Physics/Hair folders have.
// Upstream Godot ships none of the three, so nothing else proves they build and run against our
// bundle. The CPU backend runs the same HLSL kernels the GPU backends do, compiled as C++ through
// Jolt/Compute/CPU/HLSLToCPP.h, so a green run also exercises the shader sources.
//
// Deliberately routed through JoltHairSmoke rather than touching Jolt here: <Jolt/...> only
// resolves inside the module env, and widening the global include path to reach it would put our
// bundled 5.6.0 headers in front of every other translation unit.
static void check_strand_fell(const JoltHairSmoke::Result &p_result) {
	CHECK_FALSE_MESSAGE(p_result.any_vertex_nonfinite, "A hair vertex went NaN or infinite during simulation.");
	CHECK_MESSAGE(p_result.root_before.distance_to(p_result.root_after) < 1.0e-4,
			"Pinned strand root should not drift.");
	CHECK_MESSAGE(p_result.tip_before.distance_to(p_result.tip_after) > 1.0e-4,
			"Free strand tip should have moved under gravity.");
}

TEST_CASE("[Modules][Jolt] Hair solver steps on the CPU compute backend") {
	if (!JoltHairSmoke::is_available()) {
		// Default build. Enable with `scons jolt_hair_compute=cpu`.
		return;
	}

	const JoltHairSmoke::Result result = JoltHairSmoke::run(false);
	REQUIRE_MESSAGE(!result.skipped, result.skip_reason);

	CHECK_MESSAGE(result.shaders_loaded, "Hair compute shaders failed to load.");
	CHECK_MESSAGE(result.factory_creates_hair_settings,
			"HairSettings is not registered with the Jolt factory; RegisterHair() did not run.");
	REQUIRE(result.vertex_count > 1);

	check_strand_fell(result);
}

TEST_CASE("[Modules][Jolt] Hair solver collides against the scene") {
	if (!JoltHairSmoke::is_available()) {
		return;
	}

	// Same groom, plus a convex hull overlapping the lower half of the strand. This is what walks
	// Hair::InitializeContext()'s broadphase gather and the collision-plane shader; neither runs
	// otherwise.
	const JoltHairSmoke::Result with = JoltHairSmoke::run(true);
	REQUIRE_MESSAGE(!with.skipped, with.skip_reason);
	REQUIRE(with.collider_added);

	check_strand_fell(with);

	// Asserting divergence from the collision-free run rather than a particular resting pose: the
	// solver resolves contacts with compliance, so where the strand ends up is a soft equilibrium
	// not worth pinning down, but it must not land in the same place as when nothing was there.
	const JoltHairSmoke::Result without = JoltHairSmoke::run(false);
	REQUIRE_MESSAGE(!without.skipped, without.skip_reason);

	CHECK_MESSAGE(with.tip_after.distance_to(without.tip_after) > 1.0e-3,
			"Collision did not deflect the strand. With: ", with.tip_after,
			" without: ", without.tip_after);
}

TEST_CASE("[Modules][Jolt] Wind deflects hair") {
	if (!JoltHairSmoke::is_available()) {
		return;
	}

	// The property check first, because the failure this guards against is silent. Jolt has no wind
	// input of its own -- upstream lists it as missing -- so wind reaches the solver only through
	// the external-acceleration patch. If that stops being wired, everything still builds, still
	// runs, and the hair simply ignores the weather.
	const JoltHairSmoke::Result still = JoltHairSmoke::run(false, Vector3());
	REQUIRE_MESSAGE(!still.skipped, still.skip_reason);

	const JoltHairSmoke::Result windy = JoltHairSmoke::run(false, Vector3(20.0, 0.0, 0.0));
	REQUIRE_MESSAGE(!windy.skipped, windy.skip_reason);

	check_strand_fell(windy);

	CHECK_MESSAGE(still.tip_after.distance_to(windy.tip_after) > 1.0e-3,
			"Wind did not move the hair. Still: ", still.tip_after, " windy: ", windy.tip_after);

	// Direction matters, not just magnitude: a sign error would deflect the strand upwind and
	// nothing would complain.
	CHECK_MESSAGE(windy.tip_after.x > still.tip_after.x,
			"Hair should blow downwind (+X). Still x: ", still.tip_after.x,
			" windy x: ", windy.tip_after.x);

	// Reversing the wind has to mirror the deflection, which a magnitude-only bug would not.
	const JoltHairSmoke::Result upwind = JoltHairSmoke::run(false, Vector3(-20.0, 0.0, 0.0));
	REQUIRE_MESSAGE(!upwind.skipped, upwind.skip_reason);
	CHECK_MESSAGE(upwind.tip_after.x < still.tip_after.x,
			"Reversed wind should deflect the other way. Got x: ", upwind.tip_after.x);

	// Stronger wind, further deflection. Guards against the acceleration being clamped or
	// normalized away somewhere between the bus and the solver.
	const JoltHairSmoke::Result gale = JoltHairSmoke::run(false, Vector3(60.0, 0.0, 0.0));
	REQUIRE_MESSAGE(!gale.skipped, gale.skip_reason);
	CHECK_MESSAGE(gale.tip_after.x > windy.tip_after.x,
			"Stronger wind should deflect further. 20: ", windy.tip_after.x, " 60: ", gale.tip_after.x);
}

} // namespace TestJoltHair
