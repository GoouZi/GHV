#include "wasapi_audio.h"

#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ghvplayer {
namespace {
std::string hr_text(const char* operation, HRESULT hr) {
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08lx)", operation, static_cast<unsigned long>(hr));
    return buffer;
}
}

WasapiAudio::WasapiAudio() = default;
WasapiAudio::~WasapiAudio() { close(); }

bool WasapiAudio::open(const ghv::AudioBuffer& audio, std::string& error) {
    close();
    audio_ = audio;
    if (!audio.samples || !audio.frame_count) return true;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    Microsoft::WRL::ComPtr<IMMDevice> device;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { error = hr_text("Create audio enumerator", hr); return false; }
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr)) { error = hr_text("Get default audio device", hr); return false; }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_);
    if (FAILED(hr)) { error = hr_text("Activate WASAPI", hr); return false; }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = audio.channels;
    format.nSamplesPerSec = audio.sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = WORD(format.nChannels * 2);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0, &format, nullptr);
    if (FAILED(hr)) { error = hr_text("Initialize WASAPI", hr); close(); return false; }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) { error = "Cannot create WASAPI event."; close(); return false; }
    if (FAILED(hr = client_->SetEventHandle(event_)) ||
        FAILED(hr = client_->GetBufferSize(&buffer_frames_)) ||
        FAILED(hr = client_->GetService(IID_PPV_ARGS(&render_))) ||
        FAILED(hr = client_->GetService(IID_PPV_ARGS(&clock_))) ||
        FAILED(hr = clock_->GetFrequency(&clock_frequency_))) {
        error = hr_text("Configure WASAPI", hr); close(); return false;
    }
    stop_.store(false);
    thread_ = std::thread(&WasapiAudio::render_loop, this);
    return true;
}

void WasapiAudio::close() {
    stop_.store(true);
    if (event_) SetEvent(event_);
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
    playing_.store(false);
    render_.Reset();
    clock_.Reset();
    client_.Reset();
    if (event_) CloseHandle(event_);
    event_ = nullptr;
    audio_ = {};
    source_cursor_ = 0;
    base_us_ = 0;
}

bool WasapiAudio::fill_locked(UINT32 frames, std::string* error) {
    if (!frames || !render_) return true;
    BYTE* destination = nullptr;
    HRESULT hr = render_->GetBuffer(frames, &destination);
    if (FAILED(hr)) { if (error) *error = hr_text("WASAPI GetBuffer", hr); return false; }
    const uint64_t remain = source_cursor_ < audio_.frame_count ? audio_.frame_count - source_cursor_ : 0;
    const UINT32 copy_frames = static_cast<UINT32>(std::min<uint64_t>(frames, remain));
    const uint16_t channels = audio_.channels;
    const float gain = muted_.load() ? 0.0f : volume_.load();
    auto* out = reinterpret_cast<int16_t*>(destination);
    if (copy_frames) {
        const int16_t* in = audio_.samples->data() + source_cursor_ * channels;
        for (uint64_t i = 0; i < uint64_t(copy_frames) * channels; ++i) {
            const int value = static_cast<int>(std::lrint(in[i] * gain));
            out[i] = static_cast<int16_t>(std::clamp(value, -32768, 32767));
        }
    }
    std::fill(out + uint64_t(copy_frames) * channels, out + uint64_t(frames) * channels, int16_t(0));
    source_cursor_ += copy_frames;
    hr = render_->ReleaseBuffer(frames, 0);
    if (FAILED(hr)) { if (error) *error = hr_text("WASAPI ReleaseBuffer", hr); return false; }
    return true;
}

bool WasapiAudio::play_from(uint64_t position_us, std::string& error) {
    if (!client_) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    client_->Stop();
    HRESULT hr = client_->Reset();
    if (FAILED(hr)) { error = hr_text("Reset WASAPI", hr); return false; }
    source_cursor_ = std::min<uint64_t>(audio_.frame_count,
        position_us * audio_.sample_rate / 1000000);
    base_us_ = source_cursor_ * 1000000 / audio_.sample_rate;
    UINT32 padding = 0;
    client_->GetCurrentPadding(&padding);
    if (!fill_locked(buffer_frames_ - padding, &error)) return false;
    hr = client_->Start();
    if (FAILED(hr)) { error = hr_text("Start WASAPI", hr); return false; }
    clock_->GetPosition(&clock_start_, nullptr);
    playing_.store(true);
    return true;
}

void WasapiAudio::pause() {
    if (!client_ || !playing_.load()) return;
    const uint64_t position = clock_us();
    std::lock_guard<std::mutex> lock(mutex_);
    client_->Stop();
    base_us_ = position;
    playing_.store(false);
}

bool WasapiAudio::resume(std::string& error) { return play_from(clock_us(), error); }

bool WasapiAudio::seek(uint64_t position_us, std::string& error) {
    const bool was_playing = playing_.load();
    if (was_playing) return play_from(position_us, error);
    std::lock_guard<std::mutex> lock(mutex_);
    base_us_ = position_us;
    source_cursor_ = std::min<uint64_t>(audio_.frame_count,
        position_us * audio_.sample_rate / 1000000);
    return true;
}

uint64_t WasapiAudio::clock_us() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!playing_.load() || !clock_) return base_us_;
    uint64_t position = clock_start_;
    if (FAILED(clock_->GetPosition(&position, nullptr)) || position < clock_start_) return base_us_;
    return base_us_ + (position - clock_start_) * 1000000 / std::max<uint64_t>(1, clock_frequency_);
}

void WasapiAudio::set_volume(float value) { volume_.store(std::clamp(value, 0.0f, 1.0f)); }

void WasapiAudio::render_loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (!stop_.load()) {
        if (WaitForSingleObject(event_, 100) != WAIT_OBJECT_0 || stop_.load()) continue;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!playing_.load() || !client_) continue;
        UINT32 padding = 0;
        if (SUCCEEDED(client_->GetCurrentPadding(&padding)) && padding < buffer_frames_)
            fill_locked(buffer_frames_ - padding, nullptr);
    }
    CoUninitialize();
}

} // namespace ghvplayer
#endif
