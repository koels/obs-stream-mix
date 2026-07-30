/*
 * Stream Mix - private audio mix engine implementation.
 */
#include "stream-mix-audio.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <cstring>
#include <cmath>
#include <algorithm>

#define blog(level, format, ...) \
	blog(level, "[stream-mix][audio] " format, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Static trampolines                                                  */
/* ------------------------------------------------------------------ */

static void source_audio_trampoline(void *param, obs_source_t *source,
				    const struct audio_data *ad, bool muted)
{
	UNUSED_PARAMETER(source);
	auto *t = static_cast<sm_track *>(param);
	auto *engine = static_cast<StreamMixAudio *>(t->engine);
	engine->on_source_audio(t, ad, muted);
}

static bool audio_input_trampoline(void *param, uint64_t start_ts,
				   uint64_t end_ts, uint64_t *new_ts,
				   uint32_t active_mixers,
				   struct audio_output_data *mixes)
{
	UNUSED_PARAMETER(end_ts);
	UNUSED_PARAMETER(active_mixers);
	auto *self = static_cast<StreamMixAudio *>(param);

	/* We only ever produce mix index 0. */
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

	refresh_sources();
	blog(LOG_INFO, "private mix opened: %u Hz, %zu ch", samples_per_sec,
	     channels);
	return true;
}

void StreamMixAudio::stop()
{
	if (!audio)
		return;

	mtx.lock();
	for (auto &t : tracks) {
		detach(t.get());
		for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++)
			deque_free(&t->buf[c]);
		if (t->weak)
			obs_weak_source_release(t->weak);
	}
	tracks.clear();
	mtx.unlock();

	audio_output_close(audio);
	audio = nullptr;
	blog(LOG_INFO, "private mix closed");
}

/* ------------------------------------------------------------------ */
/* Source discovery                                                    */
/* ------------------------------------------------------------------ */

struct enum_ctx {
	StreamMixAudio *self;
};

static bool enum_audio_source(void *param, obs_source_t *src)
{
	auto *ctx = static_cast<enum_ctx *>(param);
	uint32_t caps = obs_source_get_output_flags(src);
	if ((caps & OBS_SOURCE_AUDIO) == 0)
		return true;

	/* "Active recording track" == routed to at least one of OBS's six
	 * mixes. Sources with no track assignment are ignored. */
	if (obs_source_get_audio_mixers(src) == 0)
		return true;

	ctx->self->on_source_discovered(src);
	return true;
}

/* Exposed so the enumerator (a free function) can call back in. */
void StreamMixAudio::refresh_sources()
{
	enum_ctx ctx{this};
	obs_enum_sources(enum_audio_source, &ctx);
}

/* Called (indirectly) from refresh_sources for each eligible source. */
void StreamMixAudio::on_source_discovered(obs_source_t *src)
{
	sm_track *t = ensure_track(src);
	if (t)
		attach(t);
}

sm_track *StreamMixAudio::find_track(const std::string &name)
{
	for (auto &t : tracks)
		if (t->name == name)
			return t.get();
	return nullptr;
}

sm_track *StreamMixAudio::ensure_track(obs_source_t *src)
{
	const char *name = obs_source_get_name(src);
	if (!name)
		return nullptr;

	mtx.lock();
	sm_track *existing = find_track(name);
	if (existing) {
		/* Refresh the weak ref in case the source was recreated. */
		if (!existing->weak) {
			existing->weak = obs_source_get_weak_source(src);
		}
		mtx.unlock();
		return existing;
	}

	auto up = std::make_unique<sm_track>();
	up->name = name;
	up->engine = this;
	up->weak = obs_source_get_weak_source(src);
	for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++)
		deque_init(&up->buf[c]);
	sm_track *ptr = up.get();
	tracks.push_back(std::move(up));
	mtx.unlock();

	blog(LOG_INFO, "tracking source '%s'", name);
	return ptr;
}

void StreamMixAudio::attach(sm_track *t)
{
	if (t->cb_added || !t->weak)
		return;
	obs_source_t *src = obs_weak_source_get_source(t->weak);
	if (!src)
		return;
	obs_source_add_audio_capture_callback(src, source_audio_trampoline, t);
	t->cb_added = true;
	obs_source_release(src);
}

