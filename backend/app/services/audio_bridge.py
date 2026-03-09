from __future__ import annotations

import struct
from collections.abc import Iterable

import av
import numpy as np


class AudioBridge:
    def __init__(self, sample_rate: int, channels: int, frame_duration_ms: int) -> None:
        self._sample_rate = sample_rate
        self._channels = channels
        self._frame_duration_ms = frame_duration_ms
        self._frame_size = int(sample_rate * frame_duration_ms / 1000)

        self._decoder = av.codec.CodecContext.create("opus", "r")

    @property
    def frame_size(self) -> int:
        return self._frame_size

    def decode_packets(self, packets: Iterable[bytes], protocol_version: int) -> bytes:
        pcm_parts: list[bytes] = []
        for packet in packets:
            payload = self._extract_payload(packet, protocol_version)
            if not payload:
                continue

            av_packet = av.packet.Packet(payload)
            frames = self._decoder.decode(av_packet)
            for frame in frames:
                pcm = frame.to_ndarray()
                if pcm.ndim > 1:
                    if pcm.shape[0] <= pcm.shape[-1]:
                        pcm = pcm[0]
                    else:
                        pcm = pcm[:, 0]
                if frame.sample_rate and frame.sample_rate != self._sample_rate:
                    pcm = self._resample_pcm(pcm, frame.sample_rate, self._sample_rate)
                pcm_parts.append(self._to_pcm16_bytes(pcm))
        return b"".join(pcm_parts)

    def encode_pcm(self, pcm_bytes: bytes, protocol_version: int) -> list[bytes]:
        if not pcm_bytes:
            return []

        frame_bytes = self._frame_size * self._channels * 2
        packets: list[bytes] = []
        encoder = self._create_encoder()
        for offset in range(0, len(pcm_bytes), frame_bytes):
            frame_chunk = pcm_bytes[offset:offset + frame_bytes]
            if len(frame_chunk) < frame_bytes:
                frame_chunk = frame_chunk + b"\x00" * (frame_bytes - len(frame_chunk))

            pcm = np.frombuffer(frame_chunk, dtype=np.int16).reshape(1, -1)
            frame = av.AudioFrame.from_ndarray(pcm, format="s16", layout="mono")
            frame.sample_rate = self._sample_rate

            for packet in encoder.encode(frame):
                packets.append(self._wrap_payload(bytes(packet), protocol_version))

        for packet in encoder.encode(None):
            packets.append(self._wrap_payload(bytes(packet), protocol_version))
        return packets

    def _create_encoder(self) -> av.codec.context.CodecContext:
        codec_names = ("libopus", "opus")
        last_error: Exception | None = None
        for codec_name in codec_names:
            try:
                encoder = av.codec.CodecContext.create(codec_name, "w")
                encoder.sample_rate = self._sample_rate
                encoder.layout = "mono"
                encoder.format = "s16"
                encoder.bit_rate = 32000
                return encoder
            except Exception as exc:  # pragma: no cover - codec availability depends on local ffmpeg build
                last_error = exc
        raise RuntimeError(f"Unable to create Opus encoder: {last_error}")

    def _extract_payload(self, packet: bytes, protocol_version: int) -> bytes:
        if protocol_version == 1:
            return packet
        if protocol_version == 2:
            if len(packet) < 16:
                return b""
            _version, msg_type, _reserved, _timestamp, payload_size = struct.unpack(">HHIII", packet[:16])
            if msg_type != 0:
                return b""
            return packet[16:16 + payload_size]
        if protocol_version == 3:
            if len(packet) < 4:
                return b""
            msg_type, _reserved, payload_size = struct.unpack(">BBH", packet[:4])
            if msg_type != 0:
                return b""
            return packet[4:4 + payload_size]
        return b""

    def _wrap_payload(self, payload: bytes, protocol_version: int) -> bytes:
        if protocol_version == 1:
            return payload
        if protocol_version == 2:
            return struct.pack(">HHIII", 2, 0, 0, 0, len(payload)) + payload
        if protocol_version == 3:
            return struct.pack(">BBH", 0, 0, len(payload)) + payload
        return payload

    def _resample_pcm(self, pcm: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
        if len(pcm) == 0 or source_rate == target_rate:
            return pcm

        duration = len(pcm) / float(source_rate)
        target_length = max(1, int(round(duration * target_rate)))
        src_positions = np.linspace(0, len(pcm) - 1, num=len(pcm), dtype=np.float32)
        dst_positions = np.linspace(0, len(pcm) - 1, num=target_length, dtype=np.float32)
        return np.interp(dst_positions, src_positions, pcm).astype(np.float32)

    def _to_pcm16_bytes(self, pcm: np.ndarray) -> bytes:
        array = np.asarray(pcm)
        if array.dtype.kind == "f":
            array = np.clip(array, -1.0, 1.0) * 32767.0
        elif array.dtype == np.int32:
            array = array / 65536.0
        return array.astype(np.int16).tobytes()
