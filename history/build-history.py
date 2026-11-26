#!/usr/bin/env python3
"""Build data.js for the BT4 Rankeds Details match-history viewer (manual rebuild).

The mod rewrites data.js itself after every real online match and once at game start, so this script is optional.
It lives in  ranked_details\history\  and reads the mod's records from  ranked_details\data\ :
  1. matches.jsonl  (one raw record per match)
  2. matches.log    (NATIVE_END lines), used only when there is no matches.jsonl
The ranked CSV is a cross-check only. Non-fights (samples=0) are filtered out. Writes data.js next to index.html.
It only reads the data folder; it never touches the game.
"""
import csv
import json
import os
import sys
from datetime import datetime

# ---- configuration (all paths relative to this file: ranked_details\history\) -----------------
HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "data")
JSONL_CANDIDATES = [os.path.join(DATA, "matches.jsonl")]
MATCH_LOG = os.path.join(DATA, "matches.log")
LEGACY_LOG = MATCH_LOG
RANKED_CSV = os.path.join(DATA, "ranked.csv")
OUT_JS = os.path.join(HERE, "data.js")
# ---------------------------------------------------------------------------------------------

FLOAT_KEYS = ("avg_ms", "min_ms", "max_ms", "sd_ms", "stalled_s",
              "measured_delay_frames", "input_delay_frames")
INT_KEYS = ("mode", "samples", "stalls", "duration_s")
KNOWN_KEYS = set(FLOAT_KEYS) | set(INT_KEYS) | {"match_id", "opponent", "reason", "opponent_id", "opponent_source",
                                                  "result", "end_type", "format", "max_dp", "my_team", "opp_team", "dp",
                                                  "active_end", "combat", "timeline_file", "input_delay_min", "input_delay_max",
                                                  "input_delay_p95", "delay_changes", "rank", "result_inferred", "dc_self"}
RESULTS = ("win", "loss", "draw", "unknown")


def result_of(v):
    return v if v in RESULTS else None


def num(v, kind):
    if v is None or v == "" or v.lower() in ("null", "nan", "none"):
        return None
    return int(float(v)) if kind is int else float(v)


def parse_log(path, stats):
    matches = []
    if not os.path.isfile(path):
        stats["log_missing"] = True
        return matches
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\r\n")
            if not line.strip():
                continue
            parts = line.split("\t")
            tag = parts[0].strip()
            if tag != "NATIVE_END":
                stats["other_lines"] += 1
                continue
            try:
                ts = datetime.strptime(parts[1].strip(), "%Y-%m-%d %H:%M:%S")
                kv = {}
                for p in parts[2:]:
                    if "=" not in p:
                        continue
                    k, v = p.split("=", 1)
                    kv[k.strip()] = v.strip()
                rec = {"ts": ts.strftime("%Y-%m-%d %H:%M:%S"),
                       "match_id": kv.get("match_id", ""),
                       "opponent": kv.get("opponent", "") or "unknown",
                       "opponent_id": kv.get("opponent_id") or None,
                       "reason": kv.get("reason", ""),
                       "result": result_of(kv.get("result")),
                       "end_type": kv.get("end_type") or None}
                for k in FLOAT_KEYS:
                    if k in kv:
                        rec[k] = num(kv[k], float)
                    elif k not in ("measured_delay_frames", "input_delay_frames"):
                        rec[k] = None
                for k in INT_KEYS:
                    rec[k] = num(kv.get(k), int)
                if "samples" not in kv:
                    raise ValueError("no samples field")
            except (ValueError, IndexError) as e:
                stats["malformed"] += 1
                stats["malformed_lines"].append(lineno)
                continue
            # input delay: prefer measured over configured, keep both if present
            idf = rec.get("measured_delay_frames")
            if idf is None:
                idf = rec.get("input_delay_frames")
            rec["input_delay_f"] = idf
            rec.pop("measured_delay_frames", None)
            rec.pop("input_delay_frames", None)
            stats["ignored_keys"].update(k for k in kv if k not in KNOWN_KEYS)
            if not rec["samples"] or rec["avg_ms"] is None:
                stats["filtered_no_samples"] += 1
                continue
            matches.append(rec)
    return matches


def parse_jsonl(path, stats):
    """Raw records written by the mod (schema_version 1), mapped to the viewer's record keys."""
    matches = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line:
                continue
            try:
                r = json.loads(line)
                rtt = r.get("rtt_ms") or {}
                idl = r.get("input_delay") or {}
                ts = (r.get("end_local") or "").replace("T", " ")
                datetime.strptime(ts, "%Y-%m-%d %H:%M:%S")
                dur = r.get("duration_s")
                rec = {"ts": ts,
                       "match_id": r.get("match_id", ""),
                       "opponent": r.get("opponent") or "unknown",
                       "opponent_id": r.get("opponent_id") or None,
                       "reason": r.get("end_reason", ""),
                       "avg_ms": rtt.get("avg"), "min_ms": rtt.get("min"), "max_ms": rtt.get("max"), "sd_ms": rtt.get("sd"),
                       "stalled_s": r.get("stalled_s"),
                       "mode": r.get("mode"),
                       "samples": int(r["samples"]),
                       "stalls": r.get("stalls"),
                       "duration_s": int(round(dur)) if isinstance(dur, (int, float)) else None,
                       "input_delay_f": idl.get("measured_avg_f") if idl.get("measured_avg_f") is not None else idl.get("scanned_avg_f"),
                       "result": result_of(r.get("result")),
                       "end_type": r.get("end_type") or None}
            except (ValueError, KeyError, TypeError, AttributeError):
                stats["malformed"] += 1
                stats["malformed_lines"].append(lineno)
                continue
            if not rec["samples"] or rec["avg_ms"] is None:
                stats["filtered_no_samples"] += 1
                continue
            # v1.2.0: characters, format, combat stats, timeline (passed through as written by the mod)
            for k in ("map", "map_name", "opp_rank", "opp_rank_name", "timeout", "format", "max_dp", "my_team", "opp_team", "dp", "active_end", "combat", "rank", "result_inferred", "dc_self",
                      "finish", "win_type", "incident", "opp_streak", "opp_card",
                      "fps", "catchup", "opp_waits", "opp_fps", "opp_delay_f", "packet_loss", "region", "transport", "game_speed", "rollback"):
                if r.get(k) is not None:
                    rec[k] = r[k]
            # v1.4.0 (delay_scan = 1): input delay object {avg, min, max, p95, samples}, ms and window changes
            if isinstance(r.get("input_delay_f"), dict) and r["input_delay_f"].get("avg") is not None:
                rec["input_delay_f"] = r["input_delay_f"]
                for k in ("input_delay_ms", "delay_changes"):
                    if r.get(k) is not None:
                        rec[k] = r[k]
            if r.get("timeline") and rec["match_id"]:
                rec["timeline_file"] = "timelines/" + rec["match_id"] + ".js"
            matches.append(rec)
    return matches


