import http.client
import json
import threading
from collections.abc import Iterator
from pathlib import Path

import pytest

from gothar_worldgen.facade.webui.server import UiServer, _bad_path

from .webui_fixture import make_site, override


@pytest.fixture
def server(tmp_path: Path) -> Iterator[UiServer]:
    srv = UiServer(make_site(tmp_path), port=0)
    thread = threading.Thread(target=srv.serve_forever, args=(0.05,), daemon=True)
    thread.start()
    yield srv
    srv.shutdown()
    srv.server_close()


def request(
    srv: UiServer, method: str, path: str, body: object = None, headers: dict | None = None
) -> tuple[int, dict, bytes]:
    conn = http.client.HTTPConnection("127.0.0.1", srv.server_address[1], timeout=10)
    hdrs = dict(headers or {})
    data = None
    if body is not None:
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        hdrs.setdefault("Content-Type", "application/json")
    conn.request(method, path, body=data, headers=hdrs)
    res = conn.getresponse()
    out = res.status, dict(res.getheaders()), res.read()
    conn.close()
    return out


def test_page_and_static_files(server: UiServer):
    status, headers, body = request(server, "GET", "/")
    assert status == 200 and b"Fassaden-Werkzeug" in body
    assert headers["Content-Type"].startswith("text/html")
    assert request(server, "GET", "/static/app.js")[0] == 200
    assert request(server, "GET", "/static/style.css")[0] == 200
    assert request(server, "GET", "/static/server.py")[0] == 404  # allow-list only
    assert request(server, "GET", "/nope")[0] == 404


def test_api_round_trip(server: UiServer):
    status, _, body = request(server, "GET", "/api/buildings")
    assert status == 200 and len(json.loads(body)["buildings"]) == 2
    status, _, body = request(server, "GET", "/api/buildings/B1")
    assert json.loads(body)["edges"][0]["candidates"][0]["index"] == 0
    status, headers, png = request(server, "GET", "/api/facade/B1/0?capture=cap1&frame=0&px=10")
    assert status == 200 and headers["Content-Type"] == "image/png" and png[:4] == b"\x89PNG"
    assert request(server, "GET", "/api/override/B1")[0] == 404
    origin = f"http://127.0.0.1:{server.server_address[1]}"
    status, _, body = request(server, "PUT", "/api/override/B1", override(), {"Origin": origin})
    assert status == 200 and json.loads(body)["status"] == "annotated"
    status, _, body = request(server, "GET", "/api/override/B1")
    assert json.loads(body)["frontFacade"]["edge"] == 0
    assert request(server, "GET", "/api/vocabulary")[0] == 200


@pytest.mark.parametrize(
    "path",
    [
        "/static/../server.py",
        "/static/%2e%2e/server.py",
        "/static/%2E%2E/%2E%2E/api.py",
        "/static/..%2fserver.py",
        "/static/..%5cserver.py",
        "/static\\..\\server.py",
        "/static/%252e%252e/server.py",
        "/api/buildings/..%2f..%2fbuildings.json",
        "/api/override/..",
        "/api/override/%2e%2e",
        "/api/override/a%5cb",
        "/api/override/a%00b",
        "/api/facade/B1/0?capture=..&frame=0",
        "/api/facade/B1/0?capture=%2e%2e%2fcap1&frame=0",
    ],
)
def test_traversal_is_refused(server: UiServer, path: str):
    status, _, body = request(server, "GET", path)
    assert status in (400, 404)
    assert b"def " not in body and b"import" not in body  # no source code leaked


@pytest.mark.parametrize("bad_id", ["a.b%2fc", "..%5c..", "x%2F..", "%2e"])
def test_put_with_path_characters_in_id(server: UiServer, bad_id: str):
    status, _, _ = request(server, "PUT", f"/api/override/{bad_id}", override(id=bad_id))
    assert status in (400, 404)


def test_foreign_host_and_origin_are_refused(server: UiServer):
    assert request(server, "GET", "/api/buildings", headers={"Host": "evil.example"})[0] == 403
    port = server.server_address[1]
    assert request(server, "GET", "/", headers={"Host": f"localhost:{port}"})[0] == 200
    status, _, _ = request(
        server, "PUT", "/api/override/B1", override(), {"Origin": "http://evil.example"}
    )
    assert status == 403
    assert not (server.workspace.overrides_dir / "B1.json").exists()


def test_put_body_checks(server: UiServer):
    assert request(server, "PUT", "/api/override/B1", b"{", {})[0] == 400
    big = json.dumps(override(notes="x" * (2 << 20))).encode()
    assert request(server, "PUT", "/api/override/B1", big, {})[0] == 413
    status, _, _ = request(
        server, "PUT", "/api/override/B1", b"id=B1", {"Content-Type": "text/plain"}
    )
    assert status == 415
    assert request(server, "POST", "/api/override/B1", override())[0] == 405
    assert request(server, "DELETE", "/api/override/B1")[0] == 405


def test_errors_have_no_stack_trace_or_local_paths(server: UiServer, tmp_path: Path):
    for path in ("/api/buildings/B9", "/api/facade/B1/2?capture=cap1&frame=0&px=10",
                 "/api/facade/B1/x?capture=cap1&frame=0", "/api/facade/B1/0?frame=0"):  # fmt: skip
        status, headers, body = request(server, "GET", path)
        assert status >= 400 and headers["Content-Type"] == "application/json"
        text = body.decode()
        assert "Traceback" not in text
        assert str(tmp_path) not in text and str(tmp_path).replace("\\", "\\\\") not in text
        assert set(json.loads(text)) == {"error"}


def test_internal_errors_are_generic(server: UiServer, monkeypatch, capsys):
    def boom() -> None:
        raise RuntimeError(f"secret {server.workspace.work_dir}")

    monkeypatch.setattr(server.workspace, "summary", boom)
    status, _, body = request(server, "GET", "/api/buildings")
    assert status == 500
    assert b"secret" not in body and b"Traceback" not in body


@pytest.mark.parametrize(
    ("path", "bad"),
    [("/static/app.js", False), ("/api/buildings/B1", False), ("/a/../b", True),
     ("/a/%2e/b", True), ("/a%5cb", True), ("/a/%25%32%65", True)],
)  # fmt: skip
def test_bad_path(path: str, bad: bool):
    assert _bad_path(path) is bad
