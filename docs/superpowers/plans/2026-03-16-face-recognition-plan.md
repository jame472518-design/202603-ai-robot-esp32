# Face Recognition Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add InsightFace-based face recognition to the AI Companion Robot, with person registration, auto-detection, LLM greeting, and appearance logging.

**Architecture:** Jetson periodically fetches JPEG snapshots from ESP32-S3-CAM via HTTP, runs InsightFace (buffalo_s) for face detection + ArcFace embedding, compares against SQLite person DB, and pushes results to Vue 3 Dashboard via WebSocket. LLM auto-generates greetings for recognized persons.

**Tech Stack:** InsightFace, ONNX Runtime, SQLite, aiohttp, FastAPI, Vue 3

**Spec:** `docs/superpowers/specs/2026-03-16-face-recognition-design.md`

---

## File Structure

```
jetson/
  config.py                (MODIFY - add face recognition config)
  requirements.txt         (MODIFY - add insightface, opencv, aiohttp)
  app/
    face_service.py        (NEW - FaceService class: detection, recognition, DB)
    main.py                (MODIFY - add face endpoints, recognition loop, ws broadcast)
  data/                    (AUTO-CREATED at runtime)
    faces.db               (AUTO-CREATED by FaceService)
frontend/
  src/
    composables/
      useWebSocket.js      (MODIFY - add currentFaces, unknownFace refs)
    components/
      FacePanel.vue        (NEW - face recognition UI)
    App.vue                (MODIFY - add FacePanel to center-section)
scripts/
  install-face.sh          (NEW - install face recognition deps on Jetson)
```

---

## Chunk 1: Backend Foundation

### Task 1: Configuration + Dependencies

**Files:**
- Modify: `jetson/config.py`
- Modify: `jetson/requirements.txt`
- Create: `scripts/install-face.sh`

- [ ] **Step 1: Add face recognition config to config.py**

Add these lines at the end of `jetson/config.py`:

```python
# Face Recognition
ESP32_CAM_URL = "http://10.175.143.84"
FACE_SIMILARITY_THRESHOLD = 0.5
FACE_LOOP_INTERVAL = 1.5
FACE_GREET_COOLDOWN = 30
```

- [ ] **Step 2: Update requirements.txt**

Add to `jetson/requirements.txt`:

```
insightface>=0.7.3
opencv-python>=4.8.0
aiohttp>=3.9.0
```

Note: Do NOT add onnxruntime here. Jetson needs a JetPack-specific wheel that must be installed separately.

- [ ] **Step 3: Create install-face.sh**

Create `scripts/install-face.sh`:

```bash
#!/bin/bash
# Install face recognition dependencies on Jetson
# Run: bash scripts/install-face.sh
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
echo "=== Installing Face Recognition Dependencies ==="

cd "$PROJECT_DIR"
source venv/bin/activate

# Core dependencies
pip install insightface opencv-python aiohttp

# ONNX Runtime - try GPU first, fallback to CPU
pip install onnxruntime-gpu 2>/dev/null || pip install onnxruntime
echo "ONNX Runtime installed (check GPU support with: python -c 'import onnxruntime; print(onnxruntime.get_available_providers())')"

# Download InsightFace model (buffalo_s) on first run
python -c "
from insightface.app import FaceAnalysis
app = FaceAnalysis(name='buffalo_s', providers=['CPUExecutionProvider'])
app.prepare(ctx_id=0, det_size=(640, 480))
print('InsightFace buffalo_s model downloaded and ready')
"

echo "=== Face Recognition Installation Complete ==="
```

- [ ] **Step 4: Commit**

```bash
git add jetson/config.py jetson/requirements.txt scripts/install-face.sh
git commit -m "feat: add face recognition config and install script"
```

---

### Task 2: FaceService Core

**Files:**
- Create: `jetson/app/face_service.py`

This is the main module. It handles InsightFace model loading, face detection, embedding comparison, and SQLite database operations.

- [ ] **Step 1: Create face_service.py with full implementation**

