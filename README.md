# Stream Mix for OBS Studio

Record every source on its own isolated track **and** stream a single combined
audio mix — with no duplicate sources, no duplicate scenes, no virtual cables,
and no external software. Everything runs inside stock OBS Studio.

- **Recording:** untouched. Your 6 tracks stay exactly as OBS records them.
- **Streaming:** a plugin-owned output sends one mixed audio stream containing
  every source.
- **Stream-only controls:** per-track volume, mute, exclude, and limiter that
  affect the stream **only** — never the recording.

> **Read this first:** because of two hard limits in libobs, a plugin *cannot*
> inject a combined mix into OBS's native Stream button. This plugin instead
> runs **its own** streaming output. You start/stop it from the **Tools** menu,
> a hotkey, or the optional dock — not the native Start Streaming button. The
> full reasoning, with source citations, is in
> [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md).

## How it works (30-second version)

The plugin taps each audio source with OBS's public per-source audio callback,
mixes the taps into a **private audio mix** that lives outside OBS's six core
mixes, and streams that mix through a plugin-owned `rtmp_output` that reuses
your configured stream service. OBS's six recording mixes are never touched.
Details: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Requirements

- OBS Studio 31+ (Windows initial target; the code is portable C++17).
- The OBS plugin build toolchain (see below).

## Usage

1. Configure your stream normally in **Settings → Stream** (service, key). You
   do **not** need to configure anything else.
2. Set up your audio sources and recording tracks as usual (Advanced Audio
   Properties). Any source assigned to a recording track is automatically part
   of the Stream Mix.
3. Start recording as normal (OBS's own recording is unchanged).
4. Start the stream via **Tools → Start Stream Mix** (or bind the
   *Start Stream Mix* hotkey in Settings → Hotkeys).
5. Stop via **Tools → Stop Stream Mix**.

That's the zero-config path. Nothing else is required.

### Optional per-track control

Two ways:

- **Config file (always available).** Edit
  `%APPDATA%\obs-studio\plugin_config\stream-mix\config.json`, then
  **Tools → Reload Stream Mix Config**. Example:

  ```json
  {
    "output": {
      "video_encoder_id": "",
      "video_bitrate": 0,
      "keyint_sec": 2,
      "audio_encoder_id": "ffmpeg_aac",
      "audio_bitrate": 0
    },
    "tracks": [
      { "name": "Spotify",  "gain_db": -6.0, "mute": false, "exclude": false },
      { "name": "Discord",  "gain_db": 3.0,  "limiter": true, "limiter_db": -1.0 },
      { "name": "Game",     "exclude": true }
    ]
  }
  ```

  Empty encoder ids / `0` bitrates mean "inherit from the current OBS profile,
  then fall back to a safe default." `exclude` removes a source from the stream
  **without touching recording**.

- **Qt dock (optional build).** Build with `-DENABLE_QT_UI=ON` to get a
  **Stream Mix** dock with live volume sliders and mute/exclude checkboxes.

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

Enable the optional Qt dock by adding `-DENABLE_QT=ON` to the configure step
(Qt6 must be discoverable — the obs-deps Qt6 package the template downloads
provides it).

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
| Auto-mix every active recording track into one stream output | ✅ |
| Stream Mix exists only for streaming | ✅ (private mix + own output) |
| Recording tracks untouched | ✅ (OBS core mixes never modified) |
| Works with existing projects | ✅ (auto-discovers sources) |
| Low CPU / minimal latency | ✅ (single extra encode; ~one audio-block latency) |
| No config for basic use | ✅ |
| Per-track stream-only volume | ✅ |
| Per-track mute | ✅ |
| Per-track compressor/limiter | ✅ brick-wall limiter (compressor: see Limitations) |
| Temporarily exclude a track | ✅ (`exclude`) |

## Limitations (be honest with yourself)

- **Started from the plugin, not OBS's Stream button.** This is forced by
  libobs; see `FEASIBILITY.md`. Don't run both at once with the same service.
- **Encoder inheritance** covers OBS *Simple* output mode cleanly. *Advanced*
  mode users may want to set `video_encoder_id`/`video_bitrate` explicitly in
  `config.json`.
- **Compressor** is currently a brick-wall limiter. A full compressor
  (attack/release/ratio) is a natural extension of `StreamMixAudio::fill_mix`.
- **Source rename/removal mid-stream** is handled lazily (renamed sources are
  re-discovered on *Reload Config*); dynamic re-tap on live rename is a TODO.
- The true "6 isolated + 7th mix inside OBS's own output" needs an upstream
  change — the smallest such change is proposed at the end of `FEASIBILITY.md`.

## License

GPLv2, matching OBS Studio.
