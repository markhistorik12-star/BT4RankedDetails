#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>

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

struct Bt4FighterSnap {
    int active = 0;
    int count = 0;
    int chara = -1;
    int hp = 0;
    int hp_max = 0;
    int ki = 0;
    int ki_max = 0;
    int blast = 0;
    bool powered_up = false;
    int combo_hits = 0;
    int combo_damage = 0;
    int hud_hits = 0;
    int hud_damage = 0;
    int hit_seq = 0;
    int message = -1;
    int message_seq = 0;
    int member_charas[5] = {-1, -1, -1, -1, -1};
};

struct Bt4BattleSnap {
    bool in_battle = false;
    bool hud_shown = false;
    Bt4FighterSnap sides[2];
    std::string player_names[2];
};

using FnNetGetMeterStats = int (*)(Bt4NetMeterStats* out);
using FnIntVoid = int (*)();
using FnPlayerName = const char* (*)(int side);
using FnHudReadState = int (*)(void* buf);

struct Bt4Bindings {
    uintptr_t module_base = 0;
    size_t module_size = 0;

    FnNetGetMeterStats net_get_meter_stats = nullptr;
    FnIntVoid net_is_relay = nullptr;
    FnIntVoid net_is_online = nullptr;
    FnIntVoid net_local_side = nullptr;
    FnPlayerName net_player_name = nullptr;
    FnHudReadState hud_read_state = nullptr;

    const float* fps_ptr = nullptr;
    const float* speed_pct_ptr = nullptr;

    bool resolved = false;
    int faults = 0;
    int battle_faults = 0;
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
    auto h1 = scan_aob(text_base, text_size,
        "8B 05 ?? ?? ?? ?? 85 C0 7E 76 83 F8 03 74 71 8B 05 ?? ?? ?? ?? 85 C0 74 5E 8B 05 ?? ?? ?? ?? 89 01");
    if (h1.size() == 1) {
        b.net_get_meter_stats = reinterpret_cast<FnNetGetMeterStats>(h1[0]);
        oss << "net_get_meter_stats=OK ";
    } else {
        oss << "net_get_meter_stats=MISS(" << h1.size() << ") ";
    }

    // 2. meter_fps_speed
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

    // 3. net_is_relay() (works in both R3 and R3.1)
    auto h3 = scan_aob(text_base, text_size,
        "E8 ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 8B 94 24 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 85 C0 48 8D 05 ?? ?? ?? ?? 4C 0F 44 C0");
    if (h3.size() == 1) {
        b.net_is_relay = reinterpret_cast<FnIntVoid>(resolve_rip_rel32(h3[0], 1, 5));
        oss << "net_is_relay=OK ";
    } else {
        oss << "net_is_relay=MISS(" << h3.size() << ") ";
    }

    // 4. lua_online_side -> net_is_online() + net_local_side()
    auto h4 = scan_aob(text_base, text_size,
        "E8 ?? ?? ?? ?? 85 C0 75 0D E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8D 50 01 48 63 D2");
    if (h4.size() == 1) {
        b.net_is_online  = reinterpret_cast<FnIntVoid>(resolve_rip_rel32(h4[0], 1, 5));
        b.net_local_side = reinterpret_cast<FnIntVoid>(resolve_rip_rel32(h4[0] + 22, 1, 5));
        oss << "online/side=OK ";
    } else {
        oss << "online/side=MISS(" << h4.size() << ") ";
    }

    // 5. bt3_net_player_name(int side) -> real player 1 & player 2 names!
    auto h5 = scan_aob(text_base, text_size,
        "B9 01 00 00 00 E8 ?? ?? ?? ?? 31 C9 48 89 C5 E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 C7");
    if (h5.size() == 1) {
        b.net_player_name = reinterpret_cast<FnPlayerName>(resolve_rip_rel32(h5[0] + 5, 1, 5));
        oss << "player_name=OK ";
    } else {
        oss << "player_name=MISS(" << h5.size() << ") ";
    }

    // 6. bt3_hud_read_state(void* buf) -> direct fighter/HP/combo/chara reader!
    auto h6 = scan_aob(text_base, text_size,
        "48 8D 84 24 ?? ?? 00 00 48 89 C1 48 89 44 24 ?? E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 48 8D 8C 24 ?? 00 00 00 E8");
    if (h6.size() == 1) {
        b.hud_read_state = reinterpret_cast<FnHudReadState>(resolve_rip_rel32(h6[0] + 16, 1, 5));
        oss << "hud_read=OK";
    } else {
        oss << "hud_read=MISS(" << h6.size() << ")";
    }

