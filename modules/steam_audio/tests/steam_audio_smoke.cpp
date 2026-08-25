/**************************************************************************/
/*  steam_audio_smoke.cpp                                                    */
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

#include "steam_audio_smoke.h"

#include "core/math/math_funcs.h"

#include "phonon.h"

namespace SteamAudioSmoke {

struct ContextScope {
	IPLContext context = nullptr;
	ContextScope() {
		IPLContextSettings settings = {};
		settings.version = STEAMAUDIO_VERSION;
		iplContextCreate(&settings, &context);
	}
	~ContextScope() {
		if (context) {
			iplContextRelease(&context);
		}
	}
};

bool context_roundtrip() {
	ContextScope scope;
	return scope.context != nullptr;
}

BinauralResult binaural_impulse() {
	BinauralResult result;
	ContextScope scope;
	if (!scope.context) {
		return result;
	}

	IPLAudioSettings audio_settings = {};
	audio_settings.samplingRate = 48000;
	audio_settings.frameSize = 512;

	IPLHRTFSettings hrtf_settings = {};
	hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
	hrtf_settings.volume = 1.0f;

	IPLHRTF hrtf = nullptr;
	result.hrtf_created = iplHRTFCreate(scope.context, &audio_settings, &hrtf_settings, &hrtf) == IPL_STATUS_SUCCESS;
	if (!result.hrtf_created) {
		return result;
	}

	IPLBinauralEffectSettings effect_settings = {};
	effect_settings.hrtf = hrtf;
	IPLBinauralEffect effect = nullptr;
	result.effect_created = iplBinauralEffectCreate(scope.context, &audio_settings, &effect_settings, &effect) == IPL_STATUS_SUCCESS;
	if (result.effect_created) {
		IPLAudioBuffer in_buffer = {};
		IPLAudioBuffer out_buffer = {};
		if (iplAudioBufferAllocate(scope.context, 1, audio_settings.frameSize, &in_buffer) == IPL_STATUS_SUCCESS &&
				iplAudioBufferAllocate(scope.context, 2, audio_settings.frameSize, &out_buffer) == IPL_STATUS_SUCCESS) {
			in_buffer.data[0][0] = 1.0f; // Impulse.

			IPLBinauralEffectParams params = {};
			params.direction = IPLVector3{ 1.0f, 0.0f, 0.0f };
			params.interpolation = IPL_HRTFINTERPOLATION_NEAREST;
			params.spatialBlend = 1.0f;
			params.hrtf = hrtf;
			iplBinauralEffectApply(effect, &params, &in_buffer, &out_buffer);

			result.all_finite = true;
			for (int ch = 0; ch < 2; ch++) {
				for (int i = 0; i < audio_settings.frameSize; i++) {
					const float v = out_buffer.data[ch][i];
					result.all_finite = result.all_finite && Math::is_finite(v);
					result.any_nonzero = result.any_nonzero || v != 0.0f;
				}
			}
		}
		iplAudioBufferFree(scope.context, &in_buffer);
		iplAudioBufferFree(scope.context, &out_buffer);
		iplBinauralEffectRelease(&effect);
	}
	iplHRTFRelease(&hrtf);
	return result;
}

bool simulator_roundtrip() {
	ContextScope scope;
	if (!scope.context) {
		return false;
	}
	IPLSimulationSettings sim_settings = {};
	// REFLECTIONS spawns the simulator thread pool — DIRECT alone does not,
	// and the review requires the pool setup/teardown to actually run.
	sim_settings.flags = (IPLSimulationFlags)(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
	sim_settings.sceneType = IPL_SCENETYPE_DEFAULT;
	sim_settings.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	sim_settings.maxNumRays = 1024;
	sim_settings.numDiffuseSamples = 32;
	sim_settings.maxDuration = 1.0f;
	sim_settings.maxOrder = 1;
	sim_settings.maxNumSources = 4;
	sim_settings.numThreads = 2;
	sim_settings.samplingRate = 48000;
	sim_settings.frameSize = 512;

	IPLSimulator simulator = nullptr;
	if (iplSimulatorCreate(scope.context, &sim_settings, &simulator) != IPL_STATUS_SUCCESS) {
		return false;
	}
	iplSimulatorRelease(&simulator);
	return simulator == nullptr;
}

bool boundary_rejects_invalid() {
	IPLContext bad_context = nullptr;
	IPLContextSettings bad_settings = {};
	bad_settings.version = 0x00010000; // Incompatible version (guard-return path).
	const bool rejected = iplContextCreate(&bad_settings, &bad_context) != IPL_STATUS_SUCCESS && bad_context == nullptr;
	// Null-handle calls on wrapped entry points must return defaults, not crash.
	const bool null_safe = iplProbeArrayGetNumProbes(nullptr) == 0;
	return rejected && null_safe;
}

bool boundary_catches_internal_throw() {
	// Deterministic fault injection: a test-only seam (patches/0001) inside
	// iplContextRetain's guarded region throws a foreign std::runtime_error --
	// not an ipl::Exception, so no internal handler can convert it -- which the
	// hardened catch(...) boundary must turn into the null-sentinel return
	// instead of letting it unwind into the exceptions-disabled engine.
	// (The guard-return cases above never reach the catch.)
	ContextScope scope;
	if (!scope.context) {
		return false;
	}
	arm_boundary_fault_hook();
	IPLContext faulted = iplContextRetain(scope.context);
	disarm_boundary_fault_hook();
	if (faulted != nullptr) {
		// The hook did not fire; balance the refcount and fail loudly rather
		// than report a catch path that never ran.
		iplContextRelease(&faulted);
		return false;
	}
	// With the hook cleared the very same call must work again: the boundary
	// converted the exception without corrupting the context.
	IPLContext again = iplContextRetain(scope.context);
	const bool intact = again == scope.context;
	if (again != nullptr) {
		iplContextRelease(&again);
	}
	return intact;
}

} // namespace SteamAudioSmoke
