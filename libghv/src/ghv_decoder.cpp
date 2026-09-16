#include "ghv/ghv.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ghvcodec7.h"
#include "ghvcodec8.h"
#include "ghvcodec9.h"

namespace ghv {
namespace {

uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t le64(const uint8_t* p) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= uint64_t(p[i]) << (i * 8);
    return value;
}
int16_t signed_le16(const uint8_t* p) {
    return static_cast<int16_t>(le16(p));
}
int16_t clamp16(int64_t value) {
    return static_cast<int16_t>(std::max<int64_t>(-32768, std::min<int64_t>(32767, value)));
}

void set_error(Error* error, ErrorCode code, std::string message) {
    if (error) *error = {code, std::move(message)};
}

struct IndexEntry {
    uint64_t offset = 0;
    uint8_t type = 0;
};

} // namespace

struct Decoder::Impl {
    std::ifstream file;
    Metadata metadata;
    std::vector<IndexEntry> index;
    std::shared_ptr<std::vector<uint8_t>> previous;
    uint32_t next_frame = 0;
    uint64_t audio_offset = 0;
    size_t frame_bytes = 0;

    std::shared_ptr<std::vector<uint8_t>> decode_internal(uint64_t* pts_us = nullptr) {
        if (next_frame >= metadata.frame_count) return {};
        const uint32_t expected_no = next_frame;
        file.clear();
        file.seekg(static_cast<std::streamoff>(index[expected_no].offset), std::ios::beg);
        uint8_t header[32]{};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        if (file.gcount() != sizeof(header) || std::memcmp(header, "VFRM", 4) != 0)
            throw std::runtime_error("corrupt or truncated VFRM header");
        const uint32_t number = le32(header + 4);
        const uint64_t pts = le64(header + 8);
        const uint8_t type = header[16];
        const uint8_t codec = header[17];
        const uint32_t raw_bytes = le32(header + 20);
        const uint32_t packed_bytes = le32(header + 24);
        if (number != expected_no || raw_bytes != frame_bytes)
            throw std::runtime_error("invalid frame number or reconstructed size");
        if (codec != 7 && codec != 8 && codec != 9)
            throw std::runtime_error("libghv supports GHVC7 through GHVC9 video");
        if (type > 2) throw std::runtime_error("invalid GHV frame type");

        std::vector<uint8_t> payload(packed_bytes);
        if (packed_bytes) {
            file.read(reinterpret_cast<char*>(payload.data()), packed_bytes);
            if (static_cast<uint32_t>(file.gcount()) != packed_bytes)
                throw std::runtime_error("truncated video payload");
        }

        std::shared_ptr<std::vector<uint8_t>> reconstructed;
        if (type == 2) {
            if (!previous) throw std::runtime_error("repeat frame has no reference");
            reconstructed = previous;
        } else {
            reconstructed = std::make_shared<std::vector<uint8_t>>();
            if (codec == 9) {
                ghvc9::decode(payload, type == 1 ? previous.get() : nullptr,
                              int(metadata.width), int(metadata.height), type,
                              frame_bytes, *reconstructed);
            } else if (codec == 8) {
                ghvc8::decode(payload, type == 1 ? previous.get() : nullptr,
                              int(metadata.width), int(metadata.height), type,
                              frame_bytes, *reconstructed);
            } else {
                ghvc7::decode(payload, type == 1 ? previous.get() : nullptr,
                              int(metadata.width), int(metadata.height), type,
                              frame_bytes, *reconstructed);
            }
        }
        previous = reconstructed;
        ++next_frame;
        if (pts_us) *pts_us = pts;
        return reconstructed;
    }
};

Decoder::Decoder() : impl_(std::make_unique<Impl>()) {}
Decoder::~Decoder() = default;
Decoder::Decoder(Decoder&&) noexcept = default;
Decoder& Decoder::operator=(Decoder&&) noexcept = default;