Create `jetson/app/face_service.py`:

```python
import asyncio
import logging
import os
import sqlite3
import time
from dataclasses import dataclass
from io import BytesIO

import cv2
import numpy as np

logger = logging.getLogger(__name__)


@dataclass
class FaceResult:
    name: str | None
    confidence: float
    bbox: list[int]  # [x, y, w, h]
    is_unknown: bool


@dataclass
class Person:
    id: int
    name: str
    created_at: str


@dataclass
class FaceLog:
    id: int
    person_id: int | None
    person_name: str | None
    confidence: float
    created_at: str


class FaceService:
    def __init__(self, db_path: str = "data/faces.db",
                 model_name: str = "buffalo_s",
                 similarity_threshold: float = 0.5):
        self.db_path = db_path
        self.model_name = model_name
        self.threshold = similarity_threshold
        self._lock = asyncio.Lock()
        self._last_snapshot: bytes | None = None

        # Ensure data directory exists
        os.makedirs(os.path.dirname(db_path) or "data", exist_ok=True)

        # Init database
        self._init_db()

        # Load InsightFace model
        self._face_app = None
        self._load_model()

    def _init_db(self):
        conn = sqlite3.connect(self.db_path)
        conn.execute("""
            CREATE TABLE IF NOT EXISTS persons (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                embedding BLOB NOT NULL,
                photo BLOB,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        conn.execute("""
            CREATE TABLE IF NOT EXISTS face_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                person_id INTEGER,
                person_name TEXT,
                confidence REAL,
                photo BLOB,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        conn.commit()
        conn.close()
        logger.info(f"Face DB initialized at {self.db_path}")

    def _load_model(self):
        try:
            from insightface.app import FaceAnalysis
            providers = ['CUDAExecutionProvider', 'CPUExecutionProvider']
            self._face_app = FaceAnalysis(name=self.model_name, providers=providers)
            self._face_app.prepare(ctx_id=0, det_size=(640, 480))
            logger.info(f"InsightFace model '{self.model_name}' loaded")
        except Exception as e:
            logger.error(f"Failed to load InsightFace: {e}")
            self._face_app = None

    @property
    def available(self) -> bool:
        return self._face_app is not None

    @property
    def last_snapshot(self) -> bytes | None:
        return self._last_snapshot

    def _decode_jpeg(self, jpeg_bytes: bytes) -> np.ndarray:
        arr = np.frombuffer(jpeg_bytes, dtype=np.uint8)
        img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if img is None:
            raise ValueError("Failed to decode JPEG image")
        return img

    def _cosine_similarity(self, a: np.ndarray, b: np.ndarray) -> float:
        return float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)))

    def _get_all_embeddings(self) -> list[tuple[int, str, np.ndarray]]:
        conn = sqlite3.connect(self.db_path)
        rows = conn.execute("SELECT id, name, embedding FROM persons").fetchall()
        conn.close()
        result = []
        for row in rows:
            emb = np.frombuffer(row[2], dtype=np.float32)
            result.append((row[0], row[1], emb))
        return result

    async def detect_and_recognize(self, jpeg_bytes: bytes) -> list[FaceResult]:
        async with self._lock:
            self._last_snapshot = jpeg_bytes
            if not self.available:
                return []

            img = self._decode_jpeg(jpeg_bytes)
            faces = self._face_app.get(img)
            if not faces:
                return []

            known_embeddings = self._get_all_embeddings()
            results = []

            for face in faces:
                bbox_raw = face.bbox.astype(int)
                # Convert [x1, y1, x2, y2] to [x, y, w, h]
                bbox = [
                    int(bbox_raw[0]), int(bbox_raw[1]),
                    int(bbox_raw[2] - bbox_raw[0]),
                    int(bbox_raw[3] - bbox_raw[1]),
                ]
                embedding = face.embedding

                best_match = None
                best_score = 0.0

                for pid, pname, pemb in known_embeddings:
                    score = self._cosine_similarity(embedding, pemb)
                    if score > best_score:
                        best_score = score
                        best_match = (pid, pname)

                if best_match and best_score >= self.threshold:
                    results.append(FaceResult(
                        name=best_match[1],
                        confidence=round(best_score, 3),
                        bbox=bbox,
                        is_unknown=False,
                    ))
                else:
                    results.append(FaceResult(
                        name=None,
                        confidence=round(best_score, 3) if best_match else 0.0,
                        bbox=bbox,
                        is_unknown=True,
                    ))

            return results

    async def register(self, name: str, jpeg_bytes: bytes) -> int:
        async with self._lock:
            if not self.available:
                raise RuntimeError("Face recognition model not loaded")

            img = self._decode_jpeg(jpeg_bytes)
            faces = self._face_app.get(img)

            if not faces:
                raise ValueError("No face detected in image")
            if len(faces) > 1:
                raise ValueError("Multiple faces detected, please ensure only one face is visible")

            embedding = faces[0].embedding
            emb_bytes = embedding.astype(np.float32).tobytes()

            conn = sqlite3.connect(self.db_path)
            cursor = conn.execute(
                "INSERT INTO persons (name, embedding, photo) VALUES (?, ?, ?)",
                (name, emb_bytes, jpeg_bytes),
            )
            person_id = cursor.lastrowid
            conn.commit()
            conn.close()

            logger.info(f"Registered person: {name} (id={person_id})")
            return person_id

    async def update_person(self, person_id: int, name: str | None = None,
                            jpeg_bytes: bytes | None = None) -> bool:
        conn = sqlite3.connect(self.db_path)
        person = conn.execute("SELECT id FROM persons WHERE id = ?", (person_id,)).fetchone()
        if not person:
            conn.close()
            return False

        if name:
            conn.execute("UPDATE persons SET name = ? WHERE id = ?", (name, person_id))

        if jpeg_bytes:
            async with self._lock:
                img = self._decode_jpeg(jpeg_bytes)
                faces = self._face_app.get(img)
                if not faces:
                    conn.close()
                    raise ValueError("No face detected in image")
                if len(faces) > 1:
                    conn.close()
                    raise ValueError("Multiple faces detected")
                emb_bytes = faces[0].embedding.astype(np.float32).tobytes()
                conn.execute(
                    "UPDATE persons SET embedding = ?, photo = ? WHERE id = ?",
                    (emb_bytes, jpeg_bytes, person_id),
                )

        conn.commit()
        conn.close()
        return True

    def get_persons(self) -> list[Person]:
        conn = sqlite3.connect(self.db_path)
        rows = conn.execute("SELECT id, name, created_at FROM persons ORDER BY id").fetchall()
        conn.close()
        return [Person(id=r[0], name=r[1], created_at=r[2]) for r in rows]

    def get_person_photo(self, person_id: int) -> bytes | None:
        conn = sqlite3.connect(self.db_path)
        row = conn.execute("SELECT photo FROM persons WHERE id = ?", (person_id,)).fetchone()
        conn.close()
        return row[0] if row else None

    def delete_person(self, person_id: int) -> bool:
        conn = sqlite3.connect(self.db_path)
        # SET NULL on face_logs
        conn.execute("UPDATE face_logs SET person_id = NULL WHERE person_id = ?", (person_id,))
        cursor = conn.execute("DELETE FROM persons WHERE id = ?", (person_id,))
        conn.commit()
        deleted = cursor.rowcount > 0
        conn.close()
        return deleted

    def add_log(self, person_id: int | None, person_name: str | None,
                confidence: float, photo: bytes | None = None):
        conn = sqlite3.connect(self.db_path)
        conn.execute(
            "INSERT INTO face_logs (person_id, person_name, confidence, photo) VALUES (?, ?, ?, ?)",
            (person_id, person_name, confidence, photo),
        )
        # Keep only latest 500 entries
        conn.execute("""
            DELETE FROM face_logs WHERE id NOT IN (
                SELECT id FROM face_logs ORDER BY id DESC LIMIT 500
            )
        """)
        conn.commit()
        conn.close()

    def get_logs(self, limit: int = 50) -> list[FaceLog]:
        conn = sqlite3.connect(self.db_path)
        rows = conn.execute(
            "SELECT id, person_id, person_name, confidence, created_at FROM face_logs ORDER BY id DESC LIMIT ?",
            (limit,),
        ).fetchall()
        conn.close()
        return [FaceLog(id=r[0], person_id=r[1], person_name=r[2],
                        confidence=r[3], created_at=r[4]) for r in rows]
```

