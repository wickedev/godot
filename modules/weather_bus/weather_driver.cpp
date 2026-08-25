/**************************************************************************/
/*  weather_driver.cpp                                                    */
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

#include "weather_driver.h"

#include "weather_bus.h"

void WeatherDriver::_blend() {
	auto precipitation_of = [](const Ref<WeatherPreset> &p_preset) {
		return p_preset.is_valid() ? p_preset->get_precipitation() : real_t(0.0);
	};
	auto fog_of = [](const Ref<WeatherPreset> &p_preset) {
		return p_preset.is_valid() ? p_preset->get_fog_density() : real_t(0.0);
	};
	auto wetness_of = [](const Ref<WeatherPreset> &p_preset) {
		return p_preset.is_valid() ? p_preset->get_wetness_target() : real_t(0.0);
	};

	if (target_preset.is_null()) {
		blended_precipitation = precipitation_of(current_preset);
		blended_fog_density = fog_of(current_preset);
		blended_wetness_target = wetness_of(current_preset);
		return;
	}

	const real_t t = get_transition_progress();
	blended_precipitation = Math::lerp(precipitation_of(current_preset), precipitation_of(target_preset), t);
	blended_fog_density = Math::lerp(fog_of(current_preset), fog_of(target_preset), t);
	blended_wetness_target = Math::lerp(wetness_of(current_preset), wetness_of(target_preset), t);
}

real_t WeatherDriver::_snow_fraction() const {
	// Read rather than assume: whatever owns temperature -- a survival sim, a biome map -- has
	// already put it on the bus.
	const WeatherBus *bus = WeatherBus::get_singleton();
	const real_t current = bus != nullptr ? bus->get_weather_temperature() : temperature;

	if (snow_temperature_band <= 0.0) {
		return current <= snow_temperature ? real_t(1.0) : real_t(0.0);
	}
	// Fully snow at band below the threshold, fully rain at band above, sleet in between.
	const real_t t = (current - (snow_temperature - snow_temperature_band)) / (2.0 * snow_temperature_band);
	return 1.0 - CLAMP(t, real_t(0.0), real_t(1.0));
}

real_t WeatherDriver::get_rain_intensity() const {
	return blended_precipitation * (1.0 - _snow_fraction());
}

real_t WeatherDriver::get_snow_intensity() const {
	return blended_precipitation * _snow_fraction();
}

void WeatherDriver::_publish() const {
	WeatherBus *bus = WeatherBus::get_singleton();
	if (bus == nullptr) {
		return;
	}
	bus->set_weather_rain_intensity(get_rain_intensity());
	bus->set_weather_snow_intensity(get_snow_intensity());
	bus->set_weather_fog_density(blended_fog_density);
	bus->set_wetness_amount(wetness);
	bus->set_wetness_porosity(wetness_porosity);

	if (publish_temperature) {
		bus->set_weather_temperature(temperature);
	}
}

real_t WeatherDriver::get_transition_progress() const {
	if (target_preset.is_null()) {
		return 1.0;
	}
	if (transition_duration <= 0.0) {
		return 1.0;
	}
	return real_t(CLAMP(transition_elapsed / transition_duration, 0.0, 1.0));
}

bool WeatherDriver::is_transitioning() const {
	return target_preset.is_valid() && get_transition_progress() < 1.0;
}

void WeatherDriver::transition_to(const Ref<WeatherPreset> &p_preset, double p_duration) {
	if (p_preset.is_null()) {
		target_preset = Ref<WeatherPreset>();
		return;
	}

	// Starting a new transition mid-flight has to begin from what is actually on screen, not from
	// the preset the last one started at, or the weather jumps backwards before moving on.
	if (is_transitioning()) {
		Ref<WeatherPreset> snapshot;
		snapshot.instantiate();
		snapshot->set_precipitation(blended_precipitation);
		snapshot->set_fog_density(blended_fog_density);
		snapshot->set_wetness_target(blended_wetness_target);
		current_preset = snapshot;
	}

	target_preset = p_preset;
	transition_duration = MAX(p_duration, 0.0);
	transition_elapsed = 0.0;

	if (transition_duration <= 0.0) {
		set_preset_immediate(p_preset);
		return;
	}

	_blend();
	_publish();
}

void WeatherDriver::set_preset_immediate(const Ref<WeatherPreset> &p_preset) {
	current_preset = p_preset;
	target_preset = Ref<WeatherPreset>();
	transition_duration = 0.0;
	transition_elapsed = 0.0;
	_blend();
	_publish();
}

void WeatherDriver::advance(double p_delta) {
	if (target_preset.is_valid() && transition_duration > 0.0) {
		transition_elapsed = MIN(transition_elapsed + p_delta, transition_duration);
		if (transition_elapsed >= transition_duration) {
			// Landed: fold the target in so a later transition starts from here.
			current_preset = target_preset;
			target_preset = Ref<WeatherPreset>();
			transition_duration = 0.0;
			transition_elapsed = 0.0;
		}
	}

	_blend();

	// Asymmetric on purpose. Wetting and drying are not the same process at the same speed, and a
	// single rate makes puddles disappear the moment a shower stops.
	const real_t rate = wetness < blended_wetness_target ? wetting_rate : drying_rate;
	const real_t step = rate * real_t(p_delta);
	if (Math::abs(blended_wetness_target - wetness) <= step) {
		wetness = blended_wetness_target;
	} else {
		wetness += (blended_wetness_target > wetness ? step : -step);
	}
	wetness = CLAMP(wetness, real_t(0.0), real_t(1.0));

	_publish();
}

