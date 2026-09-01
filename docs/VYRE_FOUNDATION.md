# VYRE DJ Mixxx foundation

VYRE DJ's second-generation foundation is pinned to Mixxx 2.5.6. The existing
JUCE VYRE application remains in its original repository while this fork is
validated and branded.

## Why this foundation

Mixxx already provides the systems that must behave together in professional
DJ software: BPM and beat-grid analysis, phase-aware sync, vinyl-style pitch
bend and scratching, key lock, multiple EQ and isolator models, crossfader
curves, scrolling GPU waveforms, hot cues, a persistent analyzed-track library,
and MIDI/HID controller mapping.

## License

Mixxx is GPL-2.0-or-later. A distributed VYRE derivative built from this fork
must remain GPL-compatible and make its corresponding source available.

## Windows 2026 bootstrap

Visual Studio 2026 removed the legacy checked-array iterator helpers referenced
by the Qt 6.5 headers in the Mixxx 2.5 Windows dependency bundle. Run the VYRE
wrapper once to download the official build environment and apply the narrow
header compatibility patch:

```powershell
& .\tools\vyre_windows_buildenv.ps1
```

Configure from a Visual Studio x64 developer environment with the generated
vcpkg toolchain, then build the `mixxx` target in `RelWithDebInfo`.

The verified local output is:

```text
build\vyre-release\RelWithDebInfo\mixxx.exe
```

Launch the development build with isolated settings and the correct Qt/resource
paths:

```powershell
& .\tools\run_vyre_foundation.ps1
```

The isolated settings directory is `build\vyre-settings`; it does not reuse or
overwrite another Mixxx installation's library.