- [ ] **Step 2: Commit**

```bash
git add jetson/app/face_service.py
git commit -m "feat: add FaceService with InsightFace detection, recognition, and SQLite DB"
```

---

### Task 3: Backend Integration (main.py)

**Files:**
- Modify: `jetson/app/main.py`

Add face recognition endpoints, background recognition loop, and WebSocket broadcasting.

- [ ] **Step 1: Add imports and face service initialization**

At the top of `main.py`, add to imports:

```python
import aiohttp
from fastapi.responses import JSONResponse
```

Add to config imports:

```python
from config import (
    MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX,
    FASTAPI_HOST, FASTAPI_PORT,
    OLLAMA_MODEL, OLLAMA_HOST,
    WHISPER_MODEL, PIPER_VOICE,
    ESP32_CAM_URL, FACE_SIMILARITY_THRESHOLD,
    FACE_LOOP_INTERVAL, FACE_GREET_COOLDOWN,
)
```

Add optional face service import (after tts import block):

```python
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
```

- [ ] **Step 2: Add WebSocket broadcast helper**

Add after `on_mqtt_data` function:

```python
async def _ws_broadcast(message: str):
    """Broadcast a message to all connected WebSocket clients."""
    for ws in ws_connections[:]:
        try:
            await ws.send_text(message)
        except Exception:
            ws_connections.remove(ws)
```