def parse_csv(path):
    if not os.path.isfile(path):
        return None
    out = {}
    try:
        with open(path, newline="", encoding="utf-8", errors="replace") as f:
            for row in csv.DictReader(f):
                try:
                    out[row["opponent"]] = {"matches": int(row["matches"]), "samples": int(row["samples"]),
                                            "avg_ms": num(row["avg_ms"], float)}
                except (KeyError, ValueError, AttributeError):
                    pass
    except OSError:
        return None
    return out


def main():
    global MATCH_LOG, RANKED_CSV, OUT_JS
    import argparse
    ap = argparse.ArgumentParser(description="Build data.js for the match-history viewer")
    ap.add_argument("--jsonl", default=None, help="matches.jsonl path (default: ..\\data\\matches.jsonl)")
    ap.add_argument("--log", default=None, help="tab log with NATIVE_END lines (used only when no matches.jsonl exists)")
    ap.add_argument("--csv", default=RANKED_CSV, help="ranked.csv path (cross-check only)")
    ap.add_argument("--out", default=OUT_JS, help="output data.js path")
    a = ap.parse_args()
    RANKED_CSV, OUT_JS = a.csv, a.out
    stats = {"log_missing": False, "other_lines": 0, "malformed": 0, "malformed_lines": [],
             "filtered_no_samples": 0, "ignored_keys": set()}
    jsonl = a.jsonl or next((p for p in JSONL_CANDIDATES if os.path.isfile(p)), None)
    if a.log is None and jsonl and os.path.isfile(jsonl):
        MATCH_LOG, source = jsonl, "jsonl"
        matches = parse_jsonl(jsonl, stats)
    else:
        MATCH_LOG = a.log or (MATCH_LOG if os.path.isfile(MATCH_LOG) else LEGACY_LOG)
        source = "log"
        matches = parse_log(MATCH_LOG, stats)
    matches.sort(key=lambda m: m["ts"])

    # Cross-check against the ranked CSV (mode 1 aggregates). Only reported, never used for display.
    csv_agg = parse_csv(RANKED_CSV)
    notes = []
    if csv_agg is not None:
        by_opp = {}
        for m in matches:
            if m["mode"] == 1:
                by_opp.setdefault(m["opponent"], []).append(m)
        for opp, ms in by_opp.items():
            c = csv_agg.get(opp)
            if not c:
                notes.append(f"{opp}: in log but not in ranked CSV")
                continue
            s = sum(m["samples"] for m in ms)
            if c["samples"] != s:
                notes.append(f"{opp}: samples log={s} csv={c['samples']}")
            elif c["avg_ms"] is not None:
                avg = sum(m["avg_ms"] * m["samples"] for m in ms) / s
                if abs(avg - c["avg_ms"]) > 0.2:
                    notes.append(f"{opp}: avg log={avg:.1f} csv={c['avg_ms']:.1f}")

    meta = {
        "generated": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "writer": "build-history.py",
        "source": source,
        "log_path": MATCH_LOG,
        "log_missing": stats["log_missing"],
        "csv_present": csv_agg is not None,
        "filtered_no_samples": stats["filtered_no_samples"],
        "malformed": stats["malformed"],
        "other_lines": stats["other_lines"],
        "ignored_keys": sorted(stats["ignored_keys"]),
        "crosscheck": notes,
    }
    js = ("// Generated by build-history.py - do not edit.\n"
          "window.SZ_GENERATED = " + json.dumps(meta["generated"]) + ";\n"
          "window.SZ_META = " + json.dumps(meta, indent=1) + ";\n"
          "window.SZ_MATCHES = " + json.dumps(matches, indent=1) + ";\n")
    tmp = OUT_JS + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(js)
    os.replace(tmp, OUT_JS)

    print(f"source ({source}): {MATCH_LOG}" + ("  (MISSING)" if stats["log_missing"] else ""))
    print(f"real matches: {len(matches)}  filtered (samples=0): {stats['filtered_no_samples']}  "
          f"malformed: {stats['malformed']}  other lines: {stats['other_lines']}")
    if stats["malformed_lines"]:
        print("malformed line numbers:", stats["malformed_lines"][:20])
    if csv_agg is None:
        print("ranked CSV: missing (cross-check skipped)")
    else:
        print("ranked CSV cross-check:", "OK" if not notes else "")
        for n in notes:
            print("  ", n)
    print(f"wrote {OUT_JS}")


if __name__ == "__main__":
    sys.exit(main())
