#pragma once

#include <ghv/ghv.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace ghvplayer {

class PlayerCore {
public:
    PlayerCore();
    ~PlayerCore();
    PlayerCore(const PlayerCore&) = delete;
    PlayerCore& operator=(const PlayerCore&) = delete;

    bool open(const std::string& path, std::string& error);
    void close();
    bool seek(uint64_t target_us, std::string& error);
    bool wait_for_prebuffer(uint32_t timeout_ms, std::string& error);
    bool frame_for_clock(uint64_t clock_us, ghv::VideoFrame& frame, uint64_t& dropped);
    const ghv::Metadata& metadata() const { return metadata_; }
    const ghv::AudioBuffer& audio() const { return audio_; }
    bool decoder_eof() const { return decode_eof_.load(); }
    size_t queue_depth() const;
    bool has_error(std::string& message) const;

private:
    void stop_worker();
    void start_worker();
    void worker_loop();

    ghv::Decoder decoder_;
    ghv::Metadata metadata_;
    ghv::AudioBuffer audio_;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable can_push_;
    std::condition_variable has_frame_;
    std::deque<ghv::VideoFrame> queue_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> decode_eof_{false};
    size_t queue_limit_ = 12;
    std::string decode_error_;
};

} // namespace ghvplayer
