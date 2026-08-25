/**************************************************************************/
/*  weather_preset.cpp                                                    */
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

#include "weather_preset.h"

void WeatherPreset::set_precipitation(real_t p_amount) {
	precipitation = CLAMP(p_amount, real_t(0.0), real_t(1.0));
	emit_changed();
}

void WeatherPreset::set_fog_density(real_t p_density) {
	fog_density = CLAMP(p_density, real_t(0.0), real_t(1.0));
	emit_changed();
}

void WeatherPreset::set_wetness_target(real_t p_target) {
	wetness_target = CLAMP(p_target, real_t(0.0), real_t(1.0));
	emit_changed();
}

void WeatherPreset::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_precipitation", "amount"), &WeatherPreset::set_precipitation);
	ClassDB::bind_method(D_METHOD("get_precipitation"), &WeatherPreset::get_precipitation);
	ClassDB::bind_method(D_METHOD("set_fog_density", "density"), &WeatherPreset::set_fog_density);
	ClassDB::bind_method(D_METHOD("get_fog_density"), &WeatherPreset::get_fog_density);
	ClassDB::bind_method(D_METHOD("set_wetness_target", "target"), &WeatherPreset::set_wetness_target);
	ClassDB::bind_method(D_METHOD("get_wetness_target"), &WeatherPreset::get_wetness_target);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "precipitation", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_precipitation", "get_precipitation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_density", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_fog_density", "get_fog_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetness_target", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_wetness_target", "get_wetness_target");
}
