# VYRE DJ on-demand stems

## Product boundary

VYRE's current `Stem Lite` pads are a zero-latency mid/side focus effect. They
are useful while playing, but they are not neural source separation and must
not be described as generated stems.

Real on-demand stems should run as a background job outside the real-time audio
engine. The worker creates files once, VYRE caches them, and later playback uses
the cached result. Neural inference must never run in an audio callback.

## Recommended first implementation

1. Add **Generate stems** to the library track context menu and track inspector.
2. Queue one background separation job at a time.
3. Start with Demucs `mdx_q` on CPU. It is a smaller quantized model and is a
   sensible default for an integrated-GPU Windows machine.
4. Expose two presets:
   - **Eco:** two CPU threads, one job, reduced overlap, low process priority.
   - **Quality:** up to half the physical CPU cores and normal overlap.
5. Generate four sources once: vocals, drums, bass, and other. Demucs documents
   that its two-stem output still performs the full separation, so a two-stem
   button should not be advertised as a cheaper inference path.
6. Package the result into a stem container supported by the playback engine,
   or retain lossless per-source files behind a VYRE stem-track manifest.
7. Show explicit states in the library: **Not generated**, **Queued**,
   **Separating 42%**, **Ready**, and **Failed — Retry**.

## Storage and cache

Never write generated files beside the user's song.

Use one application-owned root:

`%LOCALAPPDATA%\VYRE DJ\stems`

Suggested layout:

```text
stems/
  cache/<track-fingerprint>/<model-id>/manifest.json
  cache/<track-fingerprint>/<model-id>/vocals.flac
  cache/<track-fingerprint>/<model-id>/drums.flac
  cache/<track-fingerprint>/<model-id>/bass.flac
  cache/<track-fingerprint>/<model-id>/other.flac
  work/<job-id>/
  logs/<job-id>.log
```

The fingerprint should include the canonical source path, file size, modified
time, and a content sample hash. Write to `work`, validate all outputs, then
atomically rename the finished directory into `cache`. A cancelled or failed
job must not appear as ready.

## Playback prerequisite

This VYRE branch is based on Mixxx 2.5.6 and does not contain the later
multi-channel stem playback engine. Real generated stems therefore require one
of these playback foundations before the pads can control four independent
sources:

1. forward-port VYRE onto the Mixxx version that supports NI stem tracks; or
2. add a VYRE manifest-backed multi-source deck implementation.

Forward-porting is the lower-risk route. Mixxx's own stem documentation says
its current stem mixing support consumes previously generated stem files; stem
generation remains a separate third-party/background step.

## Runtime isolation

- Ship the separator as an optional worker package, not inside `VYRE DJ.exe`.
- Communicate over JSON lines through a child process or local named pipe.
- Set CPU and memory limits from VYRE settings.
- Keep one process per job and make cancellation graceful.
- Do not download models during a live set without an explicit user action.
- Record the model name and version in every manifest so cache invalidation is
  deterministic.
- Verify model-weight and training-data distribution terms before commercial
  bundling even when the surrounding code is MIT licensed.

## Primary references

- Demucs usage, quantized models, CPU jobs and two-stem behavior:
  https://github.com/facebookresearch/demucs/blob/main/README.md
- Mixxx stem mixing and on-the-fly generation boundary:
  https://mixxx.org/news/2024-08-26-stem-mixing/
- Mixxx on-demand generation workflow discussion:
  https://github.com/mixxxdj/mixxx/issues/14592
- ONNX Runtime CPU thread and spinning controls:
  https://onnxruntime.ai/docs/performance/tune-performance/threading.html
- Open-Unmix implementation and model licensing notes:
  https://github.com/sigsep/open-unmix-pytorch

