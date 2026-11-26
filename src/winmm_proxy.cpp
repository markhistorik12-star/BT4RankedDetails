#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <process.h>
#include <atomic>
#include <string>

#include "config.hpp"
#include "aob_scanner.hpp"
#include "winhttp_iat.hpp"
#include "telemetry.hpp"
#include "overlay.hpp"

// ============================================================================
// 1. Transparent System32\winmm.dll Proxy Forwarding
// ============================================================================
static HMODULE g_real_winmm = nullptr;

static HMODULE real_winmm() {
    if (!g_real_winmm) {
        wchar_t sysdir[MAX_PATH]{};
        GetSystemDirectoryW(sysdir, MAX_PATH);
        std::wstring p = std::wstring(sysdir) + L"\\winmm.dll";
        g_real_winmm = LoadLibraryW(p.c_str());
    }
    return g_real_winmm;
}

template <typename Fn>
static Fn load_winmm(const char* name) {
    static HMODULE m = real_winmm();
    return m ? reinterpret_cast<Fn>(GetProcAddress(m, name)) : nullptr;
}

extern "C" {

MMRESULT WINAPI proxy_timeBeginPeriod(UINT uPeriod) {
    static auto fn = load_winmm<decltype(&timeBeginPeriod)>("timeBeginPeriod");
    return fn ? fn(uPeriod) : TIMERR_NOCANDO;
}
MMRESULT WINAPI proxy_timeEndPeriod(UINT uPeriod) {
    static auto fn = load_winmm<decltype(&timeEndPeriod)>("timeEndPeriod");
    return fn ? fn(uPeriod) : TIMERR_NOCANDO;
}
DWORD WINAPI proxy_timeGetTime() {
    static auto fn = load_winmm<decltype(&timeGetTime)>("timeGetTime");
    return fn ? fn() : GetTickCount();
}
MMRESULT WINAPI proxy_timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc) {
    static auto fn = load_winmm<decltype(&timeGetDevCaps)>("timeGetDevCaps");
    return fn ? fn(ptc, cbtc) : TIMERR_NOCANDO;
}
MMRESULT WINAPI proxy_timeKillEvent(UINT uTimerID) {
    static auto fn = load_winmm<decltype(&timeKillEvent)>("timeKillEvent");
    return fn ? fn(uTimerID) : TIMERR_NOCANDO;
}
MMRESULT WINAPI proxy_timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK lpTimeProc, DWORD_PTR dwUser, UINT fuEvent) {
    static auto fn = load_winmm<decltype(&timeSetEvent)>("timeSetEvent");
    return fn ? fn(uDelay, uResolution, lpTimeProc, dwUser, fuEvent) : 0;
}

