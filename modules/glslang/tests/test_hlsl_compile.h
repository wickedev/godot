/**************************************************************************/
/*  test_hlsl_compile.h                                                   */
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

#include "../shader_compile.h"

#include "tests/test_macros.h"

namespace TestHLSLCompile {

// SPIR-V starts with a magic number, in either endianness depending on the producer.
static bool looks_like_spirv(const Vector<uint8_t> &p_bytes) {
	if (p_bytes.size() < 4 || (p_bytes.size() % 4) != 0) {
		return false;
	}
	uint32_t magic = 0;
	memcpy(&magic, p_bytes.ptr(), sizeof(uint32_t));
	return magic == 0x07230203 || magic == 0x03022307;
}

static Vector<uint8_t> compile(const String &p_source, String *r_error) {
	return compile_hlsl_shader(RenderingDeviceCommons::SHADER_STAGE_COMPUTE, p_source, "main",
			RenderingDeviceCommons::SHADER_LANGUAGE_VULKAN_VERSION_1_1,
			RenderingDeviceCommons::SHADER_SPIRV_VERSION_1_3, r_error);
}

TEST_CASE("[Modules][GLSLang] HLSL compute shaders compile to SPIR-V") {
	// Every construct the vendored Jolt hair kernels rely on, in one shader: [numthreads],
	// SV_DispatchThreadID, StructuredBuffer, RWStructuredBuffer and InterlockedAdd. If glslang's
	// HLSL frontend stops being vendored or ENABLE_HLSL stops being defined, this is what says so.
	const String source =
			"struct Item { float4 mValue; };\n"
			"StructuredBuffer<Item> gInput;\n"
			"RWStructuredBuffer<int4> gAccum;\n"
			"[numthreads(64, 1, 1)]\n"
			"void main(uint3 tid : SV_DispatchThreadID) {\n"
			"    float4 v = gInput[tid.x].mValue;\n"
			"    InterlockedAdd(gAccum[0].x, (int)round(v.x));\n"
			"    gAccum[tid.x].y = (int)v.y;\n"
			"}\n";

	String error;
	const Vector<uint8_t> spirv = compile(source, &error);

	REQUIRE_MESSAGE(!spirv.is_empty(), "HLSL compilation failed: ", error);
	CHECK(error.is_empty());
	CHECK_MESSAGE(looks_like_spirv(spirv), "Output is not SPIR-V.");
}

TEST_CASE("[Modules][GLSLang] Multiple HLSL resources get distinct bindings") {
	// HLSL carries no binding decorations, so without setAutoMapBindings() plus the link-time
	// mapIO() pass every resource lands on binding 0. That produces SPIR-V which looks fine here
	// and fails validation at pipeline creation, far from the cause.
	const String source =
			"RWStructuredBuffer<int> gA;\n"
			"RWStructuredBuffer<int> gB;\n"
			"RWStructuredBuffer<int> gC;\n"
			"[numthreads(1, 1, 1)]\n"
			"void main(uint3 tid : SV_DispatchThreadID) {\n"
			"    gC[tid.x] = gA[tid.x] + gB[tid.x];\n"
			"}\n";

	String error;
	const Vector<uint8_t> spirv = compile(source, &error);
	REQUIRE_MESSAGE(!spirv.is_empty(), "HLSL compilation failed: ", error);

	// Walk the SPIR-V for OpDecorate ... Binding and collect the values.
	const uint32_t *words = reinterpret_cast<const uint32_t *>(spirv.ptr());
	const uint32_t word_count = spirv.size() / sizeof(uint32_t);
	HashSet<uint32_t> bindings;
	uint32_t i = 5; // Skip the header.
	while (i < word_count) {
		const uint32_t opcode = words[i] & 0xFFFF;
		const uint32_t length = words[i] >> 16;
		if (length == 0) {
			break;
		}
		// OpDecorate = 71, Decoration Binding = 33.
		if (opcode == 71 && length >= 4 && words[i + 2] == 33) {
			bindings.insert(words[i + 3]);
		}
		i += length;
	}

	CHECK_MESSAGE(bindings.size() >= 3,
			"Three buffers should get three distinct bindings; found ", bindings.size());
}

TEST_CASE("[Modules][GLSLang] Broken HLSL reports an error instead of empty SPIR-V") {
	String error;
	const Vector<uint8_t> spirv = compile("this is not a shader\n", &error);

	CHECK(spirv.is_empty());
	CHECK_MESSAGE(!error.is_empty(), "A failed compile has to say why.");
}

TEST_CASE("[Modules][GLSLang] GLSL still compiles after the HLSL frontend was enabled") {
	// ENABLE_HLSL changes what glslang's headers declare, so the existing path is worth a check.
	const String source =
			"#version 450\n"
			"layout(local_size_x = 1) in;\n"
			"layout(set = 0, binding = 0, std430) buffer Out { int value; } gOut;\n"
			"void main() { gOut.value = 1; }\n";

	String error;
	const Vector<uint8_t> spirv = compile_glslang_shader(
			RenderingDeviceCommons::SHADER_STAGE_COMPUTE, source,
			RenderingDeviceCommons::SHADER_LANGUAGE_VULKAN_VERSION_1_1,
			RenderingDeviceCommons::SHADER_SPIRV_VERSION_1_3, &error);

	REQUIRE_MESSAGE(!spirv.is_empty(), "GLSL compilation failed: ", error);
	CHECK(looks_like_spirv(spirv));
}

} // namespace TestHLSLCompile
