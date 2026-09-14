# GVID 0.3 delta specification

GVID 0.3 keeps the GVID 0.2 container and GVC2 video bitstream. The main change is audio codec id **3**, which embeds a complete GAUD 0.1 / GAC1 stream inside the existing `AUD0` chunk.

## Header version

`major = 0`, `minor = 3` for newly encoded files.

## Audio codec ids

- 0: no audio
- 1: legacy GAD1
- 2: legacy GAD2
- 3: GAUD 0.1 / GAC1 (default)

For codec id 3, the `AUD0` payload begins with the 64-byte `GAUD` header described in `SPEC_GAUD_0.1.md`. GVID repeats sample rate, channel count and sample count in its own header for fast inspection; a decoder should reject contradictory essential metadata.

GVC1/GVC2 and GAD1/GAD2 decoding compatibility is retained by the reference player.
