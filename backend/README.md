# RobotCabeza Backend

Backend propio para el firmware `robotcabeza-esp32-inmp441`.

Objetivo:

- usar `gpt-4o-mini` solo para razonar
- usar STT local con `faster-whisper`
- usar TTS local con `Piper` y fallback a `pyttsx3`
- guardar memoria persistente en MongoDB
- hablar con el ESP32 usando el protocolo WebSocket que ya existe en el firmware

## Estado

Esto deja el backend base listo para desarrollo y pruebas.

Incluye:

- servidor WebSocket
- handshake `hello`
- sesion por dispositivo
- memoria en MongoDB
- extraccion de nombre, edad y frases `recuerda ...`
- llamada a OpenAI
- ruta prevista de STT/TTS local
- codificacion/decodificacion Opus de paquetes del ESP32

No toca el firmware ni flashea nada.

## Variables de entorno

Copia `backend/.env.example` a `backend/.env` y rellena:

- `OPENAI_API_KEY`
- `MONGODB_URI`
- `MONGO_DB_NAME`
- `WS_AUTH_TOKEN`
- `OPENAI_TRANSCRIBE_MODEL`
- `PIPER_MODEL`
- si no tienes `piper`, deja `PIPER_MODEL` vacio y el backend cae a `pyttsx3` en Windows para probar el flujo

No metas secretos en archivos del repo.

## Instalacion

```powershell
cd H:\Programacion\robotCabeza\xiaozhi-esp32\backend
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

## Arranque

```powershell
cd H:\Programacion\robotCabeza\xiaozhi-esp32\backend
.venv\Scripts\activate
python -m app.main
```

O en Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\start_backend.ps1
```

Para pararlo:

```powershell
powershell -ExecutionPolicy Bypass -File .\stop_backend.ps1
```

## Flujo

1. El ESP32 abre WebSocket.
2. El backend responde `hello`.
3. El ESP32 envia `listen start`.
4. El backend recibe audio Opus.
5. Al llegar `listen stop`, transcribe, guarda memoria, llama a `gpt-4o-mini`, sintetiza audio y devuelve `tts`.

## Configuracion futura del firmware

Cuando el backend este probado, el firmware debera apuntar a:

- `websocket.url = ws://<tu-ip>:8787/ws`
- `websocket.token = <WS_AUTH_TOKEN>`
- `websocket.version = 1`

Mantengo `version = 1` para la primera prueba porque simplifica el audio binario.