- [ ] **Step 3: Add face recognition background loop**

Add after `_ws_broadcast`:

```python
async def face_recognition_loop():
    """Background loop: fetch ESP32 snapshot, detect faces, broadcast results."""
    if not face_available:
        logger.warning("Face recognition disabled, loop not starting")
        return

    last_seen: dict[str, float] = {}  # name -> timestamp
    consecutive_failures = 0

    async with aiohttp.ClientSession() as session:
        while True:
            await asyncio.sleep(FACE_LOOP_INTERVAL)
            try:
                # Fetch snapshot from ESP32
                async with session.get(
                    f"{ESP32_CAM_URL}/capture", timeout=aiohttp.ClientTimeout(total=5)
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

            # Detect and recognize
            try:
                results = await face_service.detect_and_recognize(jpeg_bytes)
            except Exception as e:
                logger.error(f"Face recognition error: {e}")
                continue

            # Build face_event
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
                    # Log known face (debounce 30s)
                    last_time = last_seen.get(face.name, 0)
                    if now - last_time > FACE_GREET_COOLDOWN:
                        last_seen[face.name] = now
                        face_service.add_log(None, face.name, face.confidence)

                        # LLM greeting (non-blocking)
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
                    # Unknown face: send registration prompt
                    unknown_msg = json.dumps({
                        "type": "unknown_face",
                        "data": {
                            "bbox": face.bbox,
                            "snapshot": "/api/face/snapshot",
                        },
                    })
                    await _ws_broadcast(unknown_msg)
                    face_service.add_log(None, None, 0.0)

            # Broadcast face_event (every cycle, even if empty)
            face_event = json.dumps({
                "type": "face_event",
                "data": {"faces": faces_data},
            })
            await _ws_broadcast(face_event)
```

Note: `import time` needs to be added at the top of the file.

- [ ] **Step 4: Start face loop in lifespan**

Modify the `lifespan` function to start the face recognition loop:

