#!/usr/bin/env python3
"""Lets a decision model choose for one villager in the spotlight.

Use: `play --npc-model "python3 tools/npc_decider.py"`. The request and answer are the same as for
tools/npc_model.py. The request is one JSON object on stdin: {"who", "title", "situation", "mind",
"feelings", "options": [...], "rule_weights": [...]}. "feelings" says how the person feels about each
person the options touch; "rule_weights" is what the rules alone would weigh each option at. The answer
is one line: "<option index>\t<reason>", or "-1" to let SimCore's rules decide.

Default: Perplexity's pplx-decider (27B, Apache-2.0, fine-tuned from Qwen3.8-27B), through
OpenRouter's Decisions API. It needs an OpenRouter key: OPENROUTER_API_KEY from the environment, else from simcore/.env (one line
`OPENROUTER_API_KEY=...`; git ignores the file, see .env.example). This is the only place the key is
read; `npc_decider.py --has-key` (exit 0/1) is how the launchers ask. The full model is about 52 GB, more than a
16 GB Mac can hold, so it runs hosted. Input costs about $0.02–0.04 per million tokens and output is
free; a decision is a few hundred tokens and takes well under a second.
  DECIDER_MODEL  model id (default perplexity/pplx-decider-v1.1-27b, the hosted newer v1.1)
  DECIDER_URL    endpoint (default https://openrouter.ai/api/alpha/decisions)
Local alternative: run `tinyjev serve TinyJev-0.6B --quantize 8` and set
DECIDER_URL=http://127.0.0.1:8077/v1/systemone. TinyJev runs offline on a Mac, but it is tagged
English-only.

Decision models only choose; they write no reason, so the game shows no 心想 line for these choices.
They return a probability for each option. The first live test (stats/npc-decider-probe.md) found them
very sure of themselves: two-way choices often came back at 0.85 or more, so a greedy man and a kind man
chose alike, and liking the family barely changed what anyone did. So the model's probabilities are
softened (DECIDER_TEMP, default 1.6; 1 leaves them as they are) and then combined with the rules' own
weights, which carry traits and feelings (DECIDER_MIX, default 0.7 = the model's share of a weighted
geometric mean; 1 = model only). The second live test found that adding the two let options that fit
neither the model nor the person (a timid man leading a protest) keep 6–12%, so they are now multiplied,
and options the rules all but rule out are dropped (DECIDER_VETO); see blend().
We draw from the result instead of always taking the top one: a person does not always do the most
likely thing. The draw is seeded by the request, so the same situation gives the same answer.
DECIDER_TOP=1 takes the most likely option instead.
Any failure (no key, network, odd reply) prints -1, so a game always runs.
"""
import hashlib
import json
import os
import random
import sys
import urllib.error
import urllib.request

INSTRUCTIONS = (
    "This is a villager in 14th-century England (the text is in Chinese). Which of these would this "
    "person actually do? Judge by their character, their situation and above all how they feel about the "
    "people involved (see feelings): people seldom harm those they are close to, and readily harm those "
    "they hate, while content, pious, forgiving, kind or shy people usually swallow a grudge and get on "
    "with life. Use the values of their time, not modern ones. Pick what is like this person, not what is "
    "wisest or most common."
)


def request_body(req: dict, model: str) -> dict:
    body = {
        "state": {
            "person": req.get("mind", ""),
            "feelings": req.get("feelings", ""),
            "matter": req.get("title", ""),
            "situation": req.get("situation", ""),
        },
        "questions": {
            "act": {
                "type": "choice",
                "instructions": INSTRUCTIONS,
                "criteria": {f"o{i}": o for i, o in enumerate(req["options"])},
            }
        },
    }
    if model:
        body["model"] = model
    return body


