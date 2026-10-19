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


def check_layout(v):
    """The fields both clients lay the screen out from (design/ui-text.md), and the text client showing the same layers."""
    st = v["state"]
    e = call("/api/view?seq=-1&line=0&entry=0")
    ents = e.get("entries") or []
    assert ents and e.get("entry_count") == len(ents), "no chronicle entries by season"
    assert all("skey" in x and x.get("cls") in ("big", "fam", "news", "sub", "ledger") for x in ents), f"an entry without its season or kind: {ents[:3]}"
    assert any(x["cls"] == "ledger" for x in ents), "no season's books among the entries"
    tail = call(f"/api/view?seq=-1&line=0&entry={len(ents)}")
    assert tail.get("entries") == [], "the entry cursor does not skip what was read"
    assert isinstance(st.get("talk"), list), "the village's talk missing"
    assert "from" in (st.get("ledger_now") or {}), "this season's books missing"
    if st.get("ambition"):
        assert st["ambition"].get("blocker"), "the ambition's next step missing from the top bar"
    tp =subprocess.run([sys.executable, os.path.join(ROOT, "tools/ui/textplay.py"), "--port", str(PORT), "view"], capture_output=True, text=True, timeout=60).stdout
    for head in ("―― 現在要你決定 ――", "―― 家裡 ――", "―― 編年記錄"):
        assert head in tp, f"textplay view lacks {head}"
    lines = tp.splitlines()
    # Two lines of top bar, then a blank line (or the notice of the action just refused).
    assert lines[0].startswith("=== ") and (lines[2].strip() == "" or lines[2].startswith("不行")), f"textplay top bar not two lines: {lines[:4]}"


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
        seq, line, asked, looked, looked_bad, set_rule = -1, 0, 0, False, False, False
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
            if ask["kind"] == "season" and not looked_bad:
                # A mistyped key is refused with what it lacks, and the season goes on (design/fun-r6.md A).
                looked_bad = True
                call("/api/act", {"do": "interact", "target": st["family"][0]["id"], "event": "talk_heart"})
                v2 = call(f"/api/view?seq={seq}&line={line}")
                while not v2["ask"] and not v2["ended"]:
                    v2 = call(f"/api/view?seq={v2['seq']}&line={line}")
                seq = v2["seq"]
                note = (v2["state"] or {}).get("notice", "")
                assert "少了" in note and "who" in note, f"malformed action not refused: {note!r}"
                assert v2["ask"] and v2["ask"]["kind"] == "season", "a malformed action ended the season"
                assert all(p.get("gates") is not None for p in v2["state"].get("plans", [])), "a plan without its conditions"
                assert "reputation" in v2["state"] and "ladder" in v2["state"], "reputation or the ladder missing from the main screen"
                check_layout(v2)
                continue
            if ask["kind"] == "season" and not set_rule:
                # Every house rule the page offers can be changed from the season (the page's select sends this).
                set_rule = True
                for r in st.get("rules") or []:
                    other = next((c["value"] for c in r.get("choices", []) if c["value"] != r.get("value")), None)
                    if other is None:
                        continue
                    call("/api/act", {"do": "rule", "rule": r["id"], "value": other})
                    v2 = call(f"/api/view?seq={seq}&line={line}")
                    while not v2["ask"] and not v2["ended"]:
                        v2 = call(f"/api/view?seq={v2['seq']}&line={line}")
                    seq = v2["seq"]
                    now = next((x for x in v2["state"].get("rules", []) if x["id"] == r["id"]), {})
                    assert now.get("value") == other, f"rule {r['id']} not changed: {(v2['state'] or {}).get('notice', '')!r}"
                    assert v2["ask"]["kind"] == "season", "changing a rule ended the season"
                continue
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
