# DT2 Emulator — VST3 / AU plugin (macOS)

A Digitakt II–style drum computer and sampler as a plugin. It shows the same panel as the
web emulator, and makes the sound in C++ (JUCE) inside your DAW.

## What you need

- macOS 11 or later (Apple silicon or Intel; the build is universal)
- Xcode 15 or later, with command line tools
- CMake 3.22 or later (`brew install cmake`)
- An internet connection the first time you build (CMake downloads JUCE 8.0.4)

## Build and install

```bash
cd Digitakt2Plugin
./build-mac.sh
```

The build copies the plugins to:

- `~/Library/Audio/Plug-Ins/VST3/DT2 Emulator.vst3`
- `~/Library/Audio/Plug-Ins/Components/DT2 Emulator.component` (AU)

A standalone app is also built in `build/DT2Emulator_artefacts/Release/Standalone/`.
Rescan plugins in your DAW. To check the AU: `auval -v aumu Dt2p Dt2e`.

## No Mac toolchain? Build it on GitHub

`.github/workflows/build-mac.yml` builds the plugin on a GitHub-hosted Mac, so you don't need
Xcode or CMake yourself:

1. Create a GitHub repository and push this folder to it (or upload the files on github.com).
2. Open **Actions → Build macOS plugin → Run workflow**.
3. When it finishes (about 10–15 minutes), download the **DT2-Emulator-macOS** artifact. It
   contains `DT2-Emulator-VST3.zip` and `DT2-Emulator-AU.zip`.
4. Unzip them and copy the bundles:
   - `DT2 Emulator.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
   - `DT2 Emulator.component` → `~/Library/Audio/Plug-Ins/Components/`
5. The bundles are only ad-hoc signed, so macOS may block them. Clear the download flag:

   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"DT2 Emulator.vst3"
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"DT2 Emulator.component"
   ```

6. Rescan plugins in your DAW. Logic Pro and GarageBand may need a restart to pick up the AU.

If the build fails, open the failed step's log and copy the error lines.

## Using it in a DAW

- **Instrument track:** load "DT2 Emulator" as an instrument (VST3 or AU).
- **Tempo and transport:** when the DAW plays, the sequencer follows its tempo and position.
  When the DAW is stopped, the panel's PLAY/STOP run it on the panel tempo.
- **Outputs:** the main stereo output carries the full mix with effects. Sixteen extra
  stereo outputs ("Track 1"–"Track 16") carry each track on its own (after its level and
  mute, before the send effects and compressor). Turn them on in the DAW's multi-output
  options (Logic: "Multi-Output" version of the AU; Ableton/Bitwig/Reaper: route the extra
  outputs).
- **Playing tracks from the DAW:** MIDI notes 36–51 (C1–D♯2) play tracks 1–16.
- **MIDI tracks:** tracks set to the MIDI machine send their notes, CCs, pitch bend,
  aftertouch and program changes to the DAW's MIDI output from this plugin (route it to
  another instrument track).
- **Saving:** everything (patterns, sounds, songs, presets, kits and imported samples) is
  saved inside the DAW project.

## Differences from the web version

- **Sampling from a microphone** isn't available inside the plugin. Import audio files
  (FUNC + waveform key → + IMPORT AUDIO FILES) instead.
- **Projects** (Settings → PROJECT) are replaced by the DAW project.
- **MIDI CONFIG** (port and clock) doesn't apply: MIDI goes to the DAW, and the DAW is the clock.
- A new plugin instance makes no sound until its window has been opened once, because the
  patterns are created by the panel. Projects restored by the DAW play straight away.
- The STRETCH machine and the reverb are approximations; the reverb uses JUCE's built-in
  reverb rather than the web version's convolution reverb, so it sounds different.

## How it is built

| Folder / file | What it is |
|---|---|
| `Source/Engine.*` | Sequencer (host-synced, sample-accurate), voices, mixer, delay, reverb, chorus, compressor, master overdrive |
| `Source/Voice.*` | One track voice: ONESHOT, REPITCH, STRETCH, SLICE, GRID; MULTI-MODE, LOWPASS 4, EQ, COMB−/+, LEGACY; envelopes; 3 LFOs |
| `Source/Samples.*` | The 24 factory sounds, synthesized with the same algorithms as the web version |
| `Source/Model.*`, `Params.h` | The project data sent by the panel; parameter IDs match the web app exactly |
| `Source/PluginEditor.*` | The plugin window: the panel in a web view, plus the bridge between panel and engine |
| `webui/` | The built panel (from the web app), embedded into the plugin binary |

The panel talks to the engine through JUCE's web view events: it sends the full project as
JSON whenever something changes, plus transport commands, preview triggers and imported
samples. The engine sends back the play position, trigger flashes and tempo 30 times a
second.

## Updating the panel

The panel is the web app built with Vite. After changing the web app, rebuild it into
`webui/` and rebuild the plugin:

```bash
npx vite build --outDir /path/to/Digitakt2Plugin/webui --emptyOutDir
./build-mac.sh
```

## Notes

- This code has **not been compiled or tested**: it was written without access to a
  compiler, JUCE or a DAW. Expect to fix some compile errors on the first build.
- JUCE is licensed under AGPLv3 or a commercial licence; check that your use fits one of them.
- To share the plugin with others you need to sign and notarize it with an Apple Developer ID.
- "Digitakt" is a trademark of Elektron. This plugin is an independent emulation, and is not
  affiliated with or endorsed by Elektron.
