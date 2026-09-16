# GHV

**Goou_Zi High-efficiency Video**

[English](README.md)

GHV 是一个实验性的开放视频/音频格式项目，核心目标是：**小、清晰、快、稳**。
仓库包含项目自己的 GHVC 视频编码、GHAC 音频编码、GHV/GHA 容器、原生实现、
规范、兼容性测试和可复现 benchmark 历史。

> **当前状态：Beta（`0.9.0-beta.1`）。** 目前不是 Production Ready、稳定版或
> 归档格式。1.0 之前 bitstream、API、工具和性能仍可能变化；项目会尽量继续保留
> 旧 GHVC generation 的解码兼容性。

## 核心原则

- **小：** 提高真正的压缩效率，而不是给外部 codec 换扩展名。
- **清晰 / 高质量：** 不通过严重破坏画质或音质换取体积。
- **快：** 编码速度重要，软件解码也必须有充足 realtime headroom。
- **稳：** 正确时间戳、seek、CRC、有界内存、错误处理和稳定音画同步都是格式的一部分。

## 当前版本

| 项目 | 当前状态 |
|---|---|
| Project | **0.9.0-beta.1** |
| GHV container | 0.9 development |
| GHVC video | **GHVC9** encoder；GHVC4–9 decoder |
| GHA container | 0.2 development |
| GHAC audio | **GHAC1** |
| 主要验证平台 | Windows 11 x64 |

GHVC/GHAC 是本项目自己的 codec。FFmpeg 只用于转换工具读取输入媒体，不负责
编码或解码 GHVC/GHAC。版本唯一来源是 [`VERSION.json`](VERSION.json)，也可以运行
`python ghvversion.py` 查看。

## 最新验证 Benchmark

以下数据全部来自同一套 GHVC9 Milestone 1 测试：Balanced q78、GHAC1 HQ、完整
native CRC、完整 PSNR/SSIM。机器为 Windows 11、Intel64 Family 6 Model 183、
20 logical cores；RTX 4060 Ti 未被 codec 使用。FPS 是本机数据，不代表所有电脑。

| 测试 | 输入 | GHVC9 输出 | Encode | Decode | PSNR / SSIM | 播放 |
|---|---|---:|---:|---:|---:|---|
| A 压缩/质量 | 960×544, 30 fps | **210.616 MiB** | **169.317 fps** | **913.114 fps** | 45.356 / 0.984213 | Visual PASS |
| B 1080p | 1920×1080, 30 fps | **405.317 MiB** | **52.810 fps** | **279.601 fps** | 46.628 / 0.990219 | PASS，0 drop/freeze |
| C 4K | 3840×2160, 24 fps | **3062.501 MiB** | **11.196 fps** | **59.362 fps（2.473×）** | 47.084 / 0.986903 | PASS，0 drop/freeze |

相对冻结的 GHVC8，A/B/C 分别缩小 **27.12% / 26.82% / 24.97%**，编码和解码
速度同时提高。但 Test A 仍约为历史 OGV 45.2 MB reference 的 4.66 倍，因此压缩
效率仍未完成，项目也不宣称已经超过成熟 codec。

[完整报告](benchmarks/milestones/ghvc9/GHVC9_MILESTONE1_2026-09-16.md) ·
[JSON 数据](benchmarks/milestones/ghvc9/reports) · [项目状态](docs/development/HANDOFF.md)

![Test A 50% source 与 GHVC9 decoded 对比](benchmarks/milestones/ghvc9/visual/test_a/compare_50_frame_1910.png)

## Build

转换需要 Python 3.10+、FFmpeg/ffprobe 和 `requirements.txt` 中的依赖。Native core
需要 C++17 compiler；完整 libghv/Windows Player build 使用 CMake。

```powershell
python -m pip install -r requirements.txt
native\build_windows.bat
cmake -S native -B native/build
cmake --build native/build --config Release
scripts\windows\test_all.bat
```

Windows + Visual Studio Build Tools 是当前完整验证路径。Linux/macOS 有
`native/build_linux.sh` 与 `native/build_macos.sh`，但尚未完成当前版本的正式 A/B/C
与播放验收。

## 基本使用

```powershell
python ghvenc.py input.mp4 output.ghv --codec 9 --preset balanced
python ghaenc.py input.wav output.gha --mode hq
python ghvverify.py output.ghv
python ghvdoctor.py output.ghv --verify
python ghvrepair.py damaged.ghv repaired.ghv
python ghvbench.py input.mp4 output.ghv --codec 9 --preset balanced --profile --quality-metrics --decode-frames 0 --report-json report.json
```

当前真实工具还包括 GHV/GHA Studio、native encoder/decoder、libghv、Windows 原生
验证播放器、`ghvinfo`、`ghvframes`。播放器 UI、Godot/MovieWriter、VLC/PotPlayer
和其他生态集成目前暂停，先继续强化 codec。

## 规范与开发

- [GHV 0.9 / GHVC9](docs/specs/ghv/0.9.md)
- [GHV 0.8 / GHVC8](docs/specs/ghv/0.8.md)
- [GHA 0.2](docs/specs/gha/0.2.md)
- [文档索引](docs/README.md)
- [Roadmap](docs/ROADMAP.md)
- [Changelog](CHANGELOG.md)
- [开发与 Git 工作流](docs/development/WORKFLOW.md)

`main` 表示最新已验证公开 Beta，不代表 1.0。开发 milestone 在 generation branch
完成 build/test 后独立 commit 并 push；project release 使用 `v0.x.x-beta.x` tag，
codec milestone 使用独立 `ghv-x.y-mN` tag。GHVC8 稳定恢复点
`ghv-0.8-stable-perf2` 不移动。

1.0 仍要求真正同时达到：小、清晰、快、稳，以及稳定 1080p/4K、跨平台 reference
decoder 和完整公开规范。

## License

Reference implementation 使用 [Apache License 2.0](LICENSE)。
