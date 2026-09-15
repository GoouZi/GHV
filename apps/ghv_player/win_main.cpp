#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <psapi.h>

#include "d3d11_renderer.h"
#include "player_core.h"
#include "wasapi_audio.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {
constexpr wchar_t kWindowClass[] = L"GHVPlayerWindow";
constexpr int kControlHeight = 62;
constexpr UINT_PTR kRenderTimer = 1;
enum ControlId { PlayButton = 100, SeekBar, TimeLabel, VolumeBar, MuteButton, FullscreenButton };

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int chars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0);
    std::wstring out(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), int(value.size()), out.data(), chars);
    return out;
}

std::wstring format_time(uint64_t us) {
    const uint64_t seconds = us / 1000000;
    wchar_t text[32]{};
    if (seconds >= 3600) swprintf_s(text, L"%llu:%02llu:%02llu", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    else swprintf_s(text, L"%02llu:%02llu", seconds / 60, seconds % 60);
    return text;
}

struct App {
    HWND window = nullptr;
    HWND play = nullptr, seek = nullptr, time = nullptr, volume = nullptr, mute = nullptr, fullscreen = nullptr;
    ghvplayer::PlayerCore core;
    ghvplayer::WasapiAudio audio;
    ghvplayer::D3D11Renderer renderer;
    ghv::VideoFrame displayed;
    bool opened = false;
    bool playing = false;
    bool ended = false;
    bool fullscreen_mode = false;
    bool controls_visible = true;
    bool seeking = false;
    uint64_t dropped = 0;
    uint64_t displayed_count = 0;
    uint64_t video_underruns = 0;
    uint64_t freeze_events = 0;
    uint64_t max_drift_us = 0;
    bool freeze_active = false;
    bool underrun_active = false;
    bool exit_at_eof = false;
    std::wstring stats_path;
    uint64_t fallback_base_us = 0;
    std::chrono::steady_clock::time_point fallback_started{};
    std::chrono::steady_clock::time_point playback_started{};
    std::chrono::steady_clock::time_point last_displayed{};
    uint64_t playback_cpu_start_100ns = 0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    LONG_PTR window_style = 0;
    DWORD last_mouse_tick = 0;

    uint64_t clock_us() const {
        if (!opened) return 0;
        if (core.audio().samples) return audio.clock_us();
        if (!playing) return fallback_base_us;
        return fallback_base_us + uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - fallback_started).count());
    }

    static uint64_t process_cpu_100ns() {
        FILETIME create{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) return 0;
        ULARGE_INTEGER k{}, u{};
        k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
        return k.QuadPart + u.QuadPart;
    }

    void show_error(const std::string& message) {
        MessageBoxW(window, wide(message).c_str(), L"GHV Player", MB_OK | MB_ICONERROR);
    }

    void layout() {
        RECT r{}; GetClientRect(window, &r);
        const int width = std::max(1L, r.right);
        const int y = std::max(0L, r.bottom - kControlHeight + 12);
        MoveWindow(play, 10, y, 58, 32, TRUE);
        const int right_controls = 330;
        MoveWindow(seek, 76, y, std::max(80, width - right_controls - 76), 32, TRUE);
        MoveWindow(time, std::max(160, width - right_controls + 8), y + 7, 112, 24, TRUE);
        MoveWindow(volume, std::max(280, width - 208), y, 90, 32, TRUE);
        MoveWindow(mute, std::max(370, width - 116), y, 48, 32, TRUE);
        MoveWindow(fullscreen, std::max(420, width - 62), y, 52, 32, TRUE);
    }

    void show_controls(bool show) {
        controls_visible = show;
        for (HWND control : {play, seek, time, volume, mute, fullscreen}) ShowWindow(control, show ? SW_SHOW : SW_HIDE);
        SetCursor(show || !fullscreen_mode ? LoadCursor(nullptr, IDC_ARROW) : nullptr);
    }

    bool open_file(const std::wstring& path) {
        playing = false;
        audio.close();
        std::string error;
        if (!core.open(utf8(path), error)) { show_error(error); return false; }
        if (!audio.open(core.audio(), error)) { core.close(); show_error(error); return false; }
        if (!core.wait_for_prebuffer(10000, error)) { audio.close(); core.close(); show_error(error); return false; }
        opened = true; ended = false; dropped = 0; displayed_count = 0; video_underruns = 0;
        freeze_events = 0; max_drift_us = 0; freeze_active = false; underrun_active = false; displayed = {};
        SendMessageW(seek, TBM_SETPOS, TRUE, 0);
        if (!audio.play_from(0, error)) { audio.close(); core.close(); opened = false; show_error(error); return false; }
        fallback_base_us = 0; fallback_started = std::chrono::steady_clock::now();
        playback_started = fallback_started; last_displayed = fallback_started;
        playback_cpu_start_100ns = process_cpu_100ns();
        playing = true;
        SetWindowTextW(play, L"Pause");
        std::wstring title = L"GHV Player — " + std::filesystem::path(path).filename().wstring();
        SetWindowTextW(window, title.c_str());
        return true;
    }

    void toggle_play() {
        if (!opened) return;
        std::string error;
        if (ended) {
            seek_to(0); ended = false;
        }
        if (playing) {
            fallback_base_us = clock_us();
            audio.pause();
            playing = false;
            SetWindowTextW(play, L"Play");
        } else {
            fallback_started = std::chrono::steady_clock::now();
            if (!audio.resume(error)) { show_error(error); return; }
            playing = true;
            SetWindowTextW(play, L"Pause");
        }
    }

    void seek_to(uint64_t target_us) {
        if (!opened) return;
        target_us = std::min(target_us, core.metadata().duration_us ? core.metadata().duration_us - 1 : 0);
        const bool resume_after = playing;
        if (resume_after) { audio.pause(); playing = false; }
        std::string error;
        if (!core.seek(target_us, error) || !audio.seek(target_us, error)) { show_error(error); return; }
        if (!core.wait_for_prebuffer(10000, error)) { show_error(error); return; }
        fallback_base_us = target_us; fallback_started = std::chrono::steady_clock::now();
        if (resume_after) {
            if (!audio.play_from(target_us, error)) { show_error(error); return; }
            playing = true;
        }
        ended = false;
    }

    void seek_relative(int64_t delta_us) {
        const int64_t target = std::clamp<int64_t>(int64_t(clock_us()) + delta_us, 0,
            std::max<int64_t>(0, int64_t(core.metadata().duration_us) - 1));
        seek_to(uint64_t(target));
    }

    void adjust_volume(int delta) {
        const int position = std::clamp(int(SendMessageW(volume, TBM_GETPOS, 0, 0)) + delta, 0, 100);
        SendMessageW(volume, TBM_SETPOS, TRUE, position);
        audio.set_volume(float(position) / 100.0f);
    }

    void toggle_fullscreen() {
        fullscreen_mode = !fullscreen_mode;
        if (fullscreen_mode) {
            window_style = GetWindowLongPtrW(window, GWL_STYLE);
            GetWindowPlacement(window, &placement);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            SetWindowLongPtrW(window, GWL_STYLE, window_style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            last_mouse_tick = GetTickCount();
        } else {
            SetWindowLongPtrW(window, GWL_STYLE, window_style);
            SetWindowPlacement(window, &placement);
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            show_controls(true);
        }
    }

    void update() {
        if (!opened) return;
        std::string decode_error;
        if (core.has_error(decode_error)) { audio.pause(); playing = false; show_error(decode_error); core.close(); opened = false; return; }
        const uint64_t now = clock_us();
        ghv::VideoFrame frame;
        if (core.frame_for_clock(now, frame, dropped)) {
            std::string error;
            if (!renderer.upload(frame, error)) { show_error(error); return; }
            displayed = std::move(frame);
            ++displayed_count;
            max_drift_us = std::max<uint64_t>(max_drift_us,
                now > displayed.pts_us ? now - displayed.pts_us : displayed.pts_us - now);
            last_displayed = std::chrono::steady_clock::now();
            freeze_active = false; underrun_active = false;
        } else if (playing && !core.decoder_eof() && core.queue_depth() == 0) {
            if (!underrun_active) { ++video_underruns; underrun_active = true; }
            if (!freeze_active && std::chrono::steady_clock::now() - last_displayed > std::chrono::milliseconds(500)) {
                ++freeze_events;
                freeze_active = true;
            }
        }
        if (!seeking && core.metadata().duration_us) {
            const int position = int(std::min<uint64_t>(10000, now * 10000 / core.metadata().duration_us));
            SendMessageW(seek, TBM_SETPOS, TRUE, position);
        }
        const std::wstring times = format_time(now) + L" / " + format_time(core.metadata().duration_us);
        SetWindowTextW(time, times.c_str());
        if (playing && now >= core.metadata().duration_us) {
            audio.pause(); playing = false; ended = true; SetWindowTextW(play, L"Replay");
            write_stats();
            if (exit_at_eof) PostMessageW(window, WM_CLOSE, 0, 0);
        }
        RECT r{}; GetClientRect(window, &r);
        renderer.render(r.right, std::max(1L, r.bottom - (controls_visible ? kControlHeight : 0)));
        if (fullscreen_mode && controls_visible && GetTickCount() - last_mouse_tick > 2500) show_controls(false);
    }

    void write_stats() {
        if (stats_path.empty()) return;
        PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
        const double cpu_seconds = double(process_cpu_100ns() - playback_cpu_start_100ns) / 10000000.0;
        const double wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-playback_started).count();
        std::ofstream out(std::filesystem::path(stats_path), std::ios::binary);
        out << "{\n  \"schema\": \"ghv-native-player-stats-v1\",\n"
            << "  \"clock_master\": \"WASAPI audio clock\",\n"
            << "  \"audio_rate_fixed\": true,\n"
            << "  \"duration_seconds\": " << core.metadata().duration_us/1000000.0 << ",\n"
            << "  \"wall_seconds\": " << wall_seconds << ",\n"
            << "  \"displayed_frames\": " << displayed_count << ",\n"
            << "  \"dropped_frames\": " << dropped << ",\n"
            << "  \"video_underruns\": " << video_underruns << ",\n"
            << "  \"freeze_events\": " << freeze_events << ",\n"
            << "  \"pitch_change_events\": 0,\n  \"slowdown_events\": 0,\n  \"speedup_events\": 0,\n"
            << "  \"max_av_drift_seconds\": " << max_drift_us/1000000.0 << ",\n"
            << "  \"cpu_seconds\": " << cpu_seconds << ",\n"
            << "  \"average_cpu_percent_of_machine\": " << (wall_seconds>0?cpu_seconds/wall_seconds*100.0/std::max(1u,std::thread::hardware_concurrency()):0) << ",\n"
            << "  \"peak_working_set_bytes\": " << memory.PeakWorkingSetSize << "\n}\n";
    }
};

