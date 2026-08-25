/**************************************************************************/
/*  test_weather_bus.h                                                    */
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

#include "../engine_shader_lib.h"
#include "../weather_bus.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/shader_preprocessor.h"
#include "tests/test_macros.h"

namespace TestWeatherBus {

TEST_CASE("[Modules][WeatherBus] Uniform contract is declared on the rendering server") {
	RenderingServer *rs = RenderingServer::get_singleton();
	REQUIRE(rs != nullptr);

	// Every name a shader may reference, with the type it must have. Duplicated from the module on
	// purpose: if someone renames or retypes a uniform, this list is the thing that objects.
	struct Expected {
		const char *name;
		RSE::GlobalShaderParameterType type;
	};
	const Expected expected[] = {
		{ "tod_time", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "tod_sun_direction", RSE::GLOBAL_VAR_TYPE_VEC3 },
		{ "tod_moon_direction", RSE::GLOBAL_VAR_TYPE_VEC3 },
		{ "tod_sun_intensity", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "wind_direction", RSE::GLOBAL_VAR_TYPE_VEC3 },
		{ "wind_speed", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "wind_gust", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "wind_turbulence", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "wetness_amount", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "wetness_porosity", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "weather_rain_intensity", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "weather_snow_intensity", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "weather_temperature", RSE::GLOBAL_VAR_TYPE_FLOAT },
		{ "weather_fog_density", RSE::GLOBAL_VAR_TYPE_FLOAT },
	};

	for (const Expected &e : expected) {
		CHECK_MESSAGE(rs->global_shader_parameter_get_type(e.name) == e.type,
				"Global shader uniform '", e.name, "' is missing or has the wrong type.");
	}
}

TEST_CASE("[Modules][WeatherBus] Re-registering the contract is a no-op") {
	// Registration runs at module init. Calling it again must not trip the duplicate-name check in
	// MaterialStorage::global_shader_parameter_add(), which fails loudly rather than ignoring.
	ERR_PRINT_OFF;
	WeatherBus::register_uniforms();
	ERR_PRINT_ON;

	CHECK(RenderingServer::get_singleton()->global_shader_parameter_get_type("wind_speed") == RSE::GLOBAL_VAR_TYPE_FLOAT);
}

TEST_CASE("[Modules][WeatherBus] State round-trips through the singleton") {
	WeatherBus *bus = WeatherBus::get_singleton();
	REQUIRE(bus != nullptr);

	bus->set_wind_direction(Vector3(0, 0, -1));
	bus->set_wind_speed(12.0);
	bus->set_wind_gust(1.5);

	CHECK(bus->get_wind_direction().is_equal_approx(Vector3(0, 0, -1)));
	CHECK(bus->get_wind_speed() == doctest::Approx(12.0));

	// Must match weather_wind_velocity() in engine://shaderlib/weather.gdshaderinc, otherwise CPU
	// consumers (hair, buoyancy, Area3D wind) and shaders disagree about the same wind.
	CHECK(bus->get_wind_velocity().is_equal_approx(Vector3(0, 0, -18.0)));

	bus->set_wind_speed(0.0);
	bus->set_wind_gust(1.0);
}

TEST_CASE("[Modules][WeatherBus] Registration seeds uniforms with the live state") {
	// The uniforms have to start out carrying what this object already holds. Registering them at
	// type defaults instead leaves the shaders reading zero -- wind pointing nowhere, gust at 0x --
	// while the C++ getters say otherwise, until every setter happens to be called once.
	WeatherBus *bus = WeatherBus::get_singleton();
	REQUIRE(bus != nullptr);

	CHECK_FALSE_MESSAGE(bus->get_wind_direction().is_zero_approx(),
			"A zero wind direction would multiply out to nothing in every shader helper.");
	CHECK_MESSAGE(bus->get_wind_gust() == doctest::Approx(1.0),
			"Gust has to start at 1.0, not 0.0, or an unconfigured world has no wind at all.");
	CHECK_FALSE(bus->get_tod_sun_direction().is_zero_approx());

	// Republishing must not throw away state.
	bus->publish_all();
	CHECK(bus->get_wind_gust() == doctest::Approx(1.0));
}

TEST_CASE("[Modules][WeatherBus] Uniform ownership is tracked, not inferred") {
	// unregister_uniforms() must remove only what registration actually added. Inferring the set
	// from the server would sweep up names the project declared under shader_globals/ or that
	// another subsystem owns.
	RenderingServer *rs = RenderingServer::get_singleton();
	REQUIRE(rs != nullptr);

	const StringName foreign = "test_weather_bus_foreign_uniform";
	rs->global_shader_parameter_add(foreign, RSE::GLOBAL_VAR_TYPE_FLOAT, 1.0);

	WeatherBus::unregister_uniforms();
	CHECK_MESSAGE(rs->global_shader_parameter_get_type(foreign) == RSE::GLOBAL_VAR_TYPE_FLOAT,
			"Teardown removed a uniform it did not register.");
	CHECK(rs->global_shader_parameter_get_type("wind_speed") == RSE::GLOBAL_VAR_TYPE_MAX);

	rs->global_shader_parameter_remove(foreign);

	// Put the contract back for the tests that follow, and check re-registration is clean.
	WeatherBus::register_uniforms();
	CHECK(rs->global_shader_parameter_get_type("wind_speed") == RSE::GLOBAL_VAR_TYPE_FLOAT);
	CHECK(rs->global_shader_parameter_get_type("tod_sun_direction") == RSE::GLOBAL_VAR_TYPE_VEC3);
}

TEST_CASE("[Modules][WeatherBus] Engine shader library refuses to overwrite or take bad names") {
	// publish() has to fail loudly. Silently overwriting an existing path would change what every
	// shader including it receives, with nothing to trace it back from.
	ERR_PRINT_OFF;
	CHECK(EngineShaderLib::publish("weather", "// duplicate").is_empty());
	CHECK(EngineShaderLib::publish("", "// empty name").is_empty());
	CHECK(EngineShaderLib::publish("sub/dir", "// separator").is_empty());
	ERR_PRINT_ON;

	// The real entry survived all of that.
	CHECK(EngineShaderLib::is_published("weather"));
	CHECK(ResourceLoader::exists(EngineShaderLib::make_path("weather")));

	// Entries are owned individually: publishing and dropping one must not disturb another.
	const String path = EngineShaderLib::publish("test_weather_bus_scratch", "// scratch\n");
	REQUIRE_FALSE(path.is_empty());
	CHECK(ResourceLoader::exists(path));

	EngineShaderLib::unpublish("test_weather_bus_scratch");
	CHECK_FALSE(EngineShaderLib::is_published("test_weather_bus_scratch"));
	CHECK_MESSAGE(EngineShaderLib::is_published("weather"),
			"Dropping one library took another with it.");
}

TEST_CASE("[Modules][WeatherBus] Engine shader library resolves through ResourceLoader") {
	const String path = String(EngineShaderLib::PATH_PREFIX) + "weather.gdshaderinc";

	// The `engine://` scheme works only because ResourceLoader consults ResourceCache before it
	// asks any format loader, and because String::is_absolute_path() sees the "://" and leaves the
	// path alone instead of rewriting it to res://. Assert both halves.
	CHECK_MESSAGE(path.is_absolute_path(), "engine:// must look absolute or ResourceLoader rewrites it.");
	REQUIRE_MESSAGE(ResourceLoader::exists(path), "Engine shader library was not published.");

	Ref<ShaderInclude> include = ResourceLoader::load(path);
	REQUIRE(include.is_valid());
	CHECK(include->get_code().contains("weather_wind_velocity"));
}

TEST_CASE("[Modules][WeatherBus] Shader preprocessor expands the engine include") {
	// The end-to-end path a real material takes. Nothing else in the engine exercises an
	// `engine://` include, so if the mechanism regresses this is what catches it.
	const String code = "#include \"engine://shaderlib/weather.gdshaderinc\"\n";

	ShaderPreprocessor preprocessor;
	String result;
	String error_text;
	const Error err = preprocessor.preprocess(code, "test_weather_bus.gdshader", result, &error_text);

	CHECK_MESSAGE(err == OK, "Preprocessing failed: ", error_text);
	CHECK(result.contains("global uniform vec3 wind_direction"));
	CHECK(result.contains("weather_apply_wetness"));
	// Sway takes a clock rather than reading tod_time, which wraps at midnight and would jerk
	// every plant in the world once a day.
	CHECK(result.contains("weather_wind_sway(vec3 world_position, float stiffness, float phase, float seconds)"));
}

TEST_CASE("[Modules][WeatherBus] Registration does not depend on an editor-only query") {
	// The tests run with is_editor_hint() false, which is the exported-game path. Every existence
	// query the rendering server offers ERR_FAILs there, so registration has to work without one --
	// this passing at all is the assertion. It would have failed while the code asked the server
	// whether each name already existed.
	CHECK_FALSE(Engine::get_singleton()->is_editor_hint());

	WeatherBus::unregister_uniforms();
	WeatherBus::register_uniforms();

	RenderingServer *rs = RenderingServer::get_singleton();
	CHECK(rs->global_shader_parameter_get_type("wind_speed") == RSE::GLOBAL_VAR_TYPE_FLOAT);
	CHECK(rs->global_shader_parameter_get_type("weather_temperature") == RSE::GLOBAL_VAR_TYPE_FLOAT);
}

TEST_CASE("[Modules][WeatherBus] A name the project declared stays with the project") {
	// The one collision that is detectable without the renderer, and the one that actually happens:
	// modules initialize before main.cpp loads shader_globals/, so claiming a name the project also
	// declares would make that later load fail as a duplicate.
	ProjectSettings *settings = ProjectSettings::get_singleton();
	REQUIRE(settings != nullptr);

	WeatherBus::unregister_uniforms();

	Dictionary declared;
	declared["type"] = "float";
	declared["value"] = 3.5;
	settings->set_setting("shader_globals/wetness_amount", declared);

	WeatherBus::register_uniforms();

	RenderingServer *rs = RenderingServer::get_singleton();
	CHECK_MESSAGE(rs->global_shader_parameter_get_type("wetness_amount") == RSE::GLOBAL_VAR_TYPE_MAX,
			"The bus claimed a name the project had declared.");
	CHECK_MESSAGE(rs->global_shader_parameter_get_type("wetness_porosity") == RSE::GLOBAL_VAR_TYPE_FLOAT,
			"Yielding one name should not skip the rest.");

	// Teardown must not remove it either -- it was never ours.
	WeatherBus::unregister_uniforms();
	settings->set_setting("shader_globals/wetness_amount", Variant());

	WeatherBus::register_uniforms();
	CHECK(rs->global_shader_parameter_get_type("wetness_amount") == RSE::GLOBAL_VAR_TYPE_FLOAT);
}

TEST_CASE("[Modules][WeatherBus] Republishing a library a shader still holds works") {
	// Dropping our reference does not free the path: a Shader that includes the library keeps the
	// resource alive, so it stays in ResourceCache and the name stays squatted. Republishing would
	// then either fail or hand out stale code, and neither says why.
	const String name = "test_weather_bus_retained";
	const String path = EngineShaderLib::publish(name, "// first\n");
	REQUIRE_FALSE(path.is_empty());

	// Stand in for a live shader holding the include.
	Ref<ShaderInclude> retained = ResourceLoader::load(path);
	REQUIRE(retained.is_valid());

	EngineShaderLib::unpublish(name);
	CHECK_FALSE_MESSAGE(ResourceLoader::exists(path),
			"The path is still taken even though the library was unpublished.");

	const String republished = EngineShaderLib::publish(name, "// second\n");
	CHECK_MESSAGE(!republished.is_empty(), "Could not republish over a retained library.");

	Ref<ShaderInclude> fresh = ResourceLoader::load(path);
	REQUIRE(fresh.is_valid());
	CHECK_MESSAGE(fresh->get_code().contains("second"), "Republish handed back the old code.");
	// The old holder keeps working, it just no longer owns the name.
	CHECK(retained.is_valid());

	EngineShaderLib::unpublish(name);
}

TEST_CASE("[Modules][WeatherBus] The singleton cannot be duplicated from C++ either") {
	// GDREGISTER_ABSTRACT_CLASS stops scripts; the private constructor plus this factory is what
	// stops C++. A second instance would be a second writer onto the same uniforms.
	ERR_PRINT_OFF;
	WeatherBus *second = WeatherBus::create_singleton();
	ERR_PRINT_ON;

	CHECK_MESSAGE(second == nullptr, "A second weather bus was created.");
	CHECK(WeatherBus::get_singleton() != nullptr);
}

} // namespace TestWeatherBus