bool Decoder::open(const std::string& utf8_path, Error* error) {
    close();
    try {
        impl_->file.open(std::filesystem::u8path(utf8_path), std::ios::binary);
        if (!impl_->file) {
            set_error(error, ErrorCode::io, "Cannot open the selected file.");
            return false;
        }
        uint8_t h[96]{};
        impl_->file.read(reinterpret_cast<char*>(h), sizeof(h));
        if (impl_->file.gcount() != sizeof(h) || std::memcmp(h, "GHV1", 4) != 0) {
            set_error(error, ErrorCode::invalid_file, "The file is not a valid GHV container.");
            close();
            return false;
        }
        const uint16_t header_size = le16(h + 6);
        if (header_size != 96 || h[4] != 0 || h[5] > 8) {
            set_error(error, ErrorCode::unsupported_version, "This GHV container version is not supported.");
            close();
            return false;
        }
        Metadata m;
        m.container_major = h[4];
        m.container_minor = h[5];
        m.width = le32(h + 12);
        m.height = le32(h + 16);
        m.fps_num = le32(h + 20);
        m.fps_den = le32(h + 24);
        m.frame_count = le32(h + 28);
        m.audio_rate = le32(h + 40);
        m.audio_channels = le16(h + 44);
        m.audio_codec = le16(h + 46);
        m.audio_samples = le64(h + 48);
        const uint64_t index_offset = le64(h + 72);
        m.duration_us = le64(h + 80);
        impl_->audio_offset = le64(h + 64);
        if (!m.width || !m.height || (m.width & 1) || (m.height & 1) ||
            !m.fps_num || !m.fps_den || !m.frame_count || !index_offset) {
            set_error(error, ErrorCode::corrupt_data, "Invalid GHV dimensions, timing, or index.");
            close();
            return false;
        }
        const uint64_t bytes64 = uint64_t(m.width) * m.height * 3 / 2;
        if (bytes64 > std::numeric_limits<size_t>::max())
            throw std::runtime_error("video frame is too large for this build");
        impl_->frame_bytes = static_cast<size_t>(bytes64);

        impl_->file.seekg(static_cast<std::streamoff>(index_offset), std::ios::beg);
        uint8_t ih[8]{};
        impl_->file.read(reinterpret_cast<char*>(ih), sizeof(ih));
        if (impl_->file.gcount() != sizeof(ih) || std::memcmp(ih, "INDX", 4) != 0 ||
            le32(ih + 4) != m.frame_count)
            throw std::runtime_error("missing or invalid frame index");
        impl_->index.resize(m.frame_count);
        for (uint32_t i = 0; i < m.frame_count; ++i) {
            uint8_t entry[16]{};
            impl_->file.read(reinterpret_cast<char*>(entry), sizeof(entry));
            if (impl_->file.gcount() != sizeof(entry)) throw std::runtime_error("truncated frame index");
            impl_->index[i] = {le64(entry), entry[8]};
            if (impl_->index[i].type > 2) throw std::runtime_error("invalid frame type in index");
        }
        impl_->metadata = m;
        impl_->next_frame = 0;
        impl_->previous.reset();
        if (error) *error = {};
        return true;
    } catch (const std::exception& ex) {
        set_error(error, ErrorCode::corrupt_data, ex.what());
        close();
        return false;
    }
}

void Decoder::close() {
    impl_->file.close();
    impl_->metadata = {};
    impl_->index.clear();
    impl_->previous.reset();
    impl_->next_frame = 0;
    impl_->audio_offset = 0;
    impl_->frame_bytes = 0;
}

const Metadata& Decoder::metadata() const { return impl_->metadata; }

bool Decoder::decode_next(VideoFrame& frame, Error* error) {
    try {
        if (eof()) {
            set_error(error, ErrorCode::end_of_stream, "End of stream.");
            return false;
        }
        uint64_t pts = 0;
        const uint32_t number = impl_->next_frame;
        auto storage = impl_->decode_internal(&pts);
        const uint64_t y_bytes = uint64_t(impl_->metadata.width) * impl_->metadata.height;
        const uint64_t uv_bytes = y_bytes / 4;
        frame.storage = storage;
        frame.y = storage->data();
        frame.u = storage->data() + y_bytes;
        frame.v = storage->data() + y_bytes + uv_bytes;
        frame.width = impl_->metadata.width;
        frame.height = impl_->metadata.height;
        frame.y_stride = impl_->metadata.width;
        frame.u_stride = impl_->metadata.width / 2;
        frame.v_stride = impl_->metadata.width / 2;
        frame.pts_us = pts;
        frame.duration_us = uint64_t(impl_->metadata.fps_den) * 1000000 / impl_->metadata.fps_num;
        frame.frame_index = number;
        if (error) *error = {};
        return true;
    } catch (const std::exception& ex) {
        set_error(error, ErrorCode::corrupt_data, ex.what());
        return false;
    }
}

bool Decoder::seek_us(uint64_t target_us, Error* error) {
    try {
        if (!impl_->metadata.frame_count) throw std::runtime_error("decoder is not open");
        uint64_t target64 = target_us * impl_->metadata.fps_num /
                            (uint64_t(impl_->metadata.fps_den) * 1000000);
        uint32_t target = static_cast<uint32_t>(std::min<uint64_t>(target64, impl_->metadata.frame_count - 1));
        uint32_t key = target;
        while (key > 0 && impl_->index[key].type != 0) --key;
        if (impl_->index[key].type != 0) throw std::runtime_error("no keyframe before seek target");
        impl_->next_frame = key;
        impl_->previous.reset();
        while (impl_->next_frame < target) impl_->decode_internal();
        if (error) *error = {};
        return true;
    } catch (const std::exception& ex) {
        set_error(error, ErrorCode::corrupt_data, ex.what());
        return false;
    }
}

