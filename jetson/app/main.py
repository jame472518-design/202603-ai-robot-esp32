import asyncio
import json
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, UploadFile, File
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response

from config import (
    MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX,
    FASTAPI_HOST, FASTAPI_PORT,
    OLLAMA_MODEL, OLLAMA_HOST,
    WHISPER_MODEL,
    PIPER_VOICE,
)
from app.mqtt_client import MQTTManager
from app.llm import LLMService
from app.stt import STTService
from app.tts import TTSService
from app.model_scheduler import ModelScheduler

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

mqtt_manager = MQTTManager(MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX)
llm_service = LLMService(OLLAMA_MODEL, OLLAMA_HOST)
stt_service = STTService(WHISPER_MODEL)
tts_service = TTSService(PIPER_VOICE)
scheduler = ModelScheduler(stt_service, llm_service, tts_service)
ws_connections: list[WebSocket] = []


def _get_sensor_context() -> str:
    for device_data in mqtt_manager.sensor_data.values():
        if device_data:
            latest = device_data[-1]
            return f"温度:{latest.get('temperature')}°C, 湿度:{latest.get('humidity')}%, 光照:{latest.get('light')}lux"
    return ""


def on_mqtt_data(device_id: str, data_type: str, payload: dict):
    """Forward MQTT data to all WebSocket clients."""
    message = json.dumps({
        "device_id": device_id,
        "type": data_type,
        "data": payload,
    })
    for ws in ws_connections[:]:
        try:
            asyncio.run_coroutine_threadsafe(
                ws.send_text(message),
                asyncio.get_event_loop(),
            )
        except Exception:
            ws_connections.remove(ws)


@asynccontextmanager
async def lifespan(app: FastAPI):
    mqtt_manager.add_listener(on_mqtt_data)
    mqtt_manager.start()
    logger.info("MQTT manager started")
    yield
    mqtt_manager.stop()
    logger.info("MQTT manager stopped")


app = FastAPI(title="AI Companion Robot", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/api/devices")
async def get_devices():
    return mqtt_manager.devices


@app.get("/api/sensors/{device_id}")
async def get_sensor_data(device_id: str):
    return mqtt_manager.sensor_data.get(device_id, [])


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    ws_connections.append(ws)
    logger.info(f"WebSocket client connected, total: {len(ws_connections)}")
    try:
        while True:
            data = await ws.receive_text()
            message = json.loads(data)
            if message.get("type") == "chat":
                text = message.get("text", "")
                sensor_ctx = _get_sensor_context()
                reply = llm_service.chat(text, sensor_ctx)
                await ws.send_text(json.dumps({
                    "type": "chat_response",
                    "data": {"text": reply},
                }))
    except WebSocketDisconnect:
        ws_connections.remove(ws)
        logger.info(f"WebSocket client disconnected, total: {len(ws_connections)}")


@app.post("/api/transcribe")
async def transcribe_audio(file: UploadFile = File(...)):
    audio_bytes = await file.read()
    text = stt_service.transcribe(audio_bytes)
    return {"text": text}


@app.post("/api/tts")
async def text_to_speech(text: str):
    audio = tts_service.synthesize(text)
    return Response(content=audio, media_type="audio/wav")


@app.post("/api/voice")
async def voice_pipeline(file: UploadFile = File(...)):
    """Full pipeline: audio in -> text -> LLM -> TTS -> audio out"""
    audio_bytes = await file.read()

    # STT
    text = stt_service.transcribe(audio_bytes)
    logger.info(f"Voice input: {text}")

    # LLM
    sensor_ctx = _get_sensor_context()
    reply = llm_service.chat(text, sensor_ctx)
    logger.info(f"LLM reply: {reply}")

    # TTS
    audio_out = tts_service.synthesize(reply)

    return Response(
        content=audio_out,
        media_type="audio/wav",
        headers={
            "X-Input-Text": text,
            "X-Reply-Text": reply,
        },
    )


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host=FASTAPI_HOST, port=FASTAPI_PORT)
