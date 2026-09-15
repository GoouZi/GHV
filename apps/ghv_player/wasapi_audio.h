#pragma once

#ifdef _WIN32
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <windows.h>

#include <ghv/ghv.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace ghvplayer {

class WasapiAudio {
public:
    WasapiAudio();
    ~WasapiAudio();
    bool open(const ghv::AudioBuffer& audio, std::string& error);
    void close();
    bool play_from(uint64_t position_us, std::string& error);
    void pause();
    bool resume(std::string& error);
    bool seek(uint64_t position_us, std::string& error);
    uint64_t clock_us() const;
    bool playing() const { return playing_.load(); }
    void set_volume(float value);
    float volume() const { return volume_.load(); }
    void set_muted(bool value) { muted_.store(value); }
    bool muted() const { return muted_.load(); }

private:
    bool fill_locked(UINT32 frames, std::string* error);
    void render_loop();

    ghv::AudioBuffer audio_;
    Microsoft::WRL::ComPtr<IAudioClient> client_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> render_;
    Microsoft::WRL::ComPtr<IAudioClock> clock_;
    HANDLE event_ = nullptr;
    UINT32 buffer_frames_ = 0;
    uint64_t source_cursor_ = 0;
    uint64_t base_us_ = 0;
    uint64_t clock_start_ = 0;
    uint64_t clock_frequency_ = 1;
    mutable std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> playing_{false};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> muted_{false};
};

} // namespace ghvplayer
#endif
