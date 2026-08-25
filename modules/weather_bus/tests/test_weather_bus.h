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
#include "scene/resources/shader.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_preprocessor.h"
#include "servers/rendering/shader_types.h"
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
	REQUIRE_OR_RETURN(include.is_valid());
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

TEST_CASE("[Modules][WeatherBus] A reserved name declared elsewhere disables the whole bus") {
	// The prefixes are reserved, so a collision is a configuration error rather than something to
	// share around. Registering the rest anyway would leave the bus writing to a name the project
	// owns -- clobbering its value and putting the C++ and shader sides out of step -- which is
	// exactly what per-name skipping used to do.
	ProjectSettings *settings = ProjectSettings::get_singleton();
	RenderingServer *rs = RenderingServer::get_singleton();
	REQUIRE(settings != nullptr);
	REQUIRE(rs != nullptr);

	WeatherBus *bus = WeatherBus::get_singleton();
	WeatherBus::unregister_uniforms();

	Dictionary declared;
	declared["type"] = "float";
	declared["value"] = 3.5;
	settings->set_setting("shader_globals/wetness_amount", declared);

	ERR_PRINT_OFF;
	WeatherBus::register_uniforms();
	ERR_PRINT_ON;

	CHECK_MESSAGE(!bus->is_active(), "A conflicting name should disable the bus.");
	CHECK_MESSAGE(rs->global_shader_parameter_get_type("wetness_porosity") == RSE::GLOBAL_VAR_TYPE_MAX,
			"Nothing should have been registered once a conflict was found.");

	// And it must stay silent: writing would overwrite whatever owns the name.
	bus->set_wetness_amount(0.9);
	bus->publish_all();
	CHECK_MESSAGE(rs->global_shader_parameter_get_type("wetness_amount") == RSE::GLOBAL_VAR_TYPE_MAX,
			"The disabled bus registered a uniform through a setter.");
	// The C++ side keeps working, so CPU consumers are unaffected.
	CHECK(bus->get_wetness_amount() == doctest::Approx(0.9));

	// Teardown must not touch anything either -- it owns nothing.
	WeatherBus::unregister_uniforms();

	settings->set_setting("shader_globals/wetness_amount", Variant());
	WeatherBus::register_uniforms();
	CHECK(bus->is_active());
	CHECK(rs->global_shader_parameter_get_type("wetness_amount") == RSE::GLOBAL_VAR_TYPE_FLOAT);
	bus->set_wetness_amount(0.0);
}

TEST_CASE("[Modules][WeatherBus] Teardown removes nothing when registration stood down") {
	// The bug this guards: recording ownership of a name whose add() may have failed, then deleting
	// it on teardown. Ownership is all-or-nothing now, so a stood-down registration owns nothing.
	RenderingServer *rs = RenderingServer::get_singleton();
	ProjectSettings *settings = ProjectSettings::get_singleton();

	WeatherBus::unregister_uniforms();

	// Stand in for a subsystem that got here first with a reserved name.
	rs->global_shader_parameter_add("wind_speed", RSE::GLOBAL_VAR_TYPE_FLOAT, 42.0);

	Dictionary declared;
	declared["type"] = "float";
	declared["value"] = 1.0;
	settings->set_setting("shader_globals/wind_speed", declared);

	ERR_PRINT_OFF;
	WeatherBus::register_uniforms();
	WeatherBus::unregister_uniforms();
	ERR_PRINT_ON;

	CHECK_MESSAGE(rs->global_shader_parameter_get_type("wind_speed") == RSE::GLOBAL_VAR_TYPE_FLOAT,
			"Teardown deleted a uniform the bus never registered.");

	rs->global_shader_parameter_remove("wind_speed");
	settings->set_setting("shader_globals/wind_speed", Variant());
	WeatherBus::register_uniforms();
	CHECK(WeatherBus::get_singleton()->is_active());
}

