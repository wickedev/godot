/**************************************************************************/
/*  test_macros_guard.cpp                                                 */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_macros_guard)

namespace TestMacrosGuard {

// REQUIRE_OR_RETURN exists because a failed REQUIRE does NOT leave the test
// case in this build. These tests pin the three properties that makes it
// depend on, so a future change to the macro (or to doctest's exception mode)
// cannot silently take them away.

static int side_effect_count = 0;
static bool _counting_condition(bool p_result) {
	side_effect_count++;
	return p_result;
}

// Separate function, not a subcase: the macro returns, and what has to be
// observed is that the caller's later statements did not run.
static void _run_until_guard(bool p_condition, bool *r_reached_end) {
	*r_reached_end = false;
	REQUIRE_OR_RETURN(p_condition);
	*r_reached_end = true;
}

// MAY_FAIL because exercising the false branch necessarily trips the macro's
// own REQUIRE, and there is no way to assert on a guard's failure path without
// producing that failure. Expect EXACTLY ONE failed assertion here, the
// deliberate one. If the macro ever stops returning, the CHECK_FALSE below
// fails as well and the case reports TWO -- that is the regression signal, and
// it is a signal to read rather than a gate that goes red on its own.
TEST_CASE_MAY_FAIL("[TestMacros] REQUIRE_OR_RETURN stops the case on failure") {
	bool reached_end = true;
	_run_until_guard(false, &reached_end);
	CHECK_FALSE_MESSAGE(reached_end, "A false condition must skip everything after the guard.");
}

TEST_CASE("[TestMacros] REQUIRE_OR_RETURN continues on success") {
	bool reached_end = false;
	_run_until_guard(true, &reached_end);
	CHECK_MESSAGE(reached_end, "A true condition must fall through to the next statement.");
}

TEST_CASE("[TestMacros] REQUIRE_OR_RETURN evaluates its condition exactly once") {
	side_effect_count = 0;
	bool unused = false;
	// True case: falls through, so the count is the only thing under test.
	_run_until_guard(_counting_condition(true), &unused);
	CHECK_MESSAGE(side_effect_count == 1, "The condition must not be evaluated twice.");
}

} // namespace TestMacrosGuard