```python
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

    # Start face recognition background loop
    face_task = None
    if face_available:
        face_task = asyncio.create_task(face_recognition_loop())
        logger.info("Face recognition loop started")

    yield

    if face_task:
        face_task.cancel()
    mqtt_manager.stop()
    logger.info("Services stopped")
```

- [ ] **Step 5: Add face API endpoints**

Add these endpoints before `if __name__ == "__main__":`:

```python
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
    persons = face_service.get_persons()
    return [{"id": p.id, "name": p.name, "created_at": p.created_at} for p in persons]


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
    logs = face_service.get_logs(limit)
    return [{"id": l.id, "person_id": l.person_id, "person_name": l.person_name,
             "confidence": l.confidence, "created_at": l.created_at} for l in logs]


@app.get("/api/face/snapshot")
async def get_face_snapshot():
    if not face_available or not face_service.last_snapshot:
        return JSONResponse({"error": "No snapshot available"}, 404)
    return Response(content=face_service.last_snapshot, media_type="image/jpeg")
```

- [ ] **Step 6: Update /api/status endpoint**

Replace the existing `get_status` function:

```python
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
```

- [ ] **Step 7: Commit**

```bash
git add jetson/app/main.py
git commit -m "feat: add face recognition loop, API endpoints, and WebSocket events"
```

---

## Chunk 2: Frontend

### Task 4: useWebSocket.js Update

**Files:**
- Modify: `frontend/src/composables/useWebSocket.js`

- [ ] **Step 1: Add face-related reactive refs and handlers**

Replace the full content of `useWebSocket.js`:

```javascript
import { ref, onMounted, onUnmounted } from 'vue'

export function useWebSocket(url) {
  const messages = ref([])
  const devices = ref({})
  const connected = ref(false)
  const currentFaces = ref([])
  const unknownFace = ref(null)
  let ws = null
  let reconnectTimer = null

  function connect() {
    ws = new WebSocket(url)

    ws.onopen = () => {
      connected.value = true
      console.log('WebSocket connected')
    }

    ws.onclose = () => {
      connected.value = false
      console.log('WebSocket disconnected, reconnecting...')
      reconnectTimer = setTimeout(connect, 3000)
    }

    ws.onmessage = (event) => {
      const data = JSON.parse(event.data)
      if (data.type === 'heartbeat') {
        devices.value = { ...devices.value, [data.device_id]: data.data }
      } else if (data.type === 'sensor') {
        messages.value = [...messages.value.slice(-199), data]
      } else if (data.type === 'chat_response') {
        messages.value = [...messages.value, data]
      } else if (data.type === 'face_event') {
        currentFaces.value = data.data.faces
      } else if (data.type === 'unknown_face') {
        unknownFace.value = data.data
      }
    }
  }

  function send(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(data))
    }
  }

  function clearUnknownFace() {
    unknownFace.value = null
  }

  onMounted(connect)
  onUnmounted(() => {
    clearTimeout(reconnectTimer)
    if (ws) ws.close()
  })

  return { messages, devices, connected, send, currentFaces, unknownFace, clearUnknownFace }
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/composables/useWebSocket.js
git commit -m "feat: add face event handling to useWebSocket composable"
```

---

### Task 5: FacePanel Component

**Files:**
- Create: `frontend/src/components/FacePanel.vue`

- [ ] **Step 1: Create FacePanel.vue**

Create `frontend/src/components/FacePanel.vue`:

