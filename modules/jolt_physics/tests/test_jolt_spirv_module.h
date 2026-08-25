/**************************************************************************/
/*  test_jolt_spirv_module.h                                              */
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

#include "../shaders/jolt_spirv_module.h"

#include "core/crypto/crypto_core.h"
#include "tests/test_macros.h"

namespace TestJoltSpirVModule {

// Synthetic modules rather than the committed artifacts. A real .spv exercises exactly one path --
// the one where everything is fine -- while every case worth guarding against is a module that is
// wrong in a specific way, and those have to be built by hand.
static Vector<uint8_t> make_module(uint32_t p_magic, uint32_t p_version, int p_extra_words) {
	Vector<uint8_t> bytes;
	auto push_word = [&bytes](uint32_t w) {
		for (int i = 0; i < 4; ++i) {
			bytes.push_back((w >> (i * 8)) & 0xFF);
		}
	};
	push_word(p_magic);
	push_word(p_version);
	push_word(0x00080001); // Generator.
	push_word(64); // Bound.
	push_word(0); // Reserved.
	for (int i = 0; i < p_extra_words; ++i) {
		push_word(0x0002003B); // Filler instruction word.
	}
	return bytes;
}

static uint32_t spirv_version(uint32_t p_major, uint32_t p_minor) {
	return (p_major << 16) | (p_minor << 8);
}

static String sha256_of(const Vector<uint8_t> &p_bytes) {
	unsigned char digest[32];
	CryptoCore::SHA256Context ctx;
	ctx.start();
	ctx.update(p_bytes.ptr(), p_bytes.size());
	ctx.finish(digest);
	String hex;
	for (int i = 0; i < 32; ++i) {
		hex += String::num_uint64(digest[i], 16).lpad(2, "0");
	}
	return hex;
}

TEST_CASE("[Modules][Jolt] A well-formed SPIR-V module validates") {
	const Vector<uint8_t> module = make_module(0x07230203, spirv_version(1, 3), 4);
	CHECK(JoltSpirV::validate(module.span(), String()) == JoltSpirV::Status::OK);

	const JoltSpirV::Header header = JoltSpirV::read_header(module.span());
	CHECK(header.version_major == 1);
	CHECK(header.version_minor == 3);
	CHECK(header.bound == 64);
}

TEST_CASE("[Modules][Jolt] Malformed SPIR-V is rejected with the reason") {
	// Each of these is a way a committed artifact can go wrong between being built and being used,
	// and each has to be distinguishable -- an error that says only "invalid" sends whoever hits it
	// looking in the wrong place.
	CHECK(JoltSpirV::validate(Span<uint8_t>(), String()) == JoltSpirV::Status::EMPTY);

	// A bad merge or a truncated write leaves a length that is not a whole number of words.
	Vector<uint8_t> truncated = make_module(0x07230203, spirv_version(1, 3), 1);
	truncated.resize(truncated.size() - 1);
	CHECK(JoltSpirV::validate(truncated.span(), String()) == JoltSpirV::Status::NOT_WORD_ALIGNED);

	// Something that is not SPIR-V at all.
	const Vector<uint8_t> not_spirv = make_module(0xDEADBEEF, spirv_version(1, 3), 2);
	CHECK(JoltSpirV::validate(not_spirv.span(), String()) == JoltSpirV::Status::BAD_MAGIC);

	// Byte-swapped SPIR-V is legal but nothing here swaps it back, so it must not pass.
	const Vector<uint8_t> swapped = make_module(0x03022307, spirv_version(1, 3), 2);
	CHECK(JoltSpirV::validate(swapped.span(), String()) == JoltSpirV::Status::BAD_MAGIC);

	// Right magic, but the header never finishes.
	Vector<uint8_t> short_header = make_module(0x07230203, spirv_version(1, 3), 0);
	short_header.resize(12);
	CHECK(JoltSpirV::validate(short_header.span(), String()) == JoltSpirV::Status::TOO_SHORT);

	// Built by a newer toolchain than the pins describe.
	const Vector<uint8_t> too_new = make_module(0x07230203, spirv_version(1, 6), 2);
	CHECK(JoltSpirV::validate(too_new.span(), String()) == JoltSpirV::Status::UNSUPPORTED_VERSION);

	// An older module is a subset, so it is fine.
	const Vector<uint8_t> older = make_module(0x07230203, spirv_version(1, 0), 2);
	CHECK(JoltSpirV::validate(older.span(), String()) == JoltSpirV::Status::OK);
}

TEST_CASE("[Modules][Jolt] A stale module is caught by its hash") {
	// The case the format checks cannot see. A kernel whose source moved on still compiles to
	// perfectly valid SPIR-V -- just not the SPIR-V that source produces any more. Only the hash
	// recorded next to it says so.
	const Vector<uint8_t> module = make_module(0x07230203, spirv_version(1, 3), 4);
	const String correct = sha256_of(module);

	CHECK(JoltSpirV::validate(module.span(), correct) == JoltSpirV::Status::OK);
	CHECK(JoltSpirV::validate(module.span(), correct.to_upper()) == JoltSpirV::Status::OK);

	// Same shape, different contents: valid SPIR-V, wrong module.
	const Vector<uint8_t> other = make_module(0x07230203, spirv_version(1, 3), 6);
	CHECK(JoltSpirV::validate(other.span(), correct) == JoltSpirV::Status::HASH_MISMATCH);

	// An empty expectation skips the check rather than failing it.
	CHECK(JoltSpirV::validate(other.span(), String()) == JoltSpirV::Status::OK);
}

TEST_CASE("[Modules][Jolt] Every status explains itself") {
	// These strings end up in the message a developer sees when a kernel will not load, so an
	// unhandled enum value showing up as "unknown" would be worse than useless.
	const JoltSpirV::Status all[] = {
		JoltSpirV::Status::OK, JoltSpirV::Status::EMPTY, JoltSpirV::Status::NOT_WORD_ALIGNED,
		JoltSpirV::Status::BAD_MAGIC, JoltSpirV::Status::TOO_SHORT,
		JoltSpirV::Status::UNSUPPORTED_VERSION, JoltSpirV::Status::HASH_MISMATCH
	};
	for (const JoltSpirV::Status status : all) {
		const String message = JoltSpirV::status_message(status);
		CHECK_FALSE(message.is_empty());
		CHECK_MESSAGE(message != "unknown", "A status has no message of its own.");
	}
}

} // namespace TestJoltSpirVModule
