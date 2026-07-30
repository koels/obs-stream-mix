/*
 * Stream Mix - shared application accessors.
 *
 * Thin bridge so the optional Qt dock can drive the same engine/config/output
 * instances owned by plugin-main.cpp without duplicating global state.
 */
#pragma once

#include <string>

class StreamMixAudio;
struct StreamMixConfig;

namespace streammix {

StreamMixAudio *audio();       /* live audio engine (may be inactive) */
StreamMixConfig *config();     /* persisted settings */

void start();                  /* start the Stream Mix output */
void stop();                   /* stop it */
bool active();                 /* is the output live? */

/* Apply + persist a single track override (used by the dock). */
void set_track_gain_db(const std::string &name, float db);
void set_track_mute(const std::string &name, bool mute);
void set_track_exclude(const std::string &name, bool exclude);
void set_track_limiter(const std::string &name, bool on, float db);

} // namespace streammix
