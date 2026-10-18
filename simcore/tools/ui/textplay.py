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
  {"do":"agenda","id":"sell_grain"}                one of 這一季的事 (the season's list at the top of the view)
  {"do":"interact","who":12,"event":"talk_heart"}   spend season time on someone (see each person's actions;
                                                    keep_company = 陪伴, the visit they need most)
  {"do":"ask","who":12,"ask":"sell_parcel","object":30,"subject":-1,"office":"","lever":""}   開口 (see asks)
  {"do":"plan","plan":"befriend","target":64,"subject":-1}   start a plan (see 計畫)   {"do":"cancel","plan":"<id>"}
  {"do":"want","what":"parcel","object":30} / {"do":"want","what":"office:reeve"}
  {"do":"want","what":"drop","object":<want id>}     give up a want (its id is shown as want=N under 村裡在爭什麼)
  {"do":"focus","who":12,"focus":"letters"}          年度重心 (free, during a season)
  {"do":"pass"}   end this season          {"do":"year"}   let the AI play a whole year
  {"do":"regency"}   holding the house for a child: hand the years to the steward until they come of age
                     (big questions are still asked on the way)
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
BASE = f"http://127.0.0.1:{port}"
CURSOR = os.path.join(os.environ.get("TMPDIR", "/tmp"), f".textplay_cursor_{port}")  # where the last view stopped
HOUSE = {"cottage": "泥牆茅屋", "timber": "木屋", "stone": "石屋", "manor": "莊園宅邸"}
OPEN = urllib.request.build_opener(urllib.request.ProxyHandler({})).open  # never through a system proxy


def get(path):
    with OPEN(BASE + path, timeout=40) as r:
        return json.loads(r.read().decode("utf-8"))


def post(path, obj):
    req = urllib.request.Request(BASE + path, data=json.dumps(obj, ensure_ascii=False).encode("utf-8"), method="POST")
    with OPEN(req, timeout=40) as r:
        return json.loads(r.read().decode("utf-8"))


def cursor(lines):
    """How many chronicle lines were already shown, if the cursor belongs to this game (same first line)."""
    try:
        n, first = open(CURSOR, encoding="utf-8").read().split("\n", 1)
        return int(n) if lines and first == lines[0]["text"] else 0
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
    start = cursor(lines)
    new = lines[start:] if start <= len(lines) else lines
    if new:
        out.append("―― 編年史（上次看之後） ――")
        for l in new:
            out.append(f"{l['year']} {l['text']}")
    if lines:
        open(CURSOR, "w", encoding="utf-8").write(f"{v.get('line_count', len(lines))}\n{lines[0]['text']}")
    if v.get("ended"):
        out.append("―― 遊戲結束 ―― " + json.dumps(v["ended"], ensure_ascii=False)[:2000])
    if not s or "house" not in s:  # the line has ended: the last state has only the date
        return "\n".join(out)
    season = bool(a) and a.get("kind") == "season"
    d, h = s["date"], s["house"]
    out.append(f"\n=== {d['year']} 年 {d['season']} ｜ {h['surname']}家（{h['standing']}，住 {HOUSE.get(h['home'], h['home'])}）｜ 錢 {h['money']} 便士 ｜ 存糧 {h['food_days']} 天 ｜ "
               f"地 {h['acres']} 畝（自有 {h['freehold']}）｜ 欠債 {h['debt']}" + ("｜ 今年缺糧" if h.get("lean") else "")
               + (f" ｜ 這一季還有 {s.get('time_left')} 段空閒" if season else ""))
    if season and s.get("regency"):
        r = s["regency"]
        out.append(f"（你在替 {r['ward']} 持家，還有約 {r['years_left']} 年。可以 act '{{\"do\":\"regency\"}}' 交給管家，直到{r['ward']}成年，路上有大事還是會問你。）")
    amb = s.get("ambition")
    if amb:
        mark = lambda x: "✓" if x.get("done") else "△" if x.get("slipped") else "◎" if x.get("final") else "○"
        out.append(f"志向：{amb['name']}（{amb.get('desc', '')}）還剩 {amb.get('years_left', '?')} 年　" + " → ".join(mark(x) + x["name"] for x in amb.get("steps", []))
                   + ("　（△ 做到過，現在又不夠了；◎ 最後一步）" if any(x.get("slipped") for x in amb.get("steps", [])) else "　（◎ 最後一步）"))
        if amb.get("hint"):
            out.append(f"  怎麼推進：{amb['hint']}" + (f"（★ 計畫：{'、'.join(amb['plans'])}）" if amb.get("plans") else ""))
    fine = (s.get("contest") or {}).get("fine")
    if fine:
        out.append(f"入地費 {fine['amount']} 便士，{fine['year']} 年 {fine['month']} 月前要交給領主{'（已寬限過）' if fine.get('extended') else ''}。"
                   "交不出來要借、去求領主寬限，或讓出一塊地。")
    if s.get("summary"):
        out.append(s["summary"].rstrip())
    la = s.get("last_ask")
    if la:
        out.append(f"上次開口（{la.get('name', '')}）：{la.get('text', '')}")
        names = {"meet": "答應對方的條件", "gift": "送一份禮再說", "favour": "提起上回幫過的忙", "hook": "拿那件事壓對方"}
        for lv in la.get("levers", []):
            press = {"do": "ask", "who": la["who"], "ask": la["ask"], "object": la["object"], "subject": la["subject"],
                     "office": la.get("office", ""), "lever": lv}
            out.append(f"  再爭取一次：{names.get(lv, lv)} → act '{json.dumps(press, ensure_ascii=False)}'")
    if season and s.get("agenda"):
        out.append("―― 這一季的事 ――（各用一段空閒）")
        for it in s["agenda"]:
            line = f"  ・{it['title']}：{it.get('desc', '')}"
            if it.get("open", True):
                line += "  → act '" + json.dumps({"do": "agenda", "id": it["id"]}, ensure_ascii=False) + "'"
            else:
                line += f"［不行：{it.get('why', '')}］"
            out.append(line)
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
            out.append(f"  {p.get('kin', '')} {p['name']}（{p.get('age', '?')} 歲）：{p.get('station', '')}")
            if p.get("road"):
                out.append("    人生路：" + "；".join(f"{r['year']} {r['text']}" for r in p["road"]))
    for g in s.get("plans_going") or []:
        out.append("―― 進行中的計畫 ―― %s%s 進度 %s/100（放棄：{\"do\":\"cancel\",\"plan\":\"%s\"}）" % (
            g.get("name", ""), "（對象 " + g["target"] + "）" if g.get("target") else "", g.get("progress", 0), g.get("id", "")))
    shut = [p for p in s.get("plans", []) if not p.get("open") and p.get("why") and p["why"] != "正在辦"]
    if shut:
        out.append("―― 現在開不了的計畫 ―― " + "；".join(f"{'★' if p.get('ambition') else ''}「{p['name']}」{p['why']}" for p in shut))
    ops = [p for p in s.get("plans", []) if p.get("open")]
    if ops:
        out.append("―― 能開始的計畫 ――")
        for p in ops:
            tg = "；".join(f"target={t['target']} subject={t['subject']} {t['label']}" for t in p.get("targets", [])[:8])
            out.append(f"  {'★' if p.get('ambition') else ''}{p['id']}「{p['name']}」{p['desc']}  對象：{tg}")
    out.append("―― 村民 ――")
    people = s.get("people", [])
    if not full:
        # The closest, and anyone who can be asked for something or wants something of the family.
        near = {p["id"] for p in sorted(people, key=lambda p: -(p.get("closeness", 0) + abs(p.get("feud", 0)) * 50))[:14]}
        people = [p for p in people if p["id"] in near or p.get("feud") or any(x.get("open") for x in p.get("asks") or [])
                  or any(x.get("wanted") for x in p.get("actions") or [])]
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
        out.append(f"（列出最熟的 14 人和能開口的人，共 {len(s.get('people', []))} 人；view --full 看全部）")
    c = s.get("contest") or {}
    if c.get("items") or c.get("offices"):
        out.append("―― 村裡在爭什麼 ――")
        for it in c.get("items", []):
            ws = "、".join(sorted({w["house"] for w in it.get("wanters", [])}))
            ids = (f"parcel={it['parcel']} " if it.get("parcel", -1) >= 0 else "") + (f"want={it['mine']}（你家想要） " if it.get("mine", -1) >= 0 else "")
            out.append(f"  {ids}{it['thing']}（{it['holder']}）想要的：{ws or '沒人'}")
        for o in c.get("offices", []):
            who = f"{o['holder']}，到 {o['until']} 年" if o.get("holder") else "空著"
            if o.get("mine"):
                tail = "（你家的）"
            elif o.get("wanted"):
                tail = f"（你家想當，出來選的是{o.get('runner', '')}；不想了：act {{\"do\":\"want\",\"what\":\"drop\",\"object\":{o.get('want', -1)}}}）"
            elif o.get("can"):
                men = o.get("men", [])
                if len(men) > 1:
                    tail = "（可以爭，讓誰出來選：" + "；".join(f"{m['name']} act {{\"do\":\"want\",\"what\":\"office:{o['id']}\",\"object\":{m['id']}}}" for m in men) + "）"
                else:
                    tail = f"（可以爭：act {{\"do\":\"want\",\"what\":\"office:{o['id']}\",\"object\":-1}}）"
            else:
                tail = f"（爭不了：{o.get('why', '')}）"
            out.append(f"  {o['name']}：{who}{tail}")
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
    elif season:
        out.append('這一季：先看上面「這一季的事」（agenda），也可以用 interact / ask / plan / focus / want 花時間，然後 {"do":"pass"} 結束這一季（或 {"do":"year"} 讓 AI 過一年）')
    elif a:
        out.append(json.dumps(a, ensure_ascii=False))
    else:
        out.append("（遊戲在跑，稍後再 view）")
    return "\n".join(out)


def wait_ask(prev_seq):
    v = get(f"/api/view?seq={prev_seq}&line=0")
    deadline = time.time() + 60
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
    # The game ignores an action that does not fit what it asked, and the page would be left with no question;
    # refuse it here and show the question again.
    a, do = v0.get("ask"), obj.get("do")
    bad = ""
    if not a:
        bad = "現在沒有在問你（遊戲還在跑或已結束），先 view。"
    elif a.get("kind") == "choice" and not (do == "ai" or (do == "choose" and 0 <= int(obj.get("index", -1)) < len(a.get("options", [])))):
        bad = "現在是在問一個問題：只能 choose（index 在選項範圍內）或 ai。"
    elif a.get("kind") == "season" and do not in ("agenda", "interact", "ask", "plan", "cancel", "want", "focus", "pass", "year", "regency", "quit"):
        bad = "現在是一季的空閒：choose / ai 只能用在問題上。"
    if bad:
        print("不行：" + bad + "\n")
        print(render(v0))
        sys.exit(1)
    post("/api/act", obj)
    print(render(wait_ask(v0["seq"])))
elif cmd == "look":
    if len(args) < 2 or not args[1].lstrip("-").isdigit():
        print("用法：textplay.py look <人的編號>（編號是家人欄或名單裡 [ ] 中的數字）")
        sys.exit(1)
    r = post("/api/look", {"do": "inspect", "who": int(args[1])})
    print(r.get("text", json.dumps(r, ensure_ascii=False)))
elif cmd == "report":
    r = post("/api/look", {"do": "report"})
    print(r.get("text", json.dumps(r, ensure_ascii=False)))
else:
    print(__doc__)
    sys.exit(0 if cmd in ("-h", "--help", "help") else 1)
