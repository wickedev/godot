/**************************************************************************/
/*  time_of_day.cpp                                                       */
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

#include "time_of_day.h"

#include "weather_bus.h"

#include "scene/resources/curve.h"
#include "scene/resources/gradient.h"

void TimeOfDay::_recalculate() {
	// Solar position, following the usual declination / hour-angle formulation. Fractional days are
	// fine here: the sun moves continuously, so day_of_year only needs to set the season.
	const double declination = Math::deg_to_rad(23.44) * Math::sin(Math::TAU * (284.0 + day_of_year) / 365.0);
	const double latitude = Math::deg_to_rad(latitude_degrees);

	// Solar noon at time == 0.5, so the hour angle runs -pi at midnight through 0 at noon.
	const double hour_angle = Math::TAU * (time - 0.5);

	const double sin_altitude = Math::sin(latitude) * Math::sin(declination) +
			Math::cos(latitude) * Math::cos(declination) * Math::cos(hour_angle);
	const double altitude = Math::asin(CLAMP(sin_altitude, -1.0, 1.0));

	// atan2 form, which stays well-behaved across the whole day unlike solving for cos(azimuth).
	const double azimuth = Math::atan2(
			Math::sin(hour_angle),
			Math::cos(hour_angle) * Math::sin(latitude) - Math::tan(declination) * Math::cos(latitude));

	// Godot is Y-up with -Z as north. Azimuth is measured from due south, westward.
	const double cos_altitude = Math::cos(altitude);
	sun_direction = Vector3(
			real_t(cos_altitude * Math::sin(azimuth)),
			real_t(Math::sin(altitude)),
			real_t(cos_altitude * Math::cos(azimuth)));
	sun_direction.normalize();

	// The moon opposes the sun. Not astronomically true -- that needs a lunar phase -- but it does
	// give a lit night sky, and the bus only promises a direction to point a light along.
	moon_direction = -sun_direction;

	// Fade out through the horizon rather than snapping, so dusk does not pop. The window is a few
	// degrees either side, roughly matching civil twilight.
	sun_intensity = real_t(CLAMP((sin_altitude + 0.05) / 0.1, 0.0, 1.0));
}

void TimeOfDay::_publish() const {
	WeatherBus *bus = WeatherBus::get_singleton();
	if (bus == nullptr) {
		return;
	}
	bus->set_tod_time(real_t(time));
	bus->set_tod_sun_direction(sun_direction);
	bus->set_tod_moon_direction(moon_direction);
	bus->set_tod_sun_intensity(sun_intensity);
}

DirectionalLight3D *TimeOfDay::_resolve_light(const NodePath &p_path) const {
	if (p_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	return Object::cast_to<DirectionalLight3D>(get_node_or_null(p_path));
}

void TimeOfDay::_apply_to_lights() {
	// A DirectionalLight3D emits along its local -Z, so it has to look from the body's direction
	// back at the origin -- the opposite of the vector the bus carries.
	if (DirectionalLight3D *sun = _resolve_light(sun_light_path)) {
		if (!sun_direction.is_zero_approx()) {
			// Stand where the sun is and look back at the origin. `use_model_front` stays false so
			// -Z is forward, which is the axis a DirectionalLight3D actually emits along.
			sun->look_at_from_position(sun_direction, Vector3(), Vector3(0, 1, 0), false);
		}
		if (sun_color_ramp.is_valid()) {
			sun->set_color(sun_color_ramp->get_color_at_offset(float(time)));
		}
		if (sun_energy_curve.is_valid()) {
			sun->set_param(Light3D::PARAM_ENERGY, sun_energy_curve->sample_baked(float(time)));
		} else {
			sun->set_param(Light3D::PARAM_ENERGY, sun_intensity);
		}
		// Below the horizon the sun should stop lighting the scene, but leaving the node visible
		// keeps it available to sky shaders that read LIGHT0.
		sun->set_visible(sun_intensity > 0.0);
	}

	if (DirectionalLight3D *moon = _resolve_light(moon_light_path)) {
		if (!moon_direction.is_zero_approx()) {
			moon->look_at_from_position(moon_direction, Vector3(), Vector3(0, 1, 0), false);
		}
		moon->set_visible(sun_intensity <= 0.0);
	}
}

void TimeOfDay::advance(double p_delta) {
	if (day_length > 0.0) {
		time = Math::fposmod(time + p_delta / day_length, 1.0);
	}
	_recalculate();
	_publish();
	_apply_to_lights();
}

void TimeOfDay::set_time(double p_time) {
	time = Math::fposmod(p_time, 1.0);
	_recalculate();
	_publish();
	_apply_to_lights();
}

void TimeOfDay::set_day_length(double p_seconds) {
	day_length = MAX(p_seconds, 0.0);
}

void TimeOfDay::set_paused(bool p_paused) {
	paused = p_paused;
}

void TimeOfDay::set_latitude_degrees(double p_latitude) {
	latitude_degrees = CLAMP(p_latitude, -90.0, 90.0);
	set_time(time);
}

void TimeOfDay::set_day_of_year(int p_day) {
	day_of_year = CLAMP(p_day, 1, 366);
	set_time(time);
}

void TimeOfDay::set_sun_light_path(const NodePath &p_path) {
	sun_light_path = p_path;
	_apply_to_lights();
}

void TimeOfDay::set_moon_light_path(const NodePath &p_path) {
	moon_light_path = p_path;
	_apply_to_lights();
}

void TimeOfDay::set_sun_color_ramp(const Ref<Gradient> &p_ramp) {
	sun_color_ramp = p_ramp;
	_apply_to_lights();
}

void TimeOfDay::set_sun_energy_curve(const Ref<Curve> &p_curve) {
	sun_energy_curve = p_curve;
	_apply_to_lights();
}

void TimeOfDay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process_internal(true);
			set_time(time); // Publish once so the world is lit before the first frame advances.
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!paused) {
				advance(get_process_delta_time());
			}
		} break;
	}
}

