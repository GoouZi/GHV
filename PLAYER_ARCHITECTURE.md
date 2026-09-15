# GHV Player Architecture

GHV Player 0.1 is a native Windows end-user application, separate from the
Python/OpenCV developer player. Its layers are deliberately independent:

1. `libghv` parses the container/index, decodes GHVC7/8 YUV420 frames and GHAC1
   PCM, owns no window/audio device, and never sleeps.
2. `PlayerCore` runs decoding on a worker, owns a bounded frame queue (about
   96 MiB, 4–24 frames), performs keyframe/index seek, and selects due frames.
3. `WasapiAudio` submits PCM at a fixed rate. The WASAPI device position is the
   master clock; pause freezes it and seek/resume creates a new anchor.
4. `D3D11Renderer` uploads Y/U/V planes as `R8_UNORM` textures and performs
   YUV-to-RGB in a pixel shader. Resize changes only the aspect-fit viewport.
5. Win32 owns the DPI-aware window, controls, shortcuts, drag/drop, file dialog,
   fullscreen state, error dialogs, and EOF/replay behavior.

The player uses eight decoder workers on the tested 20-logical-core machine.
MSVC OpenMP workers otherwise spin between per-frame parallel regions: measured
960x544 playback fell from 82.0% to 33.7% whole-machine CPU while retaining a
57 fps 4K decode microbenchmark and stable 4K24 playback.

## Clock and seek rules

Audio is always 1.0x. Early video remains queued, due video is displayed, and
superseded late frames are dropped. Video load never changes audio pitch or
rate. Pause stops WASAPI and preserves the last texture. Seek stops the decode
worker, uses the 64-bit index to find the previous I-frame, reconstructs to the
target, flushes the queue/audio device, prebuffers three frames, and reanchors
the audio clock before resume.

## Platform boundary

`libghv` and `PlayerCore` are standard C++17. Windows-specific code is confined
to D3D11, WASAPI, and Win32 source files. A macOS player should supply Metal,
CoreAudio, and Cocoa backends around the same core; that backend is not yet
implemented or tested. Android/iOS/Linux are likewise not claimed as passing.
