/**************************************************************************/
/*  test_time_of_day.h                                                    */
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

#include "../time_of_day.h"
#include "../weather_bus.h"

#include "scene/3d/light_3d.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

namespace TestTimeOfDay {

TEST_CASE("[SceneTree][TimeOfDay] Sun tracks the sky through the day") {
	TimeOfDay *tod = memnew(TimeOfDay);
	tod->set_latitude_degrees(45.0);
	tod->set_day_of_year(172); // Northern summer solstice.

	// Solar noon. At mid-northern latitude in summer the sun is high and roughly due south, which
	// in Godot's -Z-is-north convention means +Z.
	tod->set_time(0.5);
	const Vector3 noon = tod->get_sun_direction();
	CHECK_MESSAGE(noon.y > 0.9, "Midsummer noon sun should be near the zenith. Got: ", noon);
	CHECK(tod->get_sun_intensity() == doctest::Approx(1.0));

	// Midnight, half a day later: below the horizon, and contributing nothing.
	tod->set_time(0.0);
	CHECK_MESSAGE(tod->get_sun_direction().y < 0.0, "Midnight sun should be below the horizon.");
	CHECK(tod->get_sun_intensity() == doctest::Approx(0.0));

	// The moon is the anti-sun, so it has to be up when the sun is not.
	CHECK(tod->get_moon_direction().y > 0.0);
	CHECK(tod->get_moon_direction().is_equal_approx(-tod->get_sun_direction()));

	// Morning and afternoon sit on opposite sides of the north-south line.
	tod->set_time(0.3);
	const real_t morning_x = tod->get_sun_direction().x;
	tod->set_time(0.7);
	const real_t afternoon_x = tod->get_sun_direction().x;
	CHECK_MESSAGE(morning_x * afternoon_x < 0.0,
			"Sun should cross from one side of the meridian to the other. Morning x: ", morning_x,
			" afternoon x: ", afternoon_x);

	memdelete(tod);
}

TEST_CASE("[SceneTree][TimeOfDay] The sun rises in the east and sets in the west") {
	// The check every other test here is blind to. A sign error on the X term produces a sun that
	// rises in the west and sets in the east, and nothing else notices: the altitudes are right, the
	// ranges are right, morning and afternoon still fall on opposite sides of the meridian, and
	// polar night still works. Only a fixed cardinal point catches it.
	//
	// Godot's convention: -Z is north, +Z is south, +X is east.
	TimeOfDay *tod = memnew(TimeOfDay);
	tod->set_latitude_degrees(0.0);
	tod->set_day_of_year(80); // Near the March equinox: sunrise due east, sunset due west.

	tod->set_time(0.25); // 06:00 solar.
	const Vector3 sunrise = tod->get_sun_direction();
	CHECK_MESSAGE(sunrise.x > 0.9, "The sun should rise in the east (+X). Got: ", sunrise);
	CHECK_MESSAGE(Math::abs(sunrise.y) < 0.1, "Sunrise should sit on the horizon. Got: ", sunrise);

	tod->set_time(0.75); // 18:00 solar.
	const Vector3 sunset = tod->get_sun_direction();
	CHECK_MESSAGE(sunset.x < -0.9, "The sun should set in the west (-X). Got: ", sunset);

	// Noon at a northern latitude puts the sun south, which is +Z here.
	tod->set_latitude_degrees(45.0);
	tod->set_time(0.5);
	const Vector3 noon_north = tod->get_sun_direction();
	CHECK_MESSAGE(noon_north.z > 0.0, "At 45N the noon sun should be to the south (+Z). Got: ", noon_north);

	// And north of it in the southern hemisphere, which is -Z.
	tod->set_latitude_degrees(-45.0);
	tod->set_time(0.5);
	const Vector3 noon_south = tod->get_sun_direction();
	CHECK_MESSAGE(noon_south.z < 0.0, "At 45S the noon sun should be to the north (-Z). Got: ", noon_south);

	// Sunrise stays east in the southern hemisphere too.
	tod->set_time(0.25);
	CHECK_MESSAGE(tod->get_sun_direction().x > 0.0, "The sun rises in the east at any latitude.");

	memdelete(tod);
}

TEST_CASE("[SceneTree][TimeOfDay] Sun intensity ramps through the horizon, not at it") {
	// The contract said "0 below the horizon", which is not what the code does and not what looks
	// right either -- a hard cut at the horizon pops. Half intensity at the horizon, reaching zero
	// a few degrees below it.
	TimeOfDay *tod = memnew(TimeOfDay);
	tod->set_latitude_degrees(0.0);
	tod->set_day_of_year(80);

	tod->set_time(0.25); // Sun on the horizon.
	CHECK_MESSAGE(tod->get_sun_intensity() > 0.4, "Horizon intensity should be about half, not zero.");
	CHECK(tod->get_sun_intensity() < 0.6);

	tod->set_time(0.5);
	CHECK(tod->get_sun_intensity() == doctest::Approx(1.0));

	tod->set_time(0.0); // Deep night.
	CHECK(tod->get_sun_intensity() == doctest::Approx(0.0));

	memdelete(tod);
}

TEST_CASE("[SceneTree][TimeOfDay] Latitude and season change where the sun goes") {
	TimeOfDay *tod = memnew(TimeOfDay);
	tod->set_time(0.5);

	// Solar noon gets lower as you go north. A fixed rotation axis could not tell these apart,
	// which is the reason for solving the real declination/hour-angle form.
	tod->set_latitude_degrees(0.0);
	const real_t equator = tod->get_sun_direction().y;
	tod->set_latitude_degrees(60.0);
	const real_t far_north = tod->get_sun_direction().y;
	CHECK_MESSAGE(equator > far_north, "Noon sun should be higher at the equator. Equator: ",
			equator, " 60N: ", far_north);

	// Same place, opposite seasons.
	tod->set_latitude_degrees(50.0);
	tod->set_day_of_year(172); // Summer solstice.
	const real_t summer = tod->get_sun_direction().y;
	tod->set_day_of_year(355); // Winter solstice.
	const real_t winter = tod->get_sun_direction().y;
	CHECK_MESSAGE(summer > winter, "Summer noon should be higher than winter noon. Summer: ",
			summer, " winter: ", winter);

	// Polar night: above the Arctic circle in midwinter the sun never rises.
	tod->set_latitude_degrees(80.0);
	CHECK_MESSAGE(tod->get_sun_direction().y < 0.0, "Sun should stay down during polar night.");

	memdelete(tod);
}

TEST_CASE("[SceneTree][TimeOfDay] Clock wraps and publishes to the bus") {
	WeatherBus *bus = WeatherBus::get_singleton();
	REQUIRE(bus != nullptr);

	TimeOfDay *tod = memnew(TimeOfDay);
	// Ownership of the bus is claimed on entering the tree.
	SceneTree::get_singleton()->get_root()->add_child(tod);
	tod->set_day_length(100.0);
	tod->set_time(0.9);

	// The bus is the transport; TimeOfDay is what puts values on it. Nothing else does.
	CHECK(bus->get_tod_time() == doctest::Approx(0.9));
	CHECK(bus->get_tod_sun_direction().is_equal_approx(tod->get_sun_direction()));

	// 20 seconds of a 100-second day is 0.2, so this has to wrap past midnight rather than
	// running off past 1.0.
	tod->advance(20.0);
	CHECK(tod->get_time() == doctest::Approx(0.1));
	CHECK(bus->get_tod_time() == doctest::Approx(0.1));

	memdelete(tod);
	bus->set_tod_time(0.0);
}

TEST_CASE("[SceneTree][TimeOfDay] Sun light is aimed along the published direction") {
	TimeOfDay *tod = memnew(TimeOfDay);
	DirectionalLight3D *sun = memnew(DirectionalLight3D);
	sun->set_name("Sun");

	SceneTree::get_singleton()->get_root()->add_child(tod);
	tod->add_child(sun);
	tod->set_sun_light_path(NodePath("Sun"));
	// Aiming must not move the light: a directional light has no meaningful position, and shifting
	// someone else's node would be a side effect nobody asked for.
	sun->set_global_position(Vector3(3.0, 4.0, 5.0));

	tod->set_latitude_degrees(45.0);
	tod->set_day_of_year(172);
	tod->set_time(0.5);

	// A DirectionalLight3D emits along its local -Z, so the light has to travel the opposite way
	// from the vector the bus carries. Getting this backwards lights the scene from underground and
	// nothing errors, so it is worth pinning down.
	const Vector3 emitted = -sun->get_global_transform().basis.get_column(Vector3::AXIS_Z).normalized();
	CHECK_MESSAGE(emitted.is_equal_approx(-tod->get_sun_direction()),
			"Light should emit away from the sun. Emitted: ", emitted,
			" sun direction: ", tod->get_sun_direction());

	CHECK_MESSAGE(sun->get_global_position().is_equal_approx(Vector3(3.0, 4.0, 5.0)),
			"Aiming the sun light moved it. Position: ", sun->get_global_position());

	// Below the horizon the sun stops lighting the scene.
	tod->set_time(0.0);
	CHECK_FALSE(sun->is_visible());
	tod->set_time(0.5);
	CHECK(sun->is_visible());

	memdelete(tod);
}

} // namespace TestTimeOfDay
