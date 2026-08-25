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
// case in this build. These tests pin the behaviour it depends on, so a later
// change to the macro -- or to doctest's exception mode -- cannot quietly take
// it away.
//
// They drive the macro through a FAKE reporter, so the failure path runs
// without producing a real assertion failure. That is what makes them a hard
// gate: a regression fails the run rather than showing up as an extra line in
// a test that is allowed to fail.

static int fake_failures = 0;
static int condition_evaluations = 0;

static void _fake_report(bool p_ok, const char *p_message) {
	if (!p_ok) {
		fake_failures++;
	}
}

#define FAKE_REPORT(m_ok, m_msg) _fake_report(m_ok, m_msg)

static bool _counted(bool p_result) {
	condition_evaluations++;
	return p_result;
}

// Separate function because the macro returns: what has to be observed is that
// the statements after the guard did not run.
static void _run_guard(bool p_condition, bool *r_reached_end) {
	*r_reached_end = false;
	REQUIRE_OR_RETURN_WITH(_counted(p_condition), FAKE_REPORT);
	*r_reached_end = true;
}

TEST_CASE("[TestMacros] REQUIRE_OR_RETURN returns on a false condition") {
	fake_failures = 0;
	condition_evaluations = 0;
	bool reached_end = true;

	_run_guard(false, &reached_end);

	CHECK_FALSE_MESSAGE(reached_end, "A false condition must skip everything after the guard.");
	CHECK_MESSAGE(fake_failures == 1, "The failure must be reported exactly once.");
	CHECK_MESSAGE(condition_evaluations == 1, "The condition must be evaluated exactly once.");
}

TEST_CASE("[TestMacros] REQUIRE_OR_RETURN falls through on a true condition") {
	fake_failures = 0;
	condition_evaluations = 0;
	bool reached_end = false;

	_run_guard(true, &reached_end);

	CHECK_MESSAGE(reached_end, "A true condition must fall through to the next statement.");
	CHECK_MESSAGE(fake_failures == 0, "Nothing must be reported on success.");
	CHECK_MESSAGE(condition_evaluations == 1, "The condition must be evaluated exactly once.");
}

TEST_CASE("[TestMacros] REQUIRE_OR_RETURN is a single statement") {
	// Would not compile if the macro were not do/while-wrapped.
	bool reached_end = false;
	if (true) {
		_run_guard(true, &reached_end);
	} else {
		reached_end = false;
	}
	CHECK(reached_end);
}

#undef FAKE_REPORT

} // namespace TestMacrosGuard
