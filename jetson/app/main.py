import asyncio
import json
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from config import (
    MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX,
    FASTAPI_HOST, FASTAPI_PORT,
    OLLAMA_MODEL, OLLAMA_HOST,
)
from app.mqtt_client import MQTTManager
from app.llm import LLMService

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

mqtt_manager = MQTTManager(MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX)
llm_service = LLMService(OLLAMA_MODEL, OLLAMA_HOST)
ws_connections: list[WebSocket] = []


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
                # Get latest sensor context
                sensor_ctx = ""
                for device_data in mqtt_manager.sensor_data.values():
                    if device_data:
                        latest = device_data[-1]
                        sensor_ctx = f"温度:{latest.get('temperature')}°C, 湿度:{latest.get('humidity')}%, 光照:{latest.get('light')}lux"
                        break
                reply = llm_service.chat(text, sensor_ctx)
                await ws.send_text(json.dumps({
                    "type": "chat_response",
                    "data": {"text": reply},
                }))
    except WebSocketDisconnect:
        ws_connections.remove(ws)
        logger.info(f"WebSocket client disconnected, total: {len(ws_connections)}")


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host=FASTAPI_HOST, port=FASTAPI_PORT)
