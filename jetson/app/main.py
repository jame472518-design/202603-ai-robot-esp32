import asyncio
import json
import logging
import time
from contextlib import asynccontextmanager

import aiohttp
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, UploadFile, File
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response, JSONResponse

from config import (
    MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX,
    FASTAPI_HOST, FASTAPI_PORT,
    OLLAMA_MODEL, OLLAMA_HOST,
    WHISPER_MODEL, PIPER_VOICE,
    ESP32_CAM_URL, FACE_SIMILARITY_THRESHOLD,
    FACE_LOOP_INTERVAL, FACE_GREET_COOLDOWN,
)
from app.mqtt_client import MQTTManager
from app.llm import LLMService

# Optional: STT/TTS (may not be installed yet)
try:
    from app.stt import STTService
    stt_available = True
except ImportError:
    stt_available = False
    logging.warning("faster-whisper not installed, STT disabled")

try:
    from app.tts import TTSService
    tts_available = True
except ImportError:
    tts_available = False
    logging.warning("Piper TTS not available, TTS disabled")

# Optional: Face Recognition
try:
    from app.face_service import FaceService
    face_service = FaceService(
        db_path="data/faces.db",
        similarity_threshold=FACE_SIMILARITY_THRESHOLD,
    )
    face_available = face_service.available
except Exception as e:
    face_service = None
    face_available = False
    logging.warning(f"Face recognition not available: {e}")

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

mqtt_manager = MQTTManager(MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX)
llm_service = LLMService(OLLAMA_MODEL, OLLAMA_HOST)
stt_service = STTService(WHISPER_MODEL) if stt_available else None
tts_service = TTSService(PIPER_VOICE) if tts_available else None
ws_connections: list[WebSocket] = []
_loop: asyncio.AbstractEventLoop | None = None


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
    if _loop is None:
        return
    for ws in ws_connections[:]:
        try:
            asyncio.run_coroutine_threadsafe(
                ws.send_text(message),
                _loop,
            )
        except Exception:
            ws_connections.remove(ws)


async def _ws_broadcast(message: str):
    """Broadcast a message to all connected WebSocket clients."""
    for ws in ws_connections[:]:
        try:
            await ws.send_text(message)
        except Exception:
            ws_connections.remove(ws)


face_loop_paused = False  # Pause face loop during recording

async def face_recognition_loop():
    """Background loop: fetch ESP32 snapshot, detect faces, broadcast results."""
    if not face_available:
        logger.warning("Face recognition disabled, loop not starting")
        return

    last_seen: dict[str, float] = {}
    consecutive_failures = 0

    async with aiohttp.ClientSession() as session:
        while True:
            await asyncio.sleep(FACE_LOOP_INTERVAL)
            if face_loop_paused:
                continue
            try:
                async with session.get(
                    f"{ESP32_CAM_URL}/capture",
                    timeout=aiohttp.ClientTimeout(total=5),
                ) as resp:
                    if resp.status != 200:
                        raise Exception(f"HTTP {resp.status}")
                    jpeg_bytes = await resp.read()
                consecutive_failures = 0
            except Exception as e:
                consecutive_failures += 1
                if consecutive_failures >= 5:
                    logger.warning(f"ESP32 camera unreachable ({e}), pausing 30s")
                    await asyncio.sleep(30)
                    consecutive_failures = 0
                continue

            try:
                results = await face_service.detect_and_recognize(jpeg_bytes)
            except Exception as e:
                logger.error(f"Face recognition error: {e}")
                continue

            faces_data = []
            now = time.time()

            for face in results:
                face_dict = {
                    "name": face.name,
                    "confidence": face.confidence,
                    "bbox": face.bbox,
                    "is_unknown": face.is_unknown,
                }
                faces_data.append(face_dict)

                if not face.is_unknown:
                    last_time = last_seen.get(face.name, 0)
                    if now - last_time > FACE_GREET_COOLDOWN:
                        last_seen[face.name] = now
                        face_service.add_log(None, face.name, face.confidence)

                        async def greet(name=face.name):
                            try:
                                loop = asyncio.get_running_loop()
                                reply = await loop.run_in_executor(
                                    None, llm_service.chat,
                                    f"你看到了{name}，请用一句话跟他/她打个招呼", ""
                                )
                                greeting_msg = json.dumps({
                                    "type": "chat_response",
                                    "data": {"text": reply},
                                })
                                await _ws_broadcast(greeting_msg)
                            except Exception as e:
                                logger.error(f"Greeting error: {e}")

                        asyncio.create_task(greet())
                    else:
                        last_seen[face.name] = now
                else:
                    unknown_msg = json.dumps({
                        "type": "unknown_face",
                        "data": {
                            "bbox": face.bbox,
                            "snapshot": "/api/face/snapshot",
                        },
                    })
                    await _ws_broadcast(unknown_msg)
                    face_service.add_log(None, None, 0.0)

            face_event = json.dumps({
                "type": "face_event",
                "data": {"faces": faces_data},
            })
            await _ws_broadcast(face_event)

            # Send face result to ESP32 via MQTT for OLED display
            if faces_data:
                best = max(faces_data, key=lambda f: f["confidence"])
                face_mqtt = {
                    "name": best["name"],
                    "confidence": best["confidence"],
                }
            else:
                face_mqtt = {"name": None, "confidence": 0}
            mqtt_manager.publish(
                "robot/esp32/esp32s3_cam_001/face", face_mqtt
            )


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _loop
    _loop = asyncio.get_running_loop()
    mqtt_manager.add_listener(on_mqtt_data)
    mqtt_manager.start()
    logger.info("MQTT manager started")
    logger.info(f"STT: {'enabled' if stt_available else 'disabled'}")
    logger.info(f"TTS: {'enabled' if tts_available else 'disabled'}")
    logger.info(f"Face: {'enabled' if face_available else 'disabled'}")

    face_task = None
    if face_available:
        face_task = asyncio.create_task(face_recognition_loop())
        logger.info("Face recognition loop started")

    yield

    if face_task:
        face_task.cancel()
    mqtt_manager.stop()
    logger.info("Services stopped")


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


