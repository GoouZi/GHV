#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ghv {

enum class ErrorCode {
    ok = 0,
    io,
    invalid_file,
    unsupported_version,
    corrupt_data,
    end_of_stream
};

struct Error {
    ErrorCode code = ErrorCode::ok;
    std::string message;
    explicit operator bool() const { return code != ErrorCode::ok; }
};

struct Metadata {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 1;
    uint32_t frame_count = 0;
    uint64_t duration_us = 0;
    uint32_t audio_rate = 0;
    uint16_t audio_channels = 0;
    uint16_t audio_codec = 0;
    uint64_t audio_samples = 0;
    uint8_t container_major = 0;
    uint8_t container_minor = 0;
};

struct VideoFrame {
    std::shared_ptr<const std::vector<uint8_t>> storage;
    const uint8_t* y = nullptr;
    const uint8_t* u = nullptr;
    const uint8_t* v = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t y_stride = 0;
    uint32_t u_stride = 0;
    uint32_t v_stride = 0;
    uint64_t pts_us = 0;
    uint64_t duration_us = 0;
    uint32_t frame_index = 0;
};

struct AudioBuffer {
    std::shared_ptr<const std::vector<int16_t>> samples;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint64_t frame_count = 0;
};

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(Decoder&&) noexcept;
    Decoder& operator=(Decoder&&) noexcept;
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    bool open(const std::string& utf8_path, Error* error = nullptr);
    void close();
    const Metadata& metadata() const;
    bool decode_next(VideoFrame& frame, Error* error = nullptr);
    bool seek_us(uint64_t target_us, Error* error = nullptr);
    bool decode_audio(AudioBuffer& audio, Error* error = nullptr);
    bool eof() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* version_string();

} // namespace ghv
