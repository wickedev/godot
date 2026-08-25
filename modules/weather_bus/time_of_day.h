/**************************************************************************/
/*  time_of_day.h                                                         */
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

#include "scene/3d/light_3d.h"
#include "scene/main/node.h"
#include "scene/resources/curve.h"
#include "scene/resources/gradient.h"

// Drives the time-of-day half of the weather bus.
//
// WeatherBus is transport only -- it holds tod_* and forwards it to shaders, but nothing advances
// it. This is the producer: it turns a clock into sun and moon directions and pushes them, and
// optionally aims the DirectionalLight3D nodes that stand in for those bodies so the scene and the
// shaders cannot disagree about where the sun is.
//
// Solar position is the real thing -- declination from day of year, hour angle from local solar
// time, altitude and azimuth from latitude -- rather than a rotation about a fixed axis. It costs
// the same and gets seasons and latitude right, which a fixed axis cannot.
class TimeOfDay : public Node {
	GDCLASS(TimeOfDay, Node);

	double time = 0.25; // Normalized day fraction; 0.25 is dawn.
	double day_length = 1200.0;
	bool paused = false;

	double latitude_degrees = 45.0;
	int day_of_year = 172; // Northern summer solstice, so the default scene is lit.

	NodePath sun_light_path;
	NodePath moon_light_path;
	Ref<Gradient> sun_color_ramp;
	Ref<Curve> sun_energy_curve;

	// Only one may write the bus; a second would fight the first every frame.
	static TimeOfDay *active;

	Vector3 sun_direction = Vector3(0, 1, 0);
	Vector3 moon_direction = Vector3(0, -1, 0);
	real_t sun_intensity = 1.0;

	void _recalculate();
	void _publish() const;
	void _apply_to_lights();
	DirectionalLight3D *_resolve_light(const NodePath &p_path) const;
	static void _aim_light(DirectionalLight3D *p_light, const Vector3 &p_toward_body);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_time(double p_time);
	double get_time() const { return time; }
	void set_day_length(double p_seconds);
	double get_day_length() const { return day_length; }
	void set_paused(bool p_paused);
	bool is_paused() const { return paused; }

	void set_latitude_degrees(double p_latitude);
	double get_latitude_degrees() const { return latitude_degrees; }
	void set_day_of_year(int p_day);
	int get_day_of_year() const { return day_of_year; }

	void set_sun_light_path(const NodePath &p_path);
	NodePath get_sun_light_path() const { return sun_light_path; }
	void set_moon_light_path(const NodePath &p_path);
	NodePath get_moon_light_path() const { return moon_light_path; }

	void set_sun_color_ramp(const Ref<Gradient> &p_ramp);
	Ref<Gradient> get_sun_color_ramp() const { return sun_color_ramp; }
	void set_sun_energy_curve(const Ref<Curve> &p_curve);
	Ref<Curve> get_sun_energy_curve() const { return sun_energy_curve; }

	// Results of the last solve. Unit vectors pointing TOWARD the body, matching the bus.
	Vector3 get_sun_direction() const { return sun_direction; }
	Vector3 get_moon_direction() const { return moon_direction; }
	real_t get_sun_intensity() const { return sun_intensity; }

	// Advances the clock and republishes. Called every frame while in the tree; exposed so a fixed
	// step can be driven deterministically from a test or a cutscene.
	void advance(double p_delta);

	// False when another TimeOfDay claimed the bus first; this one then leaves it alone.
	bool is_active_producer() const { return active == this; }

	TimeOfDay();
	~TimeOfDay();
};