@app.get("/api/status")
async def get_status():
    return {
        "stt": stt_available,
        "tts": tts_available,
        "llm": True,
        "mqtt": True,
        "face_recognition": face_available,
        "devices_count": len(mqtt_manager.devices),
    }


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
    if not stt_available:
        return {"text": "", "error": "STT not available"}
    audio_bytes = await file.read()
    text = stt_service.transcribe(audio_bytes)
    return {"text": text}


@app.post("/api/tts")
async def text_to_speech(text: str):
    if not tts_available:
        return Response(content=b"", media_type="audio/wav")
    audio = tts_service.synthesize(text)
    return Response(content=audio, media_type="audio/wav")


@app.post("/api/voice")
async def voice_pipeline(file: UploadFile = File(...)):
    """Full pipeline: audio in -> text -> LLM -> TTS -> audio out"""
    if not stt_available:
        return {"error": "STT not available, use text chat instead"}

    audio_bytes = await file.read()

    # STT
    text = stt_service.transcribe(audio_bytes)
    logger.info(f"Voice input: {text}")

    # LLM
    sensor_ctx = _get_sensor_context()
    reply = llm_service.chat(text, sensor_ctx)
    logger.info(f"LLM reply: {reply}")

    # TTS
    if tts_available:
        audio_out = tts_service.synthesize(reply)
    else:
        audio_out = b""

    return Response(
        content=audio_out,
        media_type="audio/wav",
        headers={
            "X-Input-Text": text,
            "X-Reply-Text": reply,
        },
    )


# ===== Voice Chat: ESP32 Mic → STT → LLM → Chat =====

