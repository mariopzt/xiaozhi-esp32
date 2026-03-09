$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$envPath = Join-Path $repoRoot "backend\.env"

if (-not (Test-Path $envPath)) {
    throw "No se encontro backend/.env"
}

$openAiKeyLine = Get-Content $envPath | Where-Object { $_ -like "OPENAI_API_KEY=*" } | Select-Object -First 1
if (-not $openAiKeyLine) {
    throw "No se encontro OPENAI_API_KEY en backend/.env"
}

$openAiKey = $openAiKeyLine.Substring("OPENAI_API_KEY=".Length).Trim()
if (-not $openAiKey) {
    throw "OPENAI_API_KEY esta vacia"
}

$agentId = "ceed04e5ce2c4a448248f81b594a4080"
$sql = @"
UPDATE ai_model_config
SET model_name = 'OpenAI GPT-4.1 nano',
    config_json = JSON_OBJECT(
        'type', 'openai',
        'model_name', 'gpt-4.1-nano',
        'base_url', 'https://api.openai.com/v1',
        'api_key', '$openAiKey',
        'temperature', 0.7,
        'max_tokens', 500,
        'top_p', 1,
        'frequency_penalty', 0
    )
WHERE id = 'LLM_DeepSeekLLM';

UPDATE ai_model_config
SET config_json = JSON_OBJECT(
        'type', 'openai',
        'api_key', '$openAiKey',
        'base_url', 'https://api.openai.com/v1/audio/transcriptions',
        'model_name', 'gpt-4o-mini-transcribe',
        'output_dir', 'tmp/'
    )
WHERE id = 'ASR_OpenaiASR';

UPDATE ai_model_config
SET config_json = JSON_OBJECT(
        'type', 'openai',
        'api_key', '$openAiKey',
        'api_url', 'https://api.openai.com/v1/audio/speech',
        'model', 'tts-1',
        'voice', 'nova',
        'format', 'wav',
        'speed', 1.08,
        'output_dir', 'tmp/'
    )
WHERE id = 'TTS_OpenAITTS';

DELETE FROM ai_tts_voice WHERE id IN ('TTS_OpenAITTS_ES0001', 'TTS_OpenAITTS_ES0002');
INSERT INTO ai_tts_voice (
    id, tts_model_id, name, tts_voice, languages, voice_demo, remark,
    reference_audio, reference_text, sort, creator, create_date, updater, update_date
)
VALUES
    ('TTS_OpenAITTS_ES0001', 'TTS_OpenAITTS', 'OpenAI ES Nova', 'nova', 'Espanol', NULL, NULL, NULL, NULL, 50, NULL, NULL, NULL, NULL),
    ('TTS_OpenAITTS_ES0002', 'TTS_OpenAITTS', 'OpenAI ES Shimmer', 'shimmer', 'Espanol', NULL, NULL, NULL, NULL, 51, NULL, NULL, NULL, NULL);

UPDATE ai_agent
SET agent_name = 'marioia',
    asr_model_id = 'ASR_OpenaiASR',
    llm_model_id = 'LLM_DeepSeekLLM',
    tts_model_id = 'TTS_OpenAITTS',
    tts_voice_id = 'TTS_OpenAITTS_ES0001',
    mem_model_id = 'Memory_mem_local_short',
    intent_model_id = 'Intent_function_call',
    lang_code = 'es',
    language = 'es-ES',
    system_prompt = 'Eres mario ia, un asistente de inteligencia artificial amigable que habla espanol de forma natural. Tu personalidad es cercana, curiosa y relajada. Hablas como un amigo con el usuario, manteniendo conversaciones naturales sobre tecnologia, programacion, proyectos, inteligencia artificial y temas cotidianos. Tu objetivo es mantener conversaciones utiles y agradables. Siempre respondes en espanol.'
WHERE id = '$agentId';
"@

$dockerExe = "C:\Program Files\Docker\Docker\resources\bin\docker.exe"
$tmpSql = Join-Path $repoRoot "scripts\.tmp_xiaozhi_es.sql"

Set-Content -Path $tmpSql -Value $sql -Encoding UTF8

try {
    Get-Content $tmpSql | & $dockerExe exec -i xiaozhi-esp32-server-db mysql -uroot -p123456 -D xiaozhi_esp32_server
    & $dockerExe restart xiaozhi-esp32-server-web xiaozhi-esp32-server | Out-Null
    Write-Output "configuracion aplicada"
}
finally {
    Remove-Item $tmpSql -Force -ErrorAction SilentlyContinue
}
