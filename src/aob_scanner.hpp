#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>

namespace bt4rd {

#pragma pack(push, 4)
struct Bt4NetMeterStats {
    int32_t ping_ms;              // +0x00 (< 0 if pending)
    int32_t rollbacks_per_sec;    // +0x04
    int32_t rollback_frames_x10;  // +0x08 (divided by 10.0f -> avg rollback frames)
    int32_t waited_times;         // +0x0C
    int32_t waited_ms_per_sec;    // +0x10
    int32_t rollback_max;         // +0x14 (<= 0 means rollback off)
    int32_t input_delay;          // +0x18 (blanks)
    int32_t delay_is_auto;        // +0x1C
    int32_t wait_reason;          // +0x20 (1 = other player behind, 2 = nothing arrived)
    int32_t desync_frame;         // +0x24 (!= 0 -> desynced since frame)
};
#pragma pack(pop)

using FnNetGetMeterStats = int (*)(Bt4NetMeterStats* out);
using FnIntVoid = int (*)();

struct Bt4Bindings {
    uintptr_t module_base = 0;
    size_t module_size = 0;

    FnNetGetMeterStats net_get_meter_stats = nullptr;
    FnIntVoid net_is_relay = nullptr;
    FnIntVoid net_is_online = nullptr;
    FnIntVoid net_local_side = nullptr;

    const float* fps_ptr = nullptr;
    const float* speed_pct_ptr = nullptr;

    bool resolved = false;
    int faults = 0;
    std::string report;
};

inline bool is_readable_ptr(const void* ptr, size_t len = 4) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD p = mbi.Protect & 0xFF;
    if (p == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD)) return false;
    uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return reinterpret_cast<uintptr_t>(ptr) + len <= end;
}

inline std::vector<int> parse_aob(const char* pat) {
    std::vector<int> out;
    std::istringstream iss(pat);
    std::string tok;
    while (iss >> tok) {
        if (tok == "?" || tok == "??") out.push_back(-1);
        else out.push_back((int)std::strtol(tok.c_str(), nullptr, 16));
    }
    return out;
}

inline std::vector<uintptr_t> scan_aob(uintptr_t base, size_t size, const char* pat) {
    std::vector<uintptr_t> hits;
    auto bs = parse_aob(pat);
    if (bs.empty() || size < bs.size()) return hits;
    const uint8_t* mem = reinterpret_cast<const uint8_t*>(base);
    size_t limit = size - bs.size();
    for (size_t i = 0; i <= limit; ++i) {
        if (bs[0] != -1 && mem[i] != (uint8_t)bs[0]) continue;
        bool ok = true;
        for (size_t j = 1; j < bs.size(); ++j) {
            if (bs[j] != -1 && mem[i + j] != (uint8_t)bs[j]) { ok = false; break; }
        }
        if (ok) {
            hits.push_back(base + i);
            if (hits.size() >= 4) break;
        }
    }
    return hits;
}

inline uintptr_t resolve_rip_rel32(uintptr_t instr_addr, size_t disp_offset, size_t instr_len) {
    int32_t disp = *reinterpret_cast<const int32_t*>(instr_addr + disp_offset);
    return static_cast<uintptr_t>(static_cast<intptr_t>(instr_addr + instr_len) + disp);
}

