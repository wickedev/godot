/**************************************************************************/
/*  weather_bus.cpp                                                       */
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

#include "weather_bus.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "servers/rendering/rendering_server.h"

WeatherBus *WeatherBus::singleton = nullptr;

namespace {

struct UniformDecl {
	const char *name;
	RSE::GlobalShaderParameterType type;
};

// The uniform contract. Keep in sync with modules/weather_bus/README.md and with
// the accessors in engine://shaderlib/weather.gdshaderinc -- all three are one agreement, and the
// shader library is what most consumers actually read.
constexpr UniformDecl UNIFORMS[] = {
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

void push(const StringName &p_name, const Variant &p_value) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		return; // Headless tooling with no rendering server; state is still readable from C++.
	}
	rs->global_shader_parameter_set(p_name, p_value);
}

} // namespace

void WeatherBus::register_uniforms() {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	WeatherBus *bus = get_singleton();
	ERR_FAIL_NULL(bus);

	for (const UniformDecl &decl : UNIFORMS) {
		const StringName name = StringName(decl.name);
		if (bus->owned_uniforms.has(name)) {
			continue; // Already ours.
		}
		// Modules initialize before main.cpp calls global_shader_parameters_load_settings(), so
		// claiming a name the project also declares would make that later call fail as a duplicate.
		// Yield instead: a project that declares one of these names means to own it. This is also the
		// only collision detectable in an exported game -- see below.
		if (ProjectSettings::get_singleton() != nullptr &&
				ProjectSettings::get_singleton()->has_setting(String("shader_globals/") + decl.name)) {
			continue;
		}

		// Every way of asking the rendering server whether a global uniform exists --
		// global_shader_parameter_get(), _get_type() and _get_list() alike -- ERR_FAILs outside the
		// editor, on both the RD and GLES3 backends. There is no production-safe existence query, so
		// this does not rely on one. Ownership of the tod_/wind_/wetness_/weather_ prefixes is a
		// declared invariant of this module rather than something discovered at runtime, and
		// idempotence comes from `owned_uniforms`. The check below catches a fork-internal name
		// collision while someone is working on it; it is not what makes this correct.
		if (Engine::get_singleton()->is_editor_hint() &&
				rs->global_shader_parameter_get_type(name) != RSE::GLOBAL_VAR_TYPE_MAX) {
			WARN_PRINT(vformat("Global shader uniform '%s' is reserved by the weather bus but is "
							   "already declared elsewhere; the bus will not drive it.",
					String(name)));
			continue;
		}

		rs->global_shader_parameter_add(name, decl.type, bus->_value_of(decl.name));
		bus->owned_uniforms.insert(name);
	}
}

void WeatherBus::unregister_uniforms() {
	RenderingServer *rs = RenderingServer::get_singleton();
	WeatherBus *bus = get_singleton();
	if (rs == nullptr || bus == nullptr) {
		return;
	}
	// Only what we added. Removing a name the project declared, or one another subsystem owns,
	// would break whoever does own it.
	for (const StringName &name : bus->owned_uniforms) {
		rs->global_shader_parameter_remove(name);
	}
	bus->owned_uniforms.clear();
}

Variant WeatherBus::_value_of(const StringName &p_name) const {
	// Registration defaults have to be the values this object already holds, or the shaders start
	// out disagreeing with the C++ side until every setter has been called once.
	if (p_name == SNAME("tod_time")) {
		return tod_time;
	} else if (p_name == SNAME("tod_sun_direction")) {
		return tod_sun_direction;
	} else if (p_name == SNAME("tod_moon_direction")) {
		return tod_moon_direction;
	} else if (p_name == SNAME("tod_sun_intensity")) {
		return tod_sun_intensity;
	} else if (p_name == SNAME("wind_direction")) {
		return wind_direction;
	} else if (p_name == SNAME("wind_speed")) {
		return wind_speed;
	} else if (p_name == SNAME("wind_gust")) {
		return wind_gust;
	} else if (p_name == SNAME("wind_turbulence")) {
		return wind_turbulence;
	} else if (p_name == SNAME("wetness_amount")) {
		return wetness_amount;
	} else if (p_name == SNAME("wetness_porosity")) {
		return wetness_porosity;
	} else if (p_name == SNAME("weather_rain_intensity")) {
		return weather_rain_intensity;
	} else if (p_name == SNAME("weather_snow_intensity")) {
		return weather_snow_intensity;
	} else if (p_name == SNAME("weather_temperature")) {
		return weather_temperature;
	} else if (p_name == SNAME("weather_fog_density")) {
		return weather_fog_density;
	}
	ERR_FAIL_V_MSG(Variant(), vformat("Unknown weather bus uniform '%s'.", p_name));
}