void TimeOfDay::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_time", "time"), &TimeOfDay::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &TimeOfDay::get_time);
	ClassDB::bind_method(D_METHOD("set_day_length", "seconds"), &TimeOfDay::set_day_length);
	ClassDB::bind_method(D_METHOD("get_day_length"), &TimeOfDay::get_day_length);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &TimeOfDay::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &TimeOfDay::is_paused);
	ClassDB::bind_method(D_METHOD("set_latitude_degrees", "latitude"), &TimeOfDay::set_latitude_degrees);
	ClassDB::bind_method(D_METHOD("get_latitude_degrees"), &TimeOfDay::get_latitude_degrees);
	ClassDB::bind_method(D_METHOD("set_day_of_year", "day"), &TimeOfDay::set_day_of_year);
	ClassDB::bind_method(D_METHOD("get_day_of_year"), &TimeOfDay::get_day_of_year);
	ClassDB::bind_method(D_METHOD("set_sun_light_path", "path"), &TimeOfDay::set_sun_light_path);
	ClassDB::bind_method(D_METHOD("get_sun_light_path"), &TimeOfDay::get_sun_light_path);
	ClassDB::bind_method(D_METHOD("set_moon_light_path", "path"), &TimeOfDay::set_moon_light_path);
	ClassDB::bind_method(D_METHOD("get_moon_light_path"), &TimeOfDay::get_moon_light_path);
	ClassDB::bind_method(D_METHOD("set_sun_color_ramp", "ramp"), &TimeOfDay::set_sun_color_ramp);
	ClassDB::bind_method(D_METHOD("get_sun_color_ramp"), &TimeOfDay::get_sun_color_ramp);
	ClassDB::bind_method(D_METHOD("set_sun_energy_curve", "curve"), &TimeOfDay::set_sun_energy_curve);
	ClassDB::bind_method(D_METHOD("get_sun_energy_curve"), &TimeOfDay::get_sun_energy_curve);

	ClassDB::bind_method(D_METHOD("get_sun_direction"), &TimeOfDay::get_sun_direction);
	ClassDB::bind_method(D_METHOD("get_moon_direction"), &TimeOfDay::get_moon_direction);
	ClassDB::bind_method(D_METHOD("get_sun_intensity"), &TimeOfDay::get_sun_intensity);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &TimeOfDay::advance);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "day_length", PROPERTY_HINT_RANGE, "0,86400,0.1,or_greater,suffix:s"), "set_day_length", "get_day_length");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_paused", "is_paused");

	ADD_GROUP("Location", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "latitude_degrees", PROPERTY_HINT_RANGE, "-90,90,0.01,suffix:°"), "set_latitude_degrees", "get_latitude_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "day_of_year", PROPERTY_HINT_RANGE, "1,366,1"), "set_day_of_year", "get_day_of_year");

	ADD_GROUP("Lights", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_light_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_sun_light_path", "get_sun_light_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "moon_light_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_moon_light_path", "get_moon_light_path");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sun_color_ramp", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_sun_color_ramp", "get_sun_color_ramp");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sun_energy_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_sun_energy_curve", "get_sun_energy_curve");
}

TimeOfDay::TimeOfDay() {
	_recalculate();
}