inline bool get_main_exe_text(uintptr_t& out_base, size_t& out_image_size, uintptr_t& out_text_base, size_t& out_text_size) {
    HMODULE hm = GetModuleHandleW(nullptr);
    if (!hm) return false;
    out_base = reinterpret_cast<uintptr_t>(hm);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(out_base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(out_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out_image_size = nt->OptionalHeader.SizeOfImage;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sec[i].Name, ".text", 5) == 0) {
            out_text_base = out_base + sec[i].VirtualAddress;
            out_text_size = sec[i].Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

inline bool resolve_bt4_bindings(Bt4Bindings& b) {
    uintptr_t text_base = 0;
    size_t text_size = 0;
    if (!get_main_exe_text(b.module_base, b.module_size, text_base, text_size)) {
        b.report = "failed to inspect main exe PE headers";
        return false;
    }

    std::ostringstream oss;

    // 1. bt3_net_get_meter_stats(Bt4NetMeterStats*)
    // 8B 05 ?? ?? ?? ?? 85 C0 7E 76 83 F8 03 74 71 8B 05 ?? ?? ?? ?? 85 C0 74 5E 8B 05 ?? ?? ?? ?? 89 01
    auto h1 = scan_aob(text_base, text_size,
        "8B 05 ?? ?? ?? ?? 85 C0 7E 76 83 F8 03 74 71 8B 05 ?? ?? ?? ?? 85 C0 74 5E 8B 05 ?? ?? ?? ?? 89 01");
    if (h1.size() == 1) {
        b.net_get_meter_stats = reinterpret_cast<FnNetGetMeterStats>(h1[0]);
        oss << "net_get_meter_stats=OK ";
    } else {
        oss << "net_get_meter_stats=MISS(" << h1.size() << ") ";
    }

    // 2. meter_fps_speed:
    // 66 0F EF D2 66 0F EF C9 48 8D 0D [7] F3 0F 5A 15 [speed_disp32 @+19, len 8 -> +23] F3 0F 5A 0D [fps_disp32 @+27, len 8 -> +31]
    auto h2 = scan_aob(text_base, text_size,
        "66 0F EF D2 66 0F EF C9 48 8D 0D ?? ?? ?? ?? F3 0F 5A 15 ?? ?? ?? ?? F3 0F 5A 0D ?? ?? ?? ?? 66 49 0F 7E D0");
    if (h2.size() == 1) {
        uintptr_t speed_addr = resolve_rip_rel32(h2[0] + 15, 4, 8);
        uintptr_t fps_addr   = resolve_rip_rel32(h2[0] + 23, 4, 8);
        if (is_readable_ptr((void*)speed_addr, 4) && is_readable_ptr((void*)fps_addr, 4)) {
            b.speed_pct_ptr = reinterpret_cast<const float*>(speed_addr);
            b.fps_ptr = reinterpret_cast<const float*>(fps_addr);
            oss << "fps/speed=OK ";
        }
    } else {
        oss << "fps/speed=MISS(" << h2.size() << ") ";
    }

    // 3. meter_net_call -> resolves net_get_meter_stats (fallback) + net_is_relay()
    // 4C 89 E1 E8 [rel32 @+4, next +8] 85 C0 0F 84 [4] 44 8B 8C 24 40 01 00 00 45 85 C9 0F 88 [4] E8 [rel32 @+34, next +38]
    auto h3 = scan_aob(text_base, text_size,
        "4C 89 E1 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 44 8B 8C 24 40 01 00 00 45 85 C9 0F 88 ?? ?? ?? ?? E8");
    if (h3.size() == 1) {
        if (!b.net_get_meter_stats) {
            b.net_get_meter_stats = reinterpret_cast<FnNetGetMeterStats>(resolve_rip_rel32(h3[0] + 3, 1, 5));
        }
        b.net_is_relay = reinterpret_cast<FnIntVoid>(resolve_rip_rel32(h3[0] + 33, 1, 5));
        oss << "net_is_relay=OK ";
    } else {
        oss << "net_is_relay=MISS(" << h3.size() << ") ";
    }

    // 4. lua_online_side -> resolves net_is_online() and net_local_side()
    // E8 [rel32 @+1, next +5] 85 C0 75 0D E8 [4] 85 C0 0F 84 [4] E8 [rel32 @+23, next +27] 8D 50 01 48 63 D2
    auto h4 = scan_aob(text_base, text_size,
        "E8 ?? ?? ?? ?? 85 C0 75 0D E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8D 50 01 48 63 D2");
    if (h4.size() == 1) {
        b.net_is_online  = reinterpret_cast<FnIntVoid>(resolve_rip_rel32(h4[0], 1, 5));
        b.net_local_side = reinterpret_cast<FnIntVoid>(resolve_rip_rel32(h4[0] + 22, 1, 5));
        oss << "online/side=OK";
    } else {
        oss << "online/side=MISS(" << h4.size() << ")";
    }

    b.report = oss.str();
    b.resolved = (b.net_get_meter_stats != nullptr);
    return b.resolved;
}

// SEH-guarded safe readers (fail-closed)
inline bool safe_read_net_stats(Bt4Bindings& b, Bt4NetMeterStats& out, bool& out_relay, bool& out_online, int& out_side, float& out_fps, float& out_speed) {
    if (!b.resolved || b.faults >= 5) return false;
    __try {
        out = {};
        int ok = b.net_get_meter_stats ? b.net_get_meter_stats(&out) : 0;
        out_relay  = b.net_is_relay ? (b.net_is_relay() != 0) : false;
        out_online = b.net_is_online ? (b.net_is_online() != 0) : (ok != 0);
        out_side   = b.net_local_side ? b.net_local_side() : 0;
        if (b.fps_ptr && is_readable_ptr(b.fps_ptr, 4)) out_fps = *b.fps_ptr;
        if (b.speed_pct_ptr && is_readable_ptr(b.speed_pct_ptr, 4)) out_speed = *b.speed_pct_ptr;
        return ok != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++b.faults;
        return false;
    }
}

} // namespace bt4rd
