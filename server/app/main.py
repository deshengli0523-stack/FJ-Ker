import asyncio
import secrets

from fastapi import Depends, FastAPI, File, Header, HTTPException, UploadFile
from fastapi.responses import Response

from .jobs import registry
from .llm_qwen import answer_question
from .render import PAGE_BYTES, render_answer_pages
from .settings import get_settings


VERSION = "0.1.0"

app = FastAPI(title="FJ-ker PC 服务端", version=VERSION)
_job_queue = None
_worker_task = None


def require_api_token(
    x_fj_ker_token: str | None = Header(default=None, alias="X-FJ-KER-TOKEN"),
) -> None:
    settings = get_settings()
    api_token = getattr(settings, "api_token", "")
    if not api_token:
        return
    if x_fj_ker_token is None or not secrets.compare_digest(x_fj_ker_token, api_token):
        raise HTTPException(status_code=401, detail="缺少或无效的设备访问 Token")


@app.get("/health")
async def health() -> dict[str, object]:
    return {"ok": True, "version": VERSION}


@app.post("/jobs")
async def create_job(
    image: UploadFile = File(...),
    _: None = Depends(require_api_token),
) -> dict[str, str]:
    settings = get_settings()
    if image.content_type not in {"image/jpeg", "image/jpg"}:
        raise HTTPException(status_code=415, detail="上传的图片必须是 JPEG 格式")

    image_jpeg = await image.read(settings.max_upload_bytes + 1)
    if len(image_jpeg) > settings.max_upload_bytes:
        raise HTTPException(status_code=413, detail="上传的图片过大")
    if not image_jpeg.startswith(b"\xff\xd8"):
        raise HTTPException(status_code=400, detail="图片内容不像 JPEG 文件")

    job = await registry.create(image_jpeg)
    await _enqueue_job(job.job_id, image_jpeg)
    return {"job_id": job.job_id, "status": "queued"}


@app.get("/jobs/{job_id}")
async def get_job(
    job_id: str,
    _: None = Depends(require_api_token),
) -> dict[str, object]:
    job = await registry.get(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="未找到任务")
    return {
        "status": job.status,
        "pages": len(job.pages),
        "error": job.error,
    }


@app.get("/jobs/{job_id}/pages/{index}")
async def get_page(
    job_id: str,
    index: int,
    _: None = Depends(require_api_token),
) -> Response:
    job = await registry.get(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="未找到任务")
    if job.status != "ready":
        raise HTTPException(status_code=409, detail="任务尚未就绪")
    if index < 0 or index >= len(job.pages):
        raise HTTPException(status_code=404, detail="未找到页面")
    page = job.pages[index]
    if len(page) != PAGE_BYTES:
        raise HTTPException(status_code=500, detail="页面大小无效")
    return Response(content=page, media_type="application/octet-stream")


@app.delete("/jobs/{job_id}")
async def delete_job(
    job_id: str,
    _: None = Depends(require_api_token),
) -> dict[str, str]:
    cancelled = await registry.cancel(job_id)
    if not cancelled:
        raise HTTPException(status_code=404, detail="未找到任务")
    return {"job_id": job_id, "status": "cancelled"}


async def _enqueue_job(job_id: str, image_jpeg: bytes) -> None:
    import asyncio

    global _job_queue, _worker_task
    if _job_queue is None:
        _job_queue = asyncio.Queue()
    if _worker_task is None or _worker_task.done():
        _worker_task = asyncio.create_task(_worker_loop())
    await _job_queue.put((job_id, image_jpeg))


async def _worker_loop() -> None:
    import asyncio

    while True:
        job_id, image_jpeg = await _job_queue.get()
        task = None
        try:
            if not await registry.mark_processing(job_id):
                continue
            task = asyncio.create_task(_process_job(job_id, image_jpeg))
            await registry.attach_task(job_id, task)
            await task
        except asyncio.CancelledError:
            current = asyncio.current_task()
            if current is not None and current.cancelling():
                raise
            continue
        except Exception as exc:
            await registry.mark_error(job_id, str(exc))
        finally:
            _job_queue.task_done()


async def _process_job(job_id: str, image_jpeg: bytes) -> None:
    try:
        answer = await answer_question(image_jpeg)
        pages = await asyncio.to_thread(
            render_answer_pages,
            answer,
            max_pages=get_settings().max_pages,
        )
        await registry.mark_ready(job_id, pages)
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        await registry.mark_error(job_id, str(exc))
