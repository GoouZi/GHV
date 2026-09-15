#include <ghv/ghv.h>

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ghvlibtest FILE.ghv\n";
        return 2;
    }
    ghv::Decoder decoder;
    ghv::Error error;
    if (!decoder.open(argv[1], &error)) {
        std::cerr << error.message << "\n";
        return 3;
    }
    const auto metadata = decoder.metadata();
    ghv::VideoFrame first;
    if (!decoder.decode_next(first, &error) || !first.storage || !first.y || !first.u || !first.v) {
        std::cerr << "first-frame decode failed: " << error.message << "\n";
        return 4;
    }
    const uint64_t middle = metadata.duration_us / 2;
    if (!decoder.seek_us(middle, &error)) {
        std::cerr << "seek failed: " << error.message << "\n";
        return 5;
    }
    ghv::VideoFrame sought;
    if (!decoder.decode_next(sought, &error) || sought.pts_us + sought.duration_us < middle) {
        std::cerr << "post-seek frame failed: " << error.message << "\n";
        return 6;
    }
    ghv::AudioBuffer audio;
    if (!decoder.decode_audio(audio, &error)) {
        std::cerr << "audio decode failed: " << error.message << "\n";
        return 7;
    }
    if (metadata.audio_samples && (!audio.samples || audio.frame_count != metadata.audio_samples ||
                                   audio.sample_rate != metadata.audio_rate ||
                                   audio.channels != metadata.audio_channels)) {
        std::cerr << "audio metadata mismatch\n";
        return 8;
    }
    std::cout << "PASS " << ghv::version_string() << " " << metadata.width << "x" << metadata.height
              << " seek_pts_us=" << sought.pts_us << " audio_frames=" << audio.frame_count << "\n";
    return EXIT_SUCCESS;
}
