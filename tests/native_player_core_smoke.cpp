#ifdef _WIN32
#include "player_core.h"
#include "wasapi_audio.h"

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: ghvplayertest FILE.ghv\n"; return 2; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ghvplayer::PlayerCore core;
    std::string error;
    if (!core.open(argv[1], error) || !core.wait_for_prebuffer(10000, error)) {
        std::cerr << "open/prebuffer failed: " << error << "\n"; return 3;
    }
    ghv::VideoFrame frame;
    uint64_t dropped = 0;
    if (!core.frame_for_clock(2000, frame, dropped) || frame.frame_index != 0) {
        std::cerr << "first presentation frame failed\n"; return 4;
    }
    const uint64_t duration = core.metadata().duration_us;
    for (const uint64_t percentage : {10ull, 25ull, 50ull, 75ull, 90ull}) {
        const uint64_t target = duration * percentage / 100;
        if (!core.seek(target, error) || !core.wait_for_prebuffer(10000, error) ||
            !core.frame_for_clock(target + 2000, frame, dropped) ||
            frame.pts_us + frame.duration_us < target) {
            std::cerr << "seek/presentation failed at " << percentage << "%: " << error << "\n";
            return 5;
        }
    }
    const uint64_t middle = duration / 2;

    ghvplayer::WasapiAudio audio;
    if (!audio.open(core.audio(), error) || !audio.play_from(0, error)) {
        std::cerr << "WASAPI open/play failed: " << error << "\n"; return 6;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const uint64_t running = audio.clock_us();
    audio.pause();
    const uint64_t paused = audio.clock_us();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const uint64_t still_paused = audio.clock_us();
    if (running < 100000 || still_paused != paused) {
        std::cerr << "audio clock did not advance/freeze correctly: " << running << "/" << paused
                  << "/" << still_paused << "\n"; return 7;
    }
    if (!audio.seek(middle, error) || !audio.resume(error)) {
        std::cerr << "audio seek/resume failed: " << error << "\n"; return 8;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const uint64_t sought = audio.clock_us();
    if (sought < middle + 50000) {
        std::cerr << "audio clock did not re-anchor after seek: " << sought << "\n"; return 9;
    }
    audio.close(); core.close(); CoUninitialize();
    std::cout << "PASS native player core prebuffer/presentation/10-90% seek and WASAPI clock"
              << " running_us=" << running << " sought_us=" << sought << "\n";
    return 0;
}
#endif
