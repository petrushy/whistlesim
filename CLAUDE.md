# WhistleSim

A Mac app (JUCE 9 / CMake) that plays a physical model of a whistle or recorder from a WARBL2 wind controller, plus room reverb. WARBL sends raw sensor frames over USB or Bluetooth MIDI SysEx. The stream is in the official WARBL2 firmware from 4.8 (amowry/WARBL2, `warbl2_firmware/Sensor_stream.ino`; protocol version 3, spec in `docs/WARBL_sensor_stream_protocol_v3.md`; the user's fork, petrushy/WARBL2, is usually checked out at `../WARBL2-phy` with upstream as remote `upstream`). The app still decodes the earlier experimental firmware (fork branch `sensor-stream`, `SENSOR_STREAM.md`, protocol 1-2 with ID 7D 57). Open questions about 4.8 for its author are in `docs/WARBL-4.8-notes.md`. This repo was split out of that one's `host/` folder on 2026-11-10, with its history.

## Layout

- `Source/StreamProtocol.h`: protocol decoder and encoder, no JUCE. It must match the firmware; the ID and version byte in each message say which format it is.
- `Source/Whistle/`: geometry, frequency-domain acoustics (`Acoustics`) and the time-domain model (`WhistleModel`).
- `Source/*Page`: the Monitor, Instrument, Tuning workshop (`WorkshopPage`: fingering table, tuning goal, tuner), Sensors and Model tabs.
- `tools/`: lowercase. WarblSimulator, ReverbCheck, WhistleCheck, stream_monitor.py, fit_voicing.py (searches a voicing against a recording, with WhistleCheck), and `Real-examples/` (recordings of real whistles the presets are fitted to).

## Build and check

    cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
    ctest --test-dir build                                         # protocol, instrument-file, preset and played-tuning tests
    build/WhistleCheck_artefacts/Release/WhistleCheck chart 0       # whistle (or a name: chart recorder): predicted + played tuning
    build/ReverbCheck_artefacts/Release/ReverbCheck <dir>          # reverb times / balance vs. targets, WAVs

- iOS (app only, no tools or tests): `cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/build/_deps/juce-src"`, then `xcodebuild -project build-ios/WhistleSim.xcodeproj -scheme WhistleSim -configuration Release -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build`. iOS-only code is under `#if JUCE_IOS`; `PickedFile.h` copies files the iOS picker returns into the app. The iPhone screens are `Source/Phone/` (`PhoneUI`): they drive the desktop pages' controls through `DesktopControls` (a friend of MonitorPage, InstrumentPage and SensorsPage) rather than the engine, so they can't change the sound; the desktop window itself is hidden on an iPhone; `DesktopView` shows it scaled on an iPad. Check them on the Mac: `WhistleSim --phone --simulate [--phone-tab 0-4] [--phone-wide] [--phone-presets] --snapshot out.png`. Signing for the user's iPhone: README, "iPhone and iPad".
- The first configure downloads JUCE 9.0.5. LTO is deliberately off, because linking took many minutes.
- To check the app without anyone at the screen, use `build/WhistleSim_artefacts/Release/WhistleSim.app/Contents/MacOS/WhistleSim --simulate --instrument --snapshot out.png`. Other options: `--workshop`, `--size 2400x1250` (a wide window, for the two-column Instrument tab), `--learn` (Tuning workshop: learn the scripted player's breath for 20 s), `--sensors`, `--monitor`, `--model` (a live look inside the model; `--play` gives it something to show), `--about`, `--play`, `--output <device>`. Read the PNG.
- App state lives in `~/Library/Application Support/WhistleSim/WhistleSim.settings`; it's the user's real settings. A `--snapshot` run doesn't save state on exit, doesn't connect to MIDI devices (so it can't disturb the user's WARBL session; use `--simulate`), outputs silence (it still computes the sound, so the CPU figure is real), and may run alongside the user's own copy of the app. If a test edits the file, put it back afterwards.
- A MacBook with its lid closed runs no audio at all: the app shows "blocks 0", and even `afplay` hangs. That isn't an app bug.

## CI and releases

- `.github/workflows/ci.yml` builds and runs the tests on macOS, Linux (Ubuntu 24.04) and Windows (VS 2022) for every push to `main` and every pull request. Code must build with GCC and MSVC too, not only Apple Clang, and for iOS (the `ios` job).
- `.github/workflows/release.yml`: pushing a tag `vX.Y.Z` builds on all three platforms (macOS universal arm64 + x86_64; Windows with the C++ runtime linked in), tests and packages each, and publishes them as one GitHub Release. The tag must equal `VERSION` in `CMakeLists.txt` (the About window shows it), so bump that first. The app is ad-hoc signed, not notarized. Running the workflow by hand is a dry run that keeps the packages as workflow artifacts.
- The workflows take JUCE from a cached clone (`JUCE_VERSION` in both files); keep it in step with `GIT_TAG` in `CMakeLists.txt`.

## Conventions

- **Physical model principle:** sound comes from simulating the instrument. Never map fingerings to notes. Hole sensors give continuous openness, and pitch, half-holing, slides and overblowing must come out of the simulation. `Acoustics` (transfer-matrix method) is only a design aid, for the tuning table and hole placement.
- **Audio-thread code:** no locks (try-lock only), no allocation. Data goes from the MIDI thread to the audio thread through `SpscQueue`, and settings through atomics. Geometry is handed over to the audio thread with a try-locked pending copy.
- **Preset dimensions are tuned offline, to the played pitch.** `PlayedTuning` plays each note at the middle of the breath it sounds at and aims the resonances to make up for the jet's pull; whistles also get their head section's diameter fitted (it sets the octaves, which holes can't). After changing the model, the acoustics or a preset, run `WhistleCheck code` (or `playtune <preset>`) and paste the printed values into `WhistleGeometry.cpp`; `PlayedTuningTest` checks they play in tune. `WhistleCheck played <preset>` shows each note's range, pressure, played pitch and pull.
- **Holes:** there are eight, one per WARBL finger sensor (LT, L1–L3, R1–R4). Each has an enabled flag, and code must skip plugged holes.
- **Verifying model changes:** run `WhistleCheck` (`play`, `chart`, `slide`) before and after. It measures what the model actually plays. For a voicing, `analyse <recording> <preset>` scores it against a recorded whistle, and `transition`, `glide` and `breath` check it stays playable. `transition` and `glide` time the fundamental only: a rich tone's harmonics add zero crossings, which once read as register jumps (`raw=1` gives the old reading).
- **Protocol:** the official firmware defines it now; changes are suggested to its author (Andrew Mowry), not made here. Keep decoding the older versions, and send commands in both formats while the experimental firmware may still be in use.
- **Instrument files** keep the format ID `warbl-whistle` (older files must still load).
- Match the surrounding style: comments explain why, units are in comments, and variable names are descriptive.
