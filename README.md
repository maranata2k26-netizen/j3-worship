# J3 Worship

J3 Worship is a native Windows live-worship audio application under active engineering. The project is **device-agnostic**: XR18 is a reference/preset target, not a dependency. Focusrite, Behringer, PreSonus, RME, MOTU, Audient, SSL and other devices are intended to work through their compatible Windows ASIO drivers.

## Windows engineering build 0.3.0-alpha
This revision adds a real JUCE desktop application target in addition to the tested native core. On Windows it enables JUCE ASIO/WASAPI support and builds `J3Worship.exe`.

Current desktop shell includes:
- native Windows GUI (not Electron/web)
- Audio/MIDI device selector with ASIO enabled
- safe-default startup: live input monitoring starts OFF
- eight-input real-time mixer path with fader, pan, mute, DSP and soft output protection
- LIVE section controls (Intro, Verse, Pre-Chorus, Chorus, Bridge, Instrumental, Free/Pad, Ending)
- system diagnostics showing device, driver type, sample rate, buffer and reported I/O latency
- core architecture for routing protection, IEM, recording, scenes, pads, setlists, MIDI, VST3 catalog and recovery
- GitHub Actions Windows x64 build
- Inno Setup conventional installer build

## Important release boundary
`0.3.0-alpha` is an **engineering validation build**, not a production-live release. A CI pass proves that the Windows source compiles, core tests pass, and the installer is generated. It does **not** prove physical behavior with a particular interface, PA/IEM wiring or commercial plugin. Hardware validation remains mandatory before calling a build production-ready.

The final product goal remains: open J3 Worship, choose the installed ASIO device, configure PA/IEM safely, run System Check, Soundcheck, then LIVE.