def blend(model_probs: list, rule_weights: list, fallback: int = -1) -> list:
    """Softens the model's probabilities, then combines them with the rules' weights.

    The two are multiplied (a weighted geometric mean), not added: an option has to make sense to both
    the model and the rules to come out likely, so neither can push an out-of-character option far on
    its own. An option the rules give almost nothing (under DECIDER_VETO of the total, default 0.06) is
    dropped unless the model itself favours it (over 0.5), and one the model gives almost nothing (under
    DECIDER_MODEL_VETO, default 0.10) is dropped unless the rules favour it (over half). Exactly even does
    not save an option: the fourth live test found two-way rule ties (8:8) letting a kind man pick a fight
    the model gave 0.04. The do-nothing
    option ("fallback" in the request) is never dropped: the third live test lost half of all "過一天算一天"
    that way, leaving mild people only brooding or revenge.
    """
    temp = float(os.environ.get("DECIDER_TEMP", "1.6"))
    mix = float(os.environ.get("DECIDER_MIX", "0.7"))
    veto = float(os.environ.get("DECIDER_VETO", "0.06"))
    model_veto = float(os.environ.get("DECIDER_MODEL_VETO", "0.10"))
    soft = [p ** (1.0 / temp) if p > 0 else 0.0 for p in model_probs]
    total = sum(soft)
    if total <= 0:
        return []
    soft = [p / total for p in soft]
    rules = [max(0.0, float(w)) for w in rule_weights] if len(rule_weights) == len(soft) else []
    rt = sum(rules)
    if rt <= 0 or mix >= 1:
        return soft
    rules = [r / rt for r in rules]
    # When the model can hardly tell the options apart (two mild courtship gifts at 0.5/0.5), its say
    # shrinks and the person's traits, through the rules, break the tie.
    spread = max(soft) - min(soft)
    mix = mix * min(1.0, spread / 0.3)
    out = []
    for i, (raw, m, r) in enumerate(zip(model_probs, soft, rules)):
        # Either side can rule an option out: the rules when it barely fits the person, the model when it
        # barely fits the situation (the rules once lifted the model's 0.01 for a fight to 0.13).
        if i != fallback and ((r < veto and raw <= 0.5) or (raw < model_veto and r <= 0.5)):
            out.append(0.0)
        else:
            out.append(m ** mix * r ** (1 - mix))
    if sum(out) <= 0:
        return soft
    return out


ENV_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), ".env")


def api_key() -> str:
    """The OpenRouter key: the environment's, else simcore/.env's (it lasts across terminals and pulls)."""
    key = os.environ.get("OPENROUTER_API_KEY", "").strip()
    if key or os.environ.get("SIM_IGNORE_ENV_FILE"):
        return key
    try:
        with open(ENV_FILE, encoding="utf-8") as f:
            for line in f:
                name, sep, value = line.strip().removeprefix("export ").partition("=")
                if sep and name.strip() == "OPENROUTER_API_KEY":
                    return value.strip().strip("'\"")
    except OSError:
        pass
    return ""


def post(url: str, body: dict) -> dict:
    headers = {"content-type": "application/json"}
    key = api_key()
    if key and "openrouter.ai" in url:
        headers["authorization"] = "Bearer " + key
    r = urllib.request.Request(url, data=json.dumps(body).encode("utf-8"), headers=headers)
    with urllib.request.urlopen(r, timeout=30) as resp:
        return json.load(resp)


def answer_of(result: dict) -> dict:
    # OpenRouter puts answers at the top level; a local TinyJev server nests them under "states".
    if "answers" in result:
        return result["answers"]["act"]
    return result["states"][0]["answers"]["act"]


def main() -> None:
    if sys.argv[1:] == ["--has-key"]:
        sys.exit(0 if api_key() else 1)
    req = json.load(sys.stdin)
    options = req.get("options", [])
    if len(options) < 2:
        print("-1")
        return
    url = os.environ.get("DECIDER_URL", "https://openrouter.ai/api/alpha/decisions")
    hosted = "openrouter.ai" in url
    # No check for OPENROUTER_API_KEY here: some environments add the key at a network proxy, so
    # it is not in this process. Without a key the request fails and we print -1 below.
    model = os.environ.get("DECIDER_MODEL", "perplexity/pplx-decider-v1.1-27b" if hosted else "")
    try:
        probs = answer_of(post(url, request_body(req, model)))["probabilities"]
        keys = [f"o{i}" for i in range(len(options))]
        weights = [max(0.0, float(probs.get(k, 0.0))) for k in keys]
    except (urllib.error.URLError, OSError, ValueError, TypeError, KeyError, IndexError):
        print("-1")
        return
    weights = blend(weights, req.get("rule_weights", []), int(req.get("fallback", -1)))
    if not weights or sum(weights) <= 0:
        print("-1")
        return
    if os.environ.get("DECIDER_TOP") == "1":
        pick = max(range(len(keys)), key=lambda i: weights[i])
    else:
        seed = hashlib.sha256(json.dumps(req, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()
        pick = random.Random(seed).choices(range(len(keys)), weights=weights)[0]
    print(f"{pick}\t")


if __name__ == "__main__":
    main()
