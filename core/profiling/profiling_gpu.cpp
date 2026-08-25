/**************************************************************************/
/*  profiling_gpu.cpp                                                     */
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

#include "core/profiling/profiling_gpu.h"

namespace godot_profiling {

namespace {
GpuBackend g_backend;
bool g_registered = false;
bool g_context_live = false;
} // namespace

void gpu_backend_register(const GpuBackend &p_backend) {
	g_backend = p_backend;
	g_registered = true;
}

bool gpu_backend_is_registered() {
	return g_registered;
}

void gpu_context_init(uint64_t p_physical_device, uint64_t p_device, uint64_t p_queue) {
	if (g_backend.context_init == nullptr || g_context_live) {
		return;
	}
	g_backend.context_init(p_physical_device, p_device, p_queue);
	g_context_live = true;
}

void gpu_collect(uint64_t p_command_buffer) {
	// Guarded on the context rather than the pointer: collecting before init or after
	// shutdown is a use-after-free in the backend, and the call site is a per-frame
	// hot path that should not have to track lifetime itself.
	if (g_backend.collect == nullptr || !g_context_live) {
		return;
	}
	g_backend.collect(p_command_buffer);
}

void gpu_context_shutdown() {
	if (g_backend.context_shutdown == nullptr || !g_context_live) {
		return;
	}
	g_backend.context_shutdown();
	g_context_live = false;
}

} // namespace godot_profiling
