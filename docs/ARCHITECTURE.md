# Architecture

```
 OBS source graph (unchanged)
 ┌───────────────────────────────────────────────┐
 │ Desktop  Mic  Discord  Spotify  Browser  Game  │
 └───┬────────┬──────┬───────┬────────┬──────┬────┘
     │        │      │       │        │      │        each source stays routed
     ▼        ▼      ▼       ▼        ▼      ▼        to its own OBS mix/track
  ╔═══════════════════════════════════════════════╗
  ║   OBS core: 6 mixes  ->  Recording output      ║   <- COMPLETELY UNTOUCHED
  ╚═══════════════════════════════════════════════╝
     │        │      │       │        │      │
     │ obs_source_add_audio_capture_callback (taps)
     ▼        ▼      ▼       ▼        ▼      ▼
  ┌───────────────────────────────────────────────┐
  │ StreamMixAudio                                 │
  │  per-source jitter buffers (deque, planar f32) │
  │  gain / mute / exclude / limiter  (STREAM ONLY)│
  │  audio_output_open("stream_mix")  = private mix│
  └───────────────────────┬───────────────────────┘
                          │ audio_t*  (mix 0)
                          ▼
  ┌───────────────────────────────────────────────┐
  │ StreamMixOutput                                │
  │  rtmp_output  (plugin-owned)                   │
  │   + video encoder  (obs_get_video())           │
  │   + audio encoder  bound to the private mix    │
  │   + service = obs_frontend_get_streaming_service│
  └───────────────────────┬───────────────────────┘
                          ▼
                    Streaming platform
```

## Modules

| File | Role |
|------|------|
| `src/stream-mix-audio.*`  | The private mix. Discovers audio sources routed to any recording track, taps them with `obs_source_add_audio_capture_callback`, buffers per channel in a `deque`, and mixes them in the `audio_output` input callback. Applies stream-only gain/mute/exclude/limiter. |
| `src/stream-mix-output.*` | Plugin-owned `rtmp_output`. Reuses OBS's configured stream service and inherits encoder settings from the current profile (with overrides). Audio encoder is bound to the private mix. |
| `src/stream-mix-config.*` | JSON persistence (`config.json`) of encoder settings and per-track overrides. Zero-config defaults. |
| `src/plugin-main.cpp`     | Module entry, Tools-menu actions, hotkeys, frontend-event cleanup, and the `streammix::` bridge. |
| `src/stream-mix-dock.cpp` | Optional Qt dock (`-DENABLE_QT_UI=ON`) for live per-track control. |

## The audio path in detail

1. **Discovery.** `obs_enum_sources` selects sources with `OBS_SOURCE_AUDIO`
   whose `obs_source_get_audio_mixers() != 0` — i.e. anything routed to a
   recording track. That is the "all active recording tracks" definition.

2. **Tap.** Each selected source gets an
   `obs_source_add_audio_capture_callback`. The callback delivers the source's
   audio already converted to OBS's mix format (float planar, mix sample rate,
   mix channel count) plus a `muted` flag. No resampling is needed. A globally
   muted source contributes silence, so the stream matches the recording.

3. **Buffer.** Each source's channels are pushed into per-channel `deque`s.
   Buffers are capped at ~0.5 s; a source that outruns the mix clock has its
   oldest audio dropped (prevents unbounded growth / latency creep).

4. **Mix.** The `audio_output`'s input callback pulls the frame-aligned minimum
   available from every source, applies per-track gain (and optional limiter),
   and sums into mix 0. Missing/short sources contribute silence for that block.
   The callback always returns a correctly-timed buffer, so the encoder sees a
   continuous stream.

5. **Encode + send.** The audio encoder is bound to this `audio_output` with
   `obs_encoder_set_audio`; the plugin's `rtmp_output` streams it alongside the
   shared video encoder.

## Threading

- Capture callbacks (audio thread) and the mix input callback (audio_output
  thread) both touch the per-source buffers under a single mutex. Critical
  sections are small (one 1024-frame block).
- Live control setters use atomics per field, so gain/mute/exclude changes are
  lock-light and click-free enough for streaming use.

## Why not just use source mixer bitmasks?

Routing every source into one spare core mix (Advanced Audio Properties) *does*
produce a combined track using OBS's own mixer — but (a) it costs one of the six
mixes, capping you at 5 isolated recording tracks, and (b) volume is per-source
and shared with recording, so there is no **stream-only** volume/compressor.
The private-mix design removes both restrictions. See `FEASIBILITY.md`.
