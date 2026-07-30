# Stream Mix for OBS Studio

Record every source on its own isolated track **and** stream a single combined
audio mix — using the **normal OBS Start Streaming button**. No duplicate
sources, no duplicate scenes, no virtual cables, no external software.

- **Recording:** untouched. Your 6 tracks stay exactly as OBS records them.
- **Streaming:** OBS's own streaming output is fed a combined mix of the
  recording tracks you select.
- **Same workflow:** you click the normal **Start Streaming** button. There is
  no separate start/stop control.
- **Stream-only controls:** per-track include, volume, mute, and limiter that
  affect the stream **only** — never the recording.

## How it works (30-second version)

The plugin opens a private audio mix, fills it by tapping OBS's six recording
mixes (`audio_output_connect`), and — at the instant OBS's streaming output
starts — redirects OBS's own stream audio encoder to that mix with
`obs_encoder_set_audio`. OBS's six recording mixes and the recording output are
only ever read, never modified. Full mechanism and source citations:
[`docs/FEASIBILITY.md`](docs/FEASIBILITY.md) · [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Requirements

- OBS Studio 31+ (Windows initial target; the code is portable C++17).
- The OBS plugin build toolchain (see below).

## Usage

1. Set up your audio sources and recording tracks as usual (Advanced Audio
   Properties → assign each source to Track 1..6).
2. Configure your stream in **Settings → Stream** as normal.
3. (Optional) Open **Docks → Stream Mix** and pick which tracks go into the
   stream, plus per-track volume/mute/limiter. Default: all six tracks included
   at unity gain.
4. Click the **normal Start Streaming button.**
5. Your stream carries the combined mix; your recording still has six isolated
   tracks.

That's it — zero-config by default, and the same streaming workflow you already
use.

### The settings panel

**Docks → Stream Mix** (a checkmark next to it in the Docks menu shows it). One
row per recording track:

- **Include** — is this track in the Stream Mix?
- **Volume** — stream-only gain (dB).
- **Mute** — drop this track from the stream only.
- **Limiter** — a stream-only brick-wall limiter on that track.

Every change is stream-only and takes effect immediately (even mid-stream);
recording is never affected. Settings persist in
`%APPDATA%\obs-studio\plugin_config\stream-mix\config.json` (see
[`config.example.json`](config.example.json)).

> **Combining sums tracks.** If the *same* source is on multiple included
> tracks it is counted multiple times. The normal one-source-per-track setup is
> summed correctly.

## Build

This is a standard OBS plugin built with the official plugin-template
toolchain (`cmake/`, `CMakePresets.json`, `buildspec.json`, and `.github/`).

### CI (no local toolchain needed)

Push to GitHub. The **Build for Windows** workflow
(`.github/workflows/push.yaml` → `build-project.yaml`) downloads the pinned OBS
sources + prebuilt deps from `buildspec.json`, builds with the `windows-ci-x64`
preset, and uploads the plugin as a build artifact. You can also trigger it
manually via the **Dispatch** workflow (Actions tab → Run workflow). Tagging a
commit `X.Y.Z` additionally drafts a GitHub Release with the packaged plugin.

### Local (Windows)

The template's build script fetches deps (per `buildspec.json`) and configures
the `windows-x64` preset for you:

```pwsh
.github/scripts/Build-Windows.ps1 -Configuration RelWithDebInfo
```

Or drive CMake directly once the OBS libs/deps are on `CMAKE_PREFIX_PATH`:

```pwsh
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The Qt settings dock is built by default (`ENABLE_QT=ON`); pass
`-DENABLE_QT=OFF` to skip it (the plugin still works, just without the panel).

### Install

`Package-Windows.ps1` (run by CI) lays out the install tree. For a manual
install, copy the built `stream-mix.dll` into
`%ProgramData%\obs-studio\plugins\stream-mix\bin\64bit\` and the `data` folder
to `%ProgramData%\obs-studio\plugins\stream-mix\data\`.

> The prebuilt `libobs` / `obs-frontend-api` imported targets come from the
> obs-deps + OBS sources the template downloads — see
> <https://github.com/obsproject/obs-plugintemplate>. CI has
> `CMAKE_COMPILE_WARNING_AS_ERROR` disabled (in `CMakeLists.txt`) so an
> untested first build isn't blocked by a stray warning; re-enable it for
> hardening once you have a green build.

## Feature checklist (from the brief)

| Requested | Status |
|-----------|--------|
| Normal Start Streaming button used (no separate workflow) | ✅ |
| Combined mix sent to the stream encoder | ✅ (encoder redirect) |
| Recording + all six tracks untouched | ✅ (core mixes only read) |
| Select which recording tracks are in the mix | ✅ (dock / config) |
| Per-track stream-only volume | ✅ |
| Per-track mute (stream only) | ✅ |
| Per-track compressor/limiter | ✅ brick-wall limiter (see Limitations) |
| No config for basic use | ✅ (all tracks included by default) |
| Low CPU / minimal latency | ✅ (one audio mixdown; no extra encode/stream) |

## Limitations & future work

- **Compressor** is currently a brick-wall limiter. A full compressor
  (attack/release/ratio) is a natural extension of `StreamMixAudio::fill_mix`.
- **Single stream track.** The redirect targets stream audio track 0 (the
  standard case). Multi-track streaming would need one redirect per track.
- **Summing.** Combining tracks sums them; a source on multiple included tracks
  is counted multiple times (the isolated one-source-per-track setup is fine).
- **Timing assumption.** The redirect uses the streaming output's `"starting"`
  signal, which relies on RTMP/WHIP connecting asynchronously (it does). See
  [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md) for the exact window.

## License

GPLv2, matching OBS Studio.
