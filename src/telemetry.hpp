#pragma once
#include "config.hpp"
#include "aob_scanner.hpp"
#include "winhttp_iat.hpp"
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bt4rd {

struct LiveSnapshot {
    bool in_session = false;
    bool online = false;
    bool relay = false;
    int ping_ms = -1;
    double jitter_ms = 0.0;
    int input_delay = -1;
    int start_delay = -1;
    bool delay_auto = false;
    int rollbacks_per_sec = 0;
    double rollback_frames = 0.0;
    int rollback_max = 0;
    int waits_total = 0;
    int wait_ms_sec = 0;
    int wait_reason = 0;
    int desync_frame = 0;
    float fps = 0.0f;
    float speed_pct = 100.0f;
    int64_t last_delay_change_ms = 0;
    std::string opponent;
    std::string region;
    std::string mode_name;
};

struct FighterEntry {
    int chara = -1;
    std::string id;
    std::string name;
};

struct CombatSide {
    int dmg = 0;
    int taken = 0;
    int healed = 0;
    int heal_n = 0;
    int combo_n = 0;
    int combo_max = 0;
    int combo_sum = 0;
    int combo_max_dmg = 0;
    int sparking = 0;
    double sparking_s = 0.0;
    int switches = 0;
    int counters = 0;
    int blast_spent = 0;
    double charge_s = 0.0;
    int hits_total = 0;
};

struct DirectFighterTracker {
    int last_hp = -1;
    int stable_hp = -1;
    int pending_dir = 0;   // -1 = pending damage, +1 = pending heal
    int pending_ticks = 0;
    int last_chara = -1;
    int last_active = -1;
    int last_blast = -1;
    bool was_sparking = false;
    int last_hit_seq = -1;
    int last_combo_hits = 0;
    int last_combo_dmg = 0;
    int last_msg_seq = -1;
    int64_t switch_grace_until_ms = 0;
};

inline bool same_name_ci(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return lower_ascii(trim(a)) == lower_ascii(trim(b));
}

inline bool is_placeholder_player_name(const std::string& s) {
    std::string l = lower_ascii(trim(s));
    return l.empty() || l == "player 1" || l == "player 2";
}

inline std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o.push_back((char)c);
                }
        }
    }
    return o;
}

inline std::string json_str(const std::string& s) { return "\"" + json_escape(s) + "\""; }
inline std::string json_str_or_null(const std::string& s) { return s.empty() ? "null" : json_str(s); }

inline std::string now_local_iso(bool filename_safe = false) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[32];
    if (filename_safe) {
        std::snprintf(buf, sizeof(buf), "%04u%02u%02u_%02u%02u%02u",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    } else {
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    }
    return buf;
}

inline double now_unix_s() {
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 10000000.0;
}

inline int64_t now_mono_ms() {
    static LARGE_INTEGER freq = []() { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return (int64_t)((c.QuadPart * 1000LL) / (freq.QuadPart ? freq.QuadPart : 1LL));
}

inline bool write_file_atomic(const std::wstring& path, const std::string& content) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, content.data(), (DWORD)content.size(), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || written != content.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

inline bool append_file_line(const std::wstring& path, const std::string& line) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string data = line + "\n";
    DWORD written = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok && written == data.size();
}

inline bool json_extract_string(const std::string& j, const std::string& key, std::string& out) {
    std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < j.size() && std::isspace((unsigned char)j[p])) ++p;
    if (p >= j.size() || j[p] != '"') return false;
    ++p;
    std::string val;
    while (p < j.size() && j[p] != '"') {
        if (j[p] == '\\' && p + 1 < j.size()) { ++p; }
        val.push_back(j[p++]);
    }
    out = std::move(val);
    return true;
}

inline bool json_extract_int(const std::string& j, const std::string& key, int& out) {
    std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < j.size() && std::isspace((unsigned char)j[p])) ++p;
    if (p >= j.size() || (!std::isdigit((unsigned char)j[p]) && j[p] != '-')) return false;
    out = std::atoi(j.c_str() + p);
    return true;
}

class TelemetryEngine {
public:
    void init(const std::wstring& game_dir) {
        game_dir_ = game_dir;
        rd_dir_   = game_dir + L"\\ranked_details";
        data_dir_ = rd_dir_ + L"\\data";
        hist_dir_ = rd_dir_ + L"\\history";
        tl_data_dir_ = data_dir_ + L"\\timelines";
        tl_hist_dir_ = hist_dir_ + L"\\timelines";

        CreateDirectoryW(rd_dir_.c_str(), nullptr);
        CreateDirectoryW(data_dir_.c_str(), nullptr);
        CreateDirectoryW(hist_dir_.c_str(), nullptr);
        CreateDirectoryW(tl_data_dir_.c_str(), nullptr);
        CreateDirectoryW(tl_hist_dir_.c_str(), nullptr);

        load_self_name();
        load_character_names();
        rebuild_history_js();
    }

    std::wstring ini_path() const { return rd_dir_ + L"\\BT4RankedDetails.ini"; }
    std::wstring log_path() const { return data_dir_ + L"\\bt4rd.log"; }

    void log_line(const std::string& msg) {
        append_file_line(log_path(), "[" + now_local_iso() + "] " + msg);
    }

    void on_http_event(const HttpObservedEvent& ev) {
        std::lock_guard<std::mutex> lk(mu_);
        const std::string& req = ev.request_body;
        const std::string& res = ev.response_body;

        if (req.find("ranked_status") != std::string::npos || res.find("\"rating\"") != std::string::npos) {
            int r = 0, st = 0;
            if (json_extract_int(res, "rating", r)) {
                if (!session_.active) {
                    last_known_rating_ = r;
                    if (json_extract_int(res, "streak_wins", st)) last_known_streak_ = st;
                } else {
                    session_.rank_after = r;
                    if (json_extract_int(res, "streak_wins", st)) session_.streak_after = st;
                }
            }
        }

        if (req.find("ranked_report") != std::string::npos || req.find("ranked_forfeit") != std::string::npos) {
            session_.mode = 1;
            session_.mode_name = "ranking";
            int d = 0, r = 0, st = 0;
            if (json_extract_int(res, "delta", d)) {
                session_.rank_delta = d;
                session_.has_rank_delta = true;
            }
            if (json_extract_int(res, "rating", r)) {
                session_.rank_after = r;
                last_known_rating_ = r;
            }
            if (json_extract_int(res, "streak_wins", st)) {
                session_.streak_after = st;
                last_known_streak_ = st;
            }
            if (req.find("ranked_forfeit") != std::string::npos) {
                session_.end_type = "Forfeit";
                session_.has_incident = true;
                session_.incident_kind = "forfeit";
            }
            std::string st_str;
            if (json_extract_string(res, "status", st_str) && (st_str == "disputed" || st_str == "under_review")) {
                session_.has_incident = true;
                session_.incident_kind = st_str;
            }
        }

        std::string opp, oid, reg, rset;
        if (json_extract_string(res, "opponent_name", opp) || json_extract_string(res, "rival", opp) || json_extract_string(res, "nickname", opp)) {
            if (!is_placeholder_player_name(opp) && !same_name_ci(opp, self_name_)) session_.opponent = opp;
        }
        if (json_extract_string(res, "opponent_id", oid) || json_extract_string(res, "account_id", oid)) {
            if (!oid.empty()) session_.opponent_id = oid;
        }
        if (json_extract_string(res, "region", reg) && !reg.empty()) {
            session_.region = reg;
        }
        if (json_extract_string(req, "ruleset", rset) || json_extract_string(res, "ruleset", rset)) {
            if (rset == "team" || rset == "teams") session_.format = "team";
            else if (rset == "solo") session_.format = "singles";
        }
    }

