/*
 * Stream Mix - configuration implementation.
 */
#include "stream-mix-config.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <cmath>

#define blog(level, format, ...) \
	blog(level, "[stream-mix][config] " format, ##__VA_ARGS__)

float StreamMixConfig::db_to_mul(float db)
{
	if (db <= -96.0f)
		return 0.0f;
	return powf(10.0f, db / 20.0f);
}

static char *config_path()
{
	/* obs_module_config_path ensures a per-module directory. */
	char *dir = obs_module_config_path(nullptr);
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	return obs_module_config_path("config.json");
}

void StreamMixConfig::load()
{
	char *path = config_path();
	if (!path)
		return;

	obs_data_t *root = obs_data_create_from_json_file(path);
	bfree(path);
	if (!root) {
		/* First run: write defaults so the user has something to
		 * edit. */
		save();
		return;
	}

	obs_data_t *out = obs_data_get_obj(root, "output");
	if (out) {
		video_encoder_id =
			obs_data_get_string(out, "video_encoder_id");
		video_bitrate = (int)obs_data_get_int(out, "video_bitrate");
		if (obs_data_has_user_value(out, "keyint_sec"))
			keyint_sec = (int)obs_data_get_int(out, "keyint_sec");
		audio_encoder_id =
			obs_data_get_string(out, "audio_encoder_id");
		audio_bitrate = (int)obs_data_get_int(out, "audio_bitrate");
		obs_data_release(out);
	}

	tracks.clear();
	obs_data_array_t *arr = obs_data_get_array(root, "tracks");
	size_t count = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(arr, i);
		const char *name = obs_data_get_string(item, "name");
		if (name && *name) {
			sm_track_cfg c;
			c.gain_db = (float)obs_data_get_double(item, "gain_db");
			c.mute = obs_data_get_bool(item, "mute");
			c.exclude = obs_data_get_bool(item, "exclude");
			c.limiter = obs_data_get_bool(item, "limiter");
			c.limiter_db = obs_data_has_user_value(item,
							       "limiter_db")
					       ? (float)obs_data_get_double(
							 item, "limiter_db")
					       : -1.0f;
			tracks[name] = c;
		}
		obs_data_release(item);
	}
	if (arr)
		obs_data_array_release(arr);
	obs_data_release(root);
	blog(LOG_INFO, "loaded config: %zu track override(s)", tracks.size());
}

void StreamMixConfig::save() const
{
	obs_data_t *root = obs_data_create();

	obs_data_t *out = obs_data_create();
	obs_data_set_string(out, "video_encoder_id",
			    video_encoder_id.c_str());
	obs_data_set_int(out, "video_bitrate", video_bitrate);
	obs_data_set_int(out, "keyint_sec", keyint_sec);
	obs_data_set_string(out, "audio_encoder_id",
			    audio_encoder_id.c_str());
	obs_data_set_int(out, "audio_bitrate", audio_bitrate);
	obs_data_set_obj(root, "output", out);
	obs_data_release(out);

	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &kv : tracks) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "name", kv.first.c_str());
		obs_data_set_double(item, "gain_db", kv.second.gain_db);
		obs_data_set_bool(item, "mute", kv.second.mute);
		obs_data_set_bool(item, "exclude", kv.second.exclude);
		obs_data_set_bool(item, "limiter", kv.second.limiter);
		obs_data_set_double(item, "limiter_db", kv.second.limiter_db);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(root, "tracks", arr);
	obs_data_array_release(arr);

	char *path = config_path();
	if (path) {
		obs_data_save_json_safe(root, path, "tmp", "bak");
		bfree(path);
	}
	obs_data_release(root);
}
