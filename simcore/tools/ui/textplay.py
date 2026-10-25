#!/usr/bin/env python3
"""Text client for the browser version of the game (tools/ui/server.py), for a playtester without a browser
(the fun playtest in design/fun-playtest.md drives the game through it). It shows the same three layers as the page
(design/ui-text.md): the top bar, the decision in front of you, then the family, the chronicle by season and the
village's talk.

    textplay.py --port 8820 view            the screen now: top bar, the decision, family, chronicle, village talk
    textplay.py --port 8820 act '<json>'    send an action (same JSON the page sends), then show the next view
    textplay.py --port 8820 more            the top bar opened: every condition, name, standing, the generation's lines, rules
    textplay.py --port 8820 people [--full] villagers, with what you can do with each and what you can ask (開口)
    textplay.py --port 8820 contest         what the village is contending over, the posts and their conditions
    textplay.py --port 8820 plans           every plan with each of its conditions ✓ / ✗ (look plan:<id> for one)
    textplay.py --port 8820 chronicle [N]   the chronicle, the last N seasons (all of them without N)
    textplay.py --port 8820 talk            everything the village is saying (the view shows the last three years)
    textplay.py --port 8820 look <id>       look at a person (the page's click on a name)
    textplay.py --port 8820 report          what the family knows (secrets, rumours)

Actions (the page's buttons):
  {"do":"choose","index":0}                         answer a question (options are numbered from 0)
  {"do":"ai"}                                       let the AI answer the current question
  {"do":"agenda","id":"sell_grain"}                one of the season's list (shown under 現在要你決定)
  {"do":"interact","who":12,"event":"talk_heart"}   spend season time on someone (★ = what they want; people / look for more;
                                                    keep_company = 陪伴, the visit they need most)
  {"do":"ask","who":12,"ask":"sell_parcel","object":30,"subject":-1,"office":"","lever":""}   開口 (see people)
  {"do":"plan","plan":"befriend","target":64,"subject":-1}   start a plan (see plans)   {"do":"cancel","plan":"<id>"}
  {"do":"want","what":"parcel","object":30} / {"do":"want","what":"office:reeve"}
  {"do":"want","what":"drop","object":<want id>}     give up a want (its id is shown as want=N in contest)
  {"do":"focus","who":12,"focus":"letters"}          年度重心 (free, during a season; see more)
  {"do":"pass"}   end this season          {"do":"year"}   let the AI play a whole year
  {"do":"regency"}   holding the house for a child: hand the years to the steward until they come of age
  {"do":"rule","rule":"occasions","value":"close"}   家裡的規矩 (standing rules: occasions = all|close|none|ask,
                                                     grain = harvest|monthly|ask)
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
CURSOR = os.path.join(os.environ.get("TMPDIR", "/tmp"), f".textplay_cursor_{port}")  # the season the last view stopped at
HOUSE = {"cottage": "泥牆茅屋", "timber": "木屋", "stone": "石屋", "manor": "莊園宅邸"}
OPEN = urllib.request.build_opener(urllib.request.ProxyHandler({})).open  # never through a system proxy


def get(path):
    with OPEN(BASE + path, timeout=40) as r:
        return json.loads(r.read().decode("utf-8"))


def post(path, obj):
    req = urllib.request.Request(BASE + path, data=json.dumps(obj, ensure_ascii=False).encode("utf-8"), method="POST")
    with OPEN(req, timeout=40) as r:
        return json.loads(r.read().decode("utf-8"))


def act_json(obj):
    return "act '" + json.dumps(obj, ensure_ascii=False) + "'"


def signed(v):
    return f"+{v}" if v > 0 else (f"−{-v}" if v < 0 else "0")


# ---- the chronicle, by season (the same grouping as index.html's seasons()) ----

def seasons(entries):
    """skey -> {label, big, fam, news, ledger, late}: entries in the order they came, a line that belongs to the one
    above it under that one, a late outcome also under the season it came from."""
    out, last = {}, None
    for e in entries:
        s = out.setdefault(e["skey"], {"label": e["season"], "big": [], "fam": [], "news": [], "ledger": None, "late": []})
        cls = e.get("cls")
        if cls == "ledger":
            s["ledger"] = e
            continue
        if cls == "sub":
            if last is not None and last["skey"] == e["skey"]:
                last["subs"].append(e["text"])
            continue
        item = {"skey": e["skey"], "text": e["text"], "subs": [], "near": e.get("near", False)}
        if cls == "news" and not item["near"]:
            s["news"].append(item)
        else:
            (s["big"] if cls in ("big", "news") else s["fam"]).append(item)
        last = item
        if e.get("origin"):
            for k, o in out.items():
                if o["label"] == e["origin"] and k != e["skey"]:
                    o["late"].append(f"{e['season']}：{e['text']}")
    return out


def ledger_text(l):
    if not l:
        return ""
    ins = "、".join(f"{w} {signed(v)}" for w, v in l.get("ins", []))
    outs = "、".join(f"{w} {signed(v)}" for w, v in l.get("outs", []) if w != "買糧")
    g = l.get("grain")
    buy = [x for x in l.get("outs", []) if x[0] == "買糧"]
    grain = f"買糧 {g['times']} 次，{g['days']} 天，{-buy[0][1] if buy else g['pence']} 便士" if g else (f"買糧 {signed(buy[0][1])}" if buy else "")
    parts = "；".join(x for x in (ins, "、".join(y for y in (outs, grain) if y)) if x)
    moved = bool(l.get("ins") or l.get("outs"))
    head = f"錢 {l['from']} → {l['to']} 便士" if l["from"] != l["to"] else f"錢 {l['from']} 便士，" + ("進出相抵" if moved else "沒有進出")
    return head + (f"（{parts}）" if parts else "") + (f"；{l['food']}" if l.get("food") else "")


def season_block(s, now=False, news_all=False):
    out = [f"【{s['label']}{'（這一季到現在）' if now else ''}】"]
    if s["big"] or s["fam"] or s["late"]:
        out.append("  大事：")
        for it in s["big"]:
            out.append(f"   ■ {it['text']}")
            out += [f"       └ {x}" for x in it["subs"]]
        fam = s["fam"] if news_all else s["fam"][:4]
        for it in fam:
            out.append(f"   · {it['text']}")
            out += [f"       └ {x}" for x in it["subs"]]
        if len(s["fam"]) > len(fam):
            out.append(f"   · 還有 {len(s['fam']) - len(fam)} 件家裡的小事（chronicle 看全部）")
        for x in s["late"]:
            out.append(f"   ↳ 後來：{x}")
    led = ledger_text(s["ledger"])
    if led:
        out.append(f"  帳：{led}")
    if s["news"]:
        if news_all:
            out.append("  村裡的消息：")
            out += [f"   · {it['text']}" for it in s["news"]]
        else:
            out.append(f"  還有 {len(s['news'])} 則村裡的消息（chronicle 看）")
    return out


def chronicle_lines(v, count=None, since=None):
    ss = seasons(v.get("entries") or [])
    st = v.get("state") or {}
    now_key = None
    if st.get("date"):
        now_key = st["date"]["year"] * 4 + (st["date"]["month"] - 1) // 3
        ln = st.get("ledger_now")
        if ln:
            ss.setdefault(now_key, {"label": ln["season"], "big": [], "fam": [], "news": [], "ledger": None, "late": []})
            ss[now_key]["ledger"] = ss[now_key]["ledger"] or ln
    keys = sorted(ss, reverse=True)
    if since is not None:
        fresh = sum(1 for k in keys if k >= since)  # the seasons since the last view, at least two, at most six
        keys = keys[:max(2, min(fresh, 6))]
    elif count is not None:
        keys = keys[:count]
    out = []
    for k in keys:
        out += season_block(ss[k], now=k == now_key, news_all=count is None and since is None)
    return out, (max(ss) if ss else None)


# ---- the screen ----

def top_bar(s):
    d, h = s["date"], s["house"]
    rep = s.get("reputation") or {}
    debt = f"欠債 {h['debt']} 便士（{h['debt_text']}）" if h.get("debt") else "欠債：沒有"
    short = h.get("short_days", 0)
    food = (f"存糧 {h['food_days']} 天・離秋收 {h.get('to_harvest', '?')} 天"
            + (f"（{'！' if h.get('lean') else ''}差 {short} 天，{'錢不夠買' if h.get('lean') else '手上的錢買得起'}）" if short > 0 else ""))
    line1 = (f"=== {h['surname']}家｜{d['year']} 年 {d['season']}｜{h['standing']}｜錢 {h['money']} 便士｜{food}｜地 {h['acres']} 畝"
             + (f"（自由地 {h['freehold']} 畝）" if h.get("freehold") else "") + f"｜{debt}"
             + (f"｜名聲 {signed(rep['value'])}（{rep.get('word', '')}）" if rep else ""))
    out = [line1]
    amb = s.get("ambition")
    fine = (s.get("contest") or {}).get("fine")
    if amb:
        yl = amb.get("years_left", "?")
        when = "今年是最後一年！" if isinstance(yl, (int, float)) and yl <= 1 else f"還剩 {yl} 年（含今年）"
        left = f"到 {amb['deadline']} 年底，{when}" if amb.get("deadline") else when
        out.append(f"志向：{amb.get('race') or amb['name']}（{amb['name']}，{left}）｜下一步 {amb.get('blocker', '')}　（全部條件：more）")
    rv = s.get("rival")
    if rv:
        out.append(f"他在跟你搶：{rv.get('after', '')}（宿敵 {rv['name']}；people 看兩家撞過的事）")
    if fine:
        out.append(f"！入地費 {fine['amount']} 便士，{fine['year']} 年 {fine['month']} 月前交給領主{'（已寬限過）' if fine.get('extended') else ''}")
    if s.get("notice"):
        out.append("不行：" + s["notice"])
    return out


def decision(v, s, a):
    out = ["", "―― 現在要你決定 ――"]
    if v.get("ended"):
        out.append("遊戲結束：" + json.dumps(v["ended"], ensure_ascii=False)[:400])
        return out
    if a and a.get("kind") == "choice":
        out.append(f"〔{a.get('title', '')}〕{a.get('situation', '')}")
        for i, o in enumerate(a.get("options", [])):
            out.append(f"  {i}. {o}")
        for u in a.get("unavailable", []):
            out.append(f"  {u if u.startswith('✗') else '✗ ' + u}")
        out.append('回答：act \'{"do":"choose","index":N}\'，或 act \'{"do":"ai"}\'（讓 AI 決定）')
    elif a and a.get("kind") == "season":
        d = s["date"]
        out.append(f"{d['year']} 年 {d['season']}：這一季還有 {s.get('time_left')} 段空閒（每件事用一段，標「不佔空閒」的除外；沒用完的拿去下田）")
        for it in s.get("agenda") or []:
            line = f"  ・{'★ ' if it.get('amb') else ''}{it['title']}{'（不佔空閒）' if it.get('free') else ''}：{it.get('desc', '')}"
            line += f"  → {act_json({'do': 'agenda', 'id': it['id']})}" if it.get("open", True) else f"（✗ {it.get('why', '')}）"
            out.append(line)
        la = s.get("last_ask")
        if la:
            out.append(f"  上次開口（{la.get('name', '')}）：{la.get('text', '')}")
            names = {"meet": "答應對方的條件", "gift": "送一份禮再說", "favour": "提起上回幫過的忙", "hook": "拿那件事壓對方"}
            for lv in la.get("levers", []):
                press = {"do": "ask", "who": la["who"], "ask": la["ask"], "object": la["object"], "subject": la["subject"],
                         "office": la.get("office", ""), "lever": lv}
                out.append(f"    再爭取一次：{names.get(lv, lv)} → {act_json(press)}")
        if s.get("regency"):
            r = s["regency"]
            out.append(f"  替 {r['ward']} 持家，還有約 {r['years_left']} 年：{act_json({'do': 'regency'})} 交給管家到{r['ward']}成年")
        out.append('  過這一季：act \'{"do":"pass"}\'；一口氣過一年：act \'{"do":"year"}\'（也可以找家人的 ★、people 找村民開口、plans 定計畫）')
    elif a:
        out.append(json.dumps(a, ensure_ascii=False))
    else:
        out.append("（日子在過，稍後再 view）")
    return out


def recommended(p, acts):
    """The one button a name tag offers (★): what they are hoping for, else keeping them company when they want
    something of you or a need has run low. Everything else is behind 更多 (look <id>)."""
    rec = next((x for x in acts if x.get("wanted")), None)
    low = (p.get("need_low") or {}).get("value", 100) < 30
    if not rec and (p.get("want") or low):
        rec = next((x for x in acts if x["event"] == "keep_company"), None)
    return rec


def family(s, season):
    out = ["", "―― 家裡 ――"]
    small = []
    for p in s.get("family", []):
        me = p["id"] == s.get("head")
        if not me and p["age"] < 5:
            small.append(f"{p['name']} {p['age']} 歲[{p['id']}]")
            continue
        mood = p["mood"] + (f"（{p['mood_cause']}）" if p.get("mood_cause") else "")
        if p.get("mood_was") and p["mood_was"] != p["mood"]:
            mood += f"（上季{p['mood_was']}）"
        kin = "你" if me else p.get("kin", "")
        line = f"[{p['id']}] {p['name']}（{kin}，{p['age']} 歲）{mood}"
        if p.get("station"):
            line += f"，{p['station']}"
        if p.get("health", 100) < 40:
            line += "，身體很差"
        out.append(line)
        second = []
        if p.get("want"):
            second.append(f"想要：{p['want']}")
        if not me:
            op = f"對你 {signed(p.get('opinion', 0))}（{p.get('opinion_word', '')}）"
            if p.get("opinion_was") is not None and p["opinion_was"] != p.get("opinion"):
                op += f"，上季 {signed(p['opinion_was'])}"
            second.append(op)
        if p.get("need_low"):
            n = p["need_low"]
            second.append(f"缺：{n['name']} {n['value']}")
        if second:
            out.append("    " + "；".join(second))
        acts = [x for x in p.get("actions") or [] if x.get("open", True)]
        rec = recommended(p, acts)
        if season and rec:
            more = len(acts) - 1
            out.append(f"    ★ {rec['title']} → {act_json({'do': 'interact', 'who': p['id'], 'event': rec['event']})}"
                       + (f"　更多（{more} 個）：look {p['id']}" if more > 0 else ""))
    if small:
        out.append("孩子（5 歲以下）：" + "、".join(small))
    for p in s.get("away") or []:
        out.append(f"在外：{p['name']}（{p.get('kin', '')}，{p.get('age', '?')} 歲）{p.get('station', '')}")
    return out


def talk_lines(s, everything=False):
    items = s.get("talk") or []
    year = s["date"]["year"]
    shown = items if everything else [x for x in items if x.get("year", year) >= year - 2]
    if not everything:
        near = [x for x in shown if x.get("near") or x["kind"] == "near"]
        rest = [x for x in shown if not (x.get("near") or x["kind"] == "near")]
        shown = near + rest[: max(0, 6 - len(near))]
    out = []
    for x in shown:
        mark = "◆ " if x.get("near") or x["kind"] == "near" else "  "
        text = x["text"]
        if x["kind"] in ("near", "other"):
            teller = f"（{x['teller']}{'悄悄' if x.get('whisper') else ''}說）" if x.get("teller") else ""
            text = f"{x['year']} {text}{teller}"
        out.append(f"  {mark}{text}" + (f"（{len(x.get('items', []))} 則，talk 看）" if x["kind"] == "fold" and not everything else ""))
        if everything and x["kind"] == "fold":
            out += [f"      · {t}" for t in x.get("items", [])]
    hidden = len(items) - len(shown)
    if hidden > 0 and not everything:
        out.append(f"  （還有 {hidden} 條，talk 看全部）")
    return out


def top_events(v):
    """The big things of this season and the last (fun-r9 C: a post, the ox taken, the debt moving, someone coming or
    going), above the decision; the same rule as the browser's (index.html): seasons are counted from the newest entry
    of the chronicle, so old news does not stay on top."""
    entries = v.get("entries") or []
    if not entries:
        return []
    now = max(e["skey"] for e in entries)
    shown = [e for e in entries if e.get("top") and e["skey"] >= now - 1][-6:]
    return ["■ 大事：" + "／".join(f"{e['season']} {e['text']}" for e in shown)] if shown else []


def render(v):
    s, a = v.get("state") or {}, v.get("ask")
    if not s or "house" not in s:  # the line has ended: the last state has only the date
        return "―― 遊戲結束 ―― " + json.dumps(v.get("ended") or {}, ensure_ascii=False)[:2000]
    season = bool(a) and a.get("kind") == "season"
    out = top_bar(s)
    out += top_events(v)
    out += decision(v, s, a)
    out += family(s, season)
    try:
        since = int(open(CURSOR, encoding="utf-8").read().strip())
    except (OSError, ValueError):
        since = None
    lines, newest = chronicle_lines(v, since=since)
    out += ["", "―― 編年記錄（新的季在上面；chronicle 看更早的）――"] + lines
    if newest is not None:
        open(CURSOR, "w", encoding="utf-8").write(str(newest))
    t = talk_lines(s)
    if t:
        out += ["", "―― 村裡在說（最近三年，◆ 跟你家有關）――"] + t
    out += ["", "參考：more（全部條件、名聲、地位、規矩、重心）｜people（村民、開口）｜contest（在爭什麼、差事）｜plans｜chronicle｜talk｜report"]
    return "\n".join(out)


def render_more(s):
    out = []
    rep = s.get("reputation")
    if rep:
        out.append(f"名聲 {signed(rep['value'])}（{rep['word']}；−50 到 +50）：{rep.get('effect', '')}")
        if rep.get("recent"):
            out.append("  最近：" + "；".join(rep["recent"]))
        if rep.get("mend"):
            out.append("  怎麼修：" + rep["mend"])
    if s.get("ladder"):
        out.append(f"家族地位：{s['ladder']}")
    if s.get("regret"):
        out.append(f"上一代沒做完的心願：「{s['regret']}」")
    amb = s.get("ambition")
    if amb:
        mark = lambda x: "✓" if x.get("done") else "△" if x.get("slipped") else "◎" if x.get("final") else "✗" if x.get("gate") else "○"
        out.append(f"志向：{amb['name']}（{amb.get('desc', '')}）")
        out.append("  路：" + " → ".join(mark(x) + " " + x["name"] for x in amb.get("steps", [])) + "（△ 做到過又不夠了；◎ 最後一步）")
        if amb.get("hint"):
            out.append(f"  怎麼推進：{amb['hint']}" + (f"（★ 計畫：{'、'.join(amb['plans'])}）" if amb.get("plans") else ""))
        for k in ("gates", "rival", "note"):
            if amb.get(k):
                out.append(f"  {amb[k]}")
    th = [t for t in s.get("threads", []) if t.get("kind") != "ambition"]
    if th or s.get("threads_ended"):
        out.append("這一代的線：")
        for t in th:
            out.append(f"  · {t['title']}" + (f"（{t['since']} 起）" if t.get("since", -1) >= 0 else "") + f"：{t['text']}" + (f"　→ {t['ends']}" if t.get("ends") else ""))
        for e in s.get("threads_ended", []):
            out.append(f"  ✓ {e['year']} {e['text']}")
    for g in s.get("plans_going") or []:
        out.append(f"進行中的計畫：{g.get('name', '')}" + (f"（對象 {g['target']}）" if g.get("target") else "")
                   + f" 進度 {g.get('progress', 0)}/100（放棄：{act_json({'do': 'cancel', 'plan': g.get('id', '')})}）")
    for r in s.get("rules") or []:
        alts = "；".join(f"{c['text']} → {act_json({'do': 'rule', 'rule': r['id'], 'value': c['value']})}" for c in r.get("choices", []) if c["value"] != r.get("value"))
        out.append(f"家裡的規矩・{r['name']}：{r['text']}（改成：{alts}）")
    out.append("家人的細節：")
    names = ["吃飽", "歇息", "暖和", "人情", "心靈"]
    for p in s.get("family", []):
        out.append(f"  [{p['id']}] {p.get('full', p['name'])}（{p.get('kin', '')}，{p['age']} 歲）健康 {p.get('health')}"
                   + ("，" + "、".join(p["traits"]) if p.get("traits") else ""))
        out.append("    需要：" + "　".join(f"{n} {v}" for n, v in zip(names, p.get("needs", []))))
        if p.get("reasons"):
            out.append("    對你的原因：" + "、".join(f"{l} {signed(x)}" for l, x in p["reasons"]))
        if p.get("focus_name"):
            fs = "、".join(f"{f['id']}={f['name']}" for f in p.get("foci", []))
            out.append(f"    重心：{p['focus_name']}" + (f"（可改：{fs}，例 {act_json({'do': 'focus', 'who': p['id'], 'focus': 'field'})}）" if fs else ""))
        if p.get("road"):
            out.append("    人生路：" + "；".join(f"{r['year']} {r['text']}" for r in p["road"]))
        for x in p.get("actions") or []:
            t = f"    能做：{x['title']} → {act_json({'do': 'interact', 'who': p['id'], 'event': x['event']})}"
            cost = "，".join(c for c in (f"{x['time']} 段空閒" if x.get("time") else "", f"{x['money']} 便士" if x.get("money") else "") if c)
            if cost:
                t += f"（{cost}）"
            if x.get("wanted"):
                t += " ★對方正希望"
            if not x.get("open", True):
                t += f"（✗ {x.get('why', '')}）"
            out.append(t)
    return "\n".join(out)


def render_people(s, full=False):
    out = ["―― 村民 ――"]
    rv = s.get("rival")
    if rv:
        out.append(f"宿敵：{rv['name']}｜他在跟你搶：{rv.get('after', '')}")
        if rv.get("clashes"):
            out.append("  兩家撞過：" + "；".join(rv["clashes"]))
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
        for x in p.get("actions") or []:
            line = f"    能做：{x['title']} → {act_json({'do': 'interact', 'who': p['id'], 'event': x['event']})}"
            if x.get("wanted"):
                line += " ★對方正希望"
            if not x.get("open", True):
                line += f"（✗ {x.get('why', '')}）"
            out.append(line)
        for a in p.get("asks") or []:
            obj = {"do": "ask", "who": p["id"], "ask": a["ask"], "object": a["object"], "subject": a["subject"], "office": a.get("office", ""), "lever": ""}
            out.append(f"    開口：{a['label']} → {act_json(obj)}" + ("" if a.get("open", True) else f"（✗ {a.get('why', '')}）"))
    if not full:
        out.append(f"（列出最熟的 14 人和能開口的人，共 {len(s.get('people', []))} 人；people --full 看全部）")
    return "\n".join(out)


def render_contest(s):
    out = []
    c = s.get("contest") or {}
    out.append("―― 村裡在爭什麼 ――")
    for it in c.get("items", []):
        ws = "、".join(sorted({w["house"] for w in it.get("wanters", [])}))
        ids = (f"parcel={it['parcel']} " if it.get("parcel", -1) >= 0 else "") + (f"want={it['mine']}（你家想要） " if it.get("mine", -1) >= 0 else "")
        out.append(f"  {ids}{it['thing']}（{it['holder']}）想要的：{ws or '沒人'}")
    for o in c.get("offices", []):
        who = f"{o['holder']}，到 {o['until']} 年" if o.get("holder") else "空著"
        if o.get("wanted"):
            tail = f"（你家想當，出來選的是{o.get('runner', '')}；不想了：{act_json({'do': 'want', 'what': 'drop', 'object': o.get('want', -1)})}）"
        elif o.get("can"):
            men = o.get("men", [])
            if len(men) > 1:
                tail = "（可以爭，讓誰出來選：" + "；".join(f"{m['name']} {act_json({'do': 'want', 'what': 'office:' + o['id'], 'object': m['id']})}" for m in men) + "）"
            else:
                tail = f"（可以爭：{act_json({'do': 'want', 'what': 'office:' + o['id'], 'object': -1})}）"
        else:
            tail = "" if o.get("mine") else f"（爭不了：{o.get('why', '')}）"
        if o.get("mine"):
            tail = "（你家的，到期自動出來連任）" + tail
        gates = "　".join(("✓ " if g.get("ok") else "✗ ") + (g.get("detail") or g.get("label", "")) for g in o.get("gates", []))
        out.append(f"  {o['name']}：{who}{tail}" + (f"\n    條件：{gates}" if gates and not o.get("mine") else ""))
        if o.get("voters"):
            ours = sum(1 for x in o["voters"] if x.get("ours"))
            out.append(f"    各家家主（照現在看，{ours}/{len(o['voters'])} 家會投你家；開口拉票要一段空閒）：")
            for x in o["voters"]:
                t = f"      [{x['id']}] {x['name']}：看樣子投 {x.get('lean') or '?'}"
                if x.get("promised"):
                    t += f"（答應了 {x['promised']}）"
                t += f"；{x.get('note', '')}"
                a = x.get("ask")
                if a:
                    press = {"do": "ask", "who": x["id"], "ask": a["ask"], "object": a["object"], "subject": a["subject"], "office": a.get("office", ""), "lever": ""}
                    t += f" → {act_json(press)}" if a.get("open") else f"（✗ {a.get('why', '')}）"
                out.append(t)
    for pst in c.get("past", [])[:4]:
        out.append(f"  {pst['year']} {pst['thing']}：{pst['outcome']}")
    return "\n".join(out)


def wait_ask(prev_seq):
    v = get(f"/api/view?seq={prev_seq}&line=0&entry=0")
    deadline = time.time() + 60
    while not v.get("ask") and not v.get("ended") and time.time() < deadline:
        v = get(f"/api/view?seq={v['seq']}&line=0&entry=0")
    return v


def now():
    v = get("/api/view?seq=-1&line=0&entry=0")
    if not v.get("ask") and not v.get("ended"):
        v = wait_ask(v["seq"])
    return v


SEASON_DO = ("agenda", "interact", "ask", "plan", "cancel", "want", "focus", "rule", "pass", "year", "regency", "quit")
cmd = args[0] if args else "view"
if cmd == "view":
    print(render(now()))
elif cmd == "act":
    v0 = get("/api/view?seq=-1&line=0&entry=0")
    obj = json.loads(args[1])
    # The game ignores an action that does not fit what it asked, and the page would be left with no question;
    # refuse it here and show the question again.
    a, do = v0.get("ask"), obj.get("do")
    bad = ""
    if not a:
        bad = "現在沒有在問你（遊戲還在跑或已結束），先 view。"
    elif a.get("kind") == "choice" and not (do == "ai" or (do == "choose" and 0 <= int(obj.get("index", -1)) < len(a.get("options", [])))):
        bad = "現在是在問一個問題：只能 choose（index 在選項範圍內）或 ai。"
    elif a.get("kind") == "season" and do in ("choose", "ai"):
        bad = "現在是一季的空閒：choose / ai 只能用在問題上。"
    elif a.get("kind") == "season" and do not in SEASON_DO:
        bad = f"不認得「{do}」這個動作（這一季能用的：{'、'.join(SEASON_DO)}）。"
    if bad:
        if v0.get("state"):
            v0["state"].pop("notice", None)  # what the game refused before is not this refusal
        print("不行：" + bad + "\n")
        print(render(v0))
        sys.exit(1)
    post("/api/act", obj)
    print(render(wait_ask(v0["seq"])))
elif cmd == "more":
    print(render_more(now().get("state") or {}))
elif cmd == "people":
    print(render_people(now().get("state") or {}, full="--full" in args))
elif cmd == "contest":
    print(render_contest(now().get("state") or {}))
elif cmd == "chronicle":
    v = now()
    n = int(args[1]) if len(args) > 1 and args[1].isdigit() else None
    print("\n".join(chronicle_lines(v, count=n)[0]))
elif cmd == "talk":
    s = now().get("state") or {}
    print("\n".join(talk_lines(s, everything=True)) if s.get("date") else "（還沒有）")
elif cmd == "plans" or (cmd == "look" and len(args) > 1 and args[1].startswith("plan:")):
    # Every plan, each condition met (✓) or not (✗): the same checks the game makes before it lets one start.
    s = get("/api/view?seq=-1&line=0&entry=0").get("state") or {}
    want = args[1][5:] if cmd == "look" else ""
    shown = [p for p in s.get("plans", []) if not want or p["id"] == want]
    if not shown:
        print("沒有這個計畫（或這時候不能定計畫）。" if want else "這時候不能定計畫。")
        sys.exit(1)
    for p in shown:
        state = "能開始" if p.get("open") else "現在不行：" + p.get("why", "")
        print(f"{'★' if p.get('ambition') else ''}{p['id']}「{p['name']}」{state}")
        print("    " + "　".join(("✓ " if g.get("ok") else "✗ ") + (g.get("detail") or g.get("label", "")) for g in p.get("gates", [])))
        if p.get("open"):
            tg = "；".join(f"{t['label']} → {act_json({'do': 'plan', 'plan': p['id'], 'target': t['target'], 'subject': t['subject']})}" for t in p.get("targets", [])[:8])
            print(f"    對象：{tg}")
elif cmd == "look":
    if len(args) < 2 or not args[1].lstrip("-").isdigit():
        print("用法：textplay.py look <人的編號>（編號是家人欄或名單裡 [ ] 中的數字），或 look plan:<計畫 id>")
        sys.exit(1)
    who = int(args[1])
    r = post("/api/look", {"do": "inspect", "who": who})
    print(r.get("text", json.dumps(r, ensure_ascii=False)))
    # What can be done with them this season (the page's buttons on their card).
    s = get("/api/view?seq=-1&line=0&entry=0").get("state") or {}
    for p in (s.get("family") or []) + (s.get("people") or []):
        if p["id"] == who:
            for x in p.get("actions") or []:
                print(f"能做：{x['title']} → {act_json({'do': 'interact', 'who': who, 'event': x['event']})}"
                      + (" ★對方正希望" if x.get("wanted") else "") + ("" if x.get("open", True) else f"（✗ {x.get('why', '')}）"))
elif cmd == "report":
    r = post("/api/look", {"do": "report"})
    print(r.get("text", json.dumps(r, ensure_ascii=False)))
else:
    print(__doc__)
    sys.exit(0 if cmd in ("-h", "--help", "help") else 1)
