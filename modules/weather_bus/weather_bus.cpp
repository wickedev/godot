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
	const WeatherBus *bus = WeatherBus::get_singleton();
	if (bus == nullptr || !bus->is_active()) {
		// Disabled by a name conflict. The C++ side keeps working -- CPU consumers still read the
		// getters -- but writing would clobber whoever actually owns the uniform.
		return;
	}
	rs->global_shader_parameter_set(p_name, p_value);
}

} // namespace

void WeatherBus::register_uniforms() {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	WeatherBus *bus = get_singleton();
	ERR_FAIL_NULL(bus);

	if (!bus->owned_uniforms.is_empty()) {
		return; // Already registered.
	}

	// All of the contract or none of it.
	//
	// Every existence query the rendering server offers ERR_FAILs outside the editor, so an exported
	// build cannot ask whether a name is free beforehand. Claiming ownership per name regardless
	// would mean claiming names that might belong to someone else, and teardown would delete them.
	//
	// So ownership is recorded from the result of acquiring each name, not from having asked for it
	// -- global_shader_parameter_try_add() reports whether the name was free. The contract is taken
	// atomically: if any name cannot be acquired, everything already taken is handed back and the
	// bus stays inactive. The tod_/wind_/wetness_/weather_ prefixes are reserved for this module,
	// so a conflict means the configuration is wrong rather than that the bus should share.
	Vector<String> conflicts;
	if (ProjectSettings::get_singleton() != nullptr) {
		for (const UniformDecl &decl : UNIFORMS) {
			if (ProjectSettings::get_singleton()->has_setting(String("shader_globals/") + decl.name)) {
				conflicts.push_back(decl.name);
			}
		}
	}

	// Editor only, because the query is editor only. This is a diagnostic: it names every conflict
	// at once instead of stopping at the first, which is friendlier while someone is fixing one.
	// Correctness does not depend on it -- the acquisition loop below handles the exported build,
	// where no query exists.
	if (Engine::get_singleton()->is_editor_hint()) {
		for (const UniformDecl &decl : UNIFORMS) {
			if (rs->global_shader_parameter_get_type(decl.name) != RSE::GLOBAL_VAR_TYPE_MAX &&
					!conflicts.has(decl.name)) {
				conflicts.push_back(decl.name);
			}
		}
	}

	if (!conflicts.is_empty()) {
		bus->inactive = true;
		ERR_FAIL_MSG(vformat(
				"Global shader uniform name(s) %s are reserved by the weather bus but are already "
				"declared. The weather bus is disabled: it will not register or write any uniform, "
				"so it cannot overwrite whatever owns them. Rename the conflicting declarations.",
				String(", ").join(conflicts)));
	}

	bus->inactive = false;
	for (const UniformDecl &decl : UNIFORMS) {
		const StringName name = StringName(decl.name);
		// try_add reports whether the name was actually acquired. Ownership is recorded from that
		// result rather than from having asked, which is what makes the rollback below sound in an
		// exported build -- where nothing else can tell us a name was already taken.
		if (rs->global_shader_parameter_try_add(name, decl.type, bus->_value_of(decl.name))) {
			bus->owned_uniforms.insert(name);
			continue;
		}

		// Someone got here first. Hand back everything acquired so far and stand down, so the bus
		// never half-owns the contract and never writes over whoever holds the rest.
		for (const StringName &acquired : bus->owned_uniforms) {
			rs->global_shader_parameter_remove(acquired);
		}
		bus->owned_uniforms.clear();
		bus->inactive = true;
		ERR_FAIL_MSG(vformat(
				"Global shader uniform '%s' is reserved by the weather bus but was already declared "
				"elsewhere. The weather bus is disabled: it registered nothing and will not write "
				"any uniform, so it cannot overwrite whatever owns them.",
				String(name)));
	}
}

void WeatherBus::unregister_uniforms() {
	RenderingServer *rs = RenderingServer::get_singleton();
	WeatherBus *bus = get_singleton();
	if (rs == nullptr || bus == nullptr) {
		return;
	}
	// Exactly what registration added, which is either the whole contract or nothing.
	for (const StringName &name : bus->owned_uniforms) {
		rs->global_shader_parameter_remove(name);
	}
	bus->owned_uniforms.clear();
	bus->inactive = false;
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