    // Direct C++ battle & player-name tick (called at ~10-20 Hz, no Lua mod required!)
    void tick_battle_direct(const Bt4BattleSnap& snap, int local_side) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!session_.active) return;

        int me = (local_side == 1) ? 1 : 0;
        int opp = 1 - me;

        // 1. Player names directly from bt3_net_player_name(0/1)
        if (!is_placeholder_player_name(snap.player_names[me])) {
            self_name_ = trim(snap.player_names[me]);
        }
        if (!is_placeholder_player_name(snap.player_names[opp]) && !same_name_ci(snap.player_names[opp], self_name_)) {
            std::string clean_opp = trim(snap.player_names[opp]);
            if (session_.opponent != clean_opp) {
                session_.opponent = clean_opp;
                log_line("opponent detected via bt3_net_player_name: " + session_.opponent);
            }
        }

        int64_t t_ms = now_mono_ms();
        int rel_ms = (int)(t_ms - session_.start_mono_ms);
        double dt = (session_.last_battle_tick_ms > 0) ? std::min(0.25, (t_ms - session_.last_battle_tick_ms) / 1000.0) : 0.0;
        session_.last_battle_tick_ms = t_ms;

        // Detect battle start / end transitions
        if (snap.in_battle && !session_.saw_battle) {
            session_.saw_battle = true;
            session_.had_battle = true;
            session_.direct_trk[0] = DirectFighterTracker{};
            session_.direct_trk[1] = DirectFighterTracker{};
            session_.last_hp_sample_ms = 0;
        } else if (!snap.in_battle && session_.saw_battle) {
            // Battle just ended -> flush any pending damage, finalize combos & infer result
            for (int s = 0; s < 2; ++s) {
                auto& tr = session_.direct_trk[s];
                CombatSide& cs = (s == me) ? session_.combat_me : session_.combat_opp;
                CombatSide& other_cs = (s == me) ? session_.combat_opp : session_.combat_me;
                if (tr.stable_hp > 0 && tr.last_hp >= 0 && tr.last_hp < tr.stable_hp) {
                    int rem = tr.stable_hp - tr.last_hp;
                    if (rem > 0 && rem < 80000) {
                        other_cs.dmg += rem;
                        cs.taken += rem;
                        ++other_cs.hits_total;
                    }
                    tr.stable_hp = tr.last_hp;
                }
                if (tr.last_combo_hits >= 2) {
                    ++cs.combo_n;
                    cs.combo_sum += tr.last_combo_hits;
                    if (tr.last_combo_hits > cs.combo_max) cs.combo_max = tr.last_combo_hits;
                    if (tr.last_combo_dmg > cs.combo_max_dmg) cs.combo_max_dmg = tr.last_combo_dmg;
                    tr.last_combo_hits = 0;
                    tr.last_combo_dmg = 0;
                }
            }
            if (session_.result == "unknown") {
                int mhp = session_.direct_trk[me].last_hp;
                int ohp = session_.direct_trk[opp].last_hp;
                if (mhp > 0 && ohp == 0) { session_.result = "win"; session_.end_type = "KnockOut"; }
                else if (ohp > 0 && mhp == 0) { session_.result = "loss"; session_.end_type = "KnockOut"; }
                else if (mhp == 0 && ohp == 0) { session_.result = "draw"; session_.end_type = "DoubleKnockOut"; }
                else if (mhp > ohp) { session_.result = "win"; session_.end_type = "TimeUp"; }
                else if (ohp > mhp) { session_.result = "loss"; session_.end_type = "TimeUp"; }
            }
            session_.saw_battle = false;
            return;
        }

        if (!snap.in_battle) return;

        // Capture team rosters (up to 5 members per side)
        auto sync_team = [&](int s, std::vector<FighterEntry>& dst) {
            const auto& f = snap.sides[s];
            int lim = (f.count > 0 && f.count <= 5) ? f.count : 1;
            for (int m = 0; m < lim; ++m) {
                int cid = f.member_charas[m];
                if (cid < 0) continue;
                bool exists = false;
                for (auto& e : dst) if (e.chara == cid) { exists = true; break; }
                if (!exists) dst.push_back({cid, char_id_str(cid), char_name_str(cid)});
            }
            if (dst.size() > 1) session_.format = "team";
        };
        sync_team(me, session_.my_team);
        sync_team(opp, session_.opp_team);

        // Per-side combat tracking
        for (int s = 0; s < 2; ++s) {
            const auto& f = snap.sides[s];
            auto& tr = session_.direct_trk[s];
            CombatSide& cs = (s == me) ? session_.combat_me : session_.combat_opp;
            CombatSide& other_cs = (s == me) ? session_.combat_opp : session_.combat_me;
            const char* tag = (s == me) ? "me" : "opp";

            if (tr.last_active >= 0 && f.active != tr.last_active) {
                tr.switch_grace_until_ms = t_ms + 2000;
                tr.stable_hp = -1;
                tr.pending_dir = 0;
                tr.pending_ticks = 0;
                ++cs.switches;
                session_.tl_ev.push_back({rel_ms, tag, "switch", char_name_str(f.chara)});
            } else if (tr.last_chara >= 0 && f.chara >= 0 && f.chara != tr.last_chara) {
                tr.switch_grace_until_ms = t_ms + 2000;
                tr.stable_hp = -1;
                tr.pending_dir = 0;
                tr.pending_ticks = 0;
            }

            // Rollback-safe HP delta tracking: require 4 consecutive ticks (~200 ms) to confirm damage
            // (immediate on KO) and 10 consecutive ticks (~500 ms) + >=300 HP to confirm real healing.
            if (f.hp_max > 0 && f.hp >= 0) {
                if (t_ms <= tr.switch_grace_until_ms || tr.stable_hp < 0) {
                    tr.stable_hp = f.hp;
                    tr.pending_dir = 0;
                    tr.pending_ticks = 0;
                } else {
                    int d = f.hp - tr.stable_hp;
                    if (d < 0) {
                        if (tr.pending_dir == -1) ++tr.pending_ticks;
                        else { tr.pending_dir = -1; tr.pending_ticks = 1; }
                        if (f.hp == 0 || tr.pending_ticks >= 4) {
                            int dealt = tr.stable_hp - f.hp;
                            if (dealt > 0 && dealt < 80000) {
                                other_cs.dmg += dealt;
                                cs.taken += dealt;
                                ++other_cs.hits_total;
                            }
                            tr.stable_hp = f.hp;
                            tr.pending_dir = 0;
                            tr.pending_ticks = 0;
                        }
                    } else if (d >= 300 && d < 15000) {
                        if (tr.pending_dir == 1) ++tr.pending_ticks;
                        else { tr.pending_dir = 1; tr.pending_ticks = 1; }
                        if (tr.pending_ticks >= 10) {
                            cs.healed += d;
                            ++cs.heal_n;
                            tr.stable_hp = f.hp;
                            tr.pending_dir = 0;
                            tr.pending_ticks = 0;
                        }
                    } else {
                        tr.pending_dir = 0;
                        tr.pending_ticks = 0;
                    }
                }
            }
            tr.last_hp = f.hp;
            tr.last_chara = f.chara;
            tr.last_active = f.active;

            if (tr.last_blast >= 0 && f.blast < tr.last_blast) {
                int spent = (tr.last_blast - f.blast + 50000) / 100000;
                if (spent > 0 && spent <= 5) cs.blast_spent += spent;
            }
            tr.last_blast = f.blast;

            if (f.powered_up) {
                cs.sparking_s += dt;
                if (!tr.was_sparking) {
                    ++cs.sparking;
                    session_.tl_ev.push_back({rel_ms, tag, "sparking", "Max Power"});
                }
            }
            tr.was_sparking = f.powered_up;

            // Combos
            if (tr.last_hit_seq >= 0 && f.hit_seq != tr.last_hit_seq) {
                if (tr.last_combo_hits >= 2 && f.hud_hits < tr.last_combo_hits) {
                    ++cs.combo_n;
                    cs.combo_sum += tr.last_combo_hits;
                    if (tr.last_combo_hits > cs.combo_max) cs.combo_max = tr.last_combo_hits;
                    if (tr.last_combo_dmg > cs.combo_max_dmg) cs.combo_max_dmg = tr.last_combo_dmg;
                }
                tr.last_combo_hits = f.hud_hits;
                tr.last_combo_dmg = std::max(0, f.hud_damage);
            }
            tr.last_hit_seq = f.hit_seq;

            // Messages: 0=Counter, 1=MAX POWER, 3=First Attack
            if (tr.last_msg_seq >= 0 && f.message_seq != tr.last_msg_seq) {
                if (f.message == 0) {
                    ++cs.counters;
                    session_.tl_ev.push_back({rel_ms, tag, "super_counter", "Counter!"});
                } else if (f.message == 3) {
                    session_.tl_ev.push_back({rel_ms, tag, "first_attack", "First Attack!"});
                }
            }
            tr.last_msg_seq = f.message_seq;
        }

        // Active characters & HP timeline sample (every 400 ms)
        if (snap.sides[me].chara >= 0) session_.active_me_id = char_id_str(snap.sides[me].chara);
        if (snap.sides[opp].chara >= 0) session_.active_opp_id = char_id_str(snap.sides[opp].chara);

        if (t_ms - session_.last_hp_sample_ms >= 400) {
            session_.last_hp_sample_ms = t_ms;
            session_.tl_hp_me.push_back({rel_ms, std::max(0, snap.sides[me].hp), session_.active_me_id});
            session_.tl_hp_opp.push_back({rel_ms, std::max(0, snap.sides[opp].hp), session_.active_opp_id});
        }
    }

    // Network tick (called every 250 ms)
    void tick(const Config& cfg, bool has_net, const Bt4NetMeterStats& net, bool is_relay, bool is_online, int local_side, float fps, float speed_pct) {
        std::lock_guard<std::mutex> lk(mu_);
        int64_t t_ms = now_mono_ms();

        poll_bt3_log(t_ms);

        if (has_net && is_online) {
            if (!session_.active) {
                start_session(t_ms, local_side, is_relay, speed_pct);
            }
            sample_network(t_ms, net, is_relay, fps, speed_pct);
            inactive_ticks_ = 0;
        } else if (session_.active) {
            ++inactive_ticks_;
            if (inactive_ticks_ >= 8) {
                end_session(cfg, "session_ended");
            }
        }

        live_.in_session = session_.active;
        live_.online = is_online;
        live_.relay = is_relay;
        live_.ping_ms = has_net ? net.ping_ms : -1;
        live_.jitter_ms = compute_recent_jitter(t_ms);
        live_.input_delay = has_net ? net.input_delay : -1;
        live_.start_delay = session_.start_delay;
        live_.delay_auto = has_net ? (net.delay_is_auto != 0) : false;
        live_.rollbacks_per_sec = has_net ? net.rollbacks_per_sec : 0;
        live_.rollback_frames = has_net ? (net.rollback_frames_x10 / 10.0) : 0.0;
        live_.rollback_max = has_net ? net.rollback_max : 0;
        live_.waits_total = session_.stalls;
        live_.wait_ms_sec = has_net ? net.waited_ms_per_sec : 0;
        live_.wait_reason = has_net ? net.wait_reason : 0;
        live_.desync_frame = has_net ? net.desync_frame : 0;
        live_.fps = fps;
        live_.speed_pct = speed_pct > 1.0f ? speed_pct : 100.0f;
        live_.last_delay_change_ms = session_.last_delay_change_ms;
        live_.opponent = session_.opponent;
        live_.region = session_.region;
        live_.mode_name = session_.mode_name;

        if (t_ms - last_state_write_ms_ >= 1000) {
            last_state_write_ms_ = t_ms;
            write_state_json();
        }
    }

    void shutdown(const Config& cfg) {
        std::lock_guard<std::mutex> lk(mu_);
        if (session_.active && !session_.rtt_samples.empty()) {
            end_session(cfg, "game_closed");
        }
    }

    LiveSnapshot get_live_snapshot() {
        std::lock_guard<std::mutex> lk(mu_);
        return live_;
    }

    void force_rebuild_history() {
        std::lock_guard<std::mutex> lk(mu_);
        rebuild_history_js();
    }

