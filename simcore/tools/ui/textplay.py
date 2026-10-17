#!/usr/bin/env python3
"""Text client for the browser version of the game (tools/ui/server.py), for a playtester without a browser
(the fun playtest in design/fun-playtest.md drives the game through it).

    textplay.py --port 8820 view            what the page shows now, plus chronicle lines since the last view
    textplay.py --port 8820 act '<json>'    send an action (same JSON the page sends), then show the next view
    textplay.py --port 8820 look <id>       look at a person (the page's click on a name)
    textplay.py --port 8820 report          what the family knows (secrets, rumours)

Actions (the page's buttons):
  {"do":"choose","index":0}                         answer a question (options are numbered from 0)
  {"do":"ai"}                                       let the AI answer the current question
  {"do":"interact","who":12,"event":"talk_heart"}   spend season time on someone (see each person's actions)
  {"do":"ask","who":12,"ask":"sell_parcel","object":30,"subject":-1,"office":"","lever":""}   開口 (see asks)
  {"do":"plan","plan":"befriend","target":64,"subject":-1}   start a plan (see 計畫)   {"do":"cancel"}
  {"do":"want","what":"parcel","object":30} / {"do":"want","what":"office:reeve"} / {"do":"want","what":"drop"}
  {"do":"focus","who":12,"focus":"letters"}          年度重心 (free, during a season)
  {"do":"pass"}   end this season          {"do":"year"}   let the AI play a whole year
"""
import json
import os
import sys
import time
import urllib.request

port = 8820
args = sys.argv[1:]
if args[:1] == ["--port"]:
    port = int(args[1])
    args = args[2:]
CURSOR = os.path.join(os.environ.get("TMPDIR", "/tmp"), f".textplay_cursor_{port}")  # where the last view stopped


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=40) as r:
        return json.loads(r.read().decode("utf-8"))


def post(path, obj):
    req = urllib.request.Request(BASE + path, data=json.dumps(obj, ensure_ascii=False).encode("utf-8"), method="POST")
    with urllib.request.urlopen(req, timeout=40) as r:
        return json.loads(r.read().decode("utf-8"))


def cursor():
    try:
        return int(open(CURSOR).read())
    except (OSError, ValueError):
        return 0


def acts(xs):
    out = []
    for a in xs or []:
        t = f"{a['event']}「{a['title']}」"
        cost = []
        if a.get("time"):
            cost.append(f"{a['time']}段時間")
        if a.get("money"):
            cost.append(f"{a['money']}便士")
        if cost:
            t += "(" + "，".join(cost) + ")"
        if a.get("wanted"):
            t += "★對方正希望"
        if not a.get("open", True):
            t += f"[不行：{a.get('why', '')}]"
        out.append(t)
    return "；".join(out)


def asks(xs):
    out = []
    for a in xs or []:
        t = f"ask={a['ask']} object={a['object']} subject={a['subject']} office={a.get('office', '')}「{a['label']}」"
        if not a.get("open", True):
            t += f"[不行：{a.get('why', '')}]"
        out.append(t)
    return "\n      ".join(out)


