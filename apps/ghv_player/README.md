# GHV Player 0.1 for Windows

`GHV Player.exe` is the native end-user player. It does not require Python,
FFmpeg, ffplay, or OpenCV. The current development build plays
GHVC7/GHVC8/GHVC9 video with embedded GHAC1 audio.

Open the application and choose a `.ghv` file with Ctrl+O, pass a file on the
command line, or drag a `.ghv` file onto the window.

## Controls

- Space: play/pause or replay after EOF
- Left/Right: seek 5 seconds
- Ctrl+Left/Ctrl+Right: seek 30 seconds
- Home/End: beginning / final second
- Up/Down: volume
- M: mute
- F: fullscreen
- Escape: exit fullscreen
- Ctrl+O: open file

The initial 1100x700 window is independent of source resolution. Video is
always aspect-fit with letterboxing/pillarboxing. Resizing and fullscreen only
change the D3D11 viewport and never restart decoding or the audio clock.

Diagnostic automation can add `--stats FILE.json --exit-at-eof`. Normal users
do not see telemetry controls. `ghvplay` remains the separate developer and
diagnostic player for codec work.

The `due_events` array stores up to 256 observations where two queued frames
were eligible at one presentation poll. Each entry gives the older frame's
PTS, audio/playback clock and time spent in the ready queue (microseconds),
queue depth before selection, and whether that frame was skipped. A short
poll delay can be recovered by presenting the older frame first when the next
frame is at most half an interval late, capped at 20 ms. The total drop
counter continues past the diagnostic array limit.

Windows 10/11 x64 is the tested platform. The package includes the Microsoft
VC/OpenMP runtime files required by this build. D3D11, D3DCompiler 47, WASAPI,
and the common controls are Windows system components.
