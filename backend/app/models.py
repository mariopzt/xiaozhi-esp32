from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


@dataclass
class DeviceIdentity:
    client_id: str
    device_id: str
    session_id: str
    protocol_version: int = 1


@dataclass
class AudioParams:
    sample_rate: int
    frame_duration_ms: int
    channels: int = 1

    @property
    def frame_size(self) -> int:
        return int(self.sample_rate * self.frame_duration_ms / 1000)


@dataclass
class SessionState:
    identity: DeviceIdentity
    audio_params: AudioParams
    transport: str = "websocket"
    listening_mode: str = "manual"
    audio_packets: list[bytes] = field(default_factory=list)
    empty_stt_count: int = 0
    started_at: datetime = field(default_factory=utc_now)


@dataclass
class MemoryContext:
    profile: dict[str, Any]
    memories: list[str]
    recent_turns: list[dict[str, Any]]
    style_state: dict[str, Any] = field(default_factory=dict)
    relevant_turns: list[dict[str, Any]] = field(default_factory=list)
    summaries: list[str] = field(default_factory=list)
    session_summaries: list[str] = field(default_factory=list)
    reminders: list[str] = field(default_factory=list)
    archive_stats: dict[str, Any] = field(default_factory=dict)


@dataclass
class AssistantReply:
    user_text: str
    assistant_text: str
    memories_saved: list[str] = field(default_factory=list)