def render(v, full=False):
    s, a = v.get("state"), v.get("ask")
    out = []
    lines = v.get("lines", [])
    start = cursor()
    new = lines[start:] if start <= len(lines) else lines
    if new:
        out.append("―― 編年史（上次看之後） ――")
        for l in new:
            out.append(f"{l['year']} {l['text']}")
    open(CURSOR, "w").write(str(v.get("line_count", len(lines))))
    if v.get("ended"):
        out.append("―― 遊戲結束 ―― " + json.dumps(v["ended"], ensure_ascii=False)[:2000])
    if not s:
        return "\n".join(out)
    d, h = s["date"], s["house"]
    out.append(f"\n=== {d['year']} 年 {d['season']} ｜ {h['surname']}家（{h['standing']}，住 {h['home']}）｜ 錢 {h['money']} 便士 ｜ 存糧 {h['food_days']} 天 ｜ "
               f"地 {h['acres']} 畝（自有 {h['freehold']}）｜ 欠債 {h['debt']} ｜ 這一季還有 {s.get('time_left')} 段空閒")
    if s.get("summary"):
        out.append(s["summary"].rstrip())
    if s.get("last_ask"):
        out.append("上次開口：" + json.dumps(s["last_ask"], ensure_ascii=False)[:600])
    out.append("―― 家人 ――")
    for p in s["family"]:
        t = f"[{p['id']}] {p['full']}（{p['kin']}，{p['age']} 歲）心情 {p['mood']}{('：' + p['mood_cause']) if p.get('mood_cause') else ''}，健康 {p['health']}"
        if p.get("traits"):
            t += "，" + "、".join(p["traits"])
        if p.get("station"):
            t += f"，處境：{p['station']}"
        if p.get("opinion_word"):
            t += f"，對你 {p['opinion_word']} {p.get('opinion')}"
        out.append(t)
        if p.get("want"):
            out.append(f"    想要：{p['want']}")
        if p.get("focus_name"):
            fs = "、".join(f"{f['id']}={f['name']}" for f in p.get("foci", []))
            out.append(f"    重心：{p['focus_name']}" + (f"（可改：{fs}）" if fs else ""))
        if p.get("road"):
            out.append("    人生路：" + "；".join(f"{r['year']} {r['text']}" for r in p["road"]))
        if p.get("actions"):
            out.append("    能做：" + acts(p["actions"]))
    if s.get("away"):
        out.append("―― 在外的家人 ――")
        for p in s["away"]:
            out.append("  " + json.dumps(p, ensure_ascii=False)[:300])
    if s.get("plan"):
        out.append("―― 進行中的計畫 ―― " + json.dumps(s["plan"], ensure_ascii=False)[:500])
    ops = [p for p in s.get("plans", []) if p.get("open")]
    if ops:
        out.append("―― 能開始的計畫 ――")
        for p in ops:
            tg = "；".join(f"target={t['target']} subject={t['subject']} {t['label']}" for t in p.get("targets", [])[:8])
            out.append(f"  {'★' if p.get('ambition') else ''}{p['id']}「{p['name']}」{p['desc']}  對象：{tg}")
    out.append("―― 村民 ――")
    people = s.get("people", [])
    if not full:
        people = sorted(people, key=lambda p: -(p.get("closeness", 0) + abs(p.get("feud", 0)) * 50))[:14]
    for p in people:
        t = f"[{p['id']}] {p['name']}：{p['line']}；{p['familiarity']}"
        if p.get("feud"):
            t += f"，世仇 {p['feud']}"
        if p.get("owes"):
            t += f"，欠你家人情 {p['owes']}"
        if p.get("owed"):
            t += f"，你家欠他人情 {p['owed']}"
        out.append(t)
        if p.get("actions"):
            out.append("    能做：" + acts(p["actions"]))
        if p.get("asks"):
            out.append("    開口：" + asks(p["asks"]))
    if not full:
        out.append(f"（只列最熟的 14 人，共 {len(s.get('people', []))} 人；view --full 看全部）")
    c = s.get("contest") or {}
    if c.get("items") or c.get("offices"):
        out.append("―― 村裡在爭什麼 ――")
        for it in c.get("items", []):
            ws = "、".join(sorted({w["house"] for w in it.get("wanters", [])}))
            out.append(f"  parcel={it.get('parcel')} {it['thing']}（{it['holder']}）想要的：{ws or '沒人'}")
        for o in c.get("offices", []):
            out.append(f"  {o['name']}：{o['holder']}，到 {o['until']} 年{'（你家的）' if o.get('mine') else ''}")
        for pst in c.get("past", [])[:4]:
            out.append(f"  {pst['year']} {pst['thing']}：{pst['outcome']}")
    heard = [x for x in s.get("heard", []) if x.get("recent")]
    if heard:
        out.append("―― 村裡在說 ――")
        for x in heard[:10]:
            out.append(f"  {x['year']} {'（悄悄話）' if x.get('whisper') else ''}{x['text']}")
    out.append("\n―― 輪到你 ――")
    if a and a.get("kind") == "choice":
        out.append(f"〔{a.get('title', '')}〕{a.get('situation', '')}")
        for i, o in enumerate(a.get("options", [])):
            out.append(f"  {i}. {o}")
        for u in a.get("unavailable", []):
            out.append(f"  （不能選）{u}")
        out.append('回答：{"do":"choose","index":N}，或 {"do":"ai"}')
    elif a and a.get("kind") == "season":
        out.append('這一季：用 interact / ask / plan / focus / want 花時間，然後 {"do":"pass"} 結束這一季（或 {"do":"year"} 讓 AI 過一年）')
    elif a:
        out.append(json.dumps(a, ensure_ascii=False))
    else:
        out.append("（遊戲在跑，稍後再 view）")
    return "\n".join(out)


def wait_ask(prev_seq):
    v = get(f"/api/view?seq={prev_seq}&line=0")
    deadline = time.time() + 90
    while not v.get("ask") and not v.get("ended") and time.time() < deadline:
        v = get(f"/api/view?seq={v['seq']}&line=0")
    return v


cmd = args[0] if args else "view"
if cmd == "view":
    v = get("/api/view?seq=-1&line=0")
    if not v.get("ask") and not v.get("ended"):
        v = wait_ask(v["seq"])
    print(render(v, full="--full" in args))
elif cmd == "act":
    v0 = get("/api/view?seq=-1&line=0")
    obj = json.loads(args[1])
    post("/api/act", obj)
    print(render(wait_ask(v0["seq"])))
elif cmd == "look":
    r = post("/api/look", {"do": "inspect", "who": int(args[1])})
    print(r.get("text", json.dumps(r, ensure_ascii=False)))
elif cmd == "report":
    r = post("/api/look", {"do": "report"})
    print(r.get("text", json.dumps(r, ensure_ascii=False)))