void WeatherDriver::set_wetting_rate(real_t p_rate) {
	wetting_rate = MAX(p_rate, real_t(0.0));
}

void WeatherDriver::set_drying_rate(real_t p_rate) {
	drying_rate = MAX(p_rate, real_t(0.0));
}

void WeatherDriver::set_wetness(real_t p_wetness) {
	wetness = CLAMP(p_wetness, real_t(0.0), real_t(1.0));
	_publish();
}

void WeatherDriver::set_wetness_porosity(real_t p_porosity) {
	wetness_porosity = CLAMP(p_porosity, real_t(0.0), real_t(1.0));
	_publish();
}

void WeatherDriver::set_snow_temperature(real_t p_celsius) {
	snow_temperature = p_celsius;
	_publish();
}

void WeatherDriver::set_snow_temperature_band(real_t p_degrees) {
	snow_temperature_band = MAX(p_degrees, real_t(0.0));
	_publish();
}

void WeatherDriver::set_publish_temperature(bool p_publish) {
	publish_temperature = p_publish;
	if (publish_temperature) {
		_publish();
	}
}

void WeatherDriver::set_temperature(real_t p_celsius) {
	temperature = p_celsius;
	if (publish_temperature) {
		_publish();
	}
}

void WeatherDriver::set_paused(bool p_paused) {
	paused = p_paused;
}

void WeatherDriver::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process_internal(true);
			_blend();
			_publish();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!paused) {
				advance(get_process_delta_time());
			}
		} break;
	}
}

void WeatherDriver::_bind_methods() {
	ClassDB::bind_method(D_METHOD("transition_to", "preset", "duration"), &WeatherDriver::transition_to);
	ClassDB::bind_method(D_METHOD("set_preset_immediate", "preset"), &WeatherDriver::set_preset_immediate);
	ClassDB::bind_method(D_METHOD("get_current_preset"), &WeatherDriver::get_current_preset);
	ClassDB::bind_method(D_METHOD("get_target_preset"), &WeatherDriver::get_target_preset);
	ClassDB::bind_method(D_METHOD("get_transition_progress"), &WeatherDriver::get_transition_progress);
	ClassDB::bind_method(D_METHOD("is_transitioning"), &WeatherDriver::is_transitioning);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &WeatherDriver::advance);

	ClassDB::bind_method(D_METHOD("set_wetting_rate", "rate"), &WeatherDriver::set_wetting_rate);
	ClassDB::bind_method(D_METHOD("get_wetting_rate"), &WeatherDriver::get_wetting_rate);
	ClassDB::bind_method(D_METHOD("set_drying_rate", "rate"), &WeatherDriver::set_drying_rate);
	ClassDB::bind_method(D_METHOD("get_drying_rate"), &WeatherDriver::get_drying_rate);
	ClassDB::bind_method(D_METHOD("set_wetness", "wetness"), &WeatherDriver::set_wetness);
	ClassDB::bind_method(D_METHOD("get_wetness"), &WeatherDriver::get_wetness);
	ClassDB::bind_method(D_METHOD("set_wetness_porosity", "porosity"), &WeatherDriver::set_wetness_porosity);
	ClassDB::bind_method(D_METHOD("get_wetness_porosity"), &WeatherDriver::get_wetness_porosity);

	ClassDB::bind_method(D_METHOD("set_snow_temperature", "celsius"), &WeatherDriver::set_snow_temperature);
	ClassDB::bind_method(D_METHOD("get_snow_temperature"), &WeatherDriver::get_snow_temperature);
	ClassDB::bind_method(D_METHOD("set_snow_temperature_band", "degrees"), &WeatherDriver::set_snow_temperature_band);
	ClassDB::bind_method(D_METHOD("get_snow_temperature_band"), &WeatherDriver::get_snow_temperature_band);

	ClassDB::bind_method(D_METHOD("set_publish_temperature", "publish"), &WeatherDriver::set_publish_temperature);
	ClassDB::bind_method(D_METHOD("is_publishing_temperature"), &WeatherDriver::is_publishing_temperature);
	ClassDB::bind_method(D_METHOD("set_temperature", "celsius"), &WeatherDriver::set_temperature);
	ClassDB::bind_method(D_METHOD("get_temperature"), &WeatherDriver::get_temperature);

	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &WeatherDriver::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &WeatherDriver::is_paused);

	ClassDB::bind_method(D_METHOD("get_rain_intensity"), &WeatherDriver::get_rain_intensity);
	ClassDB::bind_method(D_METHOD("get_snow_intensity"), &WeatherDriver::get_snow_intensity);
	ClassDB::bind_method(D_METHOD("get_fog_density"), &WeatherDriver::get_fog_density);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_paused", "is_paused");

	ADD_GROUP("Wetness", "wetness_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_wetness", "get_wetness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetness_porosity", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_wetness_porosity", "get_wetness_porosity");

	ADD_GROUP("Rates", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetting_rate", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:1/s"), "set_wetting_rate", "get_wetting_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drying_rate", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:1/s"), "set_drying_rate", "get_drying_rate");

	ADD_GROUP("Snow", "snow_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snow_temperature", PROPERTY_HINT_NONE, "suffix:°C"), "set_snow_temperature", "get_snow_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snow_temperature_band", PROPERTY_HINT_RANGE, "0,20,0.1,suffix:°C"), "set_snow_temperature_band", "get_snow_temperature_band");

	ADD_GROUP("Temperature", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "publish_temperature"), "set_publish_temperature", "is_publishing_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temperature", PROPERTY_HINT_NONE, "suffix:°C"), "set_temperature", "get_temperature");
}

WeatherDriver::WeatherDriver() {
	_blend();
}
