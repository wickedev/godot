/**************************************************************************/
/*  fault_injection_support.cpp                                           */
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

// This TU is compiled with exceptions enabled (see SCsub): the throw below
// must be able to unwind into the vendored C API boundary's catch(...).
// Everything else in the module builds with the engine's no-exceptions flags.

#include <stdexcept>

// Test-only seam added to the vendored tree by patches/0001; invoked (when set)
// inside iplContextRetain's guarded region.
extern "C" void (*ipl_godot_boundary_fault_hook)();

namespace SteamAudioSmoke {

static void _throw_injected_fault() {
	throw std::runtime_error("injected boundary fault");
}

void arm_boundary_fault_hook() {
	ipl_godot_boundary_fault_hook = &_throw_injected_fault;
}

void disarm_boundary_fault_hook() {
	ipl_godot_boundary_fault_hook = nullptr;
}

} // namespace SteamAudioSmoke
