#!/usr/bin/env python3
"""Persistent serial A/B harness for the perf telemetry (@PERF).

Usage:
  python tools/perf_crawl.py baseline [--turns 20] [--menus 8]
"""
import argparse, json, statistics, sys, time
from pathlib import Path
import serial

PORT = "COM3"
OUT = Path(__file__).resolve().parent / "perf_results"
KIND = {1: "page", 2: "menu", 3: "menu_close", 4: "open",
        5: "layout", 6: "state", 7: "count", 8: "refresh"}


def wait_ready(s, timeout=14):
    t0 = time.time()
    buf = b""
    while time.time() - t0 < timeout:
        buf += s.read(4096)
        if b"Lysis Epub reader ready" in buf or b"library" in buf:
            time.sleep(0.5)
            return
    print("ready timeout; proceeding", file=sys.stderr)


def send(s, cmd, wait=0.6):
    s.write((cmd + "\n").encode("ascii"))
    s.flush()
    time.sleep(wait)


def read_perf(s, timeout=3.0):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        buf += s.read(8192)
        if b"PERF n=" in buf:
            time.sleep(0.4)
            buf += s.read(65536)
            break
        time.sleep(0.03)
    events = []
    lines = buf.decode("utf-8", "replace").splitlines()
    n = None
    for line in lines:
        if "PERF n=" in line:
            try: n = int(line.split("PERF n=")[1].split()[0])
            except ValueError: pass
        elif "PERF " in line:
            p = line[line.find("PERF "):].split()
            if len(p) >= 8 and p[1].isdigit():
                vals = [int(x, 0) for x in p[1:8]]
                events.append(dict(zip(
                    ["kind", "total_us", "draw_us", "pack_us", "push_us", "panel_us", "aux"], vals)))
    return n, events


def stats(events, kind):
    vals = [e["total_us"] / 1000.0 for e in events if e["kind"] == kind]
    if not vals:
        return None
    vals.sort()
    return {
        "n": len(vals),
        "min_ms": round(min(vals), 2),
        "median_ms": round(statistics.median(vals), 2),
        "p95_ms": round(vals[min(len(vals)-1, int(len(vals)*0.95))], 2),
        "max_ms": round(max(vals), 2),
        "avg_ms": round(statistics.mean(vals), 2),
        "raw_ms": [round(v, 2) for v in vals],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tag", nargs="?", default="baseline")
    ap.add_argument("--turns", type=int, default=20)
    ap.add_argument("--menus", type=int, default=8)
    args = ap.parse_args()

    s = serial.Serial(PORT, 115200, timeout=0.2)
    s.reset_input_buffer()
    try:
        wait_ready(s)
        # deterministic start: leave any sub-screen, then open Artu (row 7)
        send(s, "@LIBRARY", 1.2)
        send(s, "@PERF RESET", 0.4)
        send(s, "@TAP 270 543", 6.0)
        send(s, "@PERF RESET", 0.4)
        # one tap per completed turn: 0.75 s built a command backlog and the
        # final @PERF was parsed after read_perf's 3 s window had expired
        for _ in range(args.turns):
            send(s, "@TAP 505 480", 1.7)
        send(s, "@PERF", 0.5)
        n, ev = read_perf(s, timeout=30.0)
        page = stats(ev, 1)
        state = stats(ev, 6)
        print("PAGE", page)
        print("STATE_WRITE", state)

        menus = []
        for _ in range(args.menus):
            send(s, "@PERF RESET", 0.3)
            send(s, "@SWIPE 270 820 270 300", 2.0)
            send(s, "@PERF", 0.5)
            _, evm = read_perf(s, timeout=15.0)
            if any(e["kind"] == 2 for e in evm):
                menus.extend([e for e in evm if e["kind"] == 2])
            send(s, "@TAP 400 818", 1.2)   # Library closes reader
            send(s, "@TAP 270 543", 6.0)
        menu = stats(menus, 2)
        print("MENU_OPEN", menu)

        result = {"tag": args.tag, "when": time.strftime("%Y-%m-%dT%H:%M:%S"),
                  "page_turn": page, "menu_open": menu, "state_write": state}
        OUT.mkdir(exist_ok=True)
        (OUT / (args.tag + ".json")).write_text(json.dumps(result, indent=2), encoding="utf-8")
        print("saved", OUT / (args.tag + ".json"))
    finally:
        s.close()


if __name__ == "__main__":
    main()
