# GHV

[简体中文](README_CN.md)

**GHV** is an experimental open video/audio format project focused on four goals:

- **Smaller files**
- **Higher visual and audio quality**
- **Faster encoding and decoding**
- **Open, extensible and broadly compatible implementations**

The project is currently in active development. The latest video codec generation is **GHVC8**.

## Current project structure

GHV is more than a single file extension. The project is being developed as a small media stack that includes:

- **GHV** — container / video format work.
- **GHVC8** — current video codec generation.
- **GHA** — companion audio-format work.
- **Native decoder** — C++17 decoding path for faster playback and integration.
- **Verification tools** — used to distinguish corrupted files from playback-performance problems.
- **Benchmark tools** — used to track encoder and decoder performance between iterations.

The format and implementation are still evolving, so bitstream compatibility between development generations is not guaranteed yet.

## Why make another format?

The long-term goal is not simply to create another custom codec. GHV is being used to explore whether a format can offer a useful balance of:

1. compact files;
2. good perceptual quality;
3. lightweight software decoding;
4. fast encoding;
5. simple integration into engines, players and open-source software;
6. an openly implementable format without depending on a proprietary ecosystem.

That means development is judged with measurements rather than only by whether a file can be encoded and played.

## Current GHVC8 benchmark snapshot

The numbers below are development measurements from the current GHVC8 test line. They are **not claims of superiority over established codecs**; they exist to show the current state of the project and where work is still required.

| Test | GHV result | Encode speed | Decode speed | Notes |
| --- | ---: | ---: | ---: | --- |
| Test A | 303,023,656 bytes (~289 MiB) | 140.44 fps | 448.17 fps | Fixed comparison sample |
| Test B | 580,797,129 bytes | 45.46 fps | 141.19 fps | Heavier comparison sample |
| Test C (4K) | — | 9.78 fps | 30.21 fps | Full playback PASS; 7 drops; 0 freezes |

### File-size comparison for Test A

| Format | Approx. output size | Relative to current GHV |
| --- | ---: | ---: |
| MP4 comparison encode | ~24.6 MB | Much smaller |
| OGV comparison encode | ~45.2 MB | Much smaller |
| GHV / GHVC8 | 303,023,656 bytes (~289 MiB) | Baseline |

The important result here is that **GHVC8 is currently still far larger than the MP4 and OGV comparison outputs**. Compression efficiency remains one of the project's biggest unfinished areas.

At the same time, the native decoder already shows strong raw software-decoding throughput on the current tests, so development is now focused on reducing size without throwing away that performance advantage.

> Benchmark results are highly dependent on source material, resolution, quality settings, hardware and the exact comparison commands. A reproducible benchmark suite and command lines will be published with the first public source release.

## Development direction

### Compression

Current work continues to reduce encoded size through better prediction, residual representation, block description and entropy-friendly data layouts.

The objective is not to chase compression ratio at any cost: new compression techniques must also remain practical to decode.

### Decoder performance

A major project goal is lightweight software playback.

The current development line includes a native **C++17 decoder**, with Python-based tooling retained mainly for compatibility, testing and development workflows.

### Reliability and diagnostics

GHV development includes dedicated verification and benchmarking tools so that failures can be classified properly.

For example, a playback problem should be distinguishable from an actually corrupted file. This is important when testing high-resolution content or slower machines.

### Audio

**GHA** is being developed alongside GHV as the audio side of the project. Video and audio are intended to evolve together rather than treating audio as an afterthought.

## Comparison philosophy

GHV is often compared with MP4-based workflows and OGV because they represent two very different practical reference points.

| Area | GHV goal | MP4 ecosystem | OGV / Theora ecosystem |
| --- | --- | --- | --- |
| File size | Become substantially smaller than current GHV generations | Usually very efficient with modern codecs | Older compression technology; often larger than modern MP4 workflows |
| Quality | Preserve high perceptual quality at practical sizes | Strong with modern codecs | Usable, but comparatively old |
| Software decode | Keep decoding lightweight and fast | Depends strongly on codec and hardware acceleration | Relatively simple software decoding |
| Openness | Open specification and implementation | Container is open, but codec/licensing situation depends on the chosen codec | Open and widely implementable |
| Integration goal | Easy to integrate into engines and tools | Extremely broad ecosystem support | Particularly friendly to open-source software |

This table describes the **design targets and ecosystem characteristics**, not a claim that GHV already outperforms either format.

## Roadmap

Before the first public stable format specification, the project is focused on:

- continuing compression work beyond GHVC8;
- reducing the large file-size gap shown in current benchmarks;
- keeping or improving native decode throughput;
- improving high-resolution and 4K playback stability;
- maturing GHA audio support;
- documenting the bitstream/container layout;
- creating reproducible MP4 / OGV / GHV benchmark scripts;
- publishing encoder, decoder, verify and benchmark tools;
- testing integration paths for game engines and media players.

## Project status

GHV is **experimental**. The codec and container are under active iteration and should not yet be treated as archival formats.

The first public development release will include the actual implementation and enough documentation to reproduce the published tests.

## License

The reference implementation is intended to be released under the **Apache License 2.0**.

The project is designed around an openly implementable format. The intention is that independent implementations should be possible without requiring use of the reference encoder or decoder.

See [LICENSE](LICENSE) for the source-code license.

## Patent and royalty note

GHV is designed with the goal of being openly implementable and royalty-free. However, an experimental project cannot responsibly guarantee that every possible technique is free from all third-party patent claims in every jurisdiction.

Patent-sensitive wording and technical choices will be reviewed as the format approaches a stable specification.
