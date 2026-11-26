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

int main() {
    std::wcout << L"[TEST] BT4RankedDetails Telemetry & Persistence Suite\n";

    // Create temp test directory
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"bt4rd_unit_test";
    CreateDirectoryW(dir.c_str(), nullptr);

    // 1. Write online_name.txt
    bt4rd::write_file_atomic(dir + L"\\online_name.txt", "Pan de Ajo\n");

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

    // 4. Simulate 12 ticks of an online ranked match with rollback, waits, and rival metadata
    bt4rd::Bt4NetMeterStats net{};
    net.rollback_max = 4;
    net.input_delay = 2;
    net.delay_is_auto = 1;

    for (int i = 0; i < 12; ++i) {
        net.ping_ms = 42 + (i % 4) * 5;
        net.rollbacks_per_sec = (i % 3 == 0) ? 2 : 0;
        net.rollback_frames_x10 = (i % 3 == 0) ? 18 : 0;
        net.waited_times = (i == 6) ? 1 : 0;
        net.waited_ms_per_sec = (i == 6) ? 115 : 0;
        if (i == 7) net.input_delay = 3;
        eng.tick(cfg, true, net, false, true, 0, 59.9f, 114.0f);
    }

    // 5. Simulate ranked_report HTTP response
    bt4rd::HttpObservedEvent ev_post;
    ev_post.path = "/rest/v1/rpc/bt4_ranked_api";
    ev_post.request_body = "{\"action\":\"ranked_report\",\"ruleset\":\"solo\"}";
    ev_post.response_body = "{\"status\":\"applied\",\"delta\":17,\"rating\":1059,\"streak_wins\":3,\"opponent_name\":\"VegetaBR\",\"opponent_id\":\"acct_9981\",\"region\":\"SA\"}";
    eng.on_http_event(ev_post);

    // 6. End session (8 inactive ticks)
    for (int i = 0; i < 9; ++i) {
        eng.tick(cfg, false, net, false, false, 0, 60.0f, 100.0f);
    }

    // 7. Verify all generated outputs
    std::string state = read_all(dir + L"\\ranked_details\\data\\state.json");
    std::string jsonl = read_all(dir + L"\\ranked_details\\data\\matches.jsonl");
    std::string mlog  = read_all(dir + L"\\ranked_details\\data\\matches.log");
    std::string djs   = read_all(dir + L"\\ranked_details\\history\\data.js");

    assert(state.find("\"writer\":\"BT4RankedDetails 1.0.0\"") != std::string::npos);
    assert(jsonl.find("\"opponent\":\"VegetaBR\"") != std::string::npos);
    assert(jsonl.find("\"opponent_id\":\"acct_9981\"") != std::string::npos);
    assert(jsonl.find("\"region\":\"SA\"") != std::string::npos);
    assert(jsonl.find("\"transport\":\"direct\"") != std::string::npos);
    assert(jsonl.find("\"delta\":17") != std::string::npos);
    assert(jsonl.find("\"rollback\":{") != std::string::npos);
    assert(mlog.find("NATIVE_END\t") != std::string::npos);
    assert(djs.find("window.SZ_MATCHES = [") != std::string::npos);
    assert(djs.find("\"avg_ms\":") != std::string::npos);
    assert(djs.find("\"timeline_file\":\"timelines/") != std::string::npos);

    std::wcout << L"[PASS] All assertions passed! Verified state.json, matches.jsonl, matches.log, data.js and timelines.\n";
    return 0;
}
