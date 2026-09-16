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

Windows 10/11 x64 is the tested platform. The package includes the Microsoft
VC/OpenMP runtime files required by this build. D3D11, D3DCompiler 47, WASAPI,
and the common controls are Windows system components.