void StreamMixAudio::detach(sm_track *t)
{
	if (!t->cb_added || !t->weak)
		return;
	obs_source_t *src = obs_weak_source_get_source(t->weak);
	if (src) {
		obs_source_remove_audio_capture_callback(
			src, source_audio_trampoline, t);
		obs_source_release(src);
	}
	t->cb_added = false;
}

/* ------------------------------------------------------------------ */
/* Audio path                                                          */
/* ------------------------------------------------------------------ */

void StreamMixAudio::on_source_audio(sm_track *t, const struct audio_data *ad,
				     bool source_muted)
{
	if (!ad || ad->frames == 0)
		return;

	const size_t frames = ad->frames;
	const size_t bytes = frames * sizeof(float);
	/* Mirror OBS: a globally muted source contributes silence to the
	 * stream too, so streaming matches recording. Per-track (stream-only)
	 * mute/exclude is applied later in fill_mix(). */
	const bool silence = source_muted;

	mtx.lock();
	for (size_t c = 0; c < channels; c++) {
		struct deque *d = &t->buf[c];

		/* Drop oldest audio if a source outruns the mix clock. */
		size_t cur = d->size / sizeof(float);
		if (cur + frames > max_buffered_frames) {
			size_t drop = (cur + frames - max_buffered_frames);
			deque_pop_front(d, nullptr, drop * sizeof(float));
		}

		const float *in =
			(!silence && ad->data[c])
				? reinterpret_cast<const float *>(ad->data[c])
				: nullptr;
		if (in) {
			deque_push_back(d, in, bytes);
		} else {
			/* push silence to keep channels frame-aligned */
			static thread_local std::vector<float> zeros;
			if (zeros.size() < frames)
				zeros.assign(frames, 0.0f);
			deque_push_back(d, zeros.data(), bytes);
		}
	}
	mtx.unlock();
}

bool StreamMixAudio::fill_mix(struct audio_output_data *mix, uint32_t frames)
{
	/* Zero the output first. */
	for (size_t c = 0; c < channels; c++) {
		if (mix->data[c])
			memset(mix->data[c], 0, frames * sizeof(float));
	}

	std::vector<float> tmp(frames);

	mtx.lock();
	for (auto &up : tracks) {
		sm_track *t = up.get();
		if (t->mute.load() || t->exclude.load())
			continue;

		/* frames available across all channels (stay aligned) */
		size_t avail = frames;
		for (size_t c = 0; c < channels; c++) {
			size_t a = t->buf[c].size / sizeof(float);
			avail = std::min(avail, a);
		}
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
			for (size_t i = 0; i < avail; i++) {
				float s = tmp[i] * gain;
				if (lim) {
					if (s > thr)
						s = thr;
					else if (s < -thr)
						s = -thr;
				}
				out[i] += s;
			}
		}
	}
	mtx.unlock();

	/* Always return true: the encoder expects a continuous stream, and
	 * silence (all-zero) is a valid, correctly-timed frame. */
	return true;
}

/* ------------------------------------------------------------------ */
/* Live control                                                        */
/* ------------------------------------------------------------------ */

void StreamMixAudio::set_gain(const std::string &name, float linear)
{
	mtx.lock();
	if (sm_track *t = find_track(name))
		t->gain.store(linear);
	mtx.unlock();
}

void StreamMixAudio::set_mute(const std::string &name, bool mute)
{
	mtx.lock();
	if (sm_track *t = find_track(name))
		t->mute.store(mute);
	mtx.unlock();
}

void StreamMixAudio::set_exclude(const std::string &name, bool exclude)
{
	mtx.lock();
	if (sm_track *t = find_track(name))
		t->exclude.store(exclude);
	mtx.unlock();
}

void StreamMixAudio::set_limiter(const std::string &name, bool on,
				 float threshold)
{
	mtx.lock();
	if (sm_track *t = find_track(name)) {
		t->limiter.store(on);
		t->limiter_threshold.store(threshold);
	}
	mtx.unlock();
}
