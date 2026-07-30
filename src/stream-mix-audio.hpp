/*
 * Stream Mix - private combined-mix engine.
 *
 * Builds an independent audio_output ("the stream mix") by tapping OBS's six
 * core recording mixes with audio_output_connect and summing the selected
 * ones. This mix exists only to feed the stream encoder; OBS's six recording
 * mixes and the recording output are never modified.
 */
#pragma once

#include <obs.h>
#include <media-io/audio-io.h>
#include <util/deque.h>

#include <atomic>
#include <mutex>

/* One row per OBS recording track (mix index 0..5 == "Track 1..6"). */
struct sm_track {
	int index = 0;

	/* Stream-mix-only controls (never affect recording). */
	std::atomic<bool> include{true};
	std::atomic<float> gain{1.0f}; /* linear */
	std::atomic<bool> mute{false};
	std::atomic<bool> limiter{false};
	std::atomic<float> limiter_threshold{0.891f}; /* ~ -1 dBFS */

	/* Per-channel jitter buffer, guarded by StreamMixAudio::mtx. */
	struct deque buf[MAX_AUDIO_CHANNELS];
	bool connected = false;
	void *engine = nullptr;
};

class StreamMixAudio {
public:
	StreamMixAudio() = default;
	~StreamMixAudio();

	bool start();
	void stop();
	bool active() const { return audio != nullptr; }

	/* The private audio_output the stream encoder is redirected to. */
	audio_t *handle() const { return audio; }
	size_t channel_count() const { return channels; }

	/* Live control (safe while active). idx is 0..5. */
	void set_include(int idx, bool include);
	void set_gain(int idx, float linear);
	void set_mute(int idx, bool mute);
	void set_limiter(int idx, bool on, float threshold);

	/* --- internal, called from static trampolines --- */
	void on_track_audio(sm_track *t, const struct audio_data *ad);
	bool fill_mix(struct audio_output_data *mix, uint32_t frames);

	static constexpr int TRACK_COUNT = MAX_AUDIO_MIXES; /* 6 */

private:
	audio_t *audio = nullptr;
	size_t channels = 2;
	uint32_t samples_per_sec = 48000;
	size_t max_buffered_frames = 24000;

	mutable std::mutex mtx;
	sm_track tracks[TRACK_COUNT];
};
