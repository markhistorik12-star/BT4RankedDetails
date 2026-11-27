#pragma once
#include "config.hpp"
#include "telemetry.hpp"
#include <windows.h>
#include <objidl.h>
#include <algorithm>
using std::min;
using std::max;
#pragma warning(push)
#pragma warning(disable: 4458)
#include <gdiplus.h>
#pragma warning(pop)
#include <atomic>
#include <thread>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace bt4rd {

class NativeOverlay {
public:
    void start(TelemetryEngine* eng, const Config* cfg) {
        eng_ = eng;
        cfg_ = cfg;
        running_.store(true);
        th_ = std::thread([this]() { run_loop(); });
    }

    void stop() {
        running_.store(false);
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        if (th_.joinable()) th_.join();
    }

private:
    TelemetryEngine* eng_ = nullptr;
    const Config* cfg_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread th_;
    HWND hwnd_ = nullptr;
    HWND game_hwnd_ = nullptr;

    static BOOL CALLBACK find_game_window_cb(HWND h, LPARAM lp) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(h)) return TRUE;
        wchar_t cls[64]{};
        GetClassNameW(h, cls, 64);
        if (std::wcscmp(cls, L"BT4RankedDetailsOverlay") == 0) return TRUE;
        RECT rc{};
        GetClientRect(h, &rc);
        if ((rc.right - rc.left) >= 320 && (rc.bottom - rc.top) >= 240) {
            *reinterpret_cast<HWND*>(lp) = h;
            return FALSE;
        }
        return TRUE;
    }

    static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(h, msg, wp, lp);
    }

    static Gdiplus::Color ping_color(int ms) {
        if (ms < 0)   return Gdiplus::Color(255, 190, 200, 215);
        if (ms < 66)  return Gdiplus::Color(255, 86, 224, 128);  // Great (green)
        if (ms < 133) return Gdiplus::Color(255, 245, 214, 72);  // Decent (yellow)
        if (ms < 266) return Gdiplus::Color(255, 250, 148, 56);  // Rough (orange)
        return Gdiplus::Color(255, 242, 82, 76);                 // Poor (red)
    }

    static void add_round_rect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
        if (r <= 0.5f) {
            path.AddRectangle(Gdiplus::RectF(x, y, w, h));
            return;
        }
        float d = r * 2.0f;
        path.AddArc(x, y, d, d, 180.0f, 90.0f);
        path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
        path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    }

    void render_panel(const LiveSnapshot& snap, const Config& cfg) {
        if (!hwnd_) return;

        bool visible = cfg.display && (cfg.show_always || snap.in_session);
        if (!game_hwnd_ || !IsWindow(game_hwnd_)) {
            game_hwnd_ = nullptr;
            EnumWindows(find_game_window_cb, reinterpret_cast<LPARAM>(&game_hwnd_));
        }
        if (!visible || !game_hwnd_ || !IsWindowVisible(game_hwnd_) || IsIconic(game_hwnd_)) {
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }

        // Always hide overlay when BT4decomp is not the active foreground window (even with show_always = 1)
        HWND fg = GetForegroundWindow();
        DWORD fg_pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fg_pid);
        if (!fg || fg_pid != GetCurrentProcessId()) {
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }

        RECT crc{};
        GetClientRect(game_hwnd_, &crc);
        POINT br{crc.right, crc.bottom};
        ClientToScreen(game_hwnd_, &br);

        const int W = std::max(cfg.panel_min_width, 224);
        const int H = (cfg.swing ? 114 : 96);
        int px = br.x - W - cfg.offset_x;
        int py = br.y - H - cfg.offset_y;

        // Create 32-bit premultiplied ARGB DIB for per-pixel alpha layered window
        HDC scrDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(scrDC);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = W;
        bmi.bmiHeader.biHeight = -H; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(scrDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBmp = SelectObject(memDC, bmp);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));

            if (cfg.panel) {
                BYTE alpha = (BYTE)std::clamp((cfg.panel_opacity * 255) / 100, 20, 245);
                Gdiplus::GraphicsPath bgPath;
                add_round_rect(bgPath, 2.0f, 2.0f, (float)(W - 4), (float)(H - 4), (float)cfg.panel_radius);
                Gdiplus::LinearGradientBrush bgBrush(
                    Gdiplus::PointF(0, 0), Gdiplus::PointF(0, (float)H),
                    Gdiplus::Color(alpha, 14, 18, 28),
                    Gdiplus::Color((BYTE)std::min(255, alpha + 20), 6, 9, 16));
                g.FillPath(&bgBrush, &bgPath);

                Gdiplus::Pen borderPen(Gdiplus::Color(110, 120, 155, 210), 1.0f);
                g.DrawPath(&borderPen, &bgPath);
            }

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font fMain(&ff, (float)cfg.font_main * 0.75f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Font fSmall(&ff, (float)cfg.font_small, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::Font fTiny(&ff, (float)std::max(10, cfg.font_small - 2), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

            float y = 8.0f;

            // 1. Delay / Rollback Swing bar
            if (cfg.swing) {
                int diff = (snap.input_delay >= 0 && snap.start_delay >= 0) ? (snap.input_delay - snap.start_delay) : 0;
                float bx = 14.0f, bw = (float)(W - 64), bh = 8.0f;
                Gdiplus::SolidBrush trackBr(Gdiplus::Color(90, 255, 255, 255));
                g.FillRectangle(&trackBr, bx, y + 3.0f, bw, bh);

                float mid = bx + bw * 0.35f;
                int clamped = std::clamp(diff, -4, 8);
                if (clamped != 0) {
                    bool recent = (now_mono_ms() - snap.last_delay_change_ms) < 2000;
                    Gdiplus::SolidBrush fillBr(recent ? Gdiplus::Color(255, 240, 72, 64) : Gdiplus::Color(255, 245, 154, 48));
                    float step = bw / 14.0f;
                    float x0 = clamped > 0 ? mid : (mid + clamped * step);
                    float w0 = std::abs(clamped) * step;
                    g.FillRectangle(&fillBr, x0, y + 3.0f, w0, bh);
                }
                Gdiplus::SolidBrush notchBr(Gdiplus::Color(255, 255, 255, 255));
                g.FillRectangle(&notchBr, mid - 1.5f, y, 3.0f, bh + 6.0f);

                wchar_t swTxt[16]{};
                if (diff != 0) std::swprintf(swTxt, 16, L"%+df", diff);
                else std::swprintf(swTxt, 16, L"0f");
                Gdiplus::SolidBrush swBr(diff == 0 ? Gdiplus::Color(180, 180, 195, 215) : Gdiplus::Color(255, 255, 200, 110));
                g.DrawString(swTxt, -1, &fTiny, Gdiplus::PointF(bx + bw + 8.0f, y - 1.0f), &swBr);
                y += 18.0f;
            }

            // 2. Main Ping + Jitter + DIR/RLY badge
            wchar_t pingTxt[16]{};
            if (snap.ping_ms >= 0) std::swprintf(pingTxt, 16, L"%d", snap.ping_ms);
            else std::swprintf(pingTxt, 16, L"--");

            Gdiplus::SolidBrush pingBr(ping_color(snap.ping_ms));
            g.DrawString(pingTxt, -1, &fMain, Gdiplus::PointF(14.0f, y), &pingBr);

            wchar_t jitTxt[32]{};
            std::swprintf(jitTxt, 32, L"\u00B1%.0f", snap.jitter_ms);
            Gdiplus::SolidBrush subBr(Gdiplus::Color(220, 210, 222, 238));
            g.DrawString(jitTxt, -1, &fSmall, Gdiplus::PointF(88.0f, y + 1.0f), &subBr);
            g.DrawString(L"ms", -1, &fSmall, Gdiplus::PointF(88.0f, y + 15.0f), &subBr);

            // Transport badge: DIR (green/cyan) or RLY (amber)
            const wchar_t* badge = snap.relay ? L"RLY" : L"DIR";
            Gdiplus::SolidBrush badgeBg(snap.relay ? Gdiplus::Color(180, 180, 105, 20) : Gdiplus::Color(160, 28, 118, 168));
            Gdiplus::GraphicsPath bPath;
            add_round_rect(bPath, (float)(W - 50), y + 4.0f, 36.0f, 18.0f, 4.0f);
            g.FillPath(&badgeBg, &bPath);
            Gdiplus::SolidBrush whiteBr(Gdiplus::Color(255, 255, 255, 255));
            g.DrawString(badge, -1, &fTiny, Gdiplus::PointF((float)(W - 44), y + 6.0f), &whiteBr);

            y += 34.0f;

            // 3. Rollbacks & Waits
            if (cfg.show_rollback) {
                wchar_t rbTxt[64]{};
                if (snap.desync_frame > 0) {
                    std::swprintf(rbTxt, 64, L"DESYNC @ f%d", snap.desync_frame);
                    Gdiplus::SolidBrush redBr(Gdiplus::Color(255, 255, 90, 80));
                    g.DrawString(rbTxt, -1, &fTiny, Gdiplus::PointF(14.0f, y), &redBr);
                } else {
                    std::swprintf(rbTxt, 64, L"rb %d/s (%.1ff)   w %d", snap.rollbacks_per_sec, snap.rollback_frames, snap.waits_total);
                    g.DrawString(rbTxt, -1, &fSmall, Gdiplus::PointF(14.0f, y), &subBr);
                }
                y += 18.0f;
            }

            // 4. Input Delay + Speed
            if (cfg.show_delay) {
                wchar_t dlTxt[64]{};
                if (snap.input_delay >= 0) {
                    std::swprintf(dlTxt, 64, L"delay %d f%s \u00B7 %.0f%%", snap.input_delay, snap.delay_auto ? L"*" : L"", snap.speed_pct);
                } else {
                    std::swprintf(dlTxt, 64, L"delay -- f \u00B7 %.0f%%", snap.speed_pct);
                }
                Gdiplus::SolidBrush dimBr(Gdiplus::Color(195, 175, 192, 215));
                g.DrawString(dlTxt, -1, &fSmall, Gdiplus::PointF(14.0f, y), &dimBr);
            }
        }

        POINT ptDst{px, py};
        SIZE szWnd{W, H};
        POINT ptSrc{0, 0};
        BLENDFUNCTION bf{};
        bf.BlendOp = AC_SRC_OVER;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat = AC_SRC_ALPHA;

        SetWindowPos(hwnd_, HWND_TOPMOST, px, py, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateLayeredWindow(hwnd_, scrDC, &ptDst, &szWnd, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, scrDC);
    }

    void run_loop() {
        Gdiplus::GdiplusStartupInput gsi{};
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartup(&token, &gsi, nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BT4RankedDetailsOverlay";
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"", WS_POPUP,
            0, 0, 224, 114, nullptr, nullptr, wc.hInstance, nullptr);

        while (running_.load()) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (eng_ && cfg_) {
                LiveSnapshot snap = eng_->get_live_snapshot();
                render_panel(snap, *cfg_);
            }
            Sleep(125); // 8 Hz smooth overlay updates
        }

        if (hwnd_) DestroyWindow(hwnd_);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        Gdiplus::GdiplusShutdown(token);
    }
};

} // namespace bt4rd
