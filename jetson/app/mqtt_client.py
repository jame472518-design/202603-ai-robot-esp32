import json
import logging
from collections import defaultdict
from datetime import datetime

import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)


class MQTTManager:
    def __init__(self, broker: str, port: int, topic_prefix: str):
        self.broker = broker
        self.port = port
        self.topic_prefix = topic_prefix
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.devices: dict[str, dict] = {}
        self.sensor_data: dict[str, list] = defaultdict(list)
        self._listeners: list = []

        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        logger.info(f"Connected to MQTT broker, rc={rc}")
        client.subscribe(self.topic_prefix)

    def _on_message(self, client, userdata, msg):
        try:
            topic = msg.topic
            payload = json.loads(msg.payload.decode())
            parts = topic.split("/")
            # topic format: robot/esp32/{device_id}/{data_type}
            if len(parts) >= 4:
                device_id = parts[2]
                data_type = parts[3]

                if data_type == "heartbeat":
                    self.devices[device_id] = {
                        **payload,
                        "last_seen": datetime.now().isoformat(),
                    }
                elif data_type == "sensor":
                    self.sensor_data[device_id].append({
                        **payload,
                        "timestamp": datetime.now().isoformat(),
                    })
                    # Keep last 100 readings
                    self.sensor_data[device_id] = self.sensor_data[device_id][-100:]
                elif data_type == "status":
                    if device_id not in self.devices:
                        self.devices[device_id] = {}
                    self.devices[device_id]["status"] = payload.get("status")

                # Notify WebSocket listeners
                for listener in self._listeners:
                    listener(device_id, data_type, payload)

        except Exception as e:
            logger.error(f"Error processing MQTT message: {e}")

    def add_listener(self, callback):
        self._listeners.append(callback)

    def remove_listener(self, callback):
        self._listeners.remove(callback)

    def start(self):
        self.client.connect(self.broker, self.port)
        self.client.loop_start()
        logger.info(f"MQTT client started, subscribing to {self.topic_prefix}")

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()

    def publish(self, topic: str, payload: dict):
        self.client.publish(topic, json.dumps(payload))
