#include "player_core.h"

#include <algorithm>
#include <chrono>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace ghvplayer {

PlayerCore::PlayerCore() = default;
PlayerCore::~PlayerCore() { close(); }

bool PlayerCore::open(const std::string& path, std::string& error) {
    close();
    ghv::Error decoder_error;
    if (!decoder_.open(path, &decoder_error)) {
        error = decoder_error.message;
        return false;
    }
    metadata_ = decoder_.metadata();
    if (!decoder_.decode_audio(audio_, &decoder_error)) {
        error = decoder_error.message;
        decoder_.close();
        return false;
    }
    // Keep decoded YUV bounded near 96 MiB, with enough temporal cushion for HD.
    const uint64_t frame_bytes = uint64_t(metadata_.width) * metadata_.height * 3 / 2;
    queue_limit_ = static_cast<size_t>(std::clamp<uint64_t>((96ull << 20) / std::max<uint64_t>(1, frame_bytes), 4, 24));
    start_worker();
    return true;
}

void PlayerCore::close() {
    stop_worker();
    decoder_.close();
    metadata_ = {};
    audio_ = {};
}

void PlayerCore::stop_worker() {
    stop_.store(true);
    can_push_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

void PlayerCore::start_worker() {
    stop_.store(false);
    decode_eof_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        decode_error_.clear();
    }
    worker_ = std::thread(&PlayerCore::worker_loop, this);
}

void PlayerCore::worker_loop() {
#ifdef _OPENMP
    // Frame-by-frame MSVC OpenMP teams spin between decode calls. Eight workers
    // retain measured 4K realtime headroom while leaving CPU for UI/games.
    omp_set_dynamic(0);
    omp_set_num_threads(std::min(8, omp_get_num_procs()));
#endif
    while (!stop_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            can_push_.wait(lock, [&] { return stop_.load() || queue_.size() < queue_limit_; });
            if (stop_.load()) return;
        }
        ghv::VideoFrame frame;
        ghv::Error error;
        if (!decoder_.decode_next(frame, &error)) {
            if (error.code != ghv::ErrorCode::end_of_stream) {
                std::lock_guard<std::mutex> lock(mutex_);
                decode_error_ = error.message;
            }
            decode_eof_.store(true);
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(frame));
        has_frame_.notify_all();
    }
}

bool PlayerCore::wait_for_prebuffer(uint32_t timeout_ms, std::string& error) {
    std::unique_lock<std::mutex> lock(mutex_);
    const size_t target = std::min<size_t>(queue_limit_, 3);
    has_frame_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
        return queue_.size() >= target || decode_eof_.load() || !decode_error_.empty();
    });
    if (!decode_error_.empty()) { error = decode_error_; return false; }
    if (queue_.empty()) { error = "Video decoder did not produce a startup frame."; return false; }
    return true;
}

bool PlayerCore::seek(uint64_t target_us, std::string& error) {
    stop_worker();
    ghv::Error decoder_error;
    if (!decoder_.seek_us(target_us, &decoder_error)) {
        error = decoder_error.message;
        return false;
    }
    start_worker();
    return true;
}

bool PlayerCore::frame_for_clock(uint64_t clock_us, ghv::VideoFrame& frame, uint64_t& dropped) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool have = false;
    // Select the newest due frame. Older due frames are late and can be dropped;
    // an early frame remains queued, so decoding never owns presentation timing.
    while (!queue_.empty() && queue_.front().pts_us <= clock_us + 2000) {
        if (have) ++dropped;
        frame = std::move(queue_.front());
        queue_.pop_front();
        have = true;
    }
    if (have) can_push_.notify_one();
    return have;
}

bool PlayerCore::has_error(std::string& message) const {
    std::lock_guard<std::mutex> lock(mutex_);
    message = decode_error_;
    return !message.empty();
}

size_t PlayerCore::queue_depth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

} // namespace ghvplayer
