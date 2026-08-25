/**************************************************************************/
/*  jolt_spirv_module.h                                                   */
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

#include "core/string/ustring.h"
#include "core/templates/span.h"

// Checks that a blob really is the SPIR-V module it is supposed to be, before anything hands it to
// a driver.
//
// The kernels are compiled offline and their bytes are committed, so the usual protection -- the
// compiler runs and fails -- is gone. What is left is a blob that could be truncated by a bad
// merge, replaced by a stale copy, or built for a target this build cannot consume, and a driver
// handed any of those either rejects it with a message pointing at the driver or crashes. Checking
// here means the error names the kernel and the reason.
namespace JoltSpirV {

enum class Status {
	OK,
	EMPTY, // Nothing there at all -- usually a build that produced no artifact.
	NOT_WORD_ALIGNED, // SPIR-V is a stream of 32-bit words; a leftover byte means truncation.
	BAD_MAGIC, // Not SPIR-V, or byte-swapped from a different endianness.
	TOO_SHORT, // Has the magic but not a full five-word header.
	UNSUPPORTED_VERSION, // Built for a SPIR-V version this build does not accept.
	HASH_MISMATCH, // Byte-for-byte different from what the manifest recorded.
};

// Human-readable reason, for error messages. Never null.
const char *status_message(Status p_status);

struct Header {
	uint32_t version_major = 0;
	uint32_t version_minor = 0;
	uint32_t generator = 0;
	uint32_t bound = 0;
};

// Reads the five-word header. Only meaningful when validate() returned OK.
Header read_header(Span<uint8_t> p_bytes);

// `p_expected_sha256` is the lowercase hex digest the manifest recorded for this kernel; pass an
// empty string to skip the comparison. The hash is what catches a stale artifact: a kernel whose
// source moved on still has valid SPIR-V, just not the SPIR-V that source produces.
Status validate(Span<uint8_t> p_bytes, const String &p_expected_sha256);

} // namespace JoltSpirV
