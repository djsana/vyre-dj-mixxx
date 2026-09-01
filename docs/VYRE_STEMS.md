# VYRE DJ stems architecture

VYRE DJ's stable 2.5-based build uses **Stem Lite**, a low-cost stereo
mid/side focus mode. It is useful for quick vocal or instrumental emphasis, but
it is not source separation and must not be presented as four independent
stems.

Real stems use a two-stage design so neural inference never runs on the live
audio thread:

1. `tools/vyre_stems.py` fingerprints the track and looks for a cached result.
2. A background process uses FFmpeg, Demucs, and Stemgen to create a four-part
   `.stem.m4a` file.
3. Results and job manifests live below
   `%LOCALAPPDATA%\VYRE DJ\stems`; source folders remain untouched.
4. A stem-capable VYRE engine loads the prepared container and exposes separate
   drums, bass, vocals, and other controls.

The fourth step requires VYRE's planned forward port to the stem-capable Mixxx
2.6 code line. Do not load generated containers in the stable 2.5 engine and do
not perform separation during playback.

## Worker commands

```powershell
py -3 tools\vyre_stems.py check
py -3 tools\vyre_stems.py --stemgen-root C:\path\to\stemgen prepare C:\Music\track.mp3
```

The dependency check is intentionally non-destructive. VYRE should offer the
runtime as an optional component and display estimated storage/processing costs
before downloading model files.