void WeatherBus::publish_all() const {
	for (const UniformDecl &decl : UNIFORMS) {
		push(StringName(decl.name), _value_of(decl.name));
	}
}

#define WEATHER_BUS_SETTER(m_field, m_uniform) \
	m_field = p_value; \
	push(SNAME(m_uniform), m_field);

void WeatherBus::set_tod_time(real_t p_value) {
	WEATHER_BUS_SETTER(tod_time, "tod_time");
}

void WeatherBus::set_tod_sun_direction(const Vector3 &p_value) {
	WEATHER_BUS_SETTER(tod_sun_direction, "tod_sun_direction");
}

void WeatherBus::set_tod_moon_direction(const Vector3 &p_value) {
	WEATHER_BUS_SETTER(tod_moon_direction, "tod_moon_direction");
}

void WeatherBus::set_tod_sun_intensity(real_t p_value) {
	WEATHER_BUS_SETTER(tod_sun_intensity, "tod_sun_intensity");
}

void WeatherBus::set_wind_direction(const Vector3 &p_value) {
	WEATHER_BUS_SETTER(wind_direction, "wind_direction");
}

void WeatherBus::set_wind_speed(real_t p_value) {
	WEATHER_BUS_SETTER(wind_speed, "wind_speed");
}

void WeatherBus::set_wind_gust(real_t p_value) {
	WEATHER_BUS_SETTER(wind_gust, "wind_gust");
}

void WeatherBus::set_wind_turbulence(real_t p_value) {
	WEATHER_BUS_SETTER(wind_turbulence, "wind_turbulence");
}

void WeatherBus::set_wetness_amount(real_t p_value) {
	WEATHER_BUS_SETTER(wetness_amount, "wetness_amount");
}

void WeatherBus::set_wetness_porosity(real_t p_value) {
	WEATHER_BUS_SETTER(wetness_porosity, "wetness_porosity");
}

void WeatherBus::set_weather_rain_intensity(real_t p_value) {
	WEATHER_BUS_SETTER(weather_rain_intensity, "weather_rain_intensity");
}

void WeatherBus::set_weather_snow_intensity(real_t p_value) {
	WEATHER_BUS_SETTER(weather_snow_intensity, "weather_snow_intensity");
}

void WeatherBus::set_weather_temperature(real_t p_value) {
	WEATHER_BUS_SETTER(weather_temperature, "weather_temperature");
}

void WeatherBus::set_weather_fog_density(real_t p_value) {
	WEATHER_BUS_SETTER(weather_fog_density, "weather_fog_density");
}

#undef WEATHER_BUS_SETTER

