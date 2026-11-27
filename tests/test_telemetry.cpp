#include "../src/config.hpp"
#include "../src/aob_scanner.hpp"
#include "../src/telemetry.hpp"
#include <iostream>
#include <cassert>
#include <fstream>
#include <sstream>

static std::string read_all(const std::wstring& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static size_t count_lines(const std::string& s) {
    size_t n = 0;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

int main() {
    std::wcout << L"[TEST] BT4 Rankeds Details Telemetry & Persistence Suite\n";

    // Create isolated temp test directory (clean prior run files)
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"bt4rd_unit_test_v120";
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW((dir + L"\\ranked_details").c_str(), nullptr);
    CreateDirectoryW((dir + L"\\ranked_details\\data").c_str(), nullptr);
    DeleteFileW((dir + L"\\ranked_details\\data\\matches.jsonl").c_str());
    DeleteFileW((dir + L"\\ranked_details\\data\\matches.log").c_str());
    DeleteFileW((dir + L"\\ranked_details\\data\\incidents.jsonl").c_str());
    DeleteFileW((dir + L"\\ranked_details\\BT4RankedDetails.ini").c_str());

    // 1. Write online_name.txt and sample characters.tsv
    bt4rd::write_file_atomic(dir + L"\\online_name.txt", "Pan de Ajo\n");
    bt4rd::write_file_atomic(dir + L"\\ranked_details\\data\\characters.tsv", "0\tGoku (Early)\n3\tGoku (End)\n15\tVegeta (Scouter)\n");

    // 2. Init TelemetryEngine and default INI
    bt4rd::TelemetryEngine eng;
    eng.init(dir);
    bt4rd::Config cfg;
    bt4rd::write_default_ini(eng.ini_path());
    assert(bt4rd::load_config_if_changed(eng.ini_path(), cfg, true));
    assert(cfg.display == true);
    assert(cfg.timeline == true);

    // 3. Simulate pre-match ranked_status HTTP response
    bt4rd::HttpObservedEvent ev_pre;
    ev_pre.path = "/rest/v1/rpc/bt4_ranked_api";
    ev_pre.request_body = "{\"action\":\"ranked_status\"}";
    ev_pre.response_body = "{\"rating\":1042,\"wins\":5,\"losses\":2,\"streak_wins\":2}";
    eng.on_http_event(ev_pre);

    // 4. Simulate 12 ticks of an online ranked match + direct C++ fighter/name tracking
    bt4rd::Bt4NetMeterStats net{};
    net.rollback_max = 4;
    net.input_delay = 2;
    net.delay_is_auto = 1;

    bt4rd::Bt4BattleSnap bsnap{};
    bsnap.in_battle = true;
    bsnap.hud_shown = true;
    bsnap.player_names[0] = "Pan de Ajo";
    bsnap.player_names[1] = "pan de ajo"; // must be rejected (same as self_name, case-insensitive)
    bsnap.sides[0].count = 1; bsnap.sides[0].chara = 3; bsnap.sides[0].member_charas[0] = 3; bsnap.sides[0].hp = 40000; bsnap.sides[0].hp_max = 40000;
    bsnap.sides[1].count = 1; bsnap.sides[1].chara = 15; bsnap.sides[1].member_charas[0] = 15; bsnap.sides[1].hp = 40000; bsnap.sides[1].hp_max = 40000;

    for (int i = 0; i < 12; ++i) {
        net.ping_ms = 42 + (i % 4) * 5;
        net.rollbacks_per_sec = (i % 3 == 0) ? 2 : 0;
        net.rollback_frames_x10 = (i % 3 == 0) ? 18 : 0;
        net.waited_times = (i == 6) ? 1 : 0;
        net.waited_ms_per_sec = (i == 6) ? 115 : 0;
        if (i == 7) net.input_delay = 3;
        eng.tick(cfg, true, net, false, true, 0, 59.9f, 114.0f);

        if (i == 1) bsnap.player_names[1] = "VegetaBR";
        bsnap.sides[1].hp = std::max(0, 40000 - i * 3800);
        bsnap.sides[0].hp = std::max(5000, 40000 - i * 1500);
        if (i == 4) { bsnap.sides[0].powered_up = true; }
        if (i == 6) { bsnap.sides[0].hit_seq = 1; bsnap.sides[0].hud_hits = 8; bsnap.sides[0].hud_damage = 9200; }
        if (i == 7) { bsnap.sides[0].hit_seq = 2; bsnap.sides[0].hud_hits = 0; bsnap.sides[0].hud_damage = 0; }
        eng.tick_battle_direct(bsnap, 0);
    }
    bsnap.in_battle = false;
    eng.tick_battle_direct(bsnap, 0);

    // 5. Simulate ranked_report HTTP response
    bt4rd::HttpObservedEvent ev_post;
    ev_post.path = "/rest/v1/rpc/bt4_ranked_api";
    ev_post.request_body = "{\"action\":\"ranked_report\",\"ruleset\":\"solo\"}";
    ev_post.response_body = "{\"status\":\"applied\",\"delta\":17,\"rating\":1059,\"streak_wins\":3,\"region\":\"SA\"}";
    eng.on_http_event(ev_post);

    // 6. End session (8 inactive ticks)
    for (int i = 0; i < 9; ++i) {
        eng.tick(cfg, false, net, false, false, 0, 60.0f, 100.0f);
    }

    // 7. Regression test: ghost / aborted matchmaking session (<20s, no battle) must be skipped
    for (int i = 0; i < 4; ++i) {
        net.ping_ms = 55;
        eng.tick(cfg, true, net, false, true, 0, 60.0f, 100.0f);
    }
    for (int i = 0; i < 9; ++i) {
        eng.tick(cfg, false, net, false, false, 0, 60.0f, 100.0f);
    }

    // 8. Regression test: rollback HP oscillation must NOT inflate damage or fabricate heals
    bt4rd::Bt4BattleSnap rb{};
    rb.in_battle = true;
    rb.hud_shown = true;
    rb.player_names[0] = "Pan de Ajo";
    rb.player_names[1] = "RollbackRival";
    rb.sides[0].count = 1; rb.sides[0].chara = 3; rb.sides[0].member_charas[0] = 3; rb.sides[0].hp = 40000; rb.sides[0].hp_max = 40000;
    rb.sides[1].count = 1; rb.sides[1].chara = 15; rb.sides[1].member_charas[0] = 15; rb.sides[1].hp = 40000; rb.sides[1].hp_max = 40000;
    eng.tick(cfg, true, net, false, true, 0, 60.0f, 114.0f);
    for (int i = 0; i < 20; ++i) {
        // Alternate 40000 <-> 38000 every tick (simulated rollback jitter)
        rb.sides[0].hp = (i % 2 == 0) ? 40000 : 38000;
        rb.sides[1].hp = (i % 2 == 0) ? 40000 : 37500;
        eng.tick_battle_direct(rb, 0);
    }
    // End cleanly at 40000
    rb.sides[0].hp = 40000;
    rb.sides[1].hp = 40000;
    eng.tick_battle_direct(rb, 0);
    rb.in_battle = false;
    eng.tick_battle_direct(rb, 0);
    for (int i = 0; i < 9; ++i) {
        eng.tick(cfg, false, net, false, false, 0, 60.0f, 100.0f);
    }

    // 9. Verify all generated outputs
    std::string state = read_all(dir + L"\\ranked_details\\data\\state.json");
    std::string jsonl = read_all(dir + L"\\ranked_details\\data\\matches.jsonl");
    std::string mlog  = read_all(dir + L"\\ranked_details\\data\\matches.log");
    std::string djs   = read_all(dir + L"\\ranked_details\\history\\data.js");

    assert(state.find("\"writer\":\"BT4 Rankeds Details 1.2.0\"") != std::string::npos);
    assert(count_lines(jsonl) == 2); // Match #1 + RollbackRival; ghost session skipped!
    assert(jsonl.find("\"opponent\":\"VegetaBR\"") != std::string::npos);
    assert(jsonl.find("\"opponent\":\"Pan de Ajo\"") == std::string::npos);
    assert(jsonl.find("\"opponent\":\"pan de ajo\"") == std::string::npos);
    assert(jsonl.find("\"Goku (End)\"") != std::string::npos);
    assert(jsonl.find("\"Vegeta (Scouter)\"") != std::string::npos);
    assert(jsonl.find("\"dp\":") == std::string::npos);
    assert(jsonl.find("\"combat\":{\"v\":2") != std::string::npos);
    assert(jsonl.find("\"dmg\":40000") != std::string::npos);
    assert(jsonl.find("\"healed\":0") != std::string::npos);
    assert(jsonl.find("\"result\":\"win\"") != std::string::npos);
    assert(jsonl.find("\"region\":\"SA\"") != std::string::npos);
    assert(jsonl.find("\"delta\":17") != std::string::npos);
    assert(jsonl.find("\"rollback\":{") != std::string::npos);
    assert(mlog.find("NATIVE_END\t") != std::string::npos);
    assert(djs.find("window.SZ_MATCHES = [") != std::string::npos);
    assert(djs.find("\"avg_ms\":") != std::string::npos);
    assert(djs.find("\"timeline_file\":\"timelines/") != std::string::npos);

    // Check RollbackRival line had no combat damage/healing fabricated
    size_t pos_rb = jsonl.find("\"opponent\":\"RollbackRival\"");
    assert(pos_rb != std::string::npos);
    std::string rb_line = jsonl.substr(pos_rb);
    assert(rb_line.find("\"combat\":") == std::string::npos); // 0 dmg, 0 combos -> no combat block

    std::wcout << L"[PASS] All assertions passed! Verified rollback debounce, ghost skip, self-name guard, and outputs.\n";
    return 0;
}