App* app_from(HWND window) { return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA)); }

void choose_file(App& app) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter = L"GHV video (*.ghv)\0*.ghv\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = DWORD(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&dialog)) app.open_file(path);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App* app = app_from(window);
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
            reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
        return TRUE;
    case WM_CREATE: {
        app = app_from(window); app->window = window;
        app->play = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,0,0,0, window, HMENU(PlayButton), nullptr, nullptr);
        app->seek = CreateWindowW(TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0,0,0,0, window, HMENU(SeekBar), nullptr, nullptr);
        app->time = CreateWindowW(L"STATIC", L"00:00 / 00:00", WS_CHILD | WS_VISIBLE | SS_CENTER, 0,0,0,0, window, HMENU(TimeLabel), nullptr, nullptr);
        app->volume = CreateWindowW(TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0,0,0,0, window, HMENU(VolumeBar), nullptr, nullptr);
        app->mute = CreateWindowW(L"BUTTON", L"Mute", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,0,0,0, window, HMENU(MuteButton), nullptr, nullptr);
        app->fullscreen = CreateWindowW(L"BUTTON", L"Full", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,0,0,0, window, HMENU(FullscreenButton), nullptr, nullptr);
        SendMessageW(app->seek, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10000));
        SendMessageW(app->volume, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(app->volume, TBM_SETPOS, TRUE, 100);
        DragAcceptFiles(window, TRUE);
        app->layout();
        std::string error;
        if (!app->renderer.initialize(window, error)) { app->show_error(error); return -1; }
        SetTimer(window, kRenderTimer, 10, nullptr);
        return 0;
    }
    case WM_SIZE:
        if (app) { app->layout(); app->renderer.resize(LOWORD(lparam), HIWORD(lparam)); }
        return 0;
    case WM_COMMAND:
        if (!app) break;
        switch (LOWORD(wparam)) {
        case PlayButton: app->toggle_play(); return 0;
        case MuteButton: app->audio.set_muted(!app->audio.muted()); SetWindowTextW(app->mute, app->audio.muted()?L"Unmute":L"Mute"); return 0;
        case FullscreenButton: app->toggle_fullscreen(); return 0;
        }
        break;
    case WM_HSCROLL:
        if (!app) break;
        if (HWND(lparam) == app->volume) {
            app->audio.set_volume(float(SendMessageW(app->volume, TBM_GETPOS, 0, 0)) / 100.0f); return 0;
        }
        if (HWND(lparam) == app->seek) {
            const int code = LOWORD(wparam);
            app->seeking = code == TB_THUMBTRACK;
            if (code == TB_ENDTRACK || code == TB_THUMBPOSITION || code == TB_PAGEDOWN || code == TB_PAGEUP) {
                app->seeking = false;
                const uint64_t target = uint64_t(SendMessageW(app->seek, TBM_GETPOS, 0, 0)) * app->core.metadata().duration_us / 10000;
                app->seek_to(target);
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (!app) break;
        switch (wparam) {
        case VK_SPACE: app->toggle_play(); return 0;
        case VK_LEFT: app->seek_relative(GetKeyState(VK_CONTROL)<0 ? -30000000 : -5000000); return 0;
        case VK_RIGHT: app->seek_relative(GetKeyState(VK_CONTROL)<0 ? 30000000 : 5000000); return 0;
        case VK_UP: app->adjust_volume(5); return 0;
        case VK_DOWN: app->adjust_volume(-5); return 0;
        case 'M': SendMessageW(window, WM_COMMAND, MuteButton, 0); return 0;
        case 'F': app->toggle_fullscreen(); return 0;
        case VK_ESCAPE: if (app->fullscreen_mode) app->toggle_fullscreen(); return 0;
        case VK_HOME: app->seek_to(0); return 0;
        case VK_END: app->seek_to(app->core.metadata().duration_us > 1000000 ? app->core.metadata().duration_us - 1000000 : 0); return 0;
        case 'O': if (GetKeyState(VK_CONTROL)<0) { choose_file(*app); return 0; } break;
        }
        break;
    case WM_MOUSEMOVE:
        if (app && app->fullscreen_mode) { app->last_mouse_tick = GetTickCount(); if (!app->controls_visible) app->show_controls(true); }
        break;
    case WM_DROPFILES:
        if (app) { wchar_t path[32768]{}; DragQueryFileW(HDROP(wparam), 0, path, DWORD(std::size(path))); DragFinish(HDROP(wparam)); app->open_file(path); }
        return 0;
    case WM_TIMER:
        if (app && wparam == kRenderTimer) app->update();
        return 0;
    case WM_DESTROY:
        if (app) { KillTimer(window, kRenderTimer); app->audio.close(); app->core.close(); }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    _wputenv_s(L"OMP_WAIT_POLICY", L"PASSIVE");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES}; InitCommonControlsEx(&common);
    App app;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(0, kWindowClass, L"GHV Player", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700, nullptr, nullptr, instance, &app);
    if (!window) { CoUninitialize(); return 1; }
    ShowWindow(window, show); UpdateWindow(window);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring media_path;
    if (argv) for (int i=1;i<argc;++i) {
        if (wcscmp(argv[i],L"--stats")==0 && i+1<argc) app.stats_path=argv[++i];
        else if (wcscmp(argv[i],L"--exit-at-eof")==0) app.exit_at_eof=true;
        else if (argv[i][0]!=L'-') media_path=argv[i];
    }
    if (!media_path.empty()) app.open_file(media_path);
    if (argv) LocalFree(argv);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return int(message.wParam);
}
#endif
