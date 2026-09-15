# Godot GHV Integration Notes

GHVC decoding, timestamp generation, and playback scheduling must remain
separate layers. The codec core decodes frames and exposes timestamps; it must
not sleep, select a renderer, or own the host clock. This lets a Godot worker
thread decode ahead while Godot's audio/game clock schedules presentation.

## Proposed stable libghv surface

A future C ABI should provide open/close, metadata, decode-next-video,
decode-audio, seek, flush/reset, EOF, cancellation, and structured error state.
Video delivery should expose Y, U, and V plane pointers plus width, height,
plane strides, pixel format, PTS, duration, and an explicit retain/release or
pool-return handle. PCM delivery should expose sample rate, channels, sample
count, format, and PTS. Neither API should force CPU RGB conversion.

Frames should come from a bounded reusable pool. A decoder instance must be
safe to drive from one worker thread while the main thread polls/acquires ready
frames. Ownership rules must prevent overwrite until the host releases a frame;
backpressure and cancellation must be explicit rather than hidden in unbounded
queues.

## Godot 4.x direction

A GDExtension can eventually implement `VideoStreamGHV` and
`VideoStreamPlaybackGHV`. The worker decodes YUV420 planes; the render side
uploads the planes to reusable textures and performs YUV-to-RGB in a shader.
GHAC PCM feeds Godot's AudioServer/stream path. Godot supplies pause, loop, seek,
and its chosen audio/game clock; the presentation layer waits for early frames
and drops late video without changing audio rate.

## Missing foundation

- a built/shared `libghv` target and versioned public C ABI;
- opaque decoder/context handles instead of tool-specific entry points;
- reusable reference-counted YUV frame pools and bounded ready queues;
- public GHAC incremental decode API;
- timestamp-based seek/index API plus flush semantics;
- cancellation, thread-safety contract, and structured errors;
- fuzz, compatibility, ownership, seek, and host-clock integration tests;
- cross-platform texture/audio backend examples.

The current codec algorithms are renderer-independent and the controlled player
already keeps scheduling outside the decoder, which is the right foundation.
The stable library/API and frame ownership layer are not implemented yet; this
iteration deliberately does not create a premature Godot UI or editor plugin.
