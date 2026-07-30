# Feasibility analysis

Can a **stock OBS Studio plugin** deliver "6 fully-isolated recording tracks
**plus** one independent combined mix used only for streaming, with per-track
stream-only volume"?

Short answer:

- **The naive shape of the feature (a real 7th mix injected into OBS's own
  streaming output) is *not* possible from a plugin.** Two independent limits
  in libobs block it. Both are cited below.
- **The user-facing goal *is* achievable** by not injecting into OBS's stream
  output at all, and instead having the plugin own its own streaming output fed
  by a private mix. That is what this project implements. It is one of the
  extension points the brief explicitly allows ("a custom output module").

---

## Limit 1 — the six-mix ceiling

`libobs/media-io/audio-io.h`:

```c
#define MAX_AUDIO_MIXES 6
```

Every audio mix in OBS is one of these six buffers. A source's routing is a
6-bit mask (`obs_source_set_audio_mixers`), the recording output records a
subset of the six, and the streaming output reads one of the six. **Recording
and streaming index the same six buffers.**

So "6 isolated recording tracks" consumes all six mixes (one source per mix).
A 7th, combined mix would be mix index 6 — which does not exist. `6 + 1 = 7 > 6`.

A plugin cannot raise `MAX_AUDIO_MIXES`: it is a compile-time constant that
sizes fixed arrays and bitmasks throughout libobs (`audio_output_data`,
`obs_source` mixer masks, every encoder/output mix index). Changing it requires
recompiling libobs — i.e. an OBS fork or an upstream patch.

**Consequence:** even the manual "check a source into track 1 *and* its own
track" trick (Advanced Audio Properties) tops out at **5 isolated + 1
combined**, never 6 + 1.

## Limit 2 — no race-free hook to swap the stream audio encoder

Suppose you only need ≤5 isolated tracks, leaving a free mix — or suppose (as
this plugin does) you build a *private* `audio_output` outside the six core
mixes. You still have to attach a custom audio encoder to the **streaming
output**. There is no supported, race-free way to do that.

Ordering in `frontend/widgets/OBSBasic_Streaming.cpp`:

```
line 89:  OnEvent(OBS_FRONTEND_EVENT_STREAMING_STARTING);   // only pre-start event
line 99:  outputHandler->StartStreaming(service);           // runs AFTER
```

`StartStreaming()` calls into the output handler, which **unconditionally
(re)assigns the stream audio encoder**:

- `frontend/utility/SimpleOutput.cpp:668` — `obs_output_set_audio_encoder(streamOutput, audioStreaming, 0);`
- `frontend/utility/AdvancedOutput.cpp:319` — `obs_output_set_audio_encoder(streamOutput, streamAudioEnc, 0);`

So anything a plugin sets during `STREAMING_STARTING` is overwritten
milliseconds later. And by `STREAMING_STARTED` the output is already active, so
`obs_output_set_audio_encoder` (which no-ops on an active output) can no longer
change it. There is no frontend event in the gap between "encoder assigned" and
"output started."

**Consequence:** a plugin cannot redirect OBS's *own* streaming output's audio.

---

## The workaround this plugin uses

Don't fight either limit — sidestep both:

1. **Build a private mix.** Tap each source with
   `obs_source_add_audio_capture_callback`, mix the taps in a plugin-owned
   `audio_output` opened with `audio_output_open`. This mix lives entirely
   outside the six core mixes, so `MAX_AUDIO_MIXES` is irrelevant and OBS's six
   recording mixes are never touched. (Limit 1 sidestepped.)

2. **Own the streaming output.** Create a plugin `rtmp_output`, reuse the
   service from `obs_frontend_get_streaming_service()`, attach a video encoder
   and an audio encoder bound to the private mix via `obs_encoder_set_audio`.
   Because *we* own this output, nothing in OBS ever reassigns its encoders.
   (Limit 2 sidestepped.)

The one honest trade-off: you start/stop this stream from the plugin (Tools
menu, hotkey, or dock) rather than OBS's native **Start Streaming** button. Use
the plugin's control; leave OBS's button for a separate/normal stream if you
ever want one.

---

## Smallest upstream change that would remove the trade-off

If you wanted the feature wired into OBS's native Stream button with no plugin
gymnastics, the minimal upstream change is **one of**:

- **Add a dedicated "program/stream" mix** separate from the six recording
  mixes: a 7th `audio_output` mix that the streaming output reads by default,
  fed by the same source graph but with its own per-source gain vector. This is
  the cleanest and is roughly what this plugin prototypes in user space.
- **Or** raise `MAX_AUDIO_MIXES` (e.g. to 8) *and* add a frontend event fired
  after the output handler assigns encoders but before `obs_output_start`, so a
  plugin can legally redirect the stream encoder. This is more invasive and
  touches every fixed-size mix array.

The first option is the one worth proposing upstream.
