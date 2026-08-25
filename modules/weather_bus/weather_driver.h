/**************************************************************************/
/*  weather_driver.h                                                      */
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

#include "weather_preset.h"

#include "scene/main/node.h"

// Drives the precipitation and wetness halves of the weather bus.
//
// Blends between WeatherPreset resources over a transition, then publishes the result. Same
// producer layer as TimeOfDay and WindDriver: the bus stays transport, this owns the state.
//
// Temperature is an input, not an output. WeatherBus documents itself as a mirror for values that
// are also gameplay state, and temperature is the clearest case -- a survival system or a biome map
// is likely to own it. So this reads temperature off the bus to decide how much of the
// precipitation falls as snow, and only writes it back if `publish_temperature` is set, which it is
// not by default. Turning that on when something else also writes temperature gives one value two
// writers, which is a bug nothing here can detect.
//
// Wetness lags rather than tracking precipitation directly. Ground soaks in seconds and dries in
// minutes, and the two rates are separate for that reason: driving wetness straight off rain makes
// puddles vanish the instant a shower stops.
class WeatherDriver : public Node {
	GDCLASS(WeatherDriver, Node);

	Ref<WeatherPreset> current_preset;
	Ref<WeatherPreset> target_preset;

	double transition_duration = 0.0;
	double transition_elapsed = 0.0;

	real_t wetting_rate = 0.2; // Per second, toward the target.
	real_t drying_rate = 0.02; // Deliberately far slower.
	real_t wetness = 0.0;
	real_t wetness_porosity = 0.35;

	// Precipitation is one number; temperature decides how much of it is snow. The band gives a
	// sleet range instead of a hard switch at freezing.
	real_t snow_temperature = 0.0;
	real_t snow_temperature_band = 2.0;

	bool publish_temperature = false;
	real_t temperature = 20.0;

	bool paused = false;

	real_t blended_precipitation = 0.0;
	real_t blended_fog_density = 0.0;
	real_t blended_wetness_target = 0.0;

	void _blend();
	void _publish() const;
	real_t _snow_fraction() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Blends to `p_preset` over `p_duration` seconds. A duration of 0 snaps. Passing null clears
	// the target and holds whatever is currently blended.
	void transition_to(const Ref<WeatherPreset> &p_preset, double p_duration);
	// Drops any transition and adopts `p_preset` outright.
	void set_preset_immediate(const Ref<WeatherPreset> &p_preset);

	Ref<WeatherPreset> get_current_preset() const { return current_preset; }
	Ref<WeatherPreset> get_target_preset() const { return target_preset; }
	// 0 while a transition is starting, 1 once it has finished.
	real_t get_transition_progress() const;
	bool is_transitioning() const;

	void set_wetting_rate(real_t p_rate);
	real_t get_wetting_rate() const { return wetting_rate; }
	void set_drying_rate(real_t p_rate);
	real_t get_drying_rate() const { return drying_rate; }
	void set_wetness(real_t p_wetness);
	real_t get_wetness() const { return wetness; }
	void set_wetness_porosity(real_t p_porosity);
	real_t get_wetness_porosity() const { return wetness_porosity; }

	void set_snow_temperature(real_t p_celsius);
	real_t get_snow_temperature() const { return snow_temperature; }
	void set_snow_temperature_band(real_t p_degrees);
	real_t get_snow_temperature_band() const { return snow_temperature_band; }

	void set_publish_temperature(bool p_publish);
	bool is_publishing_temperature() const { return publish_temperature; }
	void set_temperature(real_t p_celsius);
	real_t get_temperature() const { return temperature; }

	void set_paused(bool p_paused);
	bool is_paused() const { return paused; }

	// Current split of the blended precipitation, after the temperature decision.
	real_t get_rain_intensity() const;
	real_t get_snow_intensity() const;
	real_t get_fog_density() const { return blended_fog_density; }

	// Advances the transition and the wetness, then republishes. Runs every frame while in the
	// tree; exposed so a fixed step can be driven deterministically.
	void advance(double p_delta);

	WeatherDriver();
};
