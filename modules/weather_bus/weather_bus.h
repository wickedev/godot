/**************************************************************************/
/*  weather_bus.h                                                         */
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

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

// Canonical world-state bus.
//
// One state (time of day, wind, wetness, precipitation) has to reach every material in the world.
// Godot already has a transport for exactly that -- the global shader uniform buffer is bound to
// spatial, particles, sky, fog and canvas_item shaders alike -- so this class owns the state and
// pushes it there, rather than inventing a second channel.
//
// Setters write straight through to the global uniform. That costs a buffer write plus a dirty-page
// mark (MaterialStorage::global_shader_parameter_set), and only dirty pages upload each frame, so
// there is no per-frame tick here and no reason to rate-limit callers.
//
// C++ getters exist because not every consumer is a shader: the Jolt hair solver, ocean buoyancy and
// Area3D wind all run on the CPU and need the same numbers the shaders see.
//
// On ownership: for values that are also gameplay state -- `temperature` most obviously -- this bus
// is a MIRROR, not the source of truth. Whatever simulates weather owns the value and pushes it
// here. Two writers racing to own a value on the bus is a bug this class cannot detect.
//
// Threading: main thread only. The setters are plain member writes with no synchronization, and
// RenderingServer's global uniform calls carry the same expectation. CPU consumers that run off the
// main thread -- a physics step on a worker, say -- must be handed a copy taken on the main thread
// rather than calling the getters themselves.
class WeatherBus : public Object {
	GDCLASS(WeatherBus, Object);

	static WeatherBus *singleton;

	// Only the uniforms this object actually registered. Names the project declared, or that
	// another subsystem owns, are not ours to remove on teardown.
	HashSet<StringName> owned_uniforms;

	// Direction conventions differ per domain on purpose, because each follows its own field's
	// convention and unifying them would be the more surprising choice. They are spelled out at
	// every declaration, in the shader library, and in the uniform contract table.
	real_t tod_time = 0.0;
	Vector3 tod_sun_direction = Vector3(0, 1, 0); // Points TOWARD the sun.
	Vector3 tod_moon_direction = Vector3(0, -1, 0); // Points TOWARD the moon.
	real_t tod_sun_intensity = 1.0;

	Vector3 wind_direction = Vector3(1, 0, 0); // Direction the wind blows TOWARD.
	real_t wind_speed = 0.0;
	real_t wind_gust = 1.0;
	real_t wind_turbulence = 0.0;

	real_t wetness_amount = 0.0;
	real_t wetness_porosity = 0.0;

	real_t weather_rain_intensity = 0.0;
	real_t weather_snow_intensity = 0.0;
	real_t weather_temperature = 20.0;
	real_t weather_fog_density = 0.0;

	Variant _value_of(const StringName &p_name) const;

protected:
	static void _bind_methods();

public:
	static WeatherBus *get_singleton() { return singleton; }

	// Declares the contract's uniforms on the rendering server, seeded with this object's current
	// values. Idempotent, and yields to any name already declared -- by the project under
	// `shader_globals/`, or by anything else -- so the two never collide.
	static void register_uniforms();
	// Removes only the uniforms register_uniforms() actually added.
	static void unregister_uniforms();

	// Pushes every value to its uniform. Registration already does this; call it after changing
	// state through something other than the setters.
	void publish_all() const;

	void set_tod_time(real_t p_time);
	real_t get_tod_time() const { return tod_time; }
	void set_tod_sun_direction(const Vector3 &p_direction);
	Vector3 get_tod_sun_direction() const { return tod_sun_direction; }
	void set_tod_moon_direction(const Vector3 &p_direction);
	Vector3 get_tod_moon_direction() const { return tod_moon_direction; }
	void set_tod_sun_intensity(real_t p_intensity);
	real_t get_tod_sun_intensity() const { return tod_sun_intensity; }

	void set_wind_direction(const Vector3 &p_direction);
	Vector3 get_wind_direction() const { return wind_direction; }
	void set_wind_speed(real_t p_speed);
	real_t get_wind_speed() const { return wind_speed; }
	void set_wind_gust(real_t p_gust);
	real_t get_wind_gust() const { return wind_gust; }
	void set_wind_turbulence(real_t p_turbulence);
	real_t get_wind_turbulence() const { return wind_turbulence; }

	// Convenience for CPU consumers: the wind vector shaders end up reconstructing, in m/s.
	Vector3 get_wind_velocity() const { return wind_direction * (wind_speed * wind_gust); }

	void set_wetness_amount(real_t p_amount);
	real_t get_wetness_amount() const { return wetness_amount; }
	void set_wetness_porosity(real_t p_porosity);
	real_t get_wetness_porosity() const { return wetness_porosity; }

	void set_weather_rain_intensity(real_t p_intensity);
	real_t get_weather_rain_intensity() const { return weather_rain_intensity; }
	void set_weather_snow_intensity(real_t p_intensity);
	real_t get_weather_snow_intensity() const { return weather_snow_intensity; }
	void set_weather_temperature(real_t p_celsius);
	real_t get_weather_temperature() const { return weather_temperature; }
	void set_weather_fog_density(real_t p_density);
	real_t get_weather_fog_density() const { return weather_fog_density; }

	// The only way to make one. The constructor is private so C++ cannot casually produce a second
	// writer either -- GDREGISTER_ABSTRACT_CLASS only stops scripts.
	static WeatherBus *create_singleton();

	~WeatherBus();

private:
	WeatherBus();
};
