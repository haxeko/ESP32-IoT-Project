#!/usr/bin/env python3
"""Small same-origin configuration API for KEN Dashboard."""

import json
import os
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CONFIG_PATH = os.environ.get("KEN_CONFIG_PATH", "/var/lib/ken-dashboard/config.json")
MAX_BODY = 2 * 1024 * 1024
LOCK = threading.Lock()


def read_store():
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as handle:
            value = json.load(handle)
            if isinstance(value, dict):
                return {"version": int(value.get("version", 0)), "data": value.get("data")}
    except (FileNotFoundError, ValueError, OSError, TypeError):
        pass
    return {"version": 0, "data": None}


def write_store(value):
    directory = os.path.dirname(CONFIG_PATH)
    os.makedirs(directory, mode=0o750, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="config-", suffix=".json", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, ensure_ascii=False, separators=(",", ":"))
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary, 0o640)
        os.replace(temporary, CONFIG_PATH)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


class Handler(BaseHTTPRequestHandler):
    server_version = "KenConfig/1.0"

    def reply(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/") != "/api/config":
            self.reply(404, {"error": "not_found"})
            return
        with LOCK:
            self.reply(200, read_store())

    def do_PUT(self):
        if self.path.rstrip("/") != "/api/config":
            self.reply(404, {"error": "not_found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > MAX_BODY:
                raise ValueError("invalid_size")
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            expected = int(request.get("version", -1))
            data = request.get("data")
            if not isinstance(data, dict):
                raise ValueError("invalid_data")
        except (ValueError, TypeError, json.JSONDecodeError, UnicodeDecodeError):
            self.reply(400, {"error": "invalid_request"})
            return
        with LOCK:
            current = read_store()
            if expected != current["version"]:
                self.reply(409, current)
                return
            updated = {"version": current["version"] + 1, "data": data}
            write_store(updated)
            self.reply(200, updated)

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args), flush=True)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 8091), Handler).serve_forever()
