#!/usr/bin/env python3
"""Plays a short game through the browser server the way the page does (tools/ui/index.html):
reads the view, looks at a person and the family's knowledge, answers every question, until the game ends.
    python3 tests/ui_smoke.py <out.md>
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 8790


def call(path, body=None):
    url = f"http://127.0.0.1:{PORT}{path}"
    data = None if body is None else json.dumps(body).encode()
    with urllib.request.urlopen(urllib.request.Request(url, data=data), timeout=60) as r:
        return json.loads(r.read().decode() or "{}") if "api" in path else r.read().decode()


def main():
    global PORT
    out = sys.argv[1]
    server = subprocess.Popen([sys.executable, os.path.join(ROOT, "tools/ui/server.py"), "--no-browser", "--port", str(PORT),
                               "--", "--seed", "7", "--years", "1", "--out", out], stdout=subprocess.PIPE, text=True)
    try:
        first = server.stdout.readline()
        assert "127.0.0.1" in first, first
        PORT = int(first.split("127.0.0.1:")[1].split("/")[0])
        page = call("/")
        assert "<html" in page and "/api/view" in page, "page missing"
        seq, line, asked, looked = -1, 0, 0, False
        deadline = time.time() + 120
        while time.time() < deadline:
            v = call(f"/api/view?seq={seq}&line={line}")
            seq, line = v["seq"], v["line_count"]
            if v["ended"]:
                assert not v["ended"].get("crashed"), "game crashed"
                assert line > 0, "no chronicle"
                print(f"   通過（{asked} 次選擇，{line} 行記錄）")
                return 0
            ask = v["ask"]
            if not ask:
                continue
            st = v["state"]
            assert st and st["family"] and st["head"], "state missing"
            if not looked:
                p = call("/api/look", {"do": "inspect", "who": st["family"][0]["id"]})
                assert p.get("type") == "person" and p.get("text"), f"inspect failed: {p}"
                r = call("/api/look", {"do": "report"})
                assert r.get("type") == "report" and r.get("text"), f"report failed: {r}"
                looked = True
            asked += 1
            if ask["kind"] == "season":
                call("/api/act", {"do": "pass"})
            else:
                assert ask["options"], "choice without options"
                call("/api/act", {"do": "choose", "index": 0})
        print("失敗：瀏覽器版一年沒玩完")
        return 1
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    sys.exit(main())