TEST_CASE("[Modules][WeatherBus] A name taken first is detected without the editor") {
	// The case the editor-only queries cannot see, and the reason the renderer grew
	// global_shader_parameter_try_add(): a subsystem registers a reserved name before the bus, in a
	// build where nothing can be asked about it. Without an acquisition result the bus would record
	// ownership of a name it never got, and teardown would delete someone else's uniform.
	RenderingServer *rs = RenderingServer::get_singleton();
	REQUIRE(rs != nullptr);
	REQUIRE_FALSE(Engine::get_singleton()->is_editor_hint());

	WeatherBus *bus = WeatherBus::get_singleton();
	WeatherBus::unregister_uniforms();

	// Not in ProjectSettings, so the pre-check cannot see it either. Late in the contract, so the
	// bus has already acquired several names by the time it hits this one.
	rs->global_shader_parameter_add("weather_fog_density", RSE::GLOBAL_VAR_TYPE_FLOAT, 0.75);

	ERR_PRINT_OFF;
	WeatherBus::register_uniforms();
	ERR_PRINT_ON;

	CHECK_MESSAGE(!bus->is_active(), "The bus should stand down when a name was taken.");
	CHECK_MESSAGE(rs->global_shader_parameter_get_type("tod_time") == RSE::GLOBAL_VAR_TYPE_MAX,
			"Names acquired before the conflict should have been handed back.");
	CHECK_MESSAGE(rs->global_shader_parameter_get_type("weather_fog_density") == RSE::GLOBAL_VAR_TYPE_FLOAT,
			"The bus removed the uniform that was already there.");

	// And teardown owns nothing, so it cannot take the foreign one with it.
	WeatherBus::unregister_uniforms();
	CHECK(rs->global_shader_parameter_get_type("weather_fog_density") == RSE::GLOBAL_VAR_TYPE_FLOAT);

	rs->global_shader_parameter_remove("weather_fog_density");
	WeatherBus::register_uniforms();
	CHECK(bus->is_active());
	CHECK(rs->global_shader_parameter_get_type("tod_time") == RSE::GLOBAL_VAR_TYPE_FLOAT);
}

