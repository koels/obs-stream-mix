/*
 * Stream Mix - configuration (per recording track, stream-only).
 *
 * Persisted to <module config dir>/config.json. Defaults include all six
 * tracks at unity gain, so the plugin works with zero configuration.
 */
#pragma once

#include <obs.h>

struct sm_track_cfg {
	bool include = true;
	float gain_db = 0.0f;
	bool mute = false;
	bool limiter = false;
	float limiter_db = -1.0f;
};

struct StreamMixConfig {
	static constexpr int TRACK_COUNT = 6;
	sm_track_cfg tracks[TRACK_COUNT];

	void load();
	void save() const;

	static float db_to_mul(float db);
};