    b.report = oss.str();
    b.resolved = (b.net_get_meter_stats != nullptr);
    return b.resolved;
}

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

struct Bt4BattlePod {
    bool in_battle = false;
    bool hud_shown = false;
    Bt4FighterSnap sides[2];
    char player_names[2][64]{};
};

inline bool safe_read_battle_pod(Bt4Bindings& b, Bt4BattlePod& out) {
    if (b.battle_faults >= 5) return false;
    __try {
        std::memset(&out, 0, sizeof(out));
        for (int s = 0; s < 2; ++s) {
            out.sides[s].chara = -1;
            out.sides[s].message = -1;
            for (int m = 0; m < 5; ++m) out.sides[s].member_charas[m] = -1;
        }
        if (b.net_player_name) {
            for (int s = 0; s < 2; ++s) {
                const char* nm = b.net_player_name(s);
                if (nm && is_readable_ptr(nm, 1) && nm[0] != '\0') {
                    size_t len = 0;
                    while (len < 63 && nm[len] != '\0') {
                        out.player_names[s][len] = nm[len];
                        ++len;
                    }
                    out.player_names[s][len] = '\0';
                }
            }
        }
        if (b.hud_read_state) {
            alignas(16) uint8_t buf[0x2000]{};
            int ok = b.hud_read_state(buf);
            if (ok) {
                int32_t battle_flag = *reinterpret_cast<const int32_t*>(buf + 0x000);
                int32_t shown_flag  = *reinterpret_cast<const int32_t*>(buf + 0x004);
                out.in_battle = (battle_flag != 0);
                out.hud_shown = (shown_flag != 0);
                if (out.in_battle) {
                    for (int s = 0; s < 2; ++s) {
                        const uint8_t* f = buf + s * 0x730;
                        auto& dst = out.sides[s];
                        dst.active       = *reinterpret_cast<const int32_t*>(f + 0x18);
                        dst.count        = *reinterpret_cast<const int32_t*>(f + 0x1C);
                        dst.hp           = *reinterpret_cast<const int32_t*>(f + 0x24);
                        dst.hp_max       = *reinterpret_cast<const int32_t*>(f + 0x28);
                        dst.ki           = *reinterpret_cast<const int32_t*>(f + 0x2C);
                        dst.ki_max       = *reinterpret_cast<const int32_t*>(f + 0x30);
                        dst.blast        = *reinterpret_cast<const int32_t*>(f + 0x34);
                        dst.powered_up   = (*reinterpret_cast<const int32_t*>(f + 0x38) != 0);
                        dst.combo_hits   = *reinterpret_cast<const int32_t*>(f + 0x3C);
                        dst.combo_damage = *reinterpret_cast<const int32_t*>(f + 0x40);
                        dst.hud_hits     = *reinterpret_cast<const int32_t*>(f + 0x1B8);
                        dst.hud_damage   = *reinterpret_cast<const int32_t*>(f + 0x1BC);
                        dst.hit_seq      = *reinterpret_cast<const int32_t*>(f + 0x1C0);
                        dst.message      = *reinterpret_cast<const int32_t*>(f + 0x1C4);
                        dst.message_seq  = *reinterpret_cast<const int32_t*>(f + 0x1C8);

                        int lim = (dst.count > 0 && dst.count <= 5) ? dst.count : 1;
                        for (int m = 0; m < lim; ++m) {
                            int32_t cid = *reinterpret_cast<const int32_t*>(f + 0x1D0 + m * 0x118);
                            if (cid >= 0 && cid < 300) dst.member_charas[m] = cid;
                        }
                        if (dst.active >= 0 && dst.active < 5 && dst.member_charas[dst.active] >= 0) {
                            dst.chara = dst.member_charas[dst.active];
                        } else if (dst.member_charas[0] >= 0) {
                            dst.chara = dst.member_charas[0];
                        }
                    }
                }
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++b.battle_faults;
        return false;
    }
}

inline bool safe_read_battle_and_names(Bt4Bindings& b, Bt4BattleSnap& out) {
    Bt4BattlePod pod{};
    if (!safe_read_battle_pod(b, pod)) return false;
    out.in_battle = pod.in_battle;
    out.hud_shown = pod.hud_shown;
    out.sides[0] = pod.sides[0];
    out.sides[1] = pod.sides[1];
    out.player_names[0] = pod.player_names[0];
    out.player_names[1] = pod.player_names[1];
    return true;
}

} // namespace bt4rd
