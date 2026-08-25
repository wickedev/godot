/**************************************************************************/
/*  test_audio_suspension_protocol.cpp                                    */
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

#include "core/io/marshalls.h"
#include "scene/resources/audio/audio_stream_wav.h"
#include "servers/audio/audio_frame.h"

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_audio_suspension_protocol)

namespace TestAudioSuspensionProtocol {

// Regression tests for the inaudible-suspension mutation protocol
// (`audio/general/suspend_inaudible_playbacks`): restart must not drop the
// stream start, no-op mutations must not bump the generation, and the wake
// flush must drop stale audio without advancing the decoder.

static constexpr float RATE = 44100;

// A NON-PERIODIC ramp: every frame value is unique across the stream (the
// length stays below 2^15), so no seek position aliases another — a mix from
// the wrong position can never masquerade as the right one, which a sine at a
// period-aligned seek would allow.
static Ref<AudioStreamWAV> _make_pcm16_stream(int p_frames = int(RATE) / 2) {
	Vector<uint8_t> data;
	data.resize(p_frames * 2);
	uint8_t *w = data.ptrw();
	for (int i = 0; i < p_frames; i++) {
		encode_uint16(uint16_t(i - (INT16_MAX + 1)), w + i * 2);
	}
	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	stream->set_mix_rate(RATE);
	stream->set_format(AudioStreamWAV::FORMAT_16_BITS);
	stream->set_data(data);
	return stream;
}

static bool _frames_equal(const AudioFrame &p_a, const AudioFrame &p_b) {
	return p_a.left == p_b.left && p_a.right == p_b.right;
}

static Vector<AudioFrame> _mix(const Ref<AudioStreamPlayback> &p_playback, int p_frames) {
	Vector<AudioFrame> out;
	out.resize(p_frames);
	p_playback->mix(out.ptrw(), 1.0f, p_frames);
	return out;
}

// The external (protocol-carrying) entry point, as scripts and C++ callers use it.
static Vector<AudioFrame> _mix_external(const Ref<AudioStreamPlayback> &p_playback, int p_frames) {
	return p_playback->mix_audio(1.0f, p_frames);
}

TEST_CASE("[Audio][AudioSuspension] Wake flush is a no-op for fresh residuals (suspended restart)") {
	Ref<AudioStreamWAV> stream = _make_pcm16_stream();

	// Reference: a plain start-and-mix.
	Ref<AudioStreamPlayback> reference = stream->instantiate_playback();
	reference->start(0.0);
	const Vector<AudioFrame> expected = _mix(reference, 256);

	// Suspended restart: start() already refilled the resampler from position 0,
	// so the wake-side flush must NOT touch it — flushing here used to drop the
	// first internal buffer of the restarted stream.
	Ref<AudioStreamPlayback> restarted = stream->instantiate_playback();
	restarted->start(0.0);
	restarted->flush_suspension_residuals();
	const Vector<AudioFrame> actual = _mix(restarted, 256);

	bool identical = true;
	for (int i = 0; i < expected.size(); i++) {
		if (expected[i].left != actual[i].left || expected[i].right != actual[i].right) {
			identical = false;
			break;
		}
	}
	CHECK_MESSAGE(identical, "A wake flush right after start() must not change the mixed output.");
}

TEST_CASE("[Audio][AudioSuspension] Only mutating calls bump the suspension generation") {
	SUBCASE("A real PCM seek bumps exactly once") {
		Ref<AudioStreamWAV> stream = _make_pcm16_stream();
		Ref<AudioStreamPlayback> playback = stream->instantiate_playback();
		playback->start(0.0);
		const uint64_t before = playback->get_suspension_generation();
		playback->seek(0.25);
		CHECK(playback->get_suspension_generation() == before + 1);
	}

	SUBCASE("The unsupported IMA ADPCM seek is a no-op and must not bump") {
		// Content is irrelevant: seek() returns before touching decode state.
		Vector<uint8_t> silence;
		silence.resize(256);
		silence.fill(0);
		Ref<AudioStreamWAV> stream;
		stream.instantiate();
		stream->set_mix_rate(RATE);
		stream->set_format(AudioStreamWAV::FORMAT_IMA_ADPCM);
		stream->set_data(silence);
		Ref<AudioStreamPlayback> playback = stream->instantiate_playback();
		playback->start(0.0);
		const uint64_t before = playback->get_suspension_generation();
		playback->seek(0.25);
		CHECK_MESSAGE(playback->get_suspension_generation() == before,
				"A no-op seek must not bump: a wake would needlessly discard valid lookahead.");
	}
}

TEST_CASE("[Audio][AudioSuspension] Stale flush drops buffered audio without advancing the decoder") {
	// 0.31s: not aligned to anything, and the ramp data makes every position
	// unique — the exact-compare below fails if the decoder resumes anywhere
	// other than the seek target.
	const double seek_pos = 0.31;
	Ref<AudioStreamWAV> stream = _make_pcm16_stream();
	Ref<AudioStreamPlayback> playback = stream->instantiate_playback();
	playback->start(0.0);

	// Fill the resampler with real (non-silent) audio.
	const Vector<AudioFrame> warmup = _mix(playback, 256);
	bool warmup_nonzero = false;
	for (const AudioFrame &f : warmup) {
		if (f.left != 0.0f) {
			warmup_nonzero = true;
			break;
		}
	}
	REQUIRE(warmup_nonzero);

	// seek() repositions the decoder but does not refill the resampler: the
	// buffered audio is now stale (pre-mutation).
	playback->seek(seek_pos);
	const double position_after_seek = playback->get_playback_position();

	playback->flush_suspension_residuals();

	// Non-advancing: the flush must not have decoded anything.
	CHECK(playback->get_playback_position() == doctest::Approx(position_after_seek));

	// The stale buffer was dropped: exactly one internal buffer of silence
	// (128 frames at 1:1), then the decoder's output must EXACTLY match a
	// reference playback started at the seek position — same pipeline state
	// (zeroed history, refill from the target), so equality is bit-exact.
	const Vector<AudioFrame> resumed = _mix(playback, 256);
	Ref<AudioStreamPlayback> reference = stream->instantiate_playback();
	reference->start(seek_pos);
	const Vector<AudioFrame> expected = _mix(reference, 128);

	bool prefix_silent = true;
	for (int i = 0; i < 128; i++) {
		if (resumed[i].left != 0.0f || resumed[i].right != 0.0f) {
			prefix_silent = false;
			break;
		}
	}
	CHECK_MESSAGE(prefix_silent, "The zero-filled internal buffer must play out as exactly one buffer of silence.");
	bool tail_matches_reference = true;
	for (int i = 0; i < 128; i++) {
		if (!_frames_equal(resumed[128 + i], expected[i])) {
			tail_matches_reference = false;
			break;
		}
	}
	CHECK_MESSAGE(tail_matches_reference,
			"Post-flush audio must bit-exactly match a fresh playback at the seek position — not merely be nonzero.");
}

TEST_CASE("[Audio][AudioSuspension] External mix_audio() participates in the mutation protocol") {
	Ref<AudioStreamWAV> stream = _make_pcm16_stream();
	Ref<AudioStreamPlayback> playback = stream->instantiate_playback();

	SUBCASE("A zero-frame mix is a no-op: no bump") {
		playback->start(0.0);
		const uint64_t before = playback->get_suspension_generation();
		playback->mix_audio(1.0f, 0);
		CHECK(playback->get_suspension_generation() == before);
	}

	SUBCASE("A mix bumps without inspecting what the subclass did") {
		// There is no portable "did this consume stream data?" predicate, so the
		// protocol does not try to have one: a nonempty external mix always
		// counts. The two errors are not symmetric -- a missed mutation plays
		// pre-mutation audio (audible), a spurious one costs ~3 ms of silence
		// under the wake ramp (inaudible). Even a stopped playback bumps.
		const uint64_t before = playback->get_suspension_generation();
		playback->mix_audio(1.0f, 64);
		CHECK(playback->get_suspension_generation() == before + 1);
	}

	SUBCASE("A mix that runs off the end of the stream bumps too") {
		playback->start(0.0);
		const uint64_t before = playback->get_suspension_generation();
		const Vector<AudioFrame> consumed = _mix_external(playback, int(RATE)); // Past the 0.5s end.
		CHECK(consumed.size() > 0);
		CHECK(playback->get_suspension_generation() == before + 1);
		CHECK_FALSE(playback->is_playing());
	}

	SUBCASE("A consuming mix bumps and leaves residuals fresh") {
		playback->start(0.0);
		const uint64_t before = playback->get_suspension_generation();
		const Vector<AudioFrame> consumed = _mix_external(playback, 64);
		REQUIRE(consumed.size() == 64);
		CHECK(playback->get_suspension_generation() == before + 1);
		// The remaining lookahead is the valid continuation: a wake flush must
		// NOT zero it — mixing must continue seamlessly against a reference
		// that mixed the same total without any flush in between.
		playback->flush_suspension_residuals();
		const Vector<AudioFrame> after_flush = _mix(playback, 64);
		Ref<AudioStreamPlayback> reference = stream->instantiate_playback();
		reference->start(0.0);
		_mix(reference, 64);
		const Vector<AudioFrame> expected = _mix(reference, 64);
		bool seamless = true;
		for (int i = 0; i < 64; i++) {
			if (!_frames_equal(after_flush[i], expected[i])) {
				seamless = false;
				break;
			}
		}
		CHECK_MESSAGE(seamless, "Residuals after an external mix are post-mutation audio; a wake flush must not drop them.");
	}
}

TEST_CASE("[Audio][AudioSuspension] A partial external mix must not promote stale residuals") {
	// The hazard: seek() moves the decoder but leaves PRE-seek audio buffered.
	// An external mix_audio() before the wake hands the caller some of that
	// stale audio and leaves the rest behind. Marking the leftovers "fresh"
	// there would make the wake flush skip them, and the pre-seek tail would
	// play on after the wake -- exactly what the flush exists to prevent.
	const double seek_pos = 0.31;
	Ref<AudioStreamWAV> stream = _make_pcm16_stream();
	Ref<AudioStreamPlayback> playback = stream->instantiate_playback();
	playback->start(0.0);
	_mix(playback, 256); // Fill the resampler with pre-seek audio.

	playback->seek(seek_pos); // Residuals are now stale.
	const Vector<AudioFrame> partial = _mix_external(playback, 64);
	REQUIRE(partial.size() == 64);

	// The wake flush must still fire on the leftovers.
	const double position_before_flush = playback->get_playback_position();
	playback->flush_suspension_residuals();
	CHECK_MESSAGE(playback->get_playback_position() == doctest::Approx(position_before_flush),
			"The flush must not advance the decoder.");

	// Same shape as the plain stale-flush case: one internal buffer of silence,
	// then bit-exact agreement with a playback started at the seek position.
	const Vector<AudioFrame> resumed = _mix(playback, 256);
	Ref<AudioStreamPlayback> reference = stream->instantiate_playback();
	reference->start(seek_pos);
	const Vector<AudioFrame> expected = _mix(reference, 128);

	bool prefix_silent = true;
	for (int i = 0; i < 128; i++) {
		if (resumed[i].left != 0.0f || resumed[i].right != 0.0f) {
			prefix_silent = false;
			break;
		}
	}
	CHECK_MESSAGE(prefix_silent, "Stale residuals survived a partial external mix: they were promoted to fresh.");
	bool tail_matches_reference = true;
	for (int i = 0; i < 128; i++) {
		if (!_frames_equal(resumed[128 + i], expected[i])) {
			tail_matches_reference = false;
			break;
		}
	}
	CHECK_MESSAGE(tail_matches_reference, "Post-flush audio must bit-exactly match a fresh playback at the seek position.");
}

TEST_CASE("[Audio][AudioSuspension] A looping mix inside the protocol scope does not deadlock") {
	// SCOPE, honestly: this is a smoke test for the inline WAV loop wrap running
	// inside the protocol, nothing more. It does NOT cover lock reentrancy. The
	// unit-test harness has no AudioServer, so begin/end_stream_mutation() are
	// no-ops here regardless of the setting, and a WAV loop wraps inline without
	// a nested seek. Reentrancy on the MP3/Vorbis path (public seek() during
	// mix, on the audio thread, with the setting on) and the wake-vs-mutation
	// race are unverified by this suite -- see the note in the review thread.
	Ref<AudioStreamWAV> stream = _make_pcm16_stream(1024);
	stream->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
	stream->set_loop_end(1024);
	Ref<AudioStreamPlayback> playback = stream->instantiate_playback();
	playback->start(0.0);
	// Mix well past the loop point repeatedly; completing without deadlock or
	// crash (and still playing) is the assertion.
	for (int i = 0; i < 8; i++) {
		_mix(playback, 512);
	}
	CHECK(playback->is_playing());
}

} // namespace TestAudioSuspensionProtocol
