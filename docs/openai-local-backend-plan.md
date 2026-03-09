# OpenAI Mini + Local STT/TTS Backend Plan

This plan is for the `robotcabeza-esp32-inmp441` board and keeps the existing
firmware architecture as intact as possible.

## Goal

Replace `xiaozhi.me` with our own backend so that:

- the ESP32 keeps using the current audio/chat flow
- memory belongs to one user/device profile instead of server-side chat sessions
- reasoning uses `gpt-4o-mini`
- speech-to-text and text-to-speech run locally for low cost

## Recommended Stack

### ESP32

- Transport: existing `WebSocket` protocol already implemented in:
  - `main/protocols/websocket_protocol.cc`
  - `main/protocols/protocol.cc`
- Audio format:
  - uplink: Opus, `16 kHz`, mono
  - downlink: Opus, `16-24 kHz`, mono

### Backend

- Runtime: `Python + FastAPI + websockets`
- STT: `faster-whisper`
- LLM: `OpenAI gpt-4o-mini`
- TTS: `Piper`
- Memory DB: `MongoDB`
- Cache / jobs: in-process first, Redis only if needed later

### Why this stack

- `WebSocket` is already supported by the firmware, so we avoid inventing a new protocol.
- `faster-whisper` is fast enough locally and cheap because it is free to run.
- `Piper` is offline, simple, and fast enough for short responses.
- `MongoDB` fits better if you want cloud persistence and easier future expansion.
- `gpt-4o-mini` stays only for reasoning, which keeps API cost low.

## High-Level Architecture

```text
ESP32
  -> WebSocket
Backend session server
  -> STT worker (faster-whisper)
  -> Memory store (MongoDB)
  -> LLM service (OpenAI gpt-4o-mini)
  -> TTS worker (Piper)
  -> stream Opus audio back to ESP32
```

## Conversation Flow

1. ESP32 opens WebSocket and sends `hello`.
2. Backend replies with `hello` and a stable `session_id`.
3. ESP32 sends `listen start`.
4. ESP32 streams Opus mic audio.
5. Backend buffers until end-of-speech.
6. Backend runs local STT.
7. Backend stores transcript in MongoDB.
8. Backend resolves memory:
   - profile facts
   - remembered notes
   - recent turns
9. Backend sends prompt to `gpt-4o-mini`.
10. Backend receives text response.
11. Backend runs local TTS with Piper.
12. Backend streams TTS audio to ESP32 with `tts start` / audio frames / `tts stop`.

## Memory Model

Do not rely on provider-side chat history.

Use one user profile in MongoDB with these collections:

### `profiles`

- `id`
- `display_name`
- `created_at`
- `updated_at`

### `memories`

- `id`
- `user_id`
- `text`
- `kind`
- `created_at`

Kinds:

- `explicit_remember`
- `profile_fact`
- `project`
- `preference`

### `turns`

- `id`
- `user_id`
- `session_id`
- `role`
- `text`
- `created_at`

## What should be remembered

Store only durable information:

- name
- age
- preferences
- important people
- projects
- recurring tasks
- explicit `remember this` facts

Do not store:

- one-off casual questions
- transient noise
- every raw response forever

## Prompt Strategy

Backend should build the model input like this:

1. system prompt
2. compact user profile
3. durable memories
4. last recent turns
5. latest user utterance

Example memory section:

```text
Known user facts:
- Name: Mario
- Age: 23
- Favorite color: azul

Explicit memories:
- The robot is in the salon.
- User works night shifts.
```

This is much more reliable than hoping a hosted agent UI keeps one long chat.

## WebSocket Contract

Reuse the current protocol described in `docs/websocket.md`.

### ESP32 -> backend

- `hello`
- `listen start`
- `listen stop`
- `abort`
- `mcp`
- binary Opus frames

### Backend -> ESP32

- `hello`
- `stt`
- `tts start`
- `tts sentence_start`
- `tts stop`
- `llm`
- `mcp`
- binary Opus frames

No protocol redesign is required for phase 1.

## Backend Components

### 1. WebSocket gateway

Responsibilities:

- authenticate device
- keep one in-memory session object per connection
- receive audio frames
- send JSON events and TTS audio back

Session object should track:

- `session_id`
- `user_id`
- `device_id`
- `listening_mode`
- `audio_buffer`
- `current_turn_id`
- `is_speaking`

### 2. STT worker

Responsibilities:

- decode buffered audio
- run `faster-whisper`
- return final recognized text

Pragmatic rule:

- only transcribe after end-of-speech at first
- do partial STT later if needed

### 3. Memory service

Responsibilities:

- parse explicit memory commands like `recuerda que ...`
- extract profile facts like name and age
- fetch compact context for the next LLM turn

### 4. LLM service

Responsibilities:

- call `gpt-4o-mini`
- use strict system instructions
- use memory context
- return concise text suitable for TTS

### 5. TTS service

Responsibilities:

- synthesize with Piper
- encode result to Opus
- stream to ESP32

## Latency Expectations

With a decent PC:

- end-of-speech detection: `0.2 - 0.6 s`
- local STT: `0.2 - 0.8 s`
- `gpt-4o-mini`: `0.3 - 1.0 s`
- Piper TTS first chunk: `0.2 - 0.7 s`

Typical time to begin speaking:

- around `0.9 - 2.5 s`

This is acceptable for a home assistant and gives much better memory control.

## Firmware Impact

### What does not need to change

- audio pipeline
- Opus encode/decode
- local VAD flow
- speaking/listening state machine
- MCP transport

### What should change later

Phase 1 firmware changes should be minimal:

- switch protocol config from MQTT to WebSocket
- point `websocket.url` to local backend
- set optional auth token

Possible later improvements:

- local confirmation tone when backend stores memory
- explicit `memory saved` custom/system message on screen
- better interruption rules during long music playback

## Backend API Decisions

### Device identity

Use:

- `Client-Id` header from firmware UUID as stable primary identifier when present
- `Device-Id` header as fallback identifier and diagnostics key

### Auth

Start simple:

- static bearer token per environment

Later:

- per-device tokens
- token rotation

## Development Phases

### Phase 1

- backend WebSocket server
- local STT
- `gpt-4o-mini`
- local TTS
- MongoDB memory
- one-user happy path

### Phase 2

- explicit memory confirmations
- admin page to inspect/edit memory
- better prompt shaping
- retry handling

### Phase 3

- streaming STT or partial STT
- multiple users/devices
- structured memory extraction pipeline

## File Layout Recommendation

Create a separate backend repo or sibling folder like:

```text
backend/
  app.py
  config.py
  websocket_server.py
  services/
    stt.py
    tts.py
    llm.py
    memory.py
    audio.py
  prompts/
    system.txt
```

Do not mix backend runtime code into the ESP32 firmware tree beyond docs or config examples.

## Recommended Next Step

Build phase 1 backend first, then point the ESP32 `WebSocket` settings to it.

Do not flash firmware changes until these are ready:

- backend can answer `hello`
- backend can accept Opus audio frames
- backend can return `tts start` + Opus audio + `tts stop`
- backend can persist and retrieve memory in MongoDB

## Verdict

For this robot, this architecture is better than staying tied to `xiaozhi.me` if the priority is:

- stable memory
- one user identity
- predictable behavior
- low operating cost

It is not zero work, but it is the correct long-term shape.