MMRESULT WINAPI proxy_waveInAddBuffer(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh) {
    static auto fn = load_winmm<decltype(&waveInAddBuffer)>("waveInAddBuffer");
    return fn ? fn(hwi, pwh, cbwh) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveInClose(HWAVEIN hwi) {
    static auto fn = load_winmm<decltype(&waveInClose)>("waveInClose");
    return fn ? fn(hwi) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveInGetDevCapsW(UINT_PTR uDeviceID, LPWAVEINCAPSW pwic, UINT cbwic) {
    static auto fn = load_winmm<decltype(&waveInGetDevCapsW)>("waveInGetDevCapsW");
    return fn ? fn(uDeviceID, pwic, cbwic) : MMSYSERR_ERROR;
}
UINT WINAPI proxy_waveInGetNumDevs() {
    static auto fn = load_winmm<decltype(&waveInGetNumDevs)>("waveInGetNumDevs");
    return fn ? fn() : 0;
}
MMRESULT WINAPI proxy_waveInOpen(LPHWAVEIN phwi, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen) {
    static auto fn = load_winmm<decltype(&waveInOpen)>("waveInOpen");
    return fn ? fn(phwi, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveInPrepareHeader(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh) {
    static auto fn = load_winmm<decltype(&waveInPrepareHeader)>("waveInPrepareHeader");
    return fn ? fn(hwi, pwh, cbwh) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveInReset(HWAVEIN hwi) {
    static auto fn = load_winmm<decltype(&waveInReset)>("waveInReset");
    return fn ? fn(hwi) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveInStart(HWAVEIN hwi) {
    static auto fn = load_winmm<decltype(&waveInStart)>("waveInStart");
    return fn ? fn(hwi) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveInStop(HWAVEIN hwi) {
    static auto fn = load_winmm<decltype(&waveInStop)>("waveInStop");
    return fn ? fn(hwi) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveInUnprepareHeader(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh) {
    static auto fn = load_winmm<decltype(&waveInUnprepareHeader)>("waveInUnprepareHeader");
    return fn ? fn(hwi, pwh, cbwh) : MMSYSERR_ERROR;
}

MMRESULT WINAPI proxy_waveOutClose(HWAVEOUT hwo) {
    static auto fn = load_winmm<decltype(&waveOutClose)>("waveOutClose");
    return fn ? fn(hwo) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveOutGetDevCapsW(UINT_PTR uDeviceID, LPWAVEOUTCAPSW pwoc, UINT cbwoc) {
    static auto fn = load_winmm<decltype(&waveOutGetDevCapsW)>("waveOutGetDevCapsW");
    return fn ? fn(uDeviceID, pwoc, cbwoc) : MMSYSERR_ERROR;
}
UINT WINAPI proxy_waveOutGetNumDevs() {
    static auto fn = load_winmm<decltype(&waveOutGetNumDevs)>("waveOutGetNumDevs");
    return fn ? fn() : 0;
}
MMRESULT WINAPI proxy_waveOutGetPosition(HWAVEOUT hwo, LPMMTIME pmmt, UINT cbmmt) {
    static auto fn = load_winmm<decltype(&waveOutGetPosition)>("waveOutGetPosition");
    return fn ? fn(hwo, pmmt, cbmmt) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveOutOpen(LPHWAVEOUT phwo, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen) {
    static auto fn = load_winmm<decltype(&waveOutOpen)>("waveOutOpen");
    return fn ? fn(phwo, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    static auto fn = load_winmm<decltype(&waveOutPrepareHeader)>("waveOutPrepareHeader");
    return fn ? fn(hwo, pwh, cbwh) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveOutReset(HWAVEOUT hwo) {
    static auto fn = load_winmm<decltype(&waveOutReset)>("waveOutReset");
    return fn ? fn(hwo) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    static auto fn = load_winmm<decltype(&waveOutUnprepareHeader)>("waveOutUnprepareHeader");
    return fn ? fn(hwo, pwh, cbwh) : MMSYSERR_ERROR;
}
MMRESULT WINAPI proxy_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    static auto fn = load_winmm<decltype(&waveOutWrite)>("waveOutWrite");
    return fn ? fn(hwo, pwh, cbwh) : MMSYSERR_ERROR;
}

BOOL WINAPI proxy_PlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    static auto fn = load_winmm<decltype(&PlaySoundA)>("PlaySoundA");
    return fn ? fn(pszSound, hmod, fdwSound) : FALSE;
}
BOOL WINAPI proxy_PlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    static auto fn = load_winmm<decltype(&PlaySoundW)>("PlaySoundW");
    return fn ? fn(pszSound, hmod, fdwSound) : FALSE;
}

} // extern "C"

// ============================================================================
// 2. BT4 Ranked Details Background Worker
// ============================================================================
static std::atomic<bool> g_stop{false};
static HANDLE g_worker = nullptr;
static bt4rd::Config g_cfg;
static bt4rd::Bt4Bindings g_bind;
static bt4rd::TelemetryEngine g_engine;
static bt4rd::NativeOverlay g_overlay;

static std::wstring get_exe_dir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t sl = s.find_last_of(L"\\/");
    return sl == std::wstring::npos ? L"." : s.substr(0, sl);
}

static unsigned __stdcall worker_main(void*) {
    Sleep(500); // let BT4decomp initialize CRT & window

    std::wstring exe_dir = get_exe_dir();
    g_engine.init(exe_dir);

    bt4rd::write_default_ini(g_engine.ini_path());
    bt4rd::load_config_if_changed(g_engine.ini_path(), g_cfg, true);

    g_engine.log_line("BT4 Rankeds Details v1.1.0 starting (proxy winmm.dll)");

    if (g_cfg.aob_scan) {
        bt4rd::resolve_bt4_bindings(g_bind);
        g_engine.log_line("AOB scan: " + g_bind.report);
    }

    if (g_cfg.hook_winhttp) {
        bool ok = bt4rd::WinHttpIatObserver::instance().install([](const bt4rd::HttpObservedEvent& ev) {
            g_engine.on_http_event(ev);
        });
        g_engine.log_line(std::string("WinHTTP IAT observer: ") + (ok ? "installed" : "not installed"));
    }

    g_overlay.start(&g_engine, &g_cfg);

    int tick_50ms = 0;
    while (!g_stop.load()) {
        // Every ~1 s: hot-reload INI
        if (tick_50ms % 20 == 0) {
            if (bt4rd::load_config_if_changed(g_engine.ini_path(), g_cfg, false)) {
                g_engine.log_line("BT4RankedDetails.ini reloaded");
            }
        }

        static int last_side = 0;
        // Every 250 ms (4 Hz): network stats & session lifecycle
        if (tick_50ms % 5 == 0) {
            bt4rd::Bt4NetMeterStats net{};
            bool relay = false, online = false;
            int side = 0;
            float fps = 0.0f, speed = 100.0f;
            bool has_net = bt4rd::safe_read_net_stats(g_bind, net, relay, online, side, fps, speed);
            last_side = side;
            g_engine.tick(g_cfg, has_net, net, relay, online, side, fps, speed);
        }

        // Every 50 ms (20 Hz): direct C++ player names & fighter/combat/HP tracker
        bt4rd::Bt4BattleSnap bsnap{};
        if (bt4rd::safe_read_battle_and_names(g_bind, bsnap)) {
            g_engine.tick_battle_direct(bsnap, last_side);
        }

        ++tick_50ms;
        Sleep(50);
    }

    g_overlay.stop();
    g_engine.shutdown(g_cfg);
    g_engine.log_line("BT4 Rankeds Details stopped cleanly");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        real_winmm();

        // Only activate telemetry inside BT4decomp.exe (avoid firing in setup/tools)
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring s(exe);
        for (auto& c : s) c = (wchar_t)towlower(c);
        if (s.find(L"bt4decomp.exe") != std::wstring::npos && s.find(L"setup") == std::wstring::npos) {
            g_worker = (HANDLE)_beginthreadex(nullptr, 0, worker_main, nullptr, 0, nullptr);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stop.store(true);
        if (g_worker) {
            WaitForSingleObject(g_worker, 1500);
            CloseHandle(g_worker);
            g_worker = nullptr;
        }
    }
    return TRUE;
}
