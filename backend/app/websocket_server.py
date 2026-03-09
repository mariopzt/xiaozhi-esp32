from __future__ import annotations

import json
import logging
from pathlib import Path
import wave
import numpy as np

from fastapi import WebSocket, WebSocketDisconnect, status
from motor.motor_asyncio import AsyncIOMotorClient

from .config import settings
from .models import SessionState
from .services.audio_bridge import AudioBridge
from .services.knowledge import KnowledgeService
from .services.llm import LlmService
from .services.memory import MemoryService
from .services.stt import SttService
from .services.tts import TtsService
from .session import create_session

LOGGER = logging.getLogger("robotcabeza.backend")
DIAGNOSTICS_DIR = Path(__file__).resolve().parents[1] / "diagnostics"


class BackendApplication:
    def __init__(self) -> None:
        self._mongo_client = AsyncIOMotorClient(settings.mongodb_uri)
        self._db = self._mongo_client[settings.mongo_db_name]
        self.memory = MemoryService(self._db)
        self.knowledge = KnowledgeService(Path(__file__).resolve().parents[1] / "knowledge")
        self.stt = SttService(settings.whisper_model)
        self.tts = TtsService()
        self.llm = LlmService()
        self.system_prompt = (Path(__file__).parent / "prompts" / "system.txt").read_text(encoding="utf-8")

    async def startup(self) -> None:
        await self.memory.ensure_indexes()
        await self.knowledge.ensure_directory()
        DIAGNOSTICS_DIR.mkdir(parents=True, exist_ok=True)

    async def shutdown(self) -> None:
        self._mongo_client.close()

    async def handle_socket(self, websocket: WebSocket) -> None:
        if not self._is_authorized(websocket):
            await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
            return
        await websocket.accept()

        protocol_version = self._header_protocol_version(websocket)
        session = create_session(
            client_id=websocket.headers.get("client-id", ""),
            device_id=websocket.headers.get("device-id", ""),
            protocol_version=protocol_version,
        )
        audio_bridge = AudioBridge(
            sample_rate=session.audio_params.sample_rate,
            channels=session.audio_params.channels,
            frame_duration_ms=session.audio_params.frame_duration_ms,
        )

        try:
            while True:
                message = await websocket.receive()
                if message.get("type") == "websocket.disconnect":
                    break
                if "bytes" in message and message["bytes"] is not None:
                    if session.listening_mode:
                        session.audio_packets.append(message["bytes"])
                    continue
                if "text" in message and message["text"] is not None:
                    await self._handle_text_message(websocket, session, audio_bridge, message["text"])
        except WebSocketDisconnect:
            LOGGER.info("WebSocket disconnected: %s", session.identity.client_id)

    def _is_authorized(self, websocket: WebSocket) -> bool:
        header = websocket.headers.get("authorization", "")
        expected = f"Bearer {settings.ws_auth_token}"
        return header == expected

    def _header_protocol_version(self, websocket: WebSocket) -> int:
        raw_value = websocket.headers.get("protocol-version", "1")
        try:
            return int(raw_value)
        except ValueError:
            return 1

    async def _handle_text_message(
        self,
        websocket: WebSocket,
        session: SessionState,
        audio_bridge: AudioBridge,
        text: str,
    ) -> None:
        payload = json.loads(text)
        message_type = payload.get("type")
        LOGGER.info(
            "WS text message: session=%s type=%s state=%s",
            session.identity.session_id,
            message_type,
            payload.get("state"),
        )

        if message_type == "hello":
            LOGGER.info(
                "WS hello: client_id=%s device_id=%s protocol=%s",
                session.identity.client_id,
                session.identity.device_id,
                session.identity.protocol_version,
            )
            await self._send_json(
                websocket,
                {
                    "type": "hello",
                    "transport": "websocket",
                    "session_id": session.identity.session_id,
                    "audio_params": {
                        "format": "opus",
                        "sample_rate": session.audio_params.sample_rate,
                        "channels": session.audio_params.channels,
                        "frame_duration": session.audio_params.frame_duration_ms,
                    },
                },
            )
            return

        if payload.get("session_id") and payload["session_id"] != session.identity.session_id:
            LOGGER.warning("Ignoring message with mismatched session_id")
            return

        if message_type == "listen":
            state = payload.get("state")
            if state == "start":
                LOGGER.info("Listen start: mode=%s", payload.get("mode", "manual"))
                session.listening_mode = payload.get("mode", "manual")
                session.audio_packets.clear()
            elif state == "stop":
                LOGGER.info("Listen stop: packets=%d", len(session.audio_packets))
                await self._process_turn(websocket, session, audio_bridge)
            return

        if message_type == "abort":
            LOGGER.info("Abort received")
            session.audio_packets.clear()
            return

        if message_type == "mcp":
            # Phase 1 backend does not yet initiate MCP tool calls back to the device.
            return

    async def _process_turn(self, websocket: WebSocket, session: SessionState, audio_bridge: AudioBridge) -> None:
        pcm_bytes = audio_bridge.decode_packets(session.audio_packets, session.identity.protocol_version)
        packet_count = len(session.audio_packets)
        session.audio_packets.clear()

        user_id = session.identity.device_id or session.identity.client_id or "unknown-device"
        peak = 0
        rms = 0.0
        if pcm_bytes:
            pcm = np.frombuffer(pcm_bytes, dtype=np.int16)
            if pcm.size:
                peak = int(np.max(np.abs(pcm)))
                rms = float(np.sqrt(np.mean(np.square(pcm.astype(np.float32)))))
        LOGGER.info(
            "Processing turn: session=%s packets=%d pcm_bytes=%d peak=%d rms=%.1f",
            session.identity.session_id,
            packet_count,
            len(pcm_bytes),
            peak,
            rms,
        )
        if pcm_bytes:
            self._write_diagnostic_wav(session.identity.session_id, pcm_bytes, session.audio_params.sample_rate)
        user_text = await self.stt.transcribe_pcm16(pcm_bytes, session.audio_params.sample_rate)
        if not user_text:
            LOGGER.info("STT empty: session=%s", session.identity.session_id)
            session.empty_stt_count += 1
            if session.empty_stt_count >= 1:
                await self._send_no_speech_reply(websocket, session, audio_bridge)
            return
        session.empty_stt_count = 0
        LOGGER.info("STT text: %s", user_text)

        await self.memory.save_turn(
            user_id,
            session.identity.session_id,
            "user",
            user_text,
            device_id=session.identity.device_id,
        )
        saved_memories = await self.memory.learn_from_user_text(user_id, user_text)
        context = await self.memory.get_context(user_id, user_text)
        knowledge_context = self.knowledge.render_context_block()
        context_block = self.memory.render_context_block(context)

        await self._send_json(
            websocket,
            {
                "session_id": session.identity.session_id,
                "type": "stt",
                "text": user_text,
            },
        )

        reply = await self.llm.answer(
            user_text=user_text,
            knowledge_context=knowledge_context,
            memory_context=context_block,
            system_prompt=self.system_prompt,
        )
        LOGGER.info("Assistant reply: %s", reply.assistant_text)
        reply.memories_saved = saved_memories
        await self.memory.save_turn(
            user_id,
            session.identity.session_id,
            "assistant",
            reply.assistant_text,
            device_id=session.identity.device_id,
        )
        await self.memory.save_interaction_summary(
            user_id,
            session.identity.session_id,
            user_text,
            reply.assistant_text,
        )

        tts_pcm = await self.tts.synthesize_pcm16(reply.assistant_text, session.audio_params.sample_rate)
        await self._send_json(
            websocket,
            {
                "session_id": session.identity.session_id,
                "type": "tts",
                "state": "start",
            },
        )
        await self._send_json(
            websocket,
            {
                "session_id": session.identity.session_id,
                "type": "tts",
                "state": "sentence_start",
                "text": reply.assistant_text,
            },
        )
        for packet in audio_bridge.encode_pcm(tts_pcm, session.identity.protocol_version):
            await websocket.send_bytes(packet)

        await self._send_json(
            websocket,
            {
                "session_id": session.identity.session_id,
                "type": "tts",
                "state": "stop",
            },
        )

    async def _send_no_speech_reply(
        self,
        websocket: WebSocket,
        session: SessionState,
        audio_bridge: AudioBridge,
    ) -> None:
        reply_text = "Repitelo, por favor."
        tts_pcm = await self.tts.synthesize_pcm16(reply_text, session.audio_params.sample_rate)
        await self._send_json(
            websocket,
            {
                "session_id": session.identity.session_id,
                "type": "tts",
                "state": "start",
            },
        )
        await self._send_json(
            websocket,
            {
                "session_id": session.identity.session_id,
                "type": "tts",
                "state": "sentence_start",
                "text": reply_text,
            },
        )
        for packet in audio_bridge.encode_pcm(tts_pcm, session.identity.protocol_version):
            await websocket.send_bytes(packet)

        await self._send_json(
            websocket,
            {
                "session_id": session.identity.session_id,
                "type": "tts",
                "state": "stop",
            },
        )

    async def _send_json(self, websocket: WebSocket, payload: dict) -> None:
        await websocket.send_text(json.dumps(payload, ensure_ascii=False))

    def _write_diagnostic_wav(self, session_id: str, pcm_bytes: bytes, sample_rate: int) -> None:
        wav_path = DIAGNOSTICS_DIR / f"{session_id}.wav"
        with wave.open(str(wav_path), "wb") as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(sample_rate)
            wav_file.writeframes(pcm_bytes)
        LOGGER.info("Saved diagnostic wav: %s", wav_path)
