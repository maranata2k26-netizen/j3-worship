# J3 Worship

J3 Worship is a native Windows live-worship audio application focused on stability, low latency, safe routing, simple live operation, and broad interface compatibility.

## Device compatibility
J3 Worship is not tied to XR18. XR18 is only a useful reference device. On Windows, the desktop app uses JUCE's audio-device layer with ASIO enabled and WASAPI available as a fallback. Interfaces and digital mixers from Focusrite, Behringer, PreSonus, RME, MOTU, Audient, SSL and other vendors are intended to work when Windows exposes a compatible driver.

## 0.4.0-alpha engineering build
This revision includes:
- native Windows JUCE application, not Electron/web
- ASIO + WASAPI device selection
- persisted audio-device state between launches
- first-run ASIO preference when an ASIO device is available
- device/driver rescan and inventory
- automatic recovery attempt after an unexpected device stop/error
- safe-default startup with live input monitoring OFF
- selectable PA Left/Mono and PA Right outputs
- Click/Guide output reservation with a hard safety rule preventing Click from sharing PA outputs
- driver control-panel access when provided by the device
- live diagnostics: device, driver type, active I/O, sample rate, buffer, reported latency, XRuns/dropouts and last audio error
- 8-channel low-latency live mixer path with per-channel fader, pan, mute, DSP and output soft protection
- LIVE worship section controls with quantized section transitions and FREE/PAD state
- tested native core for routing, scenes, IEM models, recording, setlists, MIDI, pads, DSP, update safety and recovery
- multitrack recorder queue moved to preallocated heap memory to avoid Windows stack overflow while keeping the audio callback allocation-free

## Release boundary
A successful CI build proves that the Windows source compiles, the native core tests pass, and the installer can be generated. It cannot prove the behavior of every third-party ASIO driver, physical interface, PA/IEM wiring, or commercial VST3 plugin. J3 Worship therefore includes local System Check and safe routing controls, and production release still requires hardware validation on representative devices.
