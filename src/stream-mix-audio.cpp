/*
 * Stream Mix - private combined-mix engine implementation.
 */
#include "stream-mix-audio.hpp"

#include <obs-module.h>

#include <cstring>
#include <vector>
#include <algorithm>

#define blog(level, format, ...) \
	blog(level, "[stream-mix][audio] " format, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Static trampolines                                                  */
/* ------------------------------------------------------------------ */

/* Receives the fully-mixed audio of one core recording mix (track). */
static void track_tap_trampoline(void *param, size_t mix_idx,
				 struct audio_data *data)
{
	UNUSED_PARAMETER(mix_idx);
	auto *t = static_cast<sm_track *>(param);
	auto *engine = static_cast<StreamMixAudio *>(t->engine);
	engine->on_track_audio(t, data);
}

/* Produces the combined stream mix for the private audio_output. */
static bool audio_input_trampoline(void *param, uint64_t start_ts,
				   uint64_t end_ts, uint64_t *new_ts,
				   uint32_t active_mixers,
				   struct audio_output_data *mixes)
{
	UNUSED_PARAMETER(end_ts);
	UNUSED_PARAMETER(active_mixers);
	auto *self = static_cast<StreamMixAudio *>(param);
	bool have = self->fill_mix(&mixes[0], AUDIO_OUTPUT_FRAMES);
	*new_ts = start_ts;
	return have;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

StreamMixAudio::~StreamMixAudio()
{
	stop();
}

bool StreamMixAudio::start()
{
	if (audio)
		return true;

	struct obs_audio_info oai;
	if (!obs_get_audio_info(&oai)) {
		blog(LOG_ERROR, "obs_get_audio_info failed");
		return false;
	}
	samples_per_sec = oai.samples_per_sec;
	channels = get_audio_channels(oai.speakers);
	max_buffered_frames = samples_per_sec / 2; /* ~0.5 s */

	struct audio_output_info aoi = {};
	aoi.name = "stream_mix";
	aoi.samples_per_sec = samples_per_sec;
	aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
	aoi.speakers = oai.speakers;
	aoi.input_callback = audio_input_trampoline;
	aoi.input_param = this;

	if (audio_output_open(&audio, &aoi) != 0) {
		blog(LOG_ERROR, "audio_output_open failed");
		audio = nullptr;
		return false;
	}

	/* Tap all six core recording mixes. We always connect (cheap when a
	 * mix is silent); inclusion is honored per-block in fill_mix(), so the
	 * user can toggle tracks live without reconnecting. */
	audio_t *core = obs_get_audio();
	for (int i = 0; i < TRACK_COUNT; i++) {
		sm_track *t = &tracks[i];
		t->index = i;
		t->engine = this;
		for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++)
			deque_init(&t->buf[c]);
		/* NULL conversion => native mix format (float planar, core
		 * rate/speakers), matching our private audio_output. */
		if (audio_output_connect(core, (size_t)i, nullptr,
					 track_tap_trampoline, t))
			t->connected = true;
	}

	blog(LOG_INFO, "stream mix opened: %u Hz, %zu ch, tapping %d tracks",
	     samples_per_sec, channels, TRACK_COUNT);
	return true;
}

void StreamMixAudio::stop()
{
	if (!audio)
		return;

	audio_t *core = obs_get_audio();
	for (int i = 0; i < TRACK_COUNT; i++) {
		sm_track *t = &tracks[i];
		if (t->connected) {
			audio_output_disconnect(core, (size_t)i,
						track_tap_trampoline, t);
			t->connected = false;
		}
	}

	{
		std::lock_guard<std::mutex> lock(mtx);
		for (int i = 0; i < TRACK_COUNT; i++)
			for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++)
				deque_free(&tracks[i].buf[c]);
	}

	audio_output_close(audio);
	audio = nullptr;
	blog(LOG_INFO, "stream mix closed");
}

/* ------------------------------------------------------------------ */
/* Audio path                                                          */
/* ------------------------------------------------------------------ */

void StreamMixAudio::on_track_audio(sm_track *t, const struct audio_data *ad)
{
	if (!ad || ad->frames == 0)
		return;

	const size_t frames = ad->frames;
	const size_t bytes = frames * sizeof(float);

	std::lock_guard<std::mutex> lock(mtx);
	for (size_t c = 0; c < channels; c++) {
		struct deque *d = &t->buf[c];

		size_t cur = d->size / sizeof(float);
		if (cur + frames > max_buffered_frames)
			deque_pop_front(d, nullptr,
					(cur + frames - max_buffered_frames) *
						sizeof(float));

		const float *in = ad->data[c]
					  ? (const float *)ad->data[c]
					  : nullptr;
		if (in) {
			deque_push_back(d, in, bytes);
		} else {
			static thread_local std::vector<float> zeros;
			if (zeros.size() < frames)
				zeros.assign(frames, 0.0f);
			deque_push_back(d, zeros.data(), bytes);
		}
	}
}

bool StreamMixAudio::fill_mix(struct audio_output_data *mix, uint32_t frames)
{
	for (size_t c = 0; c < channels; c++)
		if (mix->data[c])
			memset(mix->data[c], 0, frames * sizeof(float));

	std::vector<float> tmp(frames);

	std::lock_guard<std::mutex> lock(mtx);
	for (int i = 0; i < TRACK_COUNT; i++) {
		sm_track *t = &tracks[i];
		if (!t->include.load() || t->mute.load())
			continue;

		size_t avail = frames;
		for (size_t c = 0; c < channels; c++)
			avail = std::min(avail, t->buf[c].size / sizeof(float));
		if (avail == 0)
			continue;

		const float gain = t->gain.load();
		const bool lim = t->limiter.load();
		const float thr = t->limiter_threshold.load();

		for (size_t c = 0; c < channels; c++) {
			if (!mix->data[c]) {
				deque_pop_front(&t->buf[c], nullptr,
						avail * sizeof(float));
				continue;
			}
			deque_pop_front(&t->buf[c], tmp.data(),
					avail * sizeof(float));
			float *out = mix->data[c];
			for (size_t f = 0; f < avail; f++) {
				float s = tmp[f] * gain;
				if (lim) {
					if (s > thr)
						s = thr;
					else if (s < -thr)
						s = -thr;
				}
				out[f] += s;
			}
		}
	}
	return true; /* silence is a valid, correctly-timed frame */
}

/* ------------------------------------------------------------------ */
/* Live control                                                        */
/* ------------------------------------------------------------------ */

void StreamMixAudio::set_include(int idx, bool include)
{
	if (idx >= 0 && idx < TRACK_COUNT)
		tracks[idx].include.store(include);
}
void StreamMixAudio::set_gain(int idx, float linear)
{
	if (idx >= 0 && idx < TRACK_COUNT)
		tracks[idx].gain.store(linear);
}
void StreamMixAudio::set_mute(int idx, bool mute)
{
	if (idx >= 0 && idx < TRACK_COUNT)
		tracks[idx].mute.store(mute);
}
void StreamMixAudio::set_limiter(int idx, bool on, float threshold)
{
	if (idx >= 0 && idx < TRACK_COUNT) {
		tracks[idx].limiter.store(on);
		tracks[idx].limiter_threshold.store(threshold);
	}
}
