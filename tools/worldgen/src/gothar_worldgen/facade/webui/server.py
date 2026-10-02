"""HTTP server for the facade web UI (stdlib only, bound to 127.0.0.1).

Routes:
  GET  /                                   page (static/index.html)
  GET  /static/<app.js|style.css>          fixed allow-list, nothing else is served from disk
  GET  /api/buildings                      summary: footprints, status, camera positions
  GET  /api/buildings/<id>                 edges with candidate images, current override
  GET  /api/facade/<id>/<edge>?capture=<name>&frame=<index>&px=<px per m>   rectified PNG
  GET  /api/override/<id>                  override JSON (404 if none)
  PUT  /api/override/<id>                  validate + save override JSON
  GET  /api/vocabulary                     choices for style, timber, infill, roof cover

Requests with a foreign Host header (DNS rebinding) or, for PUT, a foreign Origin are refused.
Paths containing ``..``, backslashes or percent-encoded path characters are refused before routing.
"""

from __future__ import annotations

import json
import sys
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, TextIO
from urllib.parse import parse_qs, unquote, urlsplit

from gothar_worldgen.facade.webui.api import ApiError, Workspace

STATIC_DIR = Path(__file__).resolve().parent / "static"
STATIC_FILES = {
    "index.html": "text/html; charset=utf-8",
    "app.js": "text/javascript; charset=utf-8",
    "style.css": "text/css; charset=utf-8",
}
MAX_BODY = 1 << 20  # 1 MiB; overrides are a few KB
MAX_DRAIN = 16 << 20  # unread request bodies up to this size are discarded before answering


def _bad_path(raw_path: str) -> bool:
    """True for traversal attempts in any spelling: dot segments, backslash, encoded variants."""
    lowered = raw_path.lower()
    if "\\" in raw_path or "%5c" in lowered or "%2f" in lowered or "%00" in lowered:
        return True
    decoded = unquote(raw_path)
    if "%" in decoded:  # double encoding
        return True
    return any(seg in (".", "..") for seg in decoded.split("/"))


class _Handler(BaseHTTPRequestHandler):
    server: UiServer  # type: ignore[assignment]
    server_version = "gothar-facade-ui"
    sys_version = ""

    # --- plumbing -------------------------------------------------------------------------

    def log_message(self, format: str, *args: Any) -> None:  # noqa: A002, ANN401
        if self.server.log is not None:
            print(f"  {self.address_string()} {format % args}", file=self.server.log)

    def _drain(self) -> None:
        """Discard an unread request body, so early error answers do not abort the connection."""
        if getattr(self, "_body_read", False):
            return
        self._body_read = True
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if length > MAX_DRAIN:
            self.close_connection = True
            return
        while length > 0:
            chunk = self.rfile.read(min(length, 65536))
            if not chunk:
                break
            length -= len(chunk)

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self._drain()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, status: int, value: Any) -> None:  # noqa: ANN401
        self._send(
            status, json.dumps(value, ensure_ascii=False).encode("utf-8"), "application/json"
        )

    def _error(self, status: int, message: str) -> None:
        self._json(status, {"error": message})

    def _host_ok(self) -> bool:
        port = self.server.server_address[1]
        return self.headers.get("Host", "") in {f"127.0.0.1:{port}", f"localhost:{port}"}

    def _origin_ok(self) -> bool:
        origin = self.headers.get("Origin")
        if origin is None:
            return True  # same-origin fetch from older browsers / CLI tools
        port = self.server.server_address[1]
        return origin in {f"http://127.0.0.1:{port}", f"http://localhost:{port}"}

    def _guarded(self, method: str) -> None:
        self._body_read = False
        try:
            if not self._host_ok():
                self._error(403, "foreign host")
                return
            if _bad_path(self.path):
                self._error(400, "invalid path")
                return
            if method != "GET" and not self._origin_ok():
                self._error(403, "foreign origin")
                return
            self._route(method)
        except ApiError as e:
            self._error(e.status, str(e))
        except Exception:  # noqa: BLE001 - never leak a stack trace to the browser
            traceback.print_exc(file=sys.stderr)
            self._error(500, "internal error (see the server console)")

    def do_GET(self) -> None:  # noqa: N802
        self._guarded("GET")

    def do_HEAD(self) -> None:  # noqa: N802
        self._guarded("GET")

    def do_PUT(self) -> None:  # noqa: N802
        self._guarded("PUT")

    def do_POST(self) -> None:  # noqa: N802
        self._guarded("POST")

    def do_DELETE(self) -> None:  # noqa: N802
        self._guarded("DELETE")

    # --- routing --------------------------------------------------------------------------

    def _read_json(self) -> Any:  # noqa: ANN401
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
            raise ApiError(415, "expected application/json")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            raise ApiError(400, "invalid Content-Length") from None
        if length <= 0 or length > MAX_BODY:
            raise ApiError(413, "request body empty or too large")
        self._body_read = True
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise ApiError(400, "invalid JSON") from None

    def _route(self, method: str) -> None:
        if method not in ("GET", "PUT"):
            raise ApiError(405, "method not allowed")
        url = urlsplit(self.path)
        parts = [unquote(p) for p in url.path.split("/") if p]
        query = parse_qs(url.query)
        ws = self.server.workspace

        if method == "GET" and parts in ([], ["index.html"]):
            return self._static("index.html")
        if method == "GET" and len(parts) == 2 and parts[0] == "static":
            return self._static(parts[1])
        if not parts or parts[0] != "api":
            raise ApiError(404, "not found")
        api = parts[1:]
        if method == "GET" and api == ["buildings"]:
            return self._json(200, ws.summary())
        if method == "GET" and api == ["vocabulary"]:
            return self._json(200, ws.vocabulary())
        if method == "GET" and len(api) == 2 and api[0] == "buildings":
            return self._json(200, ws.detail(api[1]))
        if method == "GET" and len(api) == 3 and api[0] == "facade":
            png = ws.facade_png(
                api[1],
                _int(api[2], "edge"),
                _one(query, "capture"),
                _int(_one(query, "frame"), "frame"),
                _int(_one(query, "px", "50"), "px"),
            )
            return self._send(200, png, "image/png")
        if len(api) == 2 and api[0] == "override":
            if method == "GET":
                override = ws.get_override(api[1])
                if override is None:
                    raise ApiError(404, "no override yet")
                return self._json(200, override)
            return self._json(200, ws.put_override(api[1], self._read_json()))
        raise ApiError(404, "not found")

    def _static(self, name: str) -> None:
        content_type = STATIC_FILES.get(name)
        if content_type is None:
            raise ApiError(404, "not found")
        self._send(200, (STATIC_DIR / name).read_bytes(), content_type)


def _one(query: dict[str, list[str]], key: str, default: str | None = None) -> str:
    values = query.get(key)
    if values:
        return values[0]
    if default is None:
        raise ApiError(400, f"missing parameter '{key}'")
    return default


def _int(value: str, what: str) -> int:
    try:
        return int(value)
    except ValueError:
        raise ApiError(400, f"invalid {what}") from None


class UiServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, workspace: Workspace, port: int = 0, log: TextIO | None = None) -> None:
        super().__init__(("127.0.0.1", port), _Handler)
        self.workspace = workspace
        self.log = log

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.server_address[1]}/"
