import asyncio
from types import SimpleNamespace
import threading
import time
from io import BytesIO

from fastapi.testclient import TestClient
from PIL import Image

from app.render import PAGE_BYTES
from app.main import app


def _fixture_jpeg() -> bytes:
    image = Image.new("RGB", (32, 32), "white")
    buffer = BytesIO()
    image.save(buffer, format="JPEG")
    return buffer.getvalue()


def _mock_render(monkeypatch):
    monkeypatch.setattr(
        "app.main.render_answer_pages",
        lambda answer, max_pages=20: [bytes(PAGE_BYTES)],
    )


def test_job_protocol_returns_ready_page(monkeypatch):
    async def fake_answer_question(image_jpeg: bytes) -> str:
        assert image_jpeg.startswith(b"\xff\xd8")
        return "答案：$\\frac{1}{2}$"

    monkeypatch.setattr("app.main.answer_question", fake_answer_question)
    _mock_render(monkeypatch)
    client = TestClient(app)

    response = client.post(
        "/jobs",
        files={"image": ("question.jpg", _fixture_jpeg(), "image/jpeg")},
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["status"] == "queued"
    assert payload["job_id"]

    job_id = payload["job_id"]
    status = None
    for _ in range(50):
        status_response = client.get(f"/jobs/{job_id}")
        assert status_response.status_code == 200
        status = status_response.json()
        if status["status"] == "ready":
            break
        time.sleep(0.05)

    assert status is not None
    assert status["status"] == "ready"
    assert status["pages"] >= 1

    page_response = client.get(f"/jobs/{job_id}/pages/0")
    assert page_response.status_code == 200
    assert page_response.headers["content-type"] == "application/octet-stream"
    assert len(page_response.content) == PAGE_BYTES


def test_delete_job_cancels_or_removes_job(monkeypatch):
    async def fake_answer_question(image_jpeg: bytes) -> str:
        return "答案"

    monkeypatch.setattr("app.main.answer_question", fake_answer_question)
    _mock_render(monkeypatch)
    client = TestClient(app)

    created = client.post(
        "/jobs",
        files={"image": ("question.jpg", _fixture_jpeg(), "image/jpeg")},
    )
    job_id = created.json()["job_id"]

    deleted = client.delete(f"/jobs/{job_id}")

    assert deleted.status_code == 200
    assert deleted.json()["status"] in {"cancelled", "deleted"}


def test_rejects_non_jpeg_upload():
    client = TestClient(app)

    response = client.post(
        "/jobs",
        files={"image": ("question.txt", b"not an image", "text/plain")},
    )

    assert response.status_code == 415


def test_health_does_not_require_api_token(monkeypatch):
    monkeypatch.setattr(
        "app.main.get_settings",
        lambda: SimpleNamespace(max_upload_bytes=2_000_000, max_pages=20, api_token="secret"),
    )
    client = TestClient(app)

    response = client.get("/health")

    assert response.status_code == 200


def test_job_routes_require_api_token_when_configured(monkeypatch):
    monkeypatch.setattr(
        "app.main.get_settings",
        lambda: SimpleNamespace(max_upload_bytes=2_000_000, max_pages=20, api_token="secret"),
    )
    client = TestClient(app)

    missing = client.get("/jobs/abcd")
    wrong = client.get("/jobs/abcd", headers={"X-FJ-KER-TOKEN": "wrong"})
    accepted = client.get("/jobs/abcd", headers={"X-FJ-KER-TOKEN": "secret"})

    assert missing.status_code == 401
    assert wrong.status_code == 401
    assert accepted.status_code == 404


def test_rejects_upload_larger_than_configured_limit(monkeypatch):
    monkeypatch.setattr(
        "app.main.get_settings",
        lambda: SimpleNamespace(max_upload_bytes=4, max_pages=20),
    )
    client = TestClient(app)

    response = client.post(
        "/jobs",
        files={"image": ("question.jpg", b"\xff\xd8" + b"x" * 8, "image/jpeg")},
    )

    assert response.status_code == 413


def test_cancelling_processing_job_does_not_stall_queued_job(monkeypatch):
    first_job_started = threading.Event()

    async def fake_answer_question(image_jpeg: bytes) -> str:
        if image_jpeg.endswith(b"first"):
            first_job_started.set()
            await asyncio.sleep(30)
        return "答案：继续处理下一题"

    monkeypatch.setattr("app.main.answer_question", fake_answer_question)
    _mock_render(monkeypatch)
    client = TestClient(app)

    first = client.post(
        "/jobs",
        files={"image": ("first.jpg", _fixture_jpeg() + b"first", "image/jpeg")},
    ).json()["job_id"]

    for _ in range(50):
        first_status = client.get(f"/jobs/{first}").json()["status"]
        if first_status == "processing":
            break
        time.sleep(0.02)
    assert first_status == "processing"
    assert first_job_started.wait(timeout=2)

    second = client.post(
        "/jobs",
        files={"image": ("second.jpg", _fixture_jpeg() + b"second", "image/jpeg")},
    ).json()["job_id"]

    deleted = client.delete(f"/jobs/{first}")
    assert deleted.status_code == 200

    second_status = None
    for _ in range(50):
        second_status = client.get(f"/jobs/{second}").json()["status"]
        if second_status == "ready":
            break
        time.sleep(0.05)

    assert second_status == "ready"
