#pragma once
#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace bt4rd {

struct Config {
    // [Display]
    bool display = true;
    bool show_always = false;
    int offset_x = 24;
    int offset_y = 24;
    int font_main = 34;
    int font_small = 14;
    bool panel = true;
    int panel_opacity = 78;     // 0..100
    int panel_radius = 10;
    int panel_min_width = 210;
    bool show_delay = true;
    bool show_rollback = true;
    bool swing = true;

    // [Records]
    std::string history_js = "history\\data.js";
    bool rank_lookup = true;
    bool combat_tracking = true;
    bool timeline = true;
    bool fps_tracking = true;
    bool offline_log = false;

    // [Advanced]
    bool hook_winhttp = true;
    bool aob_scan = true;

    // [Debug]
    bool debug_incidents = true;
    bool verbose_log = false;

    FILETIME last_write{};
};

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string lower_ascii(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline void write_default_ini(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    f << "; BT4 Ranked Details configuration (auto-reloaded every ~1 s while the game runs)\n\n"
         "[Display]\n"
         "display = 1\n"
         "show_always = 0\n"
         "offset_x = 24\n"
         "offset_y = 24\n"
         "font_main = 34\n"
         "font_small = 14\n"
         "panel = 1\n"
         "panel_opacity = 78\n"
         "panel_radius = 10\n"
         "panel_min_width = 210\n"
         "show_delay = 1\n"
         "show_rollback = 1\n"
         "swing = 1\n\n"
         "[Records]\n"
         "history_js = history\\data.js\n"
         "rank_lookup = 1\n"
         "combat_tracking = 1\n"
         "timeline = 1\n"
         "fps_tracking = 1\n"
         "offline_log = 0\n\n"
         "[Advanced]\n"
         "hook_winhttp = 1\n"
         "aob_scan = 1\n\n"
         "[Debug]\n"
         "debug_incidents = 1\n"
         "verbose_log = 0\n";
}

inline bool load_config_if_changed(const std::wstring& path, Config& cfg, bool force = false) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr)) return false;
    if (!force && attr.ftLastWriteTime.dwLowDateTime == cfg.last_write.dwLowDateTime &&
        attr.ftLastWriteTime.dwHighDateTime == cfg.last_write.dwHighDateTime) {
        return false;
    }
    cfg.last_write = attr.ftLastWriteTime;

    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        size_t semi = line.find(';');
        if (semi != std::string::npos) line.resize(semi);
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty() || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = lower_ascii(trim(line.substr(0, eq)));
        std::string v = trim(line.substr(eq + 1));
        auto as_bool = [&]() { return v == "1" || lower_ascii(v) == "true"; };
        auto as_int = [&](int def) { try { return std::stoi(v); } catch (...) { return def; } };

        if (k == "display") cfg.display = as_bool();
        else if (k == "show_always") cfg.show_always = as_bool();
        else if (k == "offset_x") cfg.offset_x = as_int(cfg.offset_x);
        else if (k == "offset_y") cfg.offset_y = as_int(cfg.offset_y);
        else if (k == "font_main") cfg.font_main = std::clamp(as_int(cfg.font_main), 14, 72);
        else if (k == "font_small") cfg.font_small = std::clamp(as_int(cfg.font_small), 10, 32);
        else if (k == "panel") cfg.panel = as_bool();
        else if (k == "panel_opacity") cfg.panel_opacity = std::clamp(as_int(cfg.panel_opacity), 10, 100);
        else if (k == "panel_radius") cfg.panel_radius = std::clamp(as_int(cfg.panel_radius), 0, 24);
        else if (k == "panel_min_width") cfg.panel_min_width = std::clamp(as_int(cfg.panel_min_width), 140, 480);
        else if (k == "show_delay") cfg.show_delay = as_bool();
        else if (k == "show_rollback") cfg.show_rollback = as_bool();
        else if (k == "swing") cfg.swing = as_bool();
        else if (k == "history_js") cfg.history_js = v;
        else if (k == "rank_lookup") cfg.rank_lookup = as_bool();
        else if (k == "combat_tracking") cfg.combat_tracking = as_bool();
        else if (k == "timeline") cfg.timeline = as_bool();
        else if (k == "fps_tracking") cfg.fps_tracking = as_bool();
        else if (k == "offline_log") cfg.offline_log = as_bool();
        else if (k == "hook_winhttp") cfg.hook_winhttp = as_bool();
        else if (k == "aob_scan") cfg.aob_scan = as_bool();
        else if (k == "debug_incidents") cfg.debug_incidents = as_bool();
        else if (k == "verbose_log") cfg.verbose_log = as_bool();
    }
    return true;
}

} // namespace bt4rd