private:
    struct Session {
        bool active = false;
        bool saw_battle = false;
        bool had_battle = false;
        std::string match_id;
        std::string start_local;
        double start_unix = 0.0;
        int64_t start_mono_ms = 0;
        int64_t last_battle_tick_ms = 0;
        int64_t last_hp_sample_ms = 0;

        int mode = 2;
        std::string mode_name = "quick";
        std::string format = "singles";
        int my_side = 0;
        bool relay = false;
        float speed_pct = 114.0f;

        std::string opponent;
        std::string opponent_id;
        std::string region;

        std::vector<int> rtt_samples;
        std::vector<int> delay_samples;
        std::vector<double> rb_rate_samples;
        std::vector<double> rb_depth_samples;
        std::vector<float> fps_samples;

        int start_delay = -1;
        int last_delay = -1;
        int delay_changes = 0;
        int64_t last_delay_change_ms = 0;

        int stalls = 0;
        double stalled_s = 0.0;
        int last_waited_times = 0;
        int desync_frame = 0;
        int rollback_max_cap = 0;

        int rank_before = -1;
        int rank_after = -1;
        int rank_delta = 0;
        bool has_rank_delta = false;
        int streak_before = 0;
        int streak_after = 0;

        std::string result = "unknown";
        std::string end_type;

        bool has_incident = false;
        std::string incident_kind;
        std::string incident_detail;

        std::vector<FighterEntry> my_team;
        std::vector<FighterEntry> opp_team;
        std::string active_me_id;
        std::string active_opp_id;
        CombatSide combat_me;
        CombatSide combat_opp;
        DirectFighterTracker direct_trk[2];

        std::vector<std::pair<int, int>> tl_rtt;
        std::vector<std::pair<int, int>> tl_delay;
        std::vector<std::pair<int, double>> tl_rb;
        std::vector<std::pair<int, int>> tl_stalls;
        std::vector<std::tuple<int, int, std::string>> tl_hp_me;
        std::vector<std::tuple<int, int, std::string>> tl_hp_opp;
        std::vector<std::tuple<int, std::string, std::string, std::string>> tl_ev;
        std::vector<float> tl_fps_1s;
        int last_fps_sec = -1;
    };

    std::mutex mu_;
    std::wstring game_dir_, rd_dir_, data_dir_, hist_dir_, tl_data_dir_, tl_hist_dir_;
    std::string self_name_;
    std::unordered_map<int, std::string> char_names_;

    Session session_;
    LiveSnapshot live_;
    std::deque<std::pair<int64_t, int>> recent_rtt_;
    int inactive_ticks_ = 0;
    int64_t last_state_write_ms_ = 0;
    int match_counter_ = 0;
    int last_known_rating_ = -1;
    int last_known_streak_ = 0;
    uint64_t bt3_log_offset_ = 0;
    bool bt3_log_inited_ = false;

    void load_self_name() {
        std::ifstream f(game_dir_ + L"\\online_name.txt");
        if (f) {
            std::string line;
            if (std::getline(f, line)) self_name_ = trim(line);
        }
        if (self_name_.empty()) {
            std::ifstream s(game_dir_ + L"\\bt3_settings.txt");
            std::string line;
            while (std::getline(s, line)) {
                if (line.rfind("net_name=", 0) == 0) {
                    std::string v = trim(line.substr(9));
                    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
                    self_name_ = v;
                    break;
                }
            }
        }
    }

    void load_character_names() {
        std::ifstream f(data_dir_ + L"\\characters.tsv");
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            int id = std::atoi(line.substr(0, t1).c_str());
            size_t t2 = line.find('\t', t1 + 1);
            std::string name = (t2 == std::string::npos) ? line.substr(t1 + 1) : line.substr(t1 + 1, t2 - t1 - 1);
            char_names_[id] = trim(name);
        }
    }

    std::string char_id_str(int chara) const {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%03d", chara);
        return buf;
    }

    std::string char_name_str(int chara) const {
        auto it = char_names_.find(chara);
        if (it != char_names_.end() && !it->second.empty()) return it->second;
        return "Fighter #" + std::to_string(chara);
    }

    void start_session(int64_t t_ms, int local_side, bool is_relay, float speed_pct) {
        session_ = Session{};
        session_.active = true;
        session_.start_mono_ms = t_ms;
        session_.start_unix = now_unix_s();
        session_.start_local = now_local_iso(false);
        char seq[16];
        std::snprintf(seq, sizeof(seq), "_%03d", ++match_counter_);
        session_.match_id = now_local_iso(true) + seq;
        session_.my_side = local_side;
        session_.relay = is_relay;
        session_.speed_pct = speed_pct > 1.0f ? speed_pct : 114.0f;
        session_.rank_before = last_known_rating_;
        session_.streak_before = last_known_streak_;
        recent_rtt_.clear();
        log_line("match start " + session_.match_id + " side=" + std::to_string(local_side) + (is_relay ? " relay" : " direct"));
    }

    void sample_network(int64_t t_ms, const Bt4NetMeterStats& net, bool is_relay, float fps, float speed_pct) {
        int rel_ms = (int)(t_ms - session_.start_mono_ms);
        if (is_relay) session_.relay = true;
        if (speed_pct > 1.0f) session_.speed_pct = speed_pct;
        if (net.rollback_max > session_.rollback_max_cap) session_.rollback_max_cap = net.rollback_max;

        if (net.ping_ms >= 0 && net.ping_ms < 5000) {
            session_.rtt_samples.push_back(net.ping_ms);
            recent_rtt_.push_back({t_ms, net.ping_ms});
            while (!recent_rtt_.empty() && t_ms - recent_rtt_.front().first > 10000) recent_rtt_.pop_front();
            if (session_.tl_rtt.empty() || rel_ms - session_.tl_rtt.back().first >= 500) {
                session_.tl_rtt.push_back({rel_ms, net.ping_ms});
            }
        }

        if (net.input_delay >= 0 && net.input_delay <= 30) {
            session_.delay_samples.push_back(net.input_delay);
            if (session_.start_delay < 0) session_.start_delay = net.input_delay;
            if (session_.last_delay >= 0 && net.input_delay != session_.last_delay) {
                ++session_.delay_changes;
                session_.last_delay_change_ms = t_ms;
                session_.tl_delay.push_back({rel_ms, net.input_delay});
            } else if (session_.tl_delay.empty()) {
                session_.tl_delay.push_back({rel_ms, net.input_delay});
            }
            session_.last_delay = net.input_delay;
        }

        if (net.rollback_max > 0) {
            session_.rb_rate_samples.push_back((double)net.rollbacks_per_sec);
            double depth = net.rollback_frames_x10 / 10.0;
            session_.rb_depth_samples.push_back(depth);
            if (net.rollbacks_per_sec > 0 && (session_.tl_rb.empty() || rel_ms - session_.tl_rb.back().first >= 500)) {
                session_.tl_rb.push_back({rel_ms, depth});
            }
        }

        if (net.waited_ms_per_sec > 0 && net.waited_times > 0) {
            if (net.waited_times > session_.last_waited_times || session_.tl_stalls.empty() || rel_ms - session_.tl_stalls.back().second >= 1000) {
                ++session_.stalls;
                session_.stalled_s += net.waited_ms_per_sec / 1000.0;
                session_.tl_stalls.push_back({rel_ms, rel_ms + net.waited_ms_per_sec});
            }
            session_.last_waited_times = net.waited_times;
        }

        if (net.desync_frame > 0 && session_.desync_frame == 0) {
            session_.desync_frame = net.desync_frame;
            session_.has_incident = true;
            session_.incident_kind = "desync";
            session_.incident_detail = "desync since frame " + std::to_string(net.desync_frame);
            session_.tl_ev.push_back({rel_ms, "me", "desync", session_.incident_detail});
        }

        if (fps > 1.0f && fps < 500.0f) {
            session_.fps_samples.push_back(fps);
            int sec = rel_ms / 1000;
            if (sec != session_.last_fps_sec) {
                session_.last_fps_sec = sec;
                session_.tl_fps_1s.push_back(fps);
            }
        }
    }

    double compute_recent_jitter(int64_t t_ms) {
        while (!recent_rtt_.empty() && t_ms - recent_rtt_.front().first > 10000) recent_rtt_.pop_front();
        if (recent_rtt_.size() < 2) return 0.0;
        double sum = 0.0;
        for (auto& p : recent_rtt_) sum += p.second;
        double mean = sum / recent_rtt_.size();
        double v = 0.0;
        for (auto& p : recent_rtt_) { double d = p.second - mean; v += d * d; }
        return std::sqrt(v / recent_rtt_.size());
    }

    void poll_bt3_log(int64_t t_ms) {
        std::wstring p = game_dir_ + L"\\bt3_log.txt";
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return; }
        uint64_t file_sz = (uint64_t)sz.QuadPart;
        if (!bt3_log_inited_) {
            bt3_log_offset_ = file_sz;
            bt3_log_inited_ = true;
            CloseHandle(h);
            return;
        }
        if (file_sz < bt3_log_offset_) bt3_log_offset_ = 0;
        if (file_sz == bt3_log_offset_) { CloseHandle(h); return; }

        uint64_t to_read = std::min<uint64_t>(file_sz - bt3_log_offset_, 131072);
        LARGE_INTEGER pos{};
        pos.QuadPart = (LONGLONG)bt3_log_offset_;
        SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
        std::string chunk((size_t)to_read, '\0');
        DWORD got = 0;
        if (ReadFile(h, chunk.data(), (DWORD)to_read, &got, nullptr) && got > 0) {
            chunk.resize(got);
            bt3_log_offset_ += got;
            std::istringstream iss(chunk);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                parse_bt3_log_line(line, t_ms);
            }
        }
        CloseHandle(h);
    }

    void parse_bt3_log_line(const std::string& line, int64_t) {
        if (!session_.active) return;
        if (line.find("connected through the relay") != std::string::npos) {
            session_.relay = true;
        } else if (line.find("The other player's game was closed") != std::string::npos) {
            session_.has_incident = true;
            session_.incident_kind = "disconnect";
            session_.incident_detail = "opponent closed the game";
            session_.end_type = "Disconnect";
        } else if (line.find("rules: team battle") != std::string::npos) {
            session_.format = "team";
        } else if (line.find("rules: single battle") != std::string::npos) {
            session_.format = "singles";
        } else if (line.find("joining the lobby") != std::string::npos || line.find("bt3: lobby:") != std::string::npos) {
            session_.mode = 4;
            session_.mode_name = "player";
        }
    }

    void end_session(const Config& cfg, const std::string& end_reason) {
        session_.active = false;
        if (session_.rtt_samples.empty()) {
            log_line("match skipped (no RTT samples): " + session_.match_id);
            return;
        }

        double end_unix = now_unix_s();
        std::string end_local = now_local_iso(false);
        double dur_s = std::max(0.1, end_unix - session_.start_unix);

        if (!session_.had_battle && dur_s < 20.0 && !session_.has_incident) {
            log_line("match skipped (aborted handshake / no battle, " + std::to_string((int)dur_s) + "s): " + session_.match_id);
            return;
        }

        auto rtts = session_.rtt_samples;
        std::sort(rtts.begin(), rtts.end());
        double rtt_sum = 0; for (int x : rtts) rtt_sum += x;
        double rtt_avg = rtt_sum / rtts.size();
        double rtt_var = 0; for (int x : rtts) { double d = x - rtt_avg; rtt_var += d * d; }
        double rtt_sd = std::sqrt(rtt_var / rtts.size());
        int rtt_min = rtts.front(), rtt_max = rtts.back();
        int rtt_med = rtts[rtts.size() / 2];
        int rtt_p95 = rtts[(size_t)(0.95 * (rtts.size() - 1))];

        double delay_avg = 0; int delay_min = 0, delay_max = 0, delay_p95 = 0;
        if (!session_.delay_samples.empty()) {
            auto ds = session_.delay_samples;
            std::sort(ds.begin(), ds.end());
            double s = 0; for (int x : ds) s += x;
            delay_avg = s / ds.size();
            delay_min = ds.front(); delay_max = ds.back();
            delay_p95 = ds[(size_t)(0.95 * (ds.size() - 1))];
        }

        double fps_avg = 0, fps_low1 = 0, fps_min = 0;
        if (!session_.fps_samples.empty()) {
            auto fs = session_.fps_samples;
            std::sort(fs.begin(), fs.end());
            double s = 0; for (float x : fs) s += x;
            fps_avg = s / fs.size();
            fps_min = fs.front();
            fps_low1 = fs[(size_t)(0.01 * (fs.size() - 1))];
        }

        std::string opp = session_.opponent.empty() ? ("unknown_" + session_.match_id) : session_.opponent;
        std::string oid = session_.opponent_id.empty() ? (session_.opponent.empty() ? "" : session_.opponent) : session_.opponent_id;

        bool wrote_tl = false;
        if (cfg.timeline) {
            wrote_tl = write_timeline_files((int)(dur_s * 1000.0));
        }

        std::ostringstream j;
        j << std::fixed << std::setprecision(1);
        j << "{\"schema_version\":1,\"writer\":\"BT4 Rankeds Details 1.2.0\""
          << ",\"match_id\":" << json_str(session_.match_id)
          << ",\"start_local\":" << json_str(session_.start_local)
          << ",\"end_local\":" << json_str(end_local)
          << ",\"start_unix\":" << std::setprecision(3) << session_.start_unix
          << ",\"end_unix\":" << end_unix
          << std::setprecision(1)
          << ",\"mode\":" << session_.mode
          << ",\"mode_name\":" << json_str(session_.mode_name)
          << ",\"format\":" << json_str(session_.format)
          << ",\"opponent\":" << json_str(opp)
          << ",\"opponent_id\":" << json_str_or_null(oid)
          << ",\"region\":" << json_str_or_null(session_.region)
          << ",\"transport\":" << json_str(session_.relay ? "relay" : "direct")
          << ",\"game_speed\":" << (int)std::lround(session_.speed_pct)
          << ",\"self_name\":" << json_str(self_name_)
          << ",\"duration_s\":" << dur_s
          << ",\"end_reason\":" << json_str(end_reason)
          << ",\"samples\":" << rtts.size()
          << ",\"rtt_ms\":{\"avg\":" << rtt_avg << ",\"min\":" << rtt_min << ",\"max\":" << rtt_max
          << ",\"sd\":" << rtt_sd << ",\"median\":" << rtt_med << ",\"p95\":" << rtt_p95 << "}"
          << ",\"stalls\":" << session_.stalls
          << ",\"stalled_s\":" << session_.stalled_s
          << ",\"result\":" << json_str(session_.result)
          << ",\"end_type\":" << json_str_or_null(session_.end_type);

        if (!session_.delay_samples.empty()) {
            j << ",\"input_delay_f\":{\"avg\":" << std::setprecision(2) << delay_avg
              << ",\"min\":" << delay_min << ",\"max\":" << delay_max << ",\"p95\":" << delay_p95
              << ",\"samples\":" << session_.delay_samples.size() << "}"
              << ",\"input_delay_ms\":" << std::setprecision(1) << (delay_avg * 16.667)
              << ",\"delay_changes\":" << session_.delay_changes;
        }
        if (session_.rollback_max_cap > 0) {
            double rb_avg = 0, rb_depth_avg = 0, rb_depth_max = 0;
            for (double x : session_.rb_rate_samples) rb_avg += x;
            if (!session_.rb_rate_samples.empty()) rb_avg /= session_.rb_rate_samples.size();
            for (double x : session_.rb_depth_samples) { rb_depth_avg += x; if (x > rb_depth_max) rb_depth_max = x; }
            if (!session_.rb_depth_samples.empty()) rb_depth_avg /= session_.rb_depth_samples.size();
            j << ",\"rollback\":{\"cap\":" << session_.rollback_max_cap
              << ",\"rate_avg\":" << std::setprecision(2) << rb_avg
              << ",\"frames_avg\":" << rb_depth_avg << ",\"frames_max\":" << rb_depth_max << "}" << std::setprecision(1);
        }
        if (!session_.fps_samples.empty()) {
            j << ",\"fps\":{\"avg\":" << fps_avg << ",\"low1\":" << fps_low1 << ",\"min\":" << fps_min
              << ",\"frames\":" << session_.fps_samples.size() << ",\"target\":60}";
        }
        if (session_.has_rank_delta || session_.rank_after >= 0) {
            int bef = session_.rank_before >= 0 ? session_.rank_before : (session_.rank_after - session_.rank_delta);
            int aft = session_.rank_after >= 0 ? session_.rank_after : (bef + session_.rank_delta);
            j << ",\"rank\":{\"format\":" << json_str(session_.format)
              << ",\"before\":{\"rank\":0,\"points\":" << bef << ",\"name\":" << json_str(std::to_string(bef) + " pts") << ",\"streak\":" << session_.streak_before << "}"
              << ",\"after\":{\"rank\":0,\"points\":" << aft << ",\"name\":" << json_str(std::to_string(aft) + " pts") << ",\"streak\":" << session_.streak_after << "}"
              << ",\"delta\":" << session_.rank_delta << ",\"source\":\"bt4_ranked_api\",\"confirmed\":true}";
        }
        if (!session_.my_team.empty() || !session_.opp_team.empty()) {
            auto emit_team = [&](const std::vector<FighterEntry>& t) {
                j << "[";
                for (size_t i = 0; i < t.size(); ++i) {
                    if (i) j << ",";
                    j << "{\"id\":" << json_str(t[i].id) << ",\"name\":" << json_str(t[i].name) << ",\"short\":" << json_str(t[i].name) << "}";
                }
                j << "]";
            };
            j << ",\"my_team\":"; emit_team(session_.my_team);
            j << ",\"opp_team\":"; emit_team(session_.opp_team);
            if (!session_.active_me_id.empty()) {
                j << ",\"active_end\":{\"me\":" << json_str(session_.active_me_id) << ",\"opp\":" << json_str(session_.active_opp_id) << "}";
            }
        }
        if (session_.combat_me.dmg > 0 || session_.combat_opp.dmg > 0 || session_.combat_me.combo_n > 0 || session_.combat_opp.combo_n > 0) {
            auto emit_cs = [&](const CombatSide& c) {
                double cavg = c.combo_n ? ((double)c.combo_sum / c.combo_n) : 0.0;
                j << "{\"dmg\":" << c.dmg << ",\"taken\":" << c.taken
                  << ",\"healed\":" << c.healed << ",\"heal_n\":" << c.heal_n
                  << ",\"hits\":{\"melee\":" << c.hits_total << "}"
                  << ",\"combo\":{\"n\":" << c.combo_n << ",\"max\":" << c.combo_max << ",\"avg\":" << cavg << ",\"max_dmg\":" << c.combo_max_dmg << "}"
                  << ",\"sparking\":" << c.sparking << ",\"sparking_s\":" << c.sparking_s
                  << ",\"switches\":" << c.switches << ",\"super_counter\":" << c.counters
                  << ",\"skill_spent\":" << c.blast_spent << ",\"charge_s\":" << c.charge_s << "}";
            };
            j << ",\"combat\":{\"v\":2,\"me\":"; emit_cs(session_.combat_me);
            j << ",\"opp\":"; emit_cs(session_.combat_opp);
            j << "}";
        }
        if (session_.has_incident) {
            j << ",\"incident\":{\"kind\":" << json_str(session_.incident_kind) << ",\"detail\":" << json_str(session_.incident_detail) << "}";
        }
        if (wrote_tl) {
            j << ",\"timeline\":" << json_str("timelines/" + session_.match_id + ".json");
        }
        j << "}";

        append_file_line(data_dir_ + L"\\matches.jsonl", j.str());

        std::ostringstream lg;
        lg << std::fixed << std::setprecision(1)
           << "NATIVE_END\t" << end_local
           << "\tmatch_id=" << session_.match_id
           << "\tmode=" << session_.mode
           << "\topponent=" << opp
           << "\tavg_ms=" << rtt_avg << "\tmin_ms=" << rtt_min << "\tmax_ms=" << rtt_max << "\tsd_ms=" << rtt_sd
           << "\tsamples=" << rtts.size() << "\tstalls=" << session_.stalls << "\tstalled_s=" << session_.stalled_s
           << "\tduration_s=" << (int)std::lround(dur_s) << "\tresult=" << session_.result << "\treason=" << end_reason;
        append_file_line(data_dir_ + L"\\matches.log", lg.str());

        if (cfg.debug_incidents && session_.has_incident) {
            std::ostringstream inc;
            inc << "{\"v\":1,\"writer\":\"BT4 Rankeds Details 1.2.0\",\"time_local\":" << json_str(end_local)
                << ",\"match_id\":" << json_str(session_.match_id)
                << ",\"kind\":" << json_str(session_.incident_kind)
                << ",\"code_name\":" << json_str(session_.incident_detail)
                << ",\"mode\":" << session_.mode << ",\"format\":" << json_str(session_.format)
                << ",\"opponent\":" << json_str(opp) << ",\"opponent_id\":" << json_str_or_null(oid)
                << ",\"rtt_ms\":" << rtt_avg << ",\"stalls\":" << session_.stalls << "}";
            append_file_line(data_dir_ + L"\\incidents.jsonl", inc.str());
        }

        rebuild_history_js();
        log_line("match saved " + session_.match_id + " opp=" + opp + " samples=" + std::to_string(rtts.size()) +
                 " avg_ms=" + std::to_string((int)rtt_avg) + " dmg=" + std::to_string(session_.combat_me.dmg) + "/" + std::to_string(session_.combat_opp.dmg));
    }

    std::string serialize_timeline(int dur_ms, size_t hp_stride, size_t net_stride, size_t max_ev, bool truncated) const {
        std::ostringstream t;
        t << std::fixed << std::setprecision(1);
        t << "{\"v\":1,\"match_id\":" << json_str(session_.match_id)
          << ",\"dur_ms\":" << dur_ms << ",\"me_side\":" << session_.my_side;
        if (truncated) t << ",\"truncated\":true";

        t << ",\"rtt\":[";
        for (size_t i = 0, out_n = 0; i < session_.tl_rtt.size(); i += net_stride, ++out_n) {
            if (out_n) t << ",";
            t << "[" << session_.tl_rtt[i].first << "," << session_.tl_rtt[i].second << "]";
        }
        t << "],\"delay\":[";
        for (size_t i = 0; i < session_.tl_delay.size(); ++i) {
            if (i) t << ",";
            t << "[" << session_.tl_delay[i].first << "," << session_.tl_delay[i].second << "]";
        }
        t << "],\"opp_delay\":[";
        for (size_t i = 0, out_n = 0; i < session_.tl_rb.size(); i += net_stride, ++out_n) {
            if (out_n) t << ",";
            t << "[" << session_.tl_rb[i].first << "," << session_.tl_rb[i].second << "]";
        }
        t << "],\"stalls\":[";
        for (size_t i = 0; i < session_.tl_stalls.size(); ++i) {
            if (i) t << ",";
            t << "[" << session_.tl_stalls[i].first << "," << session_.tl_stalls[i].second << "]";
        }
        t << "]";
        if (!session_.tl_hp_me.empty() || !session_.tl_hp_opp.empty()) {
            auto emit_hp = [&](const auto& v) {
                t << "[";
                for (size_t i = 0, out_n = 0; i < v.size(); i += hp_stride, ++out_n) {
                    if (out_n) t << ",";
                    t << "[" << std::get<0>(v[i]) << "," << std::get<1>(v[i]) << "," << json_str(std::get<2>(v[i])) << "]";
                }
                if (v.size() > 1 && ((v.size() - 1) % hp_stride) != 0) {
                    const auto& last = v.back();
                    t << ",[" << std::get<0>(last) << "," << std::get<1>(last) << "," << json_str(std::get<2>(last)) << "]";
                }
                t << "]";
            };
            t << ",\"hp\":{\"me\":"; emit_hp(session_.tl_hp_me);
            t << ",\"opp\":"; emit_hp(session_.tl_hp_opp);
            t << "}";
        }
        if (!session_.tl_ev.empty() && max_ev > 0) {
            size_t lim = std::min(session_.tl_ev.size(), max_ev);
            t << ",\"ev\":[";
            for (size_t i = 0; i < lim; ++i) {
                if (i) t << ",";
                t << "[" << std::get<0>(session_.tl_ev[i]) << "," << json_str(std::get<1>(session_.tl_ev[i]))
                  << "," << json_str(std::get<2>(session_.tl_ev[i])) << "," << json_str(std::get<3>(session_.tl_ev[i])) << "]";
            }
            t << "]";
        }
        if (!session_.tl_fps_1s.empty()) {
            size_t fps_stride = std::max<size_t>(1, net_stride);
            t << ",\"fps\":{\"ms\":" << (1000 * fps_stride) << ",\"t0\":0,\"target\":60,\"min\":[";
            for (size_t i = 0, out_n = 0; i < session_.tl_fps_1s.size(); i += fps_stride, ++out_n) {
                if (out_n) t << ",";
                t << session_.tl_fps_1s[i];
            }
            t << "],\"avg_ms\":" << (1000 * fps_stride) << ",\"avg\":[";
            for (size_t i = 0, out_n = 0; i < session_.tl_fps_1s.size(); i += fps_stride, ++out_n) {
                if (out_n) t << ",";
                t << session_.tl_fps_1s[i];
            }
            t << "]}";
        }
        t << "}";
        return t.str();
    }

    bool write_timeline_files(int dur_ms) {
        size_t hp_stride = 1, net_stride = 1, max_ev = session_.tl_ev.size();
        std::string raw = serialize_timeline(dur_ms, hp_stride, net_stride, max_ev, false);
        for (int pass = 0; pass < 8 && raw.size() > 65536; ++pass) {
            hp_stride *= 2;
            net_stride *= 2;
            max_ev = (max_ev > 16) ? (max_ev / 2) : 0;
            raw = serialize_timeline(dur_ms, hp_stride, net_stride, max_ev, true);
        }

        std::wstring wid(session_.match_id.begin(), session_.match_id.end());
        write_file_atomic(tl_data_dir_ + L"\\" + wid + L".json", raw);
        std::string js = "(window.SZ_TIMELINES = window.SZ_TIMELINES || {})[" + json_str(session_.match_id) + "] = " + raw + ";\n";
        return write_file_atomic(tl_hist_dir_ + L"\\" + wid + L".js", js);
    }

    void write_state_json() {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1)
          << "{\"writer\":\"BT4 Rankeds Details 1.2.0\""
          << ",\"time\":" << json_str(now_local_iso())
          << ",\"in_session\":" << (live_.in_session ? "true" : "false")
          << ",\"online\":" << (live_.online ? "true" : "false")
          << ",\"transport\":" << json_str(live_.relay ? "relay" : "direct")
          << ",\"rtt_ms\":" << live_.ping_ms
          << ",\"jitter_ms\":" << live_.jitter_ms
          << ",\"input_delay\":" << live_.input_delay
          << ",\"delay_auto\":" << (live_.delay_auto ? "true" : "false")
          << ",\"rollbacks_per_sec\":" << live_.rollbacks_per_sec
          << ",\"rollback_frames\":" << live_.rollback_frames
          << ",\"rollback_max\":" << live_.rollback_max
          << ",\"waits\":" << live_.waits_total
          << ",\"desync_frame\":" << live_.desync_frame
          << ",\"fps\":" << live_.fps
          << ",\"speed_pct\":" << live_.speed_pct
          << ",\"self_name\":" << json_str(self_name_)
          << ",\"opponent\":" << json_str_or_null(live_.opponent)
          << ",\"region\":" << json_str_or_null(live_.region)
          << "}";
        write_file_atomic(data_dir_ + L"\\state.json", s.str());
    }

    void rebuild_history_js() {
        std::vector<std::string> matches;
        std::unordered_set<std::string> seen_ids;
        {
            std::ifstream f(data_dir_ + L"\\matches.jsonl");
            std::string line;
            while (std::getline(f, line)) {
                line = trim(line);
                if (line.size() < 2 || line.front() != '{' || line.back() != '}') continue;
                std::string ts, mid;
                json_extract_string(line, "end_local", ts);
                json_extract_string(line, "match_id", mid);
                if (!mid.empty() && !seen_ids.insert(mid).second) continue;
                std::string patched = line.substr(0, line.size() - 1);
                if (!ts.empty() && line.find("\"ts\":") == std::string::npos) {
                    patched += ",\"ts\":" + json_str(ts);
                }
                size_t rp = line.find("\"rtt_ms\":{");
                if (rp != std::string::npos && line.find("\"avg_ms\":") == std::string::npos) {
                    size_t re = line.find('}', rp);
                    if (re != std::string::npos) {
                        std::string sub = line.substr(rp, re - rp + 1);
                        auto grab_num = [&](const char* k, const char* dst) {
                            std::string pat = std::string("\"") + k + "\":";
                            size_t p = sub.find(pat);
                            if (p == std::string::npos) return;
                            p += pat.size();
                            size_t e = p;
                            while (e < sub.size() && (std::isdigit((unsigned char)sub[e]) || sub[e] == '.' || sub[e] == '-')) ++e;
                            if (e > p) patched += std::string(",\"") + dst + "\":" + sub.substr(p, e - p);
                        };
                        grab_num("avg", "avg_ms");
                        grab_num("min", "min_ms");
                        grab_num("max", "max_ms");
                        grab_num("sd",  "sd_ms");
                    }
                }
                if (line.find("\"timeline\":") != std::string::npos && line.find("\"timeline_file\":") == std::string::npos && !mid.empty()) {
                    patched += ",\"timeline_file\":" + json_str("timelines/" + mid + ".js");
                }
                patched += "}";
                matches.push_back(std::move(patched));
            }
        }

        std::vector<std::string> incidents;
        {
            std::ifstream f(data_dir_ + L"\\incidents.jsonl");
            std::string line;
            while (std::getline(f, line)) {
                line = trim(line);
                if (line.size() >= 2 && line.front() == '{') incidents.push_back(line);
            }
        }

        std::string gen = now_local_iso();
        std::ostringstream out;
        out << "// Generated by BT4 Rankeds Details (in-game, after each online match) - do not edit.\n"
            << "window.SZ_GENERATED = " << json_str(gen) << ";\n"
            << "window.SZ_META = {\n"
            << "  \"generated\": " << json_str(gen) << ",\n"
            << "  \"writer\": \"BT4 Rankeds Details 1.2.0\",\n"
            << "  \"source\": \"jsonl\",\n"
            << "  \"log_missing\": false,\n"
            << "  \"csv_present\": false,\n"
            << "  \"filtered_no_samples\": 0,\n"
            << "  \"malformed\": 0,\n"
            << "  \"other_lines\": 0,\n"
            << "  \"ignored_keys\": [],\n"
            << "  \"crosscheck\": []\n"
            << "};\n"
            << "window.SZ_INCIDENTS = [\n";
        for (size_t i = 0; i < incidents.size(); ++i) {
            if (i) out << ",\n";
            out << incidents[i];
        }
        out << "\n];\nwindow.SZ_MATCHES = [\n";
        for (size_t i = 0; i < matches.size(); ++i) {
            if (i) out << ",\n";
            out << matches[i];
        }
        out << "\n];\n";

        write_file_atomic(hist_dir_ + L"\\data.js", out.str());
    }
};

} // namespace bt4rd
