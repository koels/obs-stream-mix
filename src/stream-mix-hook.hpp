/*
 * Stream Mix - native streaming integration.
 *
 * Redirects OBS's own streaming audio encoder to the private combined mix at
 * the streaming output's "starting" signal (encoder assigned but not yet
 * active). The user keeps using the normal Start Streaming button; recording
 * and its six isolated tracks are never touched.
 *
 * Lifetime note: the private mix (StreamMixAudio) is opened on first use and
 * kept alive for the whole plugin lifetime. It is NOT torn down when a stream
 * stops, because OBS disconnects the stream encoder from it asynchronously
 * (end_data_capture_thread); freeing it on stop is a use-after-free.
 */
#pragma once

#include <obs.h>
#include "stream-mix-audio.hpp"

class StreamMixHook {
public:
	explicit StreamMixHook(StreamMixAudio *audio) : engine(audio) {}
	~StreamMixHook() { on_streaming_stopped(); }

	void on_streaming_starting(); /* arm the redirect */
	void on_streaming_stopped();  /* detach signal only; keep mix alive */

	void redirect_encoder(); /* called from the "starting" signal */

private:
	StreamMixAudio *engine = nullptr;
	obs_output_t *output = nullptr; /* held ref while armed */
	bool signal_connected = false;
};