@app.post("/api/voice-chat")
async def voice_chat(seconds: int = 3):
    """Record from ESP32 mic → STT → LLM → broadcast to chat"""
    global face_loop_paused
    if not stt_available:
        return JSONResponse({"error": "STT not available"}, 503)

    # 1. Pause face loop and record from ESP32
    face_loop_paused = True
    await asyncio.sleep(1)  # Wait for current face loop cycle to finish
    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(
                f"{ESP32_CAM_URL}/record?seconds={seconds}",
                timeout=aiohttp.ClientTimeout(total=seconds + 5),
            ) as resp:
                if resp.status != 200:
                    return JSONResponse({"error": "Cannot reach ESP32 mic"}, 502)
                wav_bytes = await resp.read()
    except Exception as e:
        face_loop_paused = False
        return JSONResponse({"error": f"Recording failed: {e}"}, 502)

    # 2. STT - skip WAV header (44 bytes)
    try:
        audio_data = wav_bytes[44:] if len(wav_bytes) > 44 else wav_bytes
        loop = asyncio.get_running_loop()
        text = await loop.run_in_executor(None, stt_service.transcribe, audio_data)
    except Exception as e:
        return JSONResponse({"error": f"STT failed: {e}"}, 500)

    if not text:
        return {"input": "", "reply": "", "error": "No speech detected"}

    # 3. Broadcast user input to chat
    user_msg = json.dumps({
        "type": "chat_response",
        "data": {"text": f"🎤 {text}"},
    })
    await _ws_broadcast(user_msg)

    # 4. LLM reply
    sensor_ctx = _get_sensor_context()
    reply = await loop.run_in_executor(None, llm_service.chat, text, sensor_ctx)

    # 5. Broadcast LLM reply to chat
    reply_msg = json.dumps({
        "type": "chat_response",
        "data": {"text": reply},
    })
    await _ws_broadcast(reply_msg)

    # 6. TTS - generate audio reply
    audio_bytes = b""
    if tts_available and tts_service:
        try:
            audio_bytes = await loop.run_in_executor(None, tts_service.synthesize, reply)
        except Exception as e:
            logger.error(f"TTS error: {e}")

    # 7. Resume face loop
    face_loop_paused = False

    if audio_bytes:
        return Response(
            content=audio_bytes,
            media_type="audio/wav",
            headers={
                "X-Input-Text": text,
                "X-Reply-Text": reply,
                "Access-Control-Expose-Headers": "X-Input-Text, X-Reply-Text",
            },
        )
    return {"input": text, "reply": reply}


# ===== Face Recognition Endpoints =====

@app.post("/api/face/register")
async def register_face(name: str):
    if not face_available:
        return JSONResponse({"error": "Face recognition not available"}, 503)
    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(
                f"{ESP32_CAM_URL}/capture",
                timeout=aiohttp.ClientTimeout(total=5),
            ) as resp:
                if resp.status != 200:
                    return JSONResponse({"error": "Cannot reach ESP32 camera"}, 502)
                jpeg_bytes = await resp.read()
        person_id = await face_service.register(name, jpeg_bytes)
        return {"id": person_id, "name": name}
    except ValueError as e:
        return JSONResponse({"error": str(e)}, 400)


@app.post("/api/face/register/upload")
async def register_face_upload(name: str, file: UploadFile = File(...)):
    if not face_available:
        return JSONResponse({"error": "Face recognition not available"}, 503)
    try:
        jpeg_bytes = await file.read()
        person_id = await face_service.register(name, jpeg_bytes)
        return {"id": person_id, "name": name}
    except ValueError as e:
        return JSONResponse({"error": str(e)}, 400)


@app.get("/api/face/persons")
async def get_persons():
    if not face_available:
        return []
    return face_service.get_persons()


@app.put("/api/face/persons/{person_id}")
async def update_person(person_id: int, name: str):
    if not face_available:
        return JSONResponse({"error": "Face recognition not available"}, 503)
    success = await face_service.update_person(person_id, name=name)
    if not success:
        return JSONResponse({"error": "Person not found"}, 404)
    return {"id": person_id, "name": name}


@app.delete("/api/face/persons/{person_id}")
async def delete_person(person_id: int):
    if not face_available:
        return JSONResponse({"error": "Face recognition not available"}, 503)
    face_service.delete_person(person_id)
    return {"ok": True}


@app.get("/api/face/persons/{person_id}/photo")
async def get_person_photo(person_id: int):
    if not face_available:
        return JSONResponse({"error": "Face recognition not available"}, 503)
    photo = face_service.get_person_photo(person_id)
    if not photo:
        return JSONResponse({"error": "Photo not found"}, 404)
    return Response(content=photo, media_type="image/jpeg")


@app.get("/api/face/logs")
async def get_face_logs(limit: int = 50):
    if not face_available:
        return []
    return face_service.get_logs(limit)


@app.get("/api/face/snapshot")
async def get_face_snapshot():
    if not face_available or not face_service.last_snapshot:
        return JSONResponse({"error": "No snapshot available"}, 404)
    return Response(content=face_service.last_snapshot, media_type="image/jpeg")


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host=FASTAPI_HOST, port=FASTAPI_PORT)
