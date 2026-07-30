/*
 * Stream Mix - private audio mix engine
 *
 * Builds an independent audio_output ("the 7th mix") from per-source audio
 * capture callbacks. This mix exists ONLY for the plugin's streaming output;
 * OBS's own six recording mixes are never touched.
 */
#pragma once

#include <obs.h>
#include <media-io/audio-io.h>
#include <util/deque.h>

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

/* Per-source, stream-only controls + jitter buffer. */
struct sm_track {
	obs_weak_source_t *weak = nullptr;
	std::string name;

	/* Stream-mix-only settings (do NOT affect recording). */
	std::atomic<float> gain{1.0f}; /* linear multiplier */
	std::atomic<bool> mute{false};
	std::atomic<bool> exclude{false};

	/* Simple brick-wall limiter (stream mix only). */
	std::atomic<bool> limiter{false};
	std::atomic<float> limiter_threshold{0.891f}; /* ~ -1 dBFS */

	/* One planar-float jitter buffer per channel. Guarded by
	 * StreamMixAudio::mtx. */
	struct deque buf[MAX_AUDIO_CHANNELS];

	bool cb_added = false;
	void *engine = nullptr; /* back-pointer to StreamMixAudio */
};

class StreamMixAudio {
public:
	StreamMixAudio() = default;
	~StreamMixAudio();

	/* Open the private audio_output, discover audio sources routed to any
	 * recording track, and attach capture callbacks. Idempotent. */
	bool start();
	void stop();
	bool active() const { return audio != nullptr; }

	audio_t *handle() const { return audio; }
	size_t channel_count() const { return channels; }
	uint32_t sample_rate() const { return samples_per_sec; }

	/* Live control (safe to call while active). */
	void set_gain(const std::string &name, float linear);
	void set_mute(const std::string &name, bool mute);
	void set_exclude(const std::string &name, bool exclude);
	void set_limiter(const std::string &name, bool on, float threshold);

	/* Re-scan sources (e.g. after config reload). */
	void refresh_sources();

	/* --- internal, called from static trampolines / enumerators --- */
	void on_source_audio(sm_track *t, const struct audio_data *ad,
			     bool muted);
	bool fill_mix(struct audio_output_data *mix, uint32_t frames);
	void on_source_discovered(obs_source_t *src);

private:
	sm_track *find_track(const std::string &name);
	sm_track *ensure_track(obs_source_t *src);
	void attach(sm_track *t);
	void detach(sm_track *t);

	audio_t *audio = nullptr;
	size_t channels = 2;
	uint32_t samples_per_sec = 48000;

	mutable std::mutex mtx;
	std::vector<std::unique_ptr<sm_track>> tracks;

	/* Cap each per-channel buffer so a runaway/faster source can't grow
	 * unbounded. ~0.5 s at 48 kHz. */
	size_t max_buffered_frames = 24000;
};