bool Decoder::decode_audio(AudioBuffer& audio, Error* error) {
    try {
        audio = {};
        if (!impl_->audio_offset || !impl_->metadata.audio_samples) {
            if (error) *error = {};
            return true;
        }
        impl_->file.clear();
        impl_->file.seekg(static_cast<std::streamoff>(impl_->audio_offset), std::ios::beg);
        uint8_t record[24]{};
        impl_->file.read(reinterpret_cast<char*>(record), sizeof(record));
        if (impl_->file.gcount() != sizeof(record) || std::memcmp(record, "AUD0", 4) != 0 || le32(record + 4) != 4)
            throw std::runtime_error("unsupported or corrupt GHV audio record");
        const uint64_t payload_bytes = le64(record + 8);
        if (payload_bytes < 64 || payload_bytes > std::numeric_limits<size_t>::max())
            throw std::runtime_error("invalid GHAC payload size");
        std::vector<uint8_t> data(static_cast<size_t>(payload_bytes));
        impl_->file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (static_cast<size_t>(impl_->file.gcount()) != data.size()) throw std::runtime_error("truncated GHAC payload");
        if (std::memcmp(data.data(), "GHAF", 4) != 0 || std::memcmp(data.data() + 8, "GHA1", 4) != 0 || le16(data.data() + 6) != 64)
            throw std::runtime_error("unsupported GHA/GHAC version");
        const uint32_t rate = le32(data.data() + 12);
        const uint16_t channels = le16(data.data() + 16);
        const uint16_t bits = le16(data.data() + 18);
        const uint32_t block_frames = le32(data.data() + 20);
        const uint64_t frame_count = le64(data.data() + 28);
        const uint64_t block_count = le64(data.data() + 36);
        const uint64_t data_offset = le64(data.data() + 44);
        if (!rate || (channels != 1 && channels != 2) || (bits != 6 && bits != 8) ||
            block_frames < 8 || block_frames > 1024 || data_offset != 64)
            throw std::runtime_error("unsupported GHAC parameters");
        const uint64_t codes_per_block = uint64_t(block_frames - 1) * channels;
        const uint64_t code_bytes = bits == 8 ? codes_per_block : ((codes_per_block + 3) / 4) * 3;
        const uint64_t record_bytes = uint64_t(channels) * 4 + code_bytes;
        if (data_offset + block_count * record_bytes > data.size() || frame_count > std::numeric_limits<size_t>::max() / channels)
            throw std::runtime_error("truncated or oversized GHAC stream");

        auto pcm = std::make_shared<std::vector<int16_t>>(static_cast<size_t>(frame_count * channels));
        uint64_t written = 0;
        size_t src = static_cast<size_t>(data_offset);
        std::vector<int32_t> level(channels);
        for (uint64_t block = 0; block < block_count && written < frame_count; ++block) {
            int16_t anchor[2]{};
            uint16_t scale[2]{};
            for (uint16_t c = 0; c < channels; ++c) anchor[c] = signed_le16(data.data() + src + c * 2);
            for (uint16_t c = 0; c < channels; ++c) scale[c] = le16(data.data() + src + channels * 2 + c * 2);
            const uint8_t* codes = data.data() + src + channels * 4;
            std::fill(level.begin(), level.end(), 0);
            for (uint32_t f = 0; f < block_frames && written < frame_count; ++f, ++written) {
                for (uint16_t c = 0; c < channels; ++c) {
                    if (f > 0) {
                        const uint64_t ci = uint64_t(f - 1) * channels + c;
                        uint8_t code = 0;
                        if (bits == 8) code = codes[ci];
                        else {
                            const uint64_t group = ci / 4;
                            const uint32_t packed = uint32_t(codes[group * 3]) |
                                (uint32_t(codes[group * 3 + 1]) << 8) |
                                (uint32_t(codes[group * 3 + 2]) << 16);
                            code = uint8_t((packed >> ((ci & 3) * 6)) & 0x3f);
                        }
                        level[c] += int(code) - (bits == 8 ? 128 : 32);
                    }
                    (*pcm)[static_cast<size_t>(written * channels + c)] =
                        clamp16(int64_t(anchor[c]) + int64_t(level[c]) * scale[c]);
                }
            }
            src += static_cast<size_t>(record_bytes);
        }
        audio.samples = pcm;
        audio.sample_rate = rate;
        audio.channels = channels;
        audio.frame_count = frame_count;
        if (error) *error = {};
        return true;
    } catch (const std::exception& ex) {
        set_error(error, ErrorCode::corrupt_data, ex.what());
        return false;
    }
}

bool Decoder::eof() const { return impl_->next_frame >= impl_->metadata.frame_count; }
const char* version_string() { return "libghv 0.1 / GHVC7-8"; }

} // namespace ghv
