/*
 * Stream Mix - plugin-owned streaming output.
 *
 * Creates an rtmp_output that reuses OBS's configured streaming service and
 * video, but pulls audio from the plugin's private mix instead of one of
 * OBS's six core mixes. This is the piece that makes the whole thing work
 * without an OBS fork: because we own the output, we control its audio
 * encoder and OBS never overwrites it.
 */
#pragma once

#include <obs.h>
#include "stream-mix-audio.hpp"
#include "stream-mix-config.hpp"

class StreamMixOutput {
public:
	explicit StreamMixOutput(StreamMixAudio *audio) : audioEngine(audio) {}
	~StreamMixOutput() { stop(); }

	bool start(const StreamMixConfig &cfg);
	void stop();
	bool active() const;

private:
	obs_encoder_t *create_video_encoder(const StreamMixConfig &cfg);
	obs_encoder_t *create_audio_encoder(const StreamMixConfig &cfg);

	StreamMixAudio *audioEngine = nullptr;
	obs_output_t *output = nullptr;
	obs_encoder_t *venc = nullptr;
	obs_encoder_t *aenc = nullptr;
};
