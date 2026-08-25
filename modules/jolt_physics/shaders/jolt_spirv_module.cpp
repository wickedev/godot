/**************************************************************************/
/*  jolt_spirv_module.cpp                                                 */
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

#include "jolt_spirv_module.h"

#include "core/crypto/crypto_core.h"

namespace JoltSpirV {

namespace {

constexpr uint32_t MAGIC = 0x07230203;
// The same word with the bytes reversed. Recognizing it lets the error say "wrong endianness"
// instead of "not SPIR-V", which is a much shorter path to the cause.
constexpr uint32_t MAGIC_REVERSED = 0x03022307;
constexpr size_t HEADER_WORDS = 5;

// The kernels are built with -fspv-target-env=vulkan1.1, which emits SPIR-V 1.3. Accepting older
// modules is fine -- they are a subset -- but a newer one would mean the committed artifact came
// from a toolchain this build does not expect, which is the situation the pinning exists to catch.
constexpr uint32_t MAX_VERSION_MAJOR = 1;
constexpr uint32_t MAX_VERSION_MINOR = 3;

uint32_t word_at(Span<uint8_t> p_bytes, size_t p_index) {
	uint32_t word = 0;
	memcpy(&word, p_bytes.ptr() + p_index * sizeof(uint32_t), sizeof(uint32_t));
	return word;
}

} // namespace

const char *status_message(Status p_status) {
	switch (p_status) {
		case Status::OK:
			return "valid";
		case Status::EMPTY:
			return "the module is empty";
		case Status::NOT_WORD_ALIGNED:
			return "the module length is not a multiple of four bytes, so it is truncated";
		case Status::BAD_MAGIC:
			return "the module does not start with the SPIR-V magic number";
		case Status::TOO_SHORT:
			return "the module is shorter than a SPIR-V header";
		case Status::UNSUPPORTED_VERSION:
			return "the module was built for a newer SPIR-V version than this build accepts";
		case Status::HASH_MISMATCH:
			return "the module does not match the hash recorded for it, so it is stale or edited";
	}
	return "unknown";
}

Header read_header(Span<uint8_t> p_bytes) {
	Header header;
	if (p_bytes.size() < HEADER_WORDS * sizeof(uint32_t)) {
		return header;
	}
	const uint32_t version = word_at(p_bytes, 1);
	header.version_major = (version >> 16) & 0xFF;
	header.version_minor = (version >> 8) & 0xFF;
	header.generator = word_at(p_bytes, 2);
	header.bound = word_at(p_bytes, 3);
	return header;
}

Status validate(Span<uint8_t> p_bytes, const String &p_expected_sha256) {
	if (p_bytes.is_empty()) {
		return Status::EMPTY;
	}
	if ((p_bytes.size() % sizeof(uint32_t)) != 0) {
		return Status::NOT_WORD_ALIGNED;
	}
	// Magic before length: a blob that is not SPIR-V at all should say so rather than complain
	// about its header size.
	const uint32_t magic = word_at(p_bytes, 0);
	if (magic != MAGIC && magic != MAGIC_REVERSED) {
		return Status::BAD_MAGIC;
	}
	if (p_bytes.size() < HEADER_WORDS * sizeof(uint32_t)) {
		return Status::TOO_SHORT;
	}
	if (magic == MAGIC_REVERSED) {
		// Byte-swapped modules are legal SPIR-V but nothing here byte-swaps them back, so treating
		// one as consumable would hand the driver nonsense.
		return Status::BAD_MAGIC;
	}

	const Header header = read_header(p_bytes);
	if (header.version_major > MAX_VERSION_MAJOR ||
			(header.version_major == MAX_VERSION_MAJOR && header.version_minor > MAX_VERSION_MINOR)) {
		return Status::UNSUPPORTED_VERSION;
	}

	if (!p_expected_sha256.is_empty()) {
		unsigned char digest[32];
		CryptoCore::SHA256Context ctx;
		ctx.start();
		ctx.update(p_bytes.ptr(), p_bytes.size());
		ctx.finish(digest);

		String hex;
		for (int i = 0; i < 32; ++i) {
			hex += String::num_uint64(digest[i], 16).lpad(2, "0");
		}
		if (hex != p_expected_sha256.to_lower()) {
			return Status::HASH_MISMATCH;
		}
	}

	return Status::OK;
}

} // namespace JoltSpirV
