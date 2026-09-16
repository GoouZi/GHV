# libghv API Foundation

The current C++17 foundation is declared in `libghv/include/ghv/ghv.h`.

`ghv::Decoder` provides:

- `open` / `close` and structured `ErrorCode` + message;
- immutable `Metadata` with resolution, rational FPS, duration and audio data;
- `decode_next` returning shared YUV420 storage, per-plane pointers/strides,
  frame index, PTS and duration;
- 64-bit timestamp `seek_us`, using the nearest preceding indexed I-frame;
- native GHAC1 `decode_audio` returning PCM16, sample rate/channels/frame count;
- explicit EOF state.

Frame storage is reference-counted and remains valid while a consumer holds the
`VideoFrame`. PlayerCore adds a bounded queue and one decode worker; the host
owns presentation timing. This is suitable for a future Godot worker-thread /
YUV-texture path without CPU RGB conversion.

This is a foundation, not the final stable ABI. Still required:

- versioned opaque C handles for ABI stability;
- incremental GHAC decode instead of materializing the whole PCM track;
- cancellation/non-blocking seek completion and reusable frame pools;
- GHVC4–6 support in the new library path (the existing native tool retains it);
- public encoder handles and direct YUV420/PCM push/finalize API;
- a direct-generated GHV demo with no MP4 or FFmpeg input.

The direct encoder API belongs to Phase 2. It was not started before completing
and validating the native Player, so `ghvcore` remains the current native
encoder implementation.
