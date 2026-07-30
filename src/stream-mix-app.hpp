/*
 * Stream Mix - shared application accessors (bridge for the Qt dock).
 */
#pragma once

class StreamMixAudio;
struct StreamMixConfig;

namespace streammix {

StreamMixAudio *audio();   /* live engine (may be inactive) */
StreamMixConfig *config(); /* persisted settings */
bool streaming();          /* is the Stream Mix currently feeding a stream? */

/* Apply + persist one track override. idx is 0..5. */
void set_track_include(int idx, bool include);
void set_track_gain_db(int idx, float db);
void set_track_mute(int idx, bool mute);
void set_track_limiter(int idx, bool on, float db);

} // namespace streammix
