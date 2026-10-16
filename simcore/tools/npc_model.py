#!/usr/bin/env python3
"""Decides for one villager in the spotlight, for `play --npc-model "python3 tools/npc_model.py"`.

Reads one request as JSON on stdin: {"who", "title", "situation", "mind", "options": [...]}.
Prints one line: "<option index>\t<reason>", or "-1" to let SimCore's rules decide.

The rules have already listed only what this person may do; the model only picks among them and
says why, in their own voice. Without credentials (or with the anthropic package missing) it prints
-1, so a game always runs. NPC_MODEL_FAKE=1 picks the first option, for testing the plumbing.
"""
import json
import os
import sys

SYSTEM = (
    "你在扮演一個 14 世紀英格蘭村莊裡的村民，替他／她做一個決定。"
    "用那個時代的眼光和價值觀來想：名聲、土地、家族、領主、教會、記仇和報恩都比現代人看得重，"
    "現代人覺得不對的事，當時的人可能覺得理所當然。照這個人的個性、處境和他對別人的感情來選，"
    "不要替他做一個「最好」的選擇，要做一個「像他」的選擇。"
    "選項都是這個人做得到的；只能從裡面挑一個。"
    "reason 用這個人的口吻寫一句心裡話（繁體中文，30 字以內，不要提到選項編號）。"
)

SCHEMA = {
    "type": "object",
    "properties": {
        "choice": {"type": "integer"},
        "reason": {"type": "string"},
    },
    "required": ["choice", "reason"],
    "additionalProperties": False,
}


def main() -> None:
    req = json.load(sys.stdin)
    options = req.get("options", [])
    if len(options) < 2:
        print("-1")
        return
    if os.environ.get("NPC_MODEL_FAKE") == "1":
        print("0\t（測試）照第一個選項")
        return
    try:
        import anthropic
    except ImportError:
        print("-1")
        return

    listed = "\n".join(f"{i}. {o}" for i, o in enumerate(options))
    user = (
        f"這個人：{req.get('mind', '')}\n\n"
        f"眼前的事（{req.get('title', '')}）：{req.get('situation', '')}\n\n"
        f"能做的選擇（編號從 0 開始）：\n{listed}"
    )
    try:
        client = anthropic.Anthropic()
        response = client.beta.messages.create(
            model=os.environ.get("NPC_MODEL", "claude-opus-5-5"),
            max_tokens=2000,
            system=SYSTEM,
            messages=[{"role": "user", "content": user}],
            output_config={"effort": "low", "format": {"type": "json_schema", "schema": SCHEMA}},
            betas=["server-side-fallback-2026-07-01"],
            fallbacks="default",
        )
    except anthropic.APIConnectionError:
        print("-1")
        return
    except anthropic.APIStatusError:
        print("-1")
        return
    if response.stop_reason == "refusal":
        print("-1")
        return
    text = next((b.text for b in response.content if b.type == "text"), "")
    try:
        data = json.loads(text)
        choice = int(data["choice"])
        reason = str(data.get("reason", "")).replace("\t", " ").replace("\n", " ").strip()
    except (ValueError, KeyError, TypeError):
        print("-1")
        return
    if not 0 <= choice < len(options):
        print("-1")
        return
    print(f"{choice}\t{reason}")


if __name__ == "__main__":
    main()
