# J3 Worship — Architecture baseline

## Non-negotiable runtime rule
The real-time audio callback must not allocate memory, perform filesystem I/O, wait on locks owned by UI/persistence code, load images, scan plugins, or do network/update work.

## Proposed production layers
1. **Audio Engine (native C++ / JUCE on Windows)**: ASIO device lifecycle, callback, buffers, clocks, transport.
2. **DSP**: HPF/LPF, 4-band parametric EQ, gate, compressor, J3 Denoise, meters.
3. **Routing**: inputs, buses, master, AUX/IEM; cycle detection; click/guide PA protection.
4. **Plugin Host**: VST3 scanner database, out-of-process scanner, plugin latency accounting, crash quarantine.
5. **Live Engine**: section graph, quantized transitions, infinite loops, FREE/PAD, count-in.
6. **Recording Engine**: per-channel writers, crash-recoverable chunk/index strategy, stereo mix writer.
7. **Session/Setlist Engine**: songs, scenes, safe channels, presets, autosave/recovery.
8. **UI**: dark, touch-safe LIVE mode, mixer, diagnostics; never owns audio state directly.
9. **Updater**: HTTPS manifest, version/hash/signature validation, no install during LIVE/recording, rollback metadata.

## Threading model
- Audio RT thread: lock-free handoff only.
- Disk recording thread(s): bounded queues; overflow is diagnosed, never stalls RT audio.
- UI thread: presentation/controller.
- Persistence thread: atomic snapshots and backups.
- Plugin scan worker/process: isolated from main session.
- Updater/network worker: disabled from install while live state is active.

## Windows production target
- Windows 10/11 x64.
- C++20 + JUCE (recommended) with ASIO support configured legally using required SDK/licensing.
- VST3 host via Steinberg/JUCE-supported VST3 APIs.
- Release x64, code signing when certificate is available.

## Hardware validation gates
XR18, physical ASIO I/O, commercial Waves/UA plugins, MIDI controllers, real IEM/PA routing and long-duration live runs must be tested on Windows hardware before a release is called production-ready.
