/**************************************************************************/
/*  test_wind_driver.h                                                    */
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

#include "../weather_bus.h"
#include "../wind_driver.h"

#include "scene/3d/physics/area_3d.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

namespace TestWindDriver {

TEST_CASE("[SceneTree][WindDriver] Wind state reaches the bus") {
	WeatherBus *bus = WeatherBus::get_singleton();
	REQUIRE(bus != nullptr);

	WindDriver *wind = memnew(WindDriver);
	wind->set_direction(Vector3(0, 0, -2)); // Deliberately unnormalized.
	wind->set_speed(10.0);
	wind->set_turbulence(0.5);

	// The bus documents wind_direction as a unit vector and every shader helper multiplies it by a
	// speed, so the driver normalizes rather than trusting the caller.
	CHECK(wind->get_direction().is_equal_approx(Vector3(0, 0, -1)));
	CHECK(bus->get_wind_direction().is_equal_approx(Vector3(0, 0, -1)));
	CHECK(bus->get_wind_speed() == doctest::Approx(10.0));
	CHECK(bus->get_wind_turbulence() == doctest::Approx(0.5));

	// Driver and bus have to agree about velocity, or CPU consumers and shaders drift apart.
	CHECK(wind->get_velocity().is_equal_approx(bus->get_wind_velocity()));

	// A zero direction would leave shaders multiplying by nothing; fall back instead.
	wind->set_direction(Vector3());
	CHECK_FALSE(wind->get_direction().is_zero_approx());

	memdelete(wind);
	bus->set_wind_speed(0.0);
	bus->set_wind_gust(1.0);
	bus->set_wind_turbulence(0.0);
}

TEST_CASE("[SceneTree][WindDriver] Gusting varies over time and never reverses") {
	WindDriver *wind = memnew(WindDriver);
	wind->set_speed(5.0);
	wind->set_gust_period(8.0);

	// With no amplitude the multiplier is exactly 1, so a scene that does not want gusting gets a
	// steady wind rather than something almost-steady.
	CHECK(wind->get_gust() == doctest::Approx(1.0));
	wind->advance(3.0);
	CHECK(wind->get_gust() == doctest::Approx(1.0));

	wind->set_gust_amplitude(0.5);
	real_t min_gust = wind->get_gust();
	real_t max_gust = wind->get_gust();
	for (int i = 0; i < 200; ++i) {
		wind->advance(0.1);
		min_gust = MIN(min_gust, wind->get_gust());
		max_gust = MAX(max_gust, wind->get_gust());
	}

	CHECK_MESSAGE(max_gust - min_gust > 0.1, "Gusting should actually vary. Range: ", min_gust,
			" to ", max_gust);
	// Gusts lull, they do not blow backwards -- a negative multiplier would flip every foliage
	// sway and the physics force with it.
	CHECK_MESSAGE(min_gust >= 0.0, "Gust multiplier went negative: ", min_gust);

	memdelete(wind);
	WeatherBus::get_singleton()->set_wind_speed(0.0);
	WeatherBus::get_singleton()->set_wind_gust(1.0);
}

TEST_CASE("[SceneTree][WindDriver] Physics wind follows the same source") {
	WindDriver *wind = memnew(WindDriver);
	Area3D *area = memnew(Area3D);
	area->set_name("WindArea");

	SceneTree::get_singleton()->get_root()->add_child(wind);
	wind->add_child(area);

	TypedArray<NodePath> paths;
	paths.push_back(NodePath("WindArea"));
	wind->set_wind_area_paths(paths);

	wind->set_direction(Vector3(0, 0, -1));
	wind->set_speed(7.0);

	// Area3D takes its wind direction from the -Z axis of whatever node wind_source_path points
	// at, so the driver has to both aim itself and hand the area that path. Without this, cloth
	// blows one way while foliage blows another and nothing reports an error.
	CHECK(area->get_wind_source_path() == area->get_path_to(wind));
	CHECK(area->get_wind_force_magnitude() == doctest::Approx(7.0));

	const Vector3 area_wind = -wind->get_global_transform().basis.get_column(Vector3::AXIS_Z).normalized();
	CHECK_MESSAGE(area_wind.is_equal_approx(wind->get_direction()),
			"Physics wind direction should match the published one. Area: ", area_wind,
			" published: ", wind->get_direction());

	// Gusting has to reach physics too, otherwise cloth ignores the gusts foliage responds to.
	wind->set_gust_amplitude(0.5);
	wind->advance(2.0);
	CHECK(area->get_wind_force_magnitude() == doctest::Approx(7.0 * wind->get_gust()));

	memdelete(wind);
	WeatherBus::get_singleton()->set_wind_speed(0.0);
	WeatherBus::get_singleton()->set_wind_gust(1.0);
}

} // namespace TestWindDriver
