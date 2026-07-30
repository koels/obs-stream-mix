# Architecture

```
 OBS core: 6 recording mixes (Track 1..6)         Recording output
 ┌───────────────────────────────────────┐        (six isolated tracks,
 │  mix0  mix1  mix2  mix3  mix4  mix5     │───────► COMPLETELY UNTOUCHED)
 └──┬─────┬─────┬─────┬─────┬─────┬────────┘
    │     │     │     │     │     │   audio_output_connect()  (read-only taps)
    ▼     ▼     ▼     ▼     ▼     ▼
 ┌───────────────────────────────────────┐
 │ StreamMixAudio                          │
 │  per-track jitter buffers (deque)       │
 │  include / gain / mute / limiter        │  (stream-only)
 │  audio_output_open("stream_mix")        │  = private audio_t
 └───────────────────────┬─────────────────┘
                         │ audio_t*
                         ▼
 ┌───────────────────────────────────────┐
 │ StreamMixHook                           │
 │  on STREAMING_STARTING → connect to the │
 │  streaming output's "starting" signal   │
 │  on "starting" → obs_encoder_set_audio( │
 │      streamAudioEncoder, privateMix )   │
 └───────────────────────┬─────────────────┘
                         ▼
        OBS's normal streaming output + encoder + Start Streaming button
                         ▼
                  Streaming platform  (combined mix)
```

## Modules

| File | Role |
|------|------|
| `src/stream-mix-audio.*`  | Opens the private `audio_output`, taps the six core recording mixes with `audio_output_connect`, buffers each, and sums the included tracks (with stream-only gain/mute/limiter) in the mix's input callback. |
| `src/stream-mix-hook.*`   | Redirects OBS's own streaming audio encoder to the private mix at the streaming output's `"starting"` signal; restores it on stop. |
| `src/stream-mix-config.*` | JSON persistence of the six per-track settings. Zero-config defaults (all tracks included, unity gain). |
| `src/plugin-main.cpp`     | Module entry, frontend-event wiring, and the `streammix::` bridge. |
| `src/stream-mix-dock.cpp` | Qt settings panel: one row per track (include / volume / mute / limiter). |

## The audio path

1. **Tap.** `audio_output_connect(obs_get_audio(), i, NULL, cb, &track[i])` for
   `i = 0..5`. Each callback receives that recording mix's fully-mixed audio in
   OBS's native format (float planar, mix sample rate, mix channels) — no
   resampling. Connecting a listener is what makes OBS produce that mix; it does
   not alter recording.
2. **Buffer.** Each track's channels go into per-channel `deque`s, capped at
   ~0.5 s (oldest dropped if a mix outruns the clock).
3. **Combine.** The private `audio_output`'s input callback pulls the
   frame-aligned minimum available from every *included, unmuted* track, applies
   gain (and optional limiter), and sums into mix 0.
4. **Redirect.** `StreamMixHook` points OBS's stream encoder at this
   `audio_output` via `obs_encoder_set_audio`, at the one moment the encoder is
   assigned but not yet active (`docs/FEASIBILITY.md`).

## Threading

- The six tap callbacks (audio thread) and the mix input callback
  (audio_output thread) share the per-track buffers under one mutex; critical
  sections are a single 1024-frame block.
- Per-track settings are atomics, so dock changes are lock-light and take
  effect on the next audio block.

## Performance

- One extra audio mixdown (six mixes → one), plus the private `audio_output`
  thread. Audio is cheap relative to video; overhead is a fraction of a percent
  of a CPU core at 48 kHz stereo.
- **No extra video encode and no second network stream** — unlike the old
  separate-output design, this reuses OBS's own streaming output and encoders.
- Added latency is about one audio buffer (~21 ms at 48 kHz), well within
  normal stream buffering.