```vue
<template>
  <div class="face-panel">
    <h3>Face Recognition</h3>

    <!-- Current detection results -->
    <div class="detection-results" v-if="currentFaces.length">
      <div v-for="(face, i) in currentFaces" :key="i" class="face-item">
        <span v-if="!face.is_unknown" class="face-name known">
          {{ face.name }}
          <span class="confidence">{{ Math.round(face.confidence * 100) }}%</span>
        </span>
        <span v-else class="face-name unknown">Unknown</span>
      </div>
    </div>
    <div v-else class="no-faces">No faces detected</div>

    <!-- Unknown face registration prompt -->
    <div v-if="unknownFace" class="register-prompt">
      <div class="prompt-header">New face detected!</div>
      <img v-if="snapshotUrl" :src="snapshotUrl" class="snapshot" />
      <div class="prompt-form">
        <input v-model="registerName" placeholder="Enter name..." @keyup.enter="registerFromPrompt" />
        <button @click="registerFromPrompt" :disabled="!registerName.trim() || registering">
          {{ registering ? 'Registering...' : 'Register' }}
        </button>
        <button class="dismiss" @click="dismissPrompt">Dismiss</button>
      </div>
    </div>

    <!-- Manual registration -->
    <div class="manual-register">
      <div class="section-title">Register New Person</div>
      <div class="register-form">
        <input v-model="manualName" placeholder="Name..." />
        <button @click="manualRegister" :disabled="!manualName.trim() || registering">
          {{ registering ? '...' : 'Capture & Register' }}
        </button>
      </div>
      <div v-if="registerMsg" :class="['register-msg', registerError ? 'error' : 'success']">
        {{ registerMsg }}
      </div>
    </div>

    <!-- Registered persons list -->
    <div class="persons-list">
      <div class="section-title">Registered ({{ persons.length }})</div>
      <div v-for="p in persons" :key="p.id" class="person-item">
        <img :src="apiBase + '/api/face/persons/' + p.id + '/photo'" class="person-photo" />
        <span class="person-name">{{ p.name }}</span>
        <button class="delete-btn" @click="deletePerson(p.id)">X</button>
      </div>
      <div v-if="!persons.length" class="empty">No registered persons</div>
    </div>

    <!-- Appearance log -->
    <div class="face-logs">
      <div class="section-title">Recent Activity</div>
      <div v-for="log in logs" :key="log.id" class="log-item">
        <span class="log-name">{{ log.person_name || 'Unknown' }}</span>
        <span class="log-time">{{ formatTime(log.created_at) }}</span>
      </div>
      <div v-if="!logs.length" class="empty">No activity yet</div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, watch } from 'vue'

const props = defineProps({
  currentFaces: Array,
  unknownFace: Object,
  clearUnknownFace: Function,
})

const apiBase = `http://${window.location.hostname}:8000`
const snapshotUrl = ref('')
const registerName = ref('')
const manualName = ref('')
const registering = ref(false)
const registerMsg = ref('')
const registerError = ref(false)
const persons = ref([])
const logs = ref([])

async function fetchPersons() {
  try {
    const res = await fetch(`${apiBase}/api/face/persons`)
    persons.value = await res.json()
  } catch (e) { console.error('Failed to fetch persons', e) }
}

async function fetchLogs() {
  try {
    const res = await fetch(`${apiBase}/api/face/logs?limit=20`)
    logs.value = await res.json()
  } catch (e) { console.error('Failed to fetch logs', e) }
}

async function manualRegister() {
  if (!manualName.value.trim()) return
  registering.value = true
  registerMsg.value = ''
  try {
    const res = await fetch(`${apiBase}/api/face/register?name=${encodeURIComponent(manualName.value)}`, {
      method: 'POST',
    })
    const data = await res.json()
    if (res.ok) {
      registerMsg.value = `Registered: ${data.name}`
      registerError.value = false
      manualName.value = ''
      fetchPersons()
    } else {
      registerMsg.value = data.error || 'Registration failed'
      registerError.value = true
    }
  } catch (e) {
    registerMsg.value = 'Network error'
    registerError.value = true
  }
  registering.value = false
}

async function registerFromPrompt() {
  if (!registerName.value.trim()) return
  registering.value = true
  try {
    const res = await fetch(`${apiBase}/api/face/register?name=${encodeURIComponent(registerName.value)}`, {
      method: 'POST',
    })
    const data = await res.json()
    if (res.ok) {
      registerName.value = ''
      props.clearUnknownFace()
      fetchPersons()
    }
  } catch (e) { console.error(e) }
  registering.value = false
}

