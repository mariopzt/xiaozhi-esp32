from __future__ import annotations

import logging

import uvicorn
from pydantic import BaseModel
from fastapi import FastAPI, WebSocket

from .config import settings
from .websocket_server import BackendApplication

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")

backend = BackendApplication()
app = FastAPI(title="RobotCabeza Backend")


class MemoryTurnIn(BaseModel):
    session_id: str
    role: str
    text: str
    device_id: str = ""


class MemorySnapshotIn(BaseModel):
    user_name: str = ""
    notes: str = ""
    recent_turns: str = ""


class MemoryRememberIn(BaseModel):
    note: str = ""


@app.on_event("startup")
async def startup() -> None:
    await backend.startup()


@app.on_event("shutdown")
async def shutdown() -> None:
    await backend.shutdown()


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/memory-sync/context/{device_id}")
async def memory_sync_context(device_id: str, query: str = "") -> dict[str, str]:
    return await backend.memory.export_device_memory(device_id, query=query)


@app.post("/memory-sync/turn/{device_id}")
async def memory_sync_turn(device_id: str, payload: MemoryTurnIn) -> dict[str, bool]:
    role = payload.role.strip().lower()
    text = payload.text.strip()
    if not text:
        return {"ok": True}

    await backend.memory.save_turn(
        user_id=device_id,
        session_id=payload.session_id.strip() or "device-live",
        role=role or "user",
        text=text,
        device_id=payload.device_id.strip() or device_id,
    )
    if role == "user":
        await backend.memory.learn_from_user_text(device_id, text)
    return {"ok": True}


@app.post("/memory-sync/snapshot/{device_id}")
async def memory_sync_snapshot(device_id: str, payload: MemorySnapshotIn) -> dict[str, bool]:
    await backend.memory.import_device_snapshot(
        device_id,
        user_name=payload.user_name,
        notes=payload.notes,
        recent_turns=payload.recent_turns,
    )
    return {"ok": True}


@app.post("/memory-sync/remember/{device_id}")
async def memory_sync_remember(device_id: str, payload: MemoryRememberIn) -> dict[str, bool]:
    note = payload.note.strip()
    if note:
        await backend.memory.remember_note(device_id, note)
    return {"ok": True}


@app.post("/memory-sync/clear/{device_id}")
async def memory_sync_clear(device_id: str) -> dict[str, bool]:
    await backend.memory.clear_device_memory(device_id)
    return {"ok": True}


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await backend.handle_socket(websocket)


if __name__ == "__main__":
    uvicorn.run("app.main:app", host=settings.backend_host, port=settings.backend_port, reload=False)
