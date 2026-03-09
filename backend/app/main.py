from __future__ import annotations

import logging

import uvicorn
from fastapi import FastAPI, WebSocket

from .config import settings
from .websocket_server import BackendApplication

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")

backend = BackendApplication()
app = FastAPI(title="RobotCabeza Backend")


@app.on_event("startup")
async def startup() -> None:
    await backend.startup()


@app.on_event("shutdown")
async def shutdown() -> None:
    await backend.shutdown()


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await backend.handle_socket(websocket)


if __name__ == "__main__":
    uvicorn.run("app.main:app", host=settings.backend_host, port=settings.backend_port, reload=False)
