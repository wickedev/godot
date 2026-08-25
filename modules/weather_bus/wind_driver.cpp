/**************************************************************************/
/*  wind_driver.cpp                                                       */
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

#include "wind_driver.h"

#include "weather_bus.h"

#include "scene/3d/physics/area_3d.h"

void WindDriver::_recalculate_gust() {
	if (gust_amplitude <= 0.0 || gust_period <= 0.0) {
		gust = 1.0;
		return;
	}
	// Two incommensurable periods, so the pattern does not visibly repeat. The 0.37 ratio is
	// arbitrary beyond being irrational enough for the purpose.
	const double base = Math::TAU * phase / gust_period;
	const double wave = 0.6 * Math::sin(base) + 0.4 * Math::sin(base * 0.37 + 1.3);
	gust = real_t(1.0 + gust_amplitude * wave);
	// Gusts lull, they do not blow backwards.
	gust = MAX(gust, real_t(0.0));
}

void WindDriver::_publish() const {
	WeatherBus *bus = WeatherBus::get_singleton();
	if (bus == nullptr) {
		return;
	}
	bus->set_wind_direction(direction);
	bus->set_wind_speed(speed);
	bus->set_wind_gust(gust);
	bus->set_wind_turbulence(turbulence);
}

void WindDriver::_orient_self() {
	// Area3D derives wind direction as -Z of its source node's global basis (see Area3D's
	// wind_source handling), so aiming this node along the wind is what makes it usable as one.
	if (!is_inside_tree() || direction.is_zero_approx()) {
		return;
	}
	const Vector3 target = get_global_position() + direction;
	// look_at() degenerates when the wind runs along the up axis; tilt the reference in that case.
	const Vector3 up = Math::abs(direction.normalized().dot(Vector3(0, 1, 0))) > 0.99
			? Vector3(0, 0, 1)
			: Vector3(0, 1, 0);
	// `use_model_front` stays false so -Z is forward -- that is the axis Area3D reads.
	look_at(target, up, false);
}

void WindDriver::_apply_to_areas() {
	if (!is_inside_tree()) {
		return;
	}
	for (int i = 0; i < wind_area_paths.size(); ++i) {
		const NodePath path = wind_area_paths[i];
		if (path.is_empty()) {
			continue;
		}
		Area3D *area = Object::cast_to<Area3D>(get_node_or_null(path));
		if (area == nullptr) {
			continue;
		}
		// Point the area at this node so physics wind and shader wind share a direction, then drive
		// the magnitude from the same gusted speed the bus is carrying.
		area->set_wind_source_path(area->get_path_to(this));
		area->set_wind_force_magnitude(speed * gust);
	}
}

void WindDriver::advance(double p_delta) {
	phase += p_delta;
	_recalculate_gust();
	_publish();
	_orient_self();
	_apply_to_areas();
}

void WindDriver::set_direction(const Vector3 &p_direction) {
	// Normalizing here rather than trusting callers: the bus documents wind_direction as a unit
	// vector, and every shader helper multiplies it by a speed.
	direction = p_direction.is_zero_approx() ? Vector3(1, 0, 0) : p_direction.normalized();
	_publish();
	_orient_self();
	_apply_to_areas();
}

void WindDriver::set_speed(real_t p_speed) {
	speed = MAX(p_speed, real_t(0.0));
	_publish();
	_apply_to_areas();
}

void WindDriver::set_turbulence(real_t p_turbulence) {
	turbulence = CLAMP(p_turbulence, real_t(0.0), real_t(1.0));
	_publish();
}

void WindDriver::set_gust_amplitude(real_t p_amplitude) {
	gust_amplitude = MAX(p_amplitude, real_t(0.0));
	_recalculate_gust();
	_publish();
	_apply_to_areas();
}

void WindDriver::set_gust_period(real_t p_seconds) {
	gust_period = MAX(p_seconds, real_t(0.0));
	_recalculate_gust();
	_publish();
	_apply_to_areas();
}

void WindDriver::set_paused(bool p_paused) {
	paused = p_paused;
}

void WindDriver::set_wind_area_paths(const TypedArray<NodePath> &p_paths) {
	wind_area_paths = p_paths;
	_apply_to_areas();
}

void WindDriver::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process_internal(true);
			// Publish once so the world is windy before the first frame advances.
			_recalculate_gust();
			_publish();
			_orient_self();
			_apply_to_areas();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!paused) {
				advance(get_process_delta_time());
			}
		} break;
	}
}

void WindDriver::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_direction", "direction"), &WindDriver::set_direction);
	ClassDB::bind_method(D_METHOD("get_direction"), &WindDriver::get_direction);
	ClassDB::bind_method(D_METHOD("set_speed", "speed"), &WindDriver::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &WindDriver::get_speed);
	ClassDB::bind_method(D_METHOD("set_turbulence", "turbulence"), &WindDriver::set_turbulence);
	ClassDB::bind_method(D_METHOD("get_turbulence"), &WindDriver::get_turbulence);
	ClassDB::bind_method(D_METHOD("set_gust_amplitude", "amplitude"), &WindDriver::set_gust_amplitude);
	ClassDB::bind_method(D_METHOD("get_gust_amplitude"), &WindDriver::get_gust_amplitude);
	ClassDB::bind_method(D_METHOD("set_gust_period", "seconds"), &WindDriver::set_gust_period);
	ClassDB::bind_method(D_METHOD("get_gust_period"), &WindDriver::get_gust_period);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &WindDriver::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &WindDriver::is_paused);
	ClassDB::bind_method(D_METHOD("set_wind_area_paths", "paths"), &WindDriver::set_wind_area_paths);
	ClassDB::bind_method(D_METHOD("get_wind_area_paths"), &WindDriver::get_wind_area_paths);

	ClassDB::bind_method(D_METHOD("get_gust"), &WindDriver::get_gust);
	ClassDB::bind_method(D_METHOD("get_velocity"), &WindDriver::get_velocity);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &WindDriver::advance);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "direction"), "set_direction", "get_direction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_speed", "get_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turbulence", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_turbulence", "get_turbulence");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_paused", "is_paused");

	ADD_GROUP("Gust", "gust_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gust_amplitude", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater"), "set_gust_amplitude", "get_gust_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gust_period", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_gust_period", "get_gust_period");

	ADD_GROUP("Physics", "");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "wind_area_paths", PROPERTY_HINT_ARRAY_TYPE,
						 vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Area3D")),
			"set_wind_area_paths", "get_wind_area_paths");
}

WindDriver::WindDriver() {
	_recalculate_gust();
}
