# Feasibility analysis

Can a **stock OBS Studio plugin** send a combined mix to the **native Start
Streaming button** while recording keeps all six tracks isolated — no forks, no
virtual cables, no duplicate sources?

**Yes.** This document explains the two constraints people assume make it
impossible, and the supported integration point that works around both.

> Earlier revisions of this file concluded the native button could not be used
> and that a plugin-owned output was required. That conclusion was **wrong**.
> The corrected mechanism is below.

---

## Constraint 1 — the six-mix ceiling (real, but not in the way)

`libobs/media-io/audio-io.h`:

```c
#define MAX_AUDIO_MIXES 6
```

OBS has exactly six audio mixes. Recording and streaming both index these six.
So "6 isolated recording tracks + a 7th combined track" cannot exist as a
*track* — there is no 7th slot, and a plugin can't raise a compile-time
constant.

**Why it doesn't block us:** the combined mix does not need to be one of OBS's
six mixes. A plugin can open its **own** `audio_output` (via
`audio_output_open`) that lives entirely outside the six, and *fill* it by
tapping the six core mixes with `audio_output_connect` (which hands you each
recording mix's fully-mixed audio). The six recording mixes and the recording
output are read-only from our side and never change.

## Constraint 2 — the stream encoder assignment (real, but beatable)

At stream start, OBS's output handler assigns the stream audio encoder:

- `frontend/utility/SimpleOutput.cpp:668` / `AdvancedOutput.cpp:319` —
  `obs_output_set_audio_encoder(streamOutput, <encoder>, 0)`

and this runs **after** the only pre-start frontend event
(`OBS_FRONTEND_EVENT_STREAMING_STARTING`, `OBSBasic_Streaming.cpp:89`). So you
cannot win by swapping the output's *encoder* at STREAMING_STARTING — OBS
overwrites it milliseconds later.

**The key realization:** you don't swap the encoder object. You **redirect the
encoder's audio source** — which `audio_t` it pulls from — with
`obs_encoder_set_audio`. OBS never re-calls that at stream start; it only
assigns the encoder to the output. And `obs_encoder_set_audio` is permitted as
long as the encoder is **not yet active** (`obs-encoder.c:1250`).

The timing window exists and is comfortable. In `obs-output.c`:

```
obs_output_start()
  └─ obs_output_actual_start()      # info.start(): RTMP begins ASYNC connect
  └─ do_output_signal("starting")   # line 411  ← encoder assigned, NOT active
       ...                          # (async) RTMP connects over the network
  └─ start_audio_encoders()         # line 2430 ← encoder becomes active here
```

The `"starting"` signal fires synchronously inside `obs_output_start`, right
after the encoder has been assigned but before the asynchronous RTMP connection
completes and `start_audio_encoders` marks the encoder active. That is the
supported hook.

## The integration this plugin uses

1. On `OBS_FRONTEND_EVENT_STREAMING_STARTING`: open the private mix, tap the six
   core recording mixes, and connect a handler to the streaming output's
   `"starting"` signal.
2. On `"starting"`: `obs_encoder_set_audio(streamAudioEncoder, privateMix)`.
   OBS's own encoder now pulls the combined mix. Native button, native encoder,
   native everything else.
3. On stream stop: detach the signal only. The private mix is **not** torn down
   and the encoder is **not** rebound — OBS disconnects the encoder from the mix
   asynchronously (`end_data_capture_thread`), so freeing the mix on stop is a
   use-after-free. The mix is opened once and lives for the plugin's lifetime.

Recording is untouched (we only *read* its mixes). Streaming carries the
combined mix. No 7th track is needed because the mix is a private `audio_t`, not
a track.

## Why the earlier "separate output" design is no longer needed

A previous version created its own `rtmp_output` and required a separate
Start/Stop control. That worked but violated the "use the native button"
requirement. The encoder-redirect approach above removes that compromise
entirely, so the separate-output module was deleted.

## Residual limitations

- The redirect targets audio track 0 of the streaming output (the standard
  single stream track). Multi-track streaming (e.g. some enhanced-broadcast
  configurations) would need one redirect per stream track — a small extension.
- The `"starting"`-signal window relies on RTMP's asynchronous connect. For an
  exotic streaming output that begins data capture *synchronously* inside
  `info.start` (before `"starting"`), the encoder would already be active and
  the redirect would be refused (and logged). Standard RTMP/WHIP outputs connect
  asynchronously, so this is not a concern in practice.
- Combining tracks sums them: if the *same* source is routed to multiple
  included tracks it is counted multiple times. The canonical one-source-per-
  track setup (the whole point of isolated tracks) is summed correctly.
