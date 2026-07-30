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
		save(); /* write defaults on first run */
		return;
	}

	obs_data_array_t *arr = obs_data_get_array(root, "tracks");
	size_t count = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(arr, i);
		int idx = (int)obs_data_get_int(item, "index");
		if (idx >= 0 && idx < TRACK_COUNT) {
			sm_track_cfg &c = tracks[idx];
			c.include = obs_data_has_user_value(item, "include")
					    ? obs_data_get_bool(item, "include")
					    : true;
			c.gain_db = (float)obs_data_get_double(item, "gain_db");
			c.mute = obs_data_get_bool(item, "mute");
			c.limiter = obs_data_get_bool(item, "limiter");
			c.limiter_db =
				obs_data_has_user_value(item, "limiter_db")
					? (float)obs_data_get_double(item,
								     "limiter_db")
					: -1.0f;
		}
		obs_data_release(item);
	}
	if (arr)
		obs_data_array_release(arr);
	obs_data_release(root);
	blog(LOG_INFO, "config loaded");
}

void StreamMixConfig::save() const
{
	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();
	for (int i = 0; i < TRACK_COUNT; i++) {
		obs_data_t *item = obs_data_create();
		obs_data_set_int(item, "index", i);
		obs_data_set_bool(item, "include", tracks[i].include);
		obs_data_set_double(item, "gain_db", tracks[i].gain_db);
		obs_data_set_bool(item, "mute", tracks[i].mute);
		obs_data_set_bool(item, "limiter", tracks[i].limiter);
		obs_data_set_double(item, "limiter_db", tracks[i].limiter_db);
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