TEST_CASE("[Modules][WeatherBus] try_add reports acquisition instead of erroring") {
	// The contract the bus depends on: a duplicate is an answer, not a failure. The void form keeps
	// erroring, so existing callers are unaffected.
	RenderingServer *rs = RenderingServer::get_singleton();
	const StringName name = "test_weather_bus_try_add";

	CHECK(rs->global_shader_parameter_try_add(name, RSE::GLOBAL_VAR_TYPE_FLOAT, 1.0));
	CHECK_MESSAGE(!rs->global_shader_parameter_try_add(name, RSE::GLOBAL_VAR_TYPE_FLOAT, 2.0),
			"A second try_add on the same name should report false.");
	CHECK(rs->global_shader_parameter_get_type(name) == RSE::GLOBAL_VAR_TYPE_FLOAT);

	rs->global_shader_parameter_remove(name);
	CHECK(rs->global_shader_parameter_try_add(name, RSE::GLOBAL_VAR_TYPE_VEC3, Vector3()));
	rs->global_shader_parameter_remove(name);
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
	REQUIRE_OR_RETURN(retained.is_valid());

	EngineShaderLib::unpublish(name);
	CHECK_FALSE_MESSAGE(ResourceLoader::exists(path),
			"The path is still taken even though the library was unpublished.");

	const String republished = EngineShaderLib::publish(name, "// second\n");
	CHECK_MESSAGE(!republished.is_empty(), "Could not republish over a retained library.");

	Ref<ShaderInclude> fresh = ResourceLoader::load(path);
	REQUIRE_OR_RETURN(fresh.is_valid());
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

namespace {

// Mirrors ShaderCompiler::_get_global_shader_uniform_type(), which is private to the renderer.
ShaderLanguage::DataType weather_test_global_uniform_type(const StringName &p_name) {
	const RSE::GlobalShaderParameterType type = RenderingServer::get_singleton()->global_shader_parameter_get_type(p_name);
	return (ShaderLanguage::DataType)RenderingServer::global_shader_uniform_type_get_shader_datatype(type);
}

// Preprocesses and compiles `p_code` as a spatial shader with global uniform validation on.
Error weather_test_compile(const String &p_code, String *r_error) {
	ShaderPreprocessor preprocessor;
	String expanded;
	String preprocess_error;
	const Error pre = preprocessor.preprocess(p_code, "test_weather_bus.gdshader", expanded, &preprocess_error);
	if (pre != OK) {
		*r_error = preprocess_error;
		return pre;
	}

	ShaderLanguage::ShaderCompileInfo info;
	info.functions = ShaderTypes::get_singleton()->get_functions(RSE::SHADER_SPATIAL);
	info.render_modes = ShaderTypes::get_singleton()->get_modes(RSE::SHADER_SPATIAL);
	info.stencil_modes = ShaderTypes::get_singleton()->get_stencil_modes(RSE::SHADER_SPATIAL);
	info.shader_types = ShaderTypes::get_singleton()->get_types();
	info.global_shader_uniform_type_func = weather_test_global_uniform_type;

	ShaderLanguage parser;
	const Error err = parser.compile(expanded, info);
	*r_error = parser.get_error_text();
	return err;
}

// ShaderLanguage only validates global uniforms when it believes it is in the editor. Tests run
// with the hint off, so it has to be turned on around the compile and put back.
struct EditorHintScope {
	bool previous = false;
	EditorHintScope() {
		previous = Engine::get_singleton()->is_editor_hint();
		Engine::get_singleton()->set_editor_hint(true);
	}
	~EditorHintScope() { Engine::get_singleton()->set_editor_hint(previous); }
};

} // namespace

TEST_CASE("[Modules][WeatherBus] The debug shader compiles against the registered uniforms") {
	// The check the rest of this file could not make. Everything else here asserts that the C++
	// side registered and stored what it meant to; none of it can tell whether a shader reading
	// those names gets anything. A typo in the shader library, or a float declared where the
	// contract registered a vec3, produces a shader that reads zero in silence.
	//
	// Compiling with validation on resolves every `global uniform` declaration against what the
	// rendering server actually holds, by name and by type. That is a mechanical check of the
	// agreement, not a restatement of it.
	REQUIRE(WeatherBus::get_singleton()->is_active());

	Ref<Shader> debug = ResourceLoader::load(EngineShaderLib::make_shader_path("weather_debug"));
	REQUIRE_MESSAGE(debug.is_valid(), "The debug consumer shader was not published.");

	EditorHintScope hint;
	String error;
	const Error err = weather_test_compile(debug->get_code(), &error);
	CHECK_MESSAGE(err == OK, "The debug shader does not agree with the registered uniforms: ", error);
}

TEST_CASE("[Modules][WeatherBus] A shader disagreeing with the contract fails to compile") {
	// Guards the guard. If validation were off -- it is gated on the editor hint, and it is off by
	// default in tests -- the check above would pass no matter what the shader said.
	EditorHintScope hint;
	String error;

	// Right name, wrong type: the contract registers wind_speed as a float.
	const Error wrong_type = weather_test_compile(
			"shader_type spatial;\nglobal uniform vec3 wind_speed;\nvoid fragment() { ALBEDO = wind_speed; }\n",
			&error);
	CHECK_MESSAGE(wrong_type != OK, "A type mismatch compiled anyway; validation is not running.");

	// A name nothing registered, which is what a typo looks like.
	const Error typo = weather_test_compile(
			"shader_type spatial;\nglobal uniform float wnid_speed;\nvoid fragment() { ROUGHNESS = wnid_speed; }\n",
			&error);
	CHECK_MESSAGE(typo != OK, "An unregistered name compiled anyway.");

	// And the correct spelling still works, so the failures above are about the mismatch.
	const Error correct = weather_test_compile(
			"shader_type spatial;\nglobal uniform float wind_speed;\nvoid fragment() { ROUGHNESS = wind_speed; }\n",
			&error);
	CHECK_MESSAGE(correct == OK, "The correct declaration failed: ", error);
}

} // namespace TestWeatherBus
