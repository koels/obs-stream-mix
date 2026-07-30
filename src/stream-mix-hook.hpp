/*
 * Stream Mix - native streaming integration.
 *
 * Redirects OBS's own streaming audio encoder to the private combined mix at
 * the streaming output's "starting" signal (encoder assigned but not yet
 * active). The user keeps using the normal Start Streaming button; recording
 * and its six isolated tracks are never touched.
 */
#pragma once

#include <obs.h>
#include "stream-mix-audio.hpp"

class StreamMixHook {
public:
	explicit StreamMixHook(StreamMixAudio *audio) : engine(audio) {}
	~StreamMixHook() { on_streaming_stopped(); }

	/* Wired to OBS frontend streaming events. */
	void on_streaming_starting();
	void on_streaming_stopped();

	/* Called from the output "starting" signal trampoline. */
	void redirect_encoder();

private:
	StreamMixAudio *engine = nullptr;
	obs_output_t *output = nullptr;      /* held ref during stream */
	obs_weak_encoder_t *weak_enc = nullptr; /* rebound stream encoder */
	bool signal_connected = false;
};
