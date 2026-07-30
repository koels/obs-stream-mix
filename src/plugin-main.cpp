/*
 * Stream Mix for OBS Studio
 *
 * Feeds OBS's normal streaming output a single combined mix of the selected
 * recording tracks, while recording keeps every track isolated. The user uses
 * the normal Start Streaming button; there is no separate streaming workflow.
 *
 * See docs/ARCHITECTURE.md and docs/FEASIBILITY.md.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <memory>

#include "stream-mix-audio.hpp"
#include "stream-mix-hook.hpp"
#include "stream-mix-config.hpp"
#include "stream-mix-app.hpp"

#ifdef ENABLE_QT
void stream_mix_register_dock();
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("stream-mix", "en-US")

#define blog(level, format, ...) \
	blog(level, "[stream-mix] " format, ##__VA_ARGS__)

namespace {

std::unique_ptr<StreamMixAudio> g_audio;
std::unique_ptr<StreamMixHook> g_hook;
StreamMixConfig g_config;
bool g_streaming = false;

/* Push persisted per-track settings into the live engine. */
void apply_config()
{
	for (int i = 0; i < StreamMixConfig::TRACK_COUNT; i++) {
		const sm_track_cfg &c = g_config.tracks[i];
		g_audio->set_include(i, c.include);
		g_audio->set_gain(i, StreamMixConfig::db_to_mul(c.gain_db));
		g_audio->set_mute(i, c.mute);
		g_audio->set_limiter(i, c.limiter,
				     StreamMixConfig::db_to_mul(c.limiter_db));
	}
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		g_config.load();
		g_hook->on_streaming_starting();
		apply_config();
		g_streaming = true;
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		g_hook->on_streaming_stopped();
		g_streaming = false;
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		g_hook->on_streaming_stopped();
		break;
	default:
		break;
	}
}

} // namespace

/* --- streammix:: bridge (used by the Qt dock) --- */
namespace streammix {

StreamMixAudio *audio() { return g_audio.get(); }
StreamMixConfig *config() { return &g_config; }
bool streaming() { return g_streaming; }

void set_track_include(int idx, bool include)
{
	if (idx < 0 || idx >= StreamMixConfig::TRACK_COUNT)
		return;
	g_config.tracks[idx].include = include;
	g_audio->set_include(idx, include);
	g_config.save();
}
void set_track_gain_db(int idx, float db)
{
	if (idx < 0 || idx >= StreamMixConfig::TRACK_COUNT)
		return;
	g_config.tracks[idx].gain_db = db;
	g_audio->set_gain(idx, StreamMixConfig::db_to_mul(db));
	g_config.save();
}
void set_track_mute(int idx, bool mute)
{
	if (idx < 0 || idx >= StreamMixConfig::TRACK_COUNT)
		return;
	g_config.tracks[idx].mute = mute;
	g_audio->set_mute(idx, mute);
	g_config.save();
}
void set_track_limiter(int idx, bool on, float db)
{
	if (idx < 0 || idx >= StreamMixConfig::TRACK_COUNT)
		return;
	g_config.tracks[idx].limiter = on;
	g_config.tracks[idx].limiter_db = db;
	g_audio->set_limiter(idx, on, StreamMixConfig::db_to_mul(db));
	g_config.save();
}

} // namespace streammix

bool obs_module_load(void)
{
	g_audio = std::make_unique<StreamMixAudio>();
	g_hook = std::make_unique<StreamMixHook>(g_audio.get());
	g_config.load();

	obs_frontend_add_event_callback(on_frontend_event, nullptr);

#ifdef ENABLE_QT
	stream_mix_register_dock();
#endif

	blog(LOG_INFO, "loaded (v%s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	if (g_hook)
		g_hook->on_streaming_stopped();
	g_hook.reset();
	g_audio.reset();
	blog(LOG_INFO, "unloaded");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Sends a combined mix of your recording tracks to the stream, "
	       "while recording keeps every track isolated.";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Stream Mix";
}
