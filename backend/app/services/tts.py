from __future__ import annotations

import asyncio
import io
import subprocess
import tempfile
from pathlib import Path
import shutil

import numpy as np
from openai import AsyncOpenAI
import pyttsx3
import soundfile as sf

from ..config import settings


class TtsService:
    def __init__(self) -> None:
        self._client = AsyncOpenAI(api_key=settings.openai_api_key)

    async def synthesize_pcm16(self, text: str, target_sample_rate: int) -> bytes:
        if not text.strip():
            return b""

        if shutil.which(settings.piper_bin):
            return await asyncio.to_thread(self._synthesize_with_piper, text, target_sample_rate)

        try:
            return await self._synthesize_with_openai(text, target_sample_rate)
        except Exception:
            return await asyncio.to_thread(self._synthesize_with_pyttsx3, text, target_sample_rate)

    def _synthesize_sync(self, text: str, target_sample_rate: int) -> bytes:
        if not text.strip():
            return b""

        if shutil.which(settings.piper_bin):
            return self._synthesize_with_piper(text, target_sample_rate)
        return self._synthesize_with_pyttsx3(text, target_sample_rate)

    async def _synthesize_with_openai(self, text: str, target_sample_rate: int) -> bytes:
        response = await self._client.audio.speech.create(
            model=settings.openai_tts_model,
            voice=settings.openai_tts_voice,
            input=text,
            response_format="wav",
        )
        wav_bytes = response.content if hasattr(response, "content") else bytes(response)
        audio, source_rate = sf.read(io.BytesIO(wav_bytes), dtype="int16", always_2d=False)
        if audio.ndim > 1:
            audio = audio[:, 0]
        if source_rate != target_sample_rate:
            audio = self._resample(audio, source_rate, target_sample_rate)
        return audio.astype(np.int16).tobytes()

    def _synthesize_with_piper(self, text: str, target_sample_rate: int) -> bytes:
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "tts.wav"
            command = [
                settings.piper_bin,
                "--model",
                settings.piper_model,
                "--output_file",
                str(output_path),
            ]
            if settings.piper_config:
                command.extend(["--config", settings.piper_config])

            subprocess.run(
                command,
                input=text.encode("utf-8"),
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )

            audio, source_rate = sf.read(output_path, dtype="int16", always_2d=False)
            if audio.ndim > 1:
                audio = audio[:, 0]
            if source_rate != target_sample_rate:
                audio = self._resample(audio, source_rate, target_sample_rate)
            return audio.astype(np.int16).tobytes()

    def _synthesize_with_pyttsx3(self, text: str, target_sample_rate: int) -> bytes:
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "tts.wav"
            engine = pyttsx3.init()
            engine.save_to_file(text, str(output_path))
            engine.runAndWait()
            engine.stop()

            audio, source_rate = sf.read(output_path, dtype="int16", always_2d=False)
            if audio.ndim > 1:
                audio = audio[:, 0]
            if source_rate != target_sample_rate:
                audio = self._resample(audio, source_rate, target_sample_rate)
            return audio.astype(np.int16).tobytes()

    def _resample(self, audio: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
        if len(audio) == 0 or source_rate == target_rate:
            return audio

        duration = len(audio) / float(source_rate)
        target_length = max(1, int(round(duration * target_rate)))
        src_positions = np.linspace(0, len(audio) - 1, num=len(audio), dtype=np.float32)
        dst_positions = np.linspace(0, len(audio) - 1, num=target_length, dtype=np.float32)
        return np.interp(dst_positions, src_positions, audio).astype(np.int16)
