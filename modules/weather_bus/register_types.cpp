/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "engine_shader_lib.h"
#include "time_of_day.h"
#include "weather_driver.h"
#include "weather_preset.h"
#include "weather_bus.h"
#include "wind_driver.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

// The shader-side half of the uniform contract. Consumers include this rather than declaring the
// globals themselves, so the spelling and the direction conventions live in exactly one place.
static const char *WEATHER_SHADER_LIB = R"SHADERLIB(
// Weather / world-state bus -- shader-side accessors.
//
// Published by modules/weather_bus. Include it with:
//     #include "engine://shaderlib/weather.gdshaderinc"
//
// Direction conventions differ per domain, following each field's own convention. Read them here
// rather than guessing:
//   tod_sun_direction  -- points TOWARD the sun  (lighting L-vector convention)
//   tod_moon_direction -- points TOWARD the moon
//   wind_direction     -- the direction the wind blows TOWARD (fluid convention)

global uniform float tod_time; // Normalized day fraction, [0,1).
global uniform vec3 tod_sun_direction; // Points TOWARD the sun.
global uniform vec3 tod_moon_direction; // Points TOWARD the moon.
global uniform float tod_sun_intensity; // 0 below the horizon.

global uniform vec3 wind_direction; // Direction the wind blows TOWARD.
global uniform float wind_speed; // Base speed, m/s, gust excluded.
global uniform float wind_gust; // Gust multiplier; 1.0 means no gust.
global uniform float wind_turbulence; // High-frequency jitter, [0,1].

global uniform float wetness_amount; // [0,1].
global uniform float wetness_porosity; // How far albedo darkens when wet, [0,1].

global uniform float weather_rain_intensity; // [0,1].
global uniform float weather_snow_intensity; // [0,1].
global uniform float weather_temperature; // Degrees Celsius.
global uniform float weather_fog_density; // [0,1].

// Wind velocity in m/s, gust applied. Matches WeatherBus::get_wind_velocity() on the CPU side.
vec3 weather_wind_velocity() {
	return wind_direction * (wind_speed * wind_gust);
}

// Sway offset for foliage. `stiffness` is 0 for grass and approaches 1 for trunks; `phase`
// decorrelates neighboring instances, so feed it something stable per instance such as the
// world-space origin. `seconds` should be a monotonic clock -- pass the shader's TIME builtin.
//
// Time is a parameter rather than being read from tod_time, which wraps at midnight: driving the
// animation from it would make every plant in the world jerk once a day.
vec3 weather_wind_sway(vec3 world_position, float stiffness, float phase, float seconds) {
	float amplitude = wind_speed * wind_gust * (1.0 - clamp(stiffness, 0.0, 1.0));
	float t = seconds + phase;
	float wave = sin(t * 1.7 + dot(world_position.xz, vec2(0.35, 0.21)));
	float jitter = sin(t * 9.1 + phase * 3.0) * wind_turbulence;
	return wind_direction * (amplitude * (wave + jitter) * 0.05);
}

// Darkens and smooths a surface as it wets. `porosity_scale` lets a material opt out (0.0) or
// exaggerate the effect; most materials pass 1.0.
void weather_apply_wetness(inout vec3 albedo, inout float roughness, float porosity_scale) {
	float wet = clamp(wetness_amount * porosity_scale, 0.0, 1.0);
	albedo *= mix(1.0, 1.0 - wetness_porosity, wet);
	roughness = mix(roughness, roughness * 0.2, wet);
}
)SHADERLIB";

void initialize_weather_bus_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// Abstract so scripts cannot construct a second one. The singleton is the only writer, and a
	// stray WeatherBus.new() would hand out an object whose setters push to the same uniforms.
	GDREGISTER_ABSTRACT_CLASS(WeatherBus);
	// The producers are ordinary nodes: a scene can hold several, or none.
	GDREGISTER_CLASS(TimeOfDay);
	GDREGISTER_CLASS(WindDriver);
	GDREGISTER_CLASS(WeatherDriver);
	GDREGISTER_CLASS(WeatherPreset);
	Engine::get_singleton()->add_singleton(Engine::Singleton("WeatherBus", WeatherBus::create_singleton()));

	WeatherBus::register_uniforms();
	EngineShaderLib::publish("weather", WEATHER_SHADER_LIB);
}

void uninitialize_weather_bus_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// Only our own entry: other subsystems publish here too.
	EngineShaderLib::unpublish("weather");
	WeatherBus::unregister_uniforms();

	Engine::get_singleton()->remove_singleton("WeatherBus");
	if (WeatherBus::get_singleton() != nullptr) {
		memdelete(WeatherBus::get_singleton());
	}
}
