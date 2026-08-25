/**************************************************************************/
/*  wind_driver.h                                                         */
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

#include "core/templates/hash_set.h"
#include "scene/3d/node_3d.h"

// Drives the wind half of the weather bus, and keeps the physics wind in step with it.
//
// Godot's only pre-existing notion of wind is Area3D's, which SoftBody3D reads. Area3D takes the
// direction from the -Z axis of its wind source node -- and reads that node's LOCAL transform, not
// its global one, despite naming the variable global_transform (see Area3D's wind handling). So
// this node writes its local basis rather than calling look_at(): what Area3D will read has to be
// the world-space wind direction, and under a rotated parent those are not the same thing.
//
// Linked areas get their wind_source_path pointed here and their wind_force_magnitude driven from
// the same gusted speed, so cloth and foliage cannot disagree about which way the wind blows.
//
// Gusting is a sum of sines rather than sampled noise. It keeps the module free of a dependency on
// modules/noise for something this small, and a couple of incommensurable periods read as
// irregular over any window a player watches.
class WindDriver : public Node3D {
	GDCLASS(WindDriver, Node3D);

	Vector3 direction = Vector3(1, 0, 0); // Direction the wind blows TOWARD, matching the bus.
	real_t speed = 0.0;
	real_t turbulence = 0.0;

	real_t gust_amplitude = 0.0;
	real_t gust_period = 8.0;
	bool paused = false;

	// m/s is a velocity and wind_force_magnitude is a force, so one does not go into the other
	// without a coefficient. This is that coefficient, and it is a tuning value rather than a
	// physical constant -- cloth stiffness and triangle area both feed into what looks right.
	real_t physics_force_scale = 1.0;

	double phase = 0.0;
	real_t gust = 1.0;

	TypedArray<NodePath> wind_area_paths;
	// Areas this driver actually bound, so a path that is removed, retyped or pointed elsewhere can
	// be released instead of being left holding a stale source and force.
	HashSet<ObjectID> bound_areas;

	// Only one driver may write the bus. A second one would fight the first every frame, and the
	// symptom is a value that flickers rather than an error.
	static WindDriver *active;

	void _recalculate_gust();
	void _release_areas();
	bool _is_active_producer() const { return active == this; }
	void _publish() const;
	void _apply_to_areas();
	void _orient_self();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_direction(const Vector3 &p_direction);
	Vector3 get_direction() const { return direction; }
	void set_speed(real_t p_speed);
	real_t get_speed() const { return speed; }
	void set_turbulence(real_t p_turbulence);
	real_t get_turbulence() const { return turbulence; }

	void set_gust_amplitude(real_t p_amplitude);
	real_t get_gust_amplitude() const { return gust_amplitude; }
	void set_gust_period(real_t p_seconds);
	real_t get_gust_period() const { return gust_period; }
	void set_paused(bool p_paused);
	bool is_paused() const { return paused; }

	void set_wind_area_paths(const TypedArray<NodePath> &p_paths);
	TypedArray<NodePath> get_wind_area_paths() const { return wind_area_paths; }

	void set_physics_force_scale(real_t p_scale);
	real_t get_physics_force_scale() const { return physics_force_scale; }

	// False when another driver claimed the bus first; this one then leaves both the uniforms and
	// the areas alone.
	bool is_active_producer() const { return _is_active_producer(); }

	// Current gust multiplier; 1.0 means no gust. Matches what the bus is carrying.
	real_t get_gust() const { return gust; }
	// Velocity in m/s with gust applied, same as WeatherBus::get_wind_velocity().
	Vector3 get_velocity() const { return direction * (speed * gust); }

	// Advances gusting and republishes. Called every frame while in the tree; exposed so a fixed
	// step can be driven deterministically.
	void advance(double p_delta);

	WindDriver();
	~WindDriver();
};
