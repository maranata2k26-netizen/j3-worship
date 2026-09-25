# J3 Worship Test Report — 0.4.0-alpha

## Native core
- Release build: PASS
- Core functional tests: PASS
- AddressSanitizer + UndefinedBehaviorSanitizer: PASS
- ThreadSanitizer: PASS

## Covered core behavior
- generic audio-device capability model (2x2 through multichannel)
- device disconnect/reconnect state
- dropout counter
- safe routing: Click/Guide direct-to-PA blocked
- routing-cycle prevention
- drum/vocal presets and conservative pan behavior
- scenes and safe channels
- LIVE quantized transitions and FREE/PAD
- click timing/accent model
- DSP stress: HPF, LPF, parametric EQ, gate, compressor, denoise
- seamless pad loop model
- stem-name classification
- setlist reorder/current/next/advance
- MIDI mapping
- independent IEM mix model and copy-mix
- worship Auto Setup layout
- Panic behavior preserving live inputs
- SPSC queue ordering/overflow
- atomic session recovery
- multitrack WAV recording on worker thread
- VST3 catalog discovery/search/quarantine model
- updater semantic-version and live/recording safety rules
- diagnostic report generation

## Windows CI finding fixed in 0.4
The initial Windows test executable crashed because MultiTrackRecorder embedded approximately 4 MB of preallocated recording queue storage directly in a stack object. Windows' default executable stack is substantially smaller than typical Linux defaults. The queue is now allocated once on the heap before recording; no heap allocation is added to the real-time submit path. Release/ASan/UBSan/TSan tests pass after this change.

## Hardware boundary
CI cannot physically validate vendor drivers or analog/digital wiring. Representative hardware tests remain required for production certification (at minimum: one 2x2 ASIO interface, one multichannel USB interface, and one digital mixer such as XR18/X32-class hardware). The application is deliberately device-agnostic and uses runtime enumeration instead of vendor-specific hardcoding.