function dismissPrompt() {
  props.clearUnknownFace()
  registerName.value = ''
}

async function deletePerson(id) {
  await fetch(`${apiBase}/api/face/persons/${id}`, { method: 'DELETE' })
  fetchPersons()
}

function formatTime(ts) {
  if (!ts) return ''
  const d = new Date(ts)
  return d.toLocaleTimeString('zh-TW', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

watch(() => props.unknownFace, (val) => {
  if (val && val.snapshot) {
    snapshotUrl.value = `${apiBase}${val.snapshot}?t=${Date.now()}`
  } else {
    snapshotUrl.value = ''
  }
})

// Poll persons and logs
onMounted(() => {
  fetchPersons()
  fetchLogs()
  setInterval(fetchLogs, 10000)
})
</script>

<style scoped>
.face-panel {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.section-title {
  font-size: 0.85em;
  color: #9399b2;
  margin-bottom: 4px;
}
.detection-results {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.face-item {
  background: #1e1e2e;
  padding: 4px 10px;
  border-radius: 6px;
}
.face-name.known { color: #a6e3a1; }
.face-name.unknown { color: #f38ba8; }
.confidence {
  font-size: 0.75em;
  color: #9399b2;
  margin-left: 4px;
}
.no-faces { color: #6c7086; font-size: 0.85em; font-style: italic; }

.register-prompt {
  background: #313244;
  border-radius: 8px;
  padding: 12px;
  border: 1px solid #f9e2af;
}
.prompt-header {
  color: #f9e2af;
  font-weight: bold;
  margin-bottom: 8px;
}
.snapshot {
  width: 100%;
  max-height: 200px;
  object-fit: contain;
  border-radius: 6px;
  margin-bottom: 8px;
}
.prompt-form, .register-form {
  display: flex;
  gap: 6px;
}
.prompt-form input, .register-form input {
  flex: 1;
  padding: 6px 10px;
  background: #1e1e2e;
  border: 1px solid #45475a;
  border-radius: 6px;
  color: #cdd6f4;
  font-size: 0.9em;
}
.prompt-form button, .register-form button {
  padding: 6px 12px;
  background: #89b4fa;
  color: #1e1e2e;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.85em;
}
.prompt-form button:disabled, .register-form button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.dismiss {
  background: #45475a !important;
  color: #cdd6f4 !important;
}
.register-msg {
  font-size: 0.8em;
  margin-top: 4px;
}
.register-msg.success { color: #a6e3a1; }
.register-msg.error { color: #f38ba8; }

.person-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0;
  border-bottom: 1px solid #313244;
}
.person-photo {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  object-fit: cover;
  background: #313244;
}
.person-name { flex: 1; font-size: 0.9em; }
.delete-btn {
  background: none;
  border: none;
  color: #f38ba8;
  cursor: pointer;
  font-size: 0.8em;
  padding: 2px 6px;
}
.delete-btn:hover { background: #45475a; border-radius: 4px; }

.log-item {
  display: flex;
  justify-content: space-between;
  font-size: 0.8em;
  padding: 2px 0;
  border-bottom: 1px solid #1e1e2e;
}
.log-name { color: #cdd6f4; }
.log-time { color: #6c7086; }
.empty { color: #6c7086; font-size: 0.8em; font-style: italic; }
</style>
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/FacePanel.vue
git commit -m "feat: add FacePanel component with registration, detection, and logs"
```

---

### Task 6: App.vue Integration

**Files:**
- Modify: `frontend/src/App.vue`
- Modify: `frontend/src/composables/useWebSocket.js` (already done in Task 4)

- [ ] **Step 1: Update App.vue**

Replace the full content of `App.vue`:

```vue
<template>
  <div class="app">
    <header>
      <h1>AI Companion Robot</h1>
      <span :class="['conn-status', connected ? 'on' : 'off']">
        {{ connected ? 'Connected' : 'Disconnected' }}
      </span>
    </header>
    <main>
      <aside>
        <DevicePanel :devices="devices" />
        <SensorPanel :messages="messages" />
      </aside>
      <section class="center-section">
        <CameraPanel :messages="messages" :devices="devices" />
        <FacePanel
          :currentFaces="currentFaces"
          :unknownFace="unknownFace"
          :clearUnknownFace="clearUnknownFace"
        />
      </section>
      <section class="chat-section">
        <ChatPanel :messages="messages" :send="send" />
      </section>
    </main>
  </div>
</template>

<script setup>
import DevicePanel from './components/DevicePanel.vue'
import ChatPanel from './components/ChatPanel.vue'
import SensorPanel from './components/SensorPanel.vue'
import CameraPanel from './components/CameraPanel.vue'
import FacePanel from './components/FacePanel.vue'
import { useWebSocket } from './composables/useWebSocket'

const wsUrl = `ws://${window.location.hostname}:8000/ws`
const { messages, devices, connected, send, currentFaces, unknownFace, clearUnknownFace } = useWebSocket(wsUrl)
</script>

<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { background: #181825; color: #cdd6f4; font-family: system-ui, sans-serif; }
.app { height: 100vh; display: flex; flex-direction: column; }
header {
  display: flex; justify-content: space-between; align-items: center;
  padding: 12px 24px; background: #1e1e2e; border-bottom: 1px solid #313244;
}
header h1 { font-size: 1.2em; }
.conn-status {
  padding: 4px 12px; border-radius: 12px; font-size: 0.8em;
}
.conn-status.on { background: #a6e3a1; color: #1e1e2e; }
.conn-status.off { background: #f38ba8; color: #1e1e2e; }
main {
  flex: 1; display: flex; overflow: hidden;
}
aside {
  width: 350px; border-right: 1px solid #313244;
  overflow-y: auto;
}
.center-section { flex: 1; border-right: 1px solid #313244; overflow-y: auto; }
.chat-section { flex: 1; }
</style>
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/App.vue
git commit -m "feat: integrate FacePanel into Dashboard layout"
```

---

## Chunk 3: Deploy & Verify

### Task 7: Deploy to Jetson

**Files:** No new files — deployment steps only.

- [ ] **Step 1: Push all changes to GitHub**

```bash
git push
```

- [ ] **Step 2: Install face recognition deps on Jetson**

```bash
ssh aopen@10.175.143.199 "cd ~/Desktop/product/202603-ai-robot-esp32 && git pull"
ssh aopen@10.175.143.199 "cd ~/Desktop/product/202603-ai-robot-esp32 && bash scripts/install-face.sh"
```

Note: InsightFace model download (~100MB) may take a few minutes. If `onnxruntime-gpu` fails on Jetson, it will fallback to CPU — this is fine for demo, just slower.

- [ ] **Step 3: Rebuild frontend on Jetson**

```bash
ssh aopen@10.175.143.199 'export NVM_DIR="$HOME/.nvm" && [ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh" && cd ~/Desktop/product/202603-ai-robot-esp32/frontend && npm run build'
```

- [ ] **Step 4: Restart backend**

```bash
ssh aopen@10.175.143.199 "cd ~/Desktop/product/202603-ai-robot-esp32 && bash scripts/stop.sh"
ssh aopen@10.175.143.199 "cd ~/Desktop/product/202603-ai-robot-esp32 && bash scripts/start.sh"
```

- [ ] **Step 5: Verify**

1. Open `http://10.175.143.199:3000` in browser
2. Check API status: `curl http://10.175.143.199:8000/api/status` — should show `"face_recognition": true`
3. Stand in front of ESP32 camera
4. FacePanel should show "Unknown" face detected
5. Click "Capture & Register" with your name
6. Wait 2-3 seconds — should see your name appear with confidence %
7. LLM should auto-greet you in ChatPanel
8. Check appearance log shows your entry

- [ ] **Step 6: Final commit with any fixes**

```bash
git add -A
git commit -m "fix: deployment adjustments for face recognition"
git push
```
