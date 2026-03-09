from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict


BASE_DIR = Path(__file__).resolve().parents[1]


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=BASE_DIR / ".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    openai_api_key: str
    mongodb_uri: str
    mongo_db_name: str = "robotcabeza"
    ws_auth_token: str

    backend_host: str = "0.0.0.0"
    backend_port: int = 8787

    openai_model: str = "gpt-4.1-nano"
    openai_transcribe_model: str = "gpt-4o-mini-transcribe"
    openai_tts_model: str = "gpt-4o-mini-tts"
    openai_tts_voice: str = "coral"
    whisper_model: str = "base"

    piper_bin: str = "piper"
    piper_model: str = ""
    piper_config: str = ""
    tts_sample_rate: int = 22050

    esp32_sample_rate: int = 16000
    esp32_frame_duration_ms: int = 60
    esp32_channels: int = 1


settings = Settings()
