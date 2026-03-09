from __future__ import annotations

import asyncio
import io
import logging
import wave

import numpy as np
from faster_whisper import WhisperModel
from openai import AsyncOpenAI

from ..config import settings

LOGGER = logging.getLogger("robotcabeza.backend")


class SttService:
    def __init__(self, model_name: str) -> None:
        self._model = WhisperModel(model_name, device="auto", compute_type="int8")
        self._client = AsyncOpenAI(api_key=settings.openai_api_key)

    async def transcribe_pcm16(self, pcm_bytes: bytes, sample_rate: int) -> str:
        text = await self._transcribe_openai(pcm_bytes, sample_rate)
        if text:
            return text
        LOGGER.warning("OpenAI STT returned empty, falling back to faster-whisper")
        return await asyncio.to_thread(self._transcribe_sync, pcm_bytes, sample_rate)

    def _transcribe_sync(self, pcm_bytes: bytes, sample_rate: int) -> str:
        audio = self._preprocess_audio(pcm_bytes, trim_silence=True)
        if audio.size == 0:
            return ""

        segments, _info = self._model.transcribe(
            audio,
            language="es",
            beam_size=5,
            vad_filter=False,
            condition_on_previous_text=False,
        )
        return " ".join(segment.text.strip() for segment in segments).strip()

    async def _transcribe_openai(self, pcm_bytes: bytes, sample_rate: int) -> str:
        audio = self._preprocess_audio(pcm_bytes, trim_silence=False)
        if audio.size == 0:
            return ""

        wav_buffer = io.BytesIO()
        wav_buffer.name = "audio.wav"
        with wave.open(wav_buffer, "wb") as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(sample_rate)
            wav_file.writeframes((np.clip(audio, -1.0, 1.0) * 32767.0).astype(np.int16).tobytes())
        wav_buffer.seek(0)

        transcription = await self._client.audio.transcriptions.create(
            model=settings.openai_transcribe_model,
            file=wav_buffer,
            language="es",
            prompt="Transcribe exactamente el habla en espanol aunque sea corta, tenue o con ruido.",
            response_format="text",
        )
        if isinstance(transcription, str):
            return transcription.strip()
        text = getattr(transcription, "text", "")
        return text.strip()

    def _preprocess_audio(self, pcm_bytes: bytes, trim_silence: bool) -> np.ndarray:
        if not pcm_bytes:
            return np.array([], dtype=np.float32)

        audio = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32) / 32768.0
        if audio.size == 0:
            return np.array([], dtype=np.float32)

        # Center waveform before energy measurements.
        audio = audio - float(np.mean(audio))

        abs_audio = np.abs(audio)
        peak = float(np.max(abs_audio))
        rms = float(np.sqrt(np.mean(np.square(audio))))
        LOGGER.info("STT preprocess raw: peak=%.4f rms=%.4f samples=%d", peak, rms, audio.size)
        if peak < 0.002:
            return np.array([], dtype=np.float32)

        if trim_silence:
            # Trim long silence around the captured utterance so Whisper focuses on speech.
            voice_threshold = max(peak * 0.04, 0.004)
            active = np.where(abs_audio >= voice_threshold)[0]
            if active.size:
                pad = 4800
                start = max(0, int(active[0]) - pad)
                end = min(audio.size, int(active[-1]) + pad)
                min_window = min(audio.size, 24000)
                if end - start < min_window:
                    center = (start + end) // 2
                    half_window = min_window // 2
                    start = max(0, center - half_window)
                    end = min(audio.size, start + min_window)
                    start = max(0, end - min_window)
                audio = audio[start:end]

        peak = float(np.max(np.abs(audio)))
        rms = float(np.sqrt(np.mean(np.square(audio))))

        if rms > 1e-5:
            audio = audio * min(12.0, 0.14 / rms)
        peak = float(np.max(np.abs(audio)))
        if peak > 1e-5:
            audio = audio * min(1.0, 0.96 / peak)

        # Mild clipping to tame sporadic spikes from the ESP32 capture path.
        audio = np.tanh(audio * 1.4).astype(np.float32)
        final_peak = float(np.max(np.abs(audio)))
        final_rms = float(np.sqrt(np.mean(np.square(audio))))
        LOGGER.info(
            "STT preprocess final: peak=%.4f rms=%.4f samples=%d",
            final_peak,
            final_rms,
            audio.size,
        )
        return audio
