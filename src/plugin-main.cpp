/*
 * Stream Mix for OBS Studio
 *
 * Adds an independent "Stream Mix" streaming output that combines every
 * audio source routed to a recording track into a single mixed stream,
 * while OBS keeps recording each source on its own isolated track.
 *
 * See docs/ARCHITECTURE.md and docs/FEASIBILITY.md.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <memory>

#include "stream-mix-audio.hpp"
#include "stream-mix-output.hpp"
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
std::unique_ptr<StreamMixOutput> g_output;
StreamMixConfig g_config;

obs_hotkey_id g_hk_start = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id g_hk_stop = OBS_INVALID_HOTKEY_ID;

/* Push persisted per-track overrides into the live audio engine. */
void apply_config()
{
	for (const auto &kv : g_config.tracks) {
		const std::string &name = kv.first;
		const sm_track_cfg &c = kv.second;
		g_audio->set_gain(name, StreamMixConfig::db_to_mul(c.gain_db));
		g_audio->set_mute(name, c.mute);
		g_audio->set_exclude(name, c.exclude);
		g_audio->set_limiter(name, c.limiter,
				     StreamMixConfig::db_to_mul(c.limiter_db));
	}
}

void do_start()
{
	if (g_output->active()) {
		blog(LOG_INFO, "Stream Mix already running");
		return;
	}
	g_config.load();
	if (g_output->start(g_config)) {
		apply_config();
	}
}

void do_stop()
{
	g_output->stop();
}

/* --- Tools menu callbacks --- */
void menu_start(void *) { do_start(); }
void menu_stop(void *) { do_stop(); }
void menu_reload(void *)
{
	g_config.load();
	if (g_audio->active()) {
		g_audio->refresh_sources();
		apply_config();
	}
	blog(LOG_INFO, "config reloaded");
}

/* --- Hotkeys --- */
void hk_start(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		do_start();
}
void hk_stop(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		do_stop();
}

/* --- Frontend events --- */
void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		if (g_output && g_output->active()) {
			blog(LOG_INFO, "stopping Stream Mix (frontend event)");
			do_stop();
		}
		break;
	default:
		break;
	}
}

} // namespace

/* --- streammix:: bridge (used by the optional Qt dock) --- */
namespace streammix {

StreamMixAudio *audio() { return g_audio.get(); }
StreamMixConfig *config() { return &g_config; }
void start() { do_start(); }
void stop() { do_stop(); }
bool active() { return g_output && g_output->active(); }

void set_track_gain_db(const std::string &name, float db)
{
	g_config.tracks[name].gain_db = db;
	g_audio->set_gain(name, StreamMixConfig::db_to_mul(db));
	g_config.save();
}
void set_track_mute(const std::string &name, bool mute)
{
	g_config.tracks[name].mute = mute;
	g_audio->set_mute(name, mute);
	g_config.save();
}
void set_track_exclude(const std::string &name, bool exclude)
{
	g_config.tracks[name].exclude = exclude;
	g_audio->set_exclude(name, exclude);
	g_config.save();
}
void set_track_limiter(const std::string &name, bool on, float db)
{
	g_config.tracks[name].limiter = on;
	g_config.tracks[name].limiter_db = db;
	g_audio->set_limiter(name, on, StreamMixConfig::db_to_mul(db));
	g_config.save();
}

} // namespace streammix

bool obs_module_load(void)
{
	g_audio = std::make_unique<StreamMixAudio>();
	g_output = std::make_unique<StreamMixOutput>(g_audio.get());
	g_config.load();

	obs_frontend_add_tools_menu_item(
		obs_module_text("Menu.Start"), menu_start, nullptr);
	obs_frontend_add_tools_menu_item(
		obs_module_text("Menu.Stop"), menu_stop, nullptr);
	obs_frontend_add_tools_menu_item(
		obs_module_text("Menu.Reload"), menu_reload, nullptr);

	obs_frontend_add_event_callback(on_frontend_event, nullptr);

	g_hk_start = obs_hotkey_register_frontend(
		"stream_mix.start", obs_module_text("Hotkey.Start"),
		hk_start, nullptr);
	g_hk_stop = obs_hotkey_register_frontend(
		"stream_mix.stop", obs_module_text("Hotkey.Stop"), hk_stop,
		nullptr);

#ifdef ENABLE_QT
	stream_mix_register_dock();
#endif

	blog(LOG_INFO, "loaded (v%s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	if (g_hk_start != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hk_start);
	if (g_hk_stop != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hk_stop);

	if (g_output)
		g_output->stop();
	g_output.reset();
	g_audio.reset();
	blog(LOG_INFO, "unloaded");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Combines all recording tracks into one Stream Mix output for "
	       "streaming, while recording keeps every source isolated.";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Stream Mix";
}
