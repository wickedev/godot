/**************************************************************************/
/*  jolt_hair_smoke.h                                                     */
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

#include "core/math/vector3.h"

// Test-only bridge to the vendored Jolt hair solver.
//
// The implementation lives in a translation unit compiled inside the Jolt module env, which is the
// only place <Jolt/...> resolves. Keeping the declarations Jolt-free is what lets the test binary
// exercise the solver without the bundled Jolt headers being forced onto the global include path,
// where they would shadow any module carrying its own copy.
namespace JoltHairSmoke {

struct Result {
	// Set when the run could not happen; every other field is meaningless then.
	bool skipped = true;
	String skip_reason;

	bool shaders_loaded = false;
	// HairSettings has to be constructible through the Jolt factory, which is what RegisterHair()
	// buys and what serialization would go through.
	bool factory_creates_hair_settings = false;

	int vertex_count = 0;
	bool any_vertex_nonfinite = false;
	Vector3 root_before;
	Vector3 root_after;
	Vector3 tip_before;
	Vector3 tip_after;
	// True when `with_collision` was asked for and the collider actually made it into the scene.
	bool collider_added = false;
	// External acceleration the solver was actually given, in hair space.
	Vector3 applied_acceleration;
};

// False when built without a hair compute backend (`jolt_hair_compute=none`, the default).
bool is_available();

// Steps a one-strand groom under gravity for half a second. `with_collision` turns collision on and
// puts a convex hull against the lower half of the strand, exercising the shape-gathering and
// collision-plane path Hair::Update takes. `wind_acceleration` is handed to
// Hair::SetExternalAcceleration(), which is how the weather bus reaches the solver.
Result run(bool with_collision, const Vector3 &wind_acceleration = Vector3());

} // namespace JoltHairSmoke
