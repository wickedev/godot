/**************************************************************************/
/*  weather_preset.h                                                      */
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

#include "core/io/resource.h"
#include "core/object/class_db.h"

// One named weather condition -- clear, overcast, rain, storm -- as a set of targets for
// WeatherDriver to blend toward.
//
// Deliberately small. Anything a preset could plausibly carry but that something else already owns
// stays out: wind belongs to WindDriver, time of day to TimeOfDay, and temperature to whatever
// simulates it. A preset that also set the wind would give the world two writers for one value.
class WeatherPreset : public Resource {
	GDCLASS(WeatherPreset, Resource);

	real_t precipitation = 0.0;
	real_t fog_density = 0.0;
	real_t wetness_target = 0.0;

protected:
	static void _bind_methods();

public:
	void set_precipitation(real_t p_amount);
	real_t get_precipitation() const { return precipitation; }

	void set_fog_density(real_t p_density);
	real_t get_fog_density() const { return fog_density; }

	void set_wetness_target(real_t p_target);
	real_t get_wetness_target() const { return wetness_target; }
};
