from __future__ import annotations

import secrets

from .config import settings
from .models import AudioParams, DeviceIdentity, SessionState


def create_session(client_id: str, device_id: str, protocol_version: int) -> SessionState:
    identity = DeviceIdentity(
        client_id=client_id or "unknown-client",
        device_id=device_id or "unknown-device",
        session_id=secrets.token_hex(12),
        protocol_version=protocol_version,
    )
    return SessionState(
        identity=identity,
        audio_params=AudioParams(
            sample_rate=settings.esp32_sample_rate,
            frame_duration_ms=settings.esp32_frame_duration_ms,
            channels=settings.esp32_channels,
        ),
    )
