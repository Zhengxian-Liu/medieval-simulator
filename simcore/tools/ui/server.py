#!/usr/bin/env python3
"""Serves the browser version of the game (tools/ui/index.html) on this computer and runs build/webplay behind it.

    python3 tools/ui/server.py [--port 8765] [--no-browser] -- <webplay arguments>

The game speaks one line of JSON per message (see tools/webplay.cpp). This server keeps what the page needs to
draw itself: the latest situation, what is being asked, the whole chronicle so far, so reloading the page loses
nothing. Python standard library only.
"""
import json
import os
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))


class Game:
    def __init__(self, cmd):
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
        self.cond = threading.Condition()
        self.seq = 0            # bumps on every change the page should redraw for
        self.state = None       # the latest situation (chronicle lines taken out)
        self.ask = None         # what the game is waiting for, or None while it runs
        self.chronicle = []     # every line so far
        self.entries = []       # every chronicle entry so far, by season (design/ui-text.md)
        self.ended = None
        self.replies = []       # answers to looks (a person, the family's knowledge), in order
        self.write_lock = threading.Lock()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            with self.cond:
                kind = msg.get("type")
                if kind == "state":
                    self.chronicle.extend(msg.pop("chronicle", []))
                    self.entries.extend(msg.pop("entries", []))
                    self.state = msg
                elif kind == "ask":
                    self.ask = msg
                elif kind in ("person", "report"):
                    self.replies.append(msg)
                elif kind == "end":
                    self.ended = msg
                    self.ask = None
                self.seq += 1
                self.cond.notify_all()
        with self.cond:
            if self.ended is None:
                self.ended = {"type": "end", "crashed": True}
            self.ask = None
            self.seq += 1
            self.cond.notify_all()

    def view(self, since_seq, since_line, wait, since_entry=0):
        with self.cond:
            if wait:
                self.cond.wait_for(lambda: self.seq > since_seq, timeout=25)
            return {
                "seq": self.seq,
                "state": self.state,
                "ask": self.ask,
                "ended": self.ended,
                "lines": self.chronicle[since_line:],
                "line_count": len(self.chronicle),
                "entries": self.entries[since_entry:],
                "entry_count": len(self.entries),
            }

    def send(self, obj):
        with self.write_lock:
            if self.proc.poll() is not None:
                return False
            with self.cond:
                if obj.get("do") not in ("inspect", "report"):
                    self.ask = None  # answered: the game runs until it asks again
                    self.seq += 1
                    self.cond.notify_all()
            self.proc.stdin.write(json.dumps(obj, ensure_ascii=False) + "\n")
            self.proc.stdin.flush()
            return True

    def look(self, obj):
        """A look is answered at once, without the game moving on."""
        with self.cond:
            n = len(self.replies)
        if not self.send(obj):
            return None
        with self.cond:
            self.cond.wait_for(lambda: len(self.replies) > n or self.ended is not None, timeout=10)
            return self.replies[n] if len(self.replies) > n else None


def make_handler(game):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def _send(self, code, body, ctype="application/json; charset=utf-8"):
            data = body if isinstance(body, bytes) else body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            url = urlparse(self.path)
            if url.path in ("/", "/index.html"):
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    return self._send(200, f.read(), "text/html; charset=utf-8")
            if url.path == "/api/view":
                q = parse_qs(url.query)
                seq = int(q.get("seq", ["-1"])[0])
                line = int(q.get("line", ["0"])[0])
                entry = int(q.get("entry", ["0"])[0])
                return self._send(200, json.dumps(game.view(seq, line, wait=seq >= 0, since_entry=entry), ensure_ascii=False))
            self._send(404, "{}")

        def do_POST(self):
            url = urlparse(self.path)
            length = int(self.headers.get("Content-Length", "0"))
            try:
                obj = json.loads(self.rfile.read(length) or b"{}")
            except ValueError:
                return self._send(400, "{}")
            if url.path == "/api/act":
                return self._send(200, json.dumps({"ok": game.send(obj)}))
            if url.path == "/api/look":
                return self._send(200, json.dumps(game.look(obj) or {}, ensure_ascii=False))
            self._send(404, "{}")

    return Handler


def main():
    args = sys.argv[1:]
    port, open_browser = 8765, True
    if "--" in args:
        cut = args.index("--")
        ours, game_args = args[:cut], args[cut + 1:]
    else:
        ours, game_args = args, []
    i = 0
    while i < len(ours):
        if ours[i] == "--port":
            port = int(ours[i + 1])
            i += 1
        elif ours[i] == "--no-browser":
            open_browser = False
        i += 1
    root = os.path.dirname(os.path.dirname(HERE))
    game = Game([os.path.join(root, "build", "webplay"), "--data", os.path.join(root, "data")] + game_args)
    server = None
    for p in range(port, port + 20):
        try:
            server = ThreadingHTTPServer(("127.0.0.1", p), make_handler(game))
            port = p
            break
        except OSError:
            continue
    if server is None:
        print("找不到可用的連接埠")
        return 1
    url = f"http://127.0.0.1:{port}/"
    print(f"遊戲開在 {url}（關掉這個視窗或按 Ctrl+C 結束）", flush=True)
    if open_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        game.send({"do": "quit"})
        try:
            game.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            game.proc.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
