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

static Ref<AudioStreamWAV> _make_pcm16_stream(int p_frames = int(RATE)) {
	Vector<uint8_t> data;
	data.resize(p_frames * 2);
	uint8_t *w = data.ptrw();
	for (int i = 0; i < p_frames; i++) {
		const float wav = Math::sin((Math::TAU * 440.0 / RATE) * i);
		const uint16_t wav_16bit = Math::fast_ftoi(((wav + 1) / 2) * UINT16_MAX);
		encode_uint16(wav_16bit - (INT16_MAX + 1), w + i * 2);
	}
	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	stream->set_mix_rate(RATE);
	stream->set_format(AudioStreamWAV::FORMAT_16_BITS);
	stream->set_data(data);
	return stream;
}

static Vector<AudioFrame> _mix(const Ref<AudioStreamPlayback> &p_playback, int p_frames) {
	Vector<AudioFrame> out;
	out.resize(p_frames);
	p_playback->mix(out.ptrw(), 1.0f, p_frames);
	return out;
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
	playback->seek(0.5);
	const double position_after_seek = playback->get_playback_position();

	playback->flush_suspension_residuals();

	// Non-advancing: the flush must not have decoded anything.
	CHECK(playback->get_playback_position() == doctest::Approx(position_after_seek));

	// The stale buffer was dropped: mixing resumes with silence, then real
	// post-seek audio once the resampler refills from the new position.
	const Vector<AudioFrame> resumed = _mix(playback, 256);
	const bool first_frame_silent = resumed[0].left == 0.0f && resumed[0].right == 0.0f;
	CHECK_MESSAGE(first_frame_silent,
			"The first mixed frame after a stale flush must be silence, not pre-seek audio.");
	bool tail_nonzero = false;
	for (int i = 128; i < resumed.size(); i++) {
		if (resumed[i].left != 0.0f) {
			tail_nonzero = true;
			break;
		}
	}
	CHECK_MESSAGE(tail_nonzero, "The decoder must keep producing audio from the post-seek position.");
}

} // namespace TestAudioSuspensionProtocol
