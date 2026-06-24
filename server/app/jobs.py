import asyncio
import contextlib
import secrets
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from typing import Literal


JobStatus = Literal["queued", "processing", "ready", "error"]


@dataclass
class Job:
    job_id: str
    image_jpeg: bytes
    status: JobStatus = "queued"
    pages: list[bytes] = field(default_factory=list)
    error: str | None = None
    task: asyncio.Task | None = None
    created_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    updated_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


class JobRegistry:
    def __init__(self) -> None:
        self._jobs: dict[str, Job] = {}
        self._lock = asyncio.Lock()

    async def create(self, image_jpeg: bytes) -> Job:
        async with self._lock:
            self._cleanup_expired_locked()
            job_id = secrets.token_hex(2)
            while job_id in self._jobs:
                job_id = secrets.token_hex(2)
            job = Job(job_id=job_id, image_jpeg=image_jpeg)
            self._jobs[job_id] = job
            return job

    async def attach_task(self, job_id: str, task: asyncio.Task) -> None:
        async with self._lock:
            job = self._jobs.get(job_id)
            if job is not None:
                job.task = task
                job.updated_at = datetime.now(timezone.utc)

    async def get(self, job_id: str) -> Job | None:
        async with self._lock:
            self._cleanup_expired_locked()
            return self._jobs.get(job_id)

    async def mark_processing(self, job_id: str) -> bool:
        async with self._lock:
            job = self._jobs.get(job_id)
            if job is None:
                return False
            job.status = "processing"
            job.updated_at = datetime.now(timezone.utc)
            return True

    async def mark_ready(self, job_id: str, pages: list[bytes]) -> None:
        async with self._lock:
            job = self._jobs.get(job_id)
            if job is not None:
                job.pages = pages
                job.status = "ready"
                job.updated_at = datetime.now(timezone.utc)

    async def mark_error(self, job_id: str, error: str) -> None:
        async with self._lock:
            job = self._jobs.get(job_id)
            if job is not None:
                job.error = error
                job.status = "error"
                job.updated_at = datetime.now(timezone.utc)

    async def cancel(self, job_id: str) -> bool:
        async with self._lock:
            job = self._jobs.pop(job_id, None)
            if job is None:
                return False
            task = job.task
        if task is not None and not task.done():
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task
        return True

    def _cleanup_expired_locked(self) -> None:
        cutoff = datetime.now(timezone.utc) - timedelta(minutes=10)
        expired = [
            job_id
            for job_id, job in self._jobs.items()
            if job.status in {"ready", "error"} and job.updated_at < cutoff
        ]
        for job_id in expired:
            del self._jobs[job_id]


registry = JobRegistry()
