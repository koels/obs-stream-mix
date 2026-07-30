/*
 * Stream Mix - configuration.
 *
 * Persisted to <module config dir>/config.json. Provides both output
 * (encoder) settings and per-track stream-only overrides. All fields have
 * sane defaults so the plugin works with zero configuration.
 */
#pragma once

#include <obs.h>
#include <string>
#include <map>

struct sm_track_cfg {
	float gain_db = 0.0f;
	bool mute = false;
	bool exclude = false;
	bool limiter = false;
	float limiter_db = -1.0f;
};

struct StreamMixConfig {
	/* Output / encoder. Empty encoder ids => inherit from the current
	 * profile, then fall back to a safe default. */
	std::string video_encoder_id; /* e.g. "obs_x264", "jim_nvenc" */
	int video_bitrate = 0;        /* kbps; 0 => inherit/default */
	int keyint_sec = 2;
	std::string audio_encoder_id; /* e.g. "ffmpeg_aac" */
	int audio_bitrate = 0;        /* kbps; 0 => inherit/default */

	std::map<std::string, sm_track_cfg> tracks;

	/* Load/save against the module config directory. */
	void load();
	void save() const;

	static float db_to_mul(float db);
};