void WeatherBus::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tod_time", "time"), &WeatherBus::set_tod_time);
	ClassDB::bind_method(D_METHOD("get_tod_time"), &WeatherBus::get_tod_time);
	ClassDB::bind_method(D_METHOD("set_tod_sun_direction", "direction"), &WeatherBus::set_tod_sun_direction);
	ClassDB::bind_method(D_METHOD("get_tod_sun_direction"), &WeatherBus::get_tod_sun_direction);
	ClassDB::bind_method(D_METHOD("set_tod_moon_direction", "direction"), &WeatherBus::set_tod_moon_direction);
	ClassDB::bind_method(D_METHOD("get_tod_moon_direction"), &WeatherBus::get_tod_moon_direction);
	ClassDB::bind_method(D_METHOD("set_tod_sun_intensity", "intensity"), &WeatherBus::set_tod_sun_intensity);
	ClassDB::bind_method(D_METHOD("get_tod_sun_intensity"), &WeatherBus::get_tod_sun_intensity);

	ClassDB::bind_method(D_METHOD("set_wind_direction", "direction"), &WeatherBus::set_wind_direction);
	ClassDB::bind_method(D_METHOD("get_wind_direction"), &WeatherBus::get_wind_direction);
	ClassDB::bind_method(D_METHOD("set_wind_speed", "speed"), &WeatherBus::set_wind_speed);
	ClassDB::bind_method(D_METHOD("get_wind_speed"), &WeatherBus::get_wind_speed);
	ClassDB::bind_method(D_METHOD("set_wind_gust", "gust"), &WeatherBus::set_wind_gust);
	ClassDB::bind_method(D_METHOD("get_wind_gust"), &WeatherBus::get_wind_gust);
	ClassDB::bind_method(D_METHOD("set_wind_turbulence", "turbulence"), &WeatherBus::set_wind_turbulence);
	ClassDB::bind_method(D_METHOD("get_wind_turbulence"), &WeatherBus::get_wind_turbulence);
	ClassDB::bind_method(D_METHOD("get_wind_velocity"), &WeatherBus::get_wind_velocity);

	ClassDB::bind_method(D_METHOD("set_wetness_amount", "amount"), &WeatherBus::set_wetness_amount);
	ClassDB::bind_method(D_METHOD("get_wetness_amount"), &WeatherBus::get_wetness_amount);
	ClassDB::bind_method(D_METHOD("set_wetness_porosity", "porosity"), &WeatherBus::set_wetness_porosity);
	ClassDB::bind_method(D_METHOD("get_wetness_porosity"), &WeatherBus::get_wetness_porosity);

	ClassDB::bind_method(D_METHOD("set_weather_rain_intensity", "intensity"), &WeatherBus::set_weather_rain_intensity);
	ClassDB::bind_method(D_METHOD("get_weather_rain_intensity"), &WeatherBus::get_weather_rain_intensity);
	ClassDB::bind_method(D_METHOD("set_weather_snow_intensity", "intensity"), &WeatherBus::set_weather_snow_intensity);
	ClassDB::bind_method(D_METHOD("get_weather_snow_intensity"), &WeatherBus::get_weather_snow_intensity);
	ClassDB::bind_method(D_METHOD("set_weather_temperature", "celsius"), &WeatherBus::set_weather_temperature);
	ClassDB::bind_method(D_METHOD("get_weather_temperature"), &WeatherBus::get_weather_temperature);
	ClassDB::bind_method(D_METHOD("set_weather_fog_density", "density"), &WeatherBus::set_weather_fog_density);
	ClassDB::bind_method(D_METHOD("get_weather_fog_density"), &WeatherBus::get_weather_fog_density);

	ADD_GROUP("Time of Day", "tod_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tod_time", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_tod_time", "get_tod_time");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "tod_sun_direction"), "set_tod_sun_direction", "get_tod_sun_direction");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "tod_moon_direction"), "set_tod_moon_direction", "get_tod_moon_direction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tod_sun_intensity"), "set_tod_sun_intensity", "get_tod_sun_intensity");

	ADD_GROUP("Wind", "wind_");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind_direction"), "set_wind_direction", "get_wind_direction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_speed", PROPERTY_HINT_NONE, "suffix:m/s"), "set_wind_speed", "get_wind_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_gust"), "set_wind_gust", "get_wind_gust");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_turbulence", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_wind_turbulence", "get_wind_turbulence");

	ADD_GROUP("Wetness", "wetness_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetness_amount", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_wetness_amount", "get_wetness_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetness_porosity", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_wetness_porosity", "get_wetness_porosity");

	ADD_GROUP("Weather", "weather_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weather_rain_intensity", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_weather_rain_intensity", "get_weather_rain_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weather_snow_intensity", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_weather_snow_intensity", "get_weather_snow_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weather_temperature", PROPERTY_HINT_NONE, "suffix:°C"), "set_weather_temperature", "get_weather_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weather_fog_density", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_weather_fog_density", "get_weather_fog_density");
}

WeatherBus *WeatherBus::create_singleton() {
	ERR_FAIL_COND_V_MSG(singleton != nullptr, nullptr, "The weather bus singleton already exists.");
	return memnew(WeatherBus);
}

WeatherBus::WeatherBus() {
	singleton = this;
}

WeatherBus::~WeatherBus() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
