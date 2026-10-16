#!/usr/bin/env python3
"""Stand-in for tools/npc_decider.py in tests: picks an option from a hash of the request, so it is
deterministic and needs no network. Same request and answer format as the real decider."""
import hashlib
import json
import sys

req = json.load(sys.stdin)
n = len(req.get("options", []))
if n < 2:
    print("-1")
else:
    h = hashlib.sha256(json.dumps(req, sort_keys=True, ensure_ascii=False).encode("utf-8")).digest()
    print(f"{h[0] % n}\t")
