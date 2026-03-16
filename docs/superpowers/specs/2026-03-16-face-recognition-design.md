# Face Recognition System Design

## Overview

Add face recognition to the AI Companion Robot demo. Jetson fetches snapshots from ESP32-S3-CAM, runs InsightFace (ArcFace) for detection and recognition, stores a database of known persons, and pushes results to the Web Dashboard in real-time.

## Constraints

- Jetson Orin 8GB UNIFIED memory (CPU + GPU shared, ~6.5GB usable after OS)
- ESP32-S3-CAM OV2640, VGA 640x480 JPEG
- 5-10 persons for demo use
- ESP32 captures only, all AI processing on Jetson

## Configuration (config.py additions)

```python
ESP32_CAM_URL = "http://10.175.143.84"   # ESP32 capture base URL
FACE_SIMILARITY_THRESHOLD = 0.5          # ArcFace cosine similarity (0.4 too permissive)
FACE_LOOP_INTERVAL = 1.5                 # seconds between recognition cycles
FACE_GREET_COOLDOWN = 30                 # seconds before re-greeting same person
```

## Architecture

```
ESP32-S3-CAM                         Jetson
┌──────────┐    HTTP /capture    ┌─────────────────────┐
│  OV2640  │ ◄────────────────── │  FaceService        │
└──────────┘   every 1.5s        │  ├─ detect()        │
                                 │  ├─ encode()        │
                                 │  └─ recognize()     │
                                 ├─────────────────────┤
                                 │  FaceDB (SQLite)    │
                                 │  ├─ persons         │
                                 │  └─ face_logs       │
                                 ├─────────────────────┤
                                 │  FastAPI endpoints  │
                                 │  ├─ POST /api/face/register
                                 │  ├─ GET  /api/face/persons
                                 │  ├─ DELETE /api/face/persons/{id}
                                 │  ├─ GET  /api/face/persons/{id}/photo
                                 │  └─ GET  /api/face/logs
                                 ├─────────────────────┤
                                 │  WebSocket events   │
                                 │  ├─ face_event
                                 │  ├─ unknown_face
                                 │  └─ chat_response (greeting)
                                 └─────────────────────┘
                                          │ WebSocket
                                          ▼
                                 ┌─────────────────────┐
                                 │  Dashboard           │
                                 │  └─ FacePanel (new)  │
                                 └─────────────────────┘
```

## Tech Stack

| Component | Choice | Reason |
|-----------|--------|--------|
| Face detection | InsightFace RetinaFace | High accuracy, ONNX GPU support |
| Face embedding | ArcFace 512-dim | Industry-leading recognition |
| Model pack | buffalo_s | Lightweight, ~300MB, fits 8GB RAM |
| Database | SQLite | Simple, no server needed, 5-10 persons |
| Runtime | ONNX Runtime GPU | Leverage Jetson GPU |

## Database Schema

File: `jetson/data/faces.db`

### persons table

| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER PRIMARY KEY | Auto-increment |
| name | TEXT NOT NULL | Person name |
| embedding | BLOB NOT NULL | 512-dim numpy bytes |
| photo | BLOB | Registration JPEG photo |
| created_at | TIMESTAMP | Default current time |

### face_logs table

| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER PRIMARY KEY | Auto-increment |
| person_id | INTEGER | NULL = unknown face |
| person_name | TEXT | Name at time of detection |
| confidence | REAL | Cosine similarity score |
| photo | BLOB | Snapshot at detection time |
| created_at | TIMESTAMP | Default current time |

Design decisions:
- One embedding per person (sufficient for 5-10 person demo)
- Photos stored in DB (small dataset, no need for filesystem)
- face_logs keeps latest 500 entries, auto-cleanup
- Same person not re-logged within 30 seconds (debounce)
- persons.name does NOT have UNIQUE constraint (allow duplicate names)
- On person deletion, face_logs.person_id SET NULL (person_name preserved for audit)
- `data/` directory auto-created with `os.makedirs("data", exist_ok=True)`

## FaceService Module

File: `jetson/app/face_service.py`

```python
class FaceService:
    __init__(db_path="data/faces.db", model_name="buffalo_s")
        # Load InsightFace model
        # Connect SQLite, create tables
        # asyncio.Lock for thread safety (model is NOT thread-safe)

    _lock: asyncio.Lock  # Serialize model access between loop and API

    detect_and_recognize(jpeg_bytes) -> list[FaceResult]
        # Acquires _lock
        # 1. Decode JPEG to numpy
        # 2. FaceAnalysis.get() to detect faces
        # 3. Extract 512-dim embedding per face
        # 4. Compare cosine similarity with all persons in DB
        # 5. similarity > FACE_SIMILARITY_THRESHOLD = match, else unknown
        # 6. bbox converted from InsightFace [x1,y1,x2,y2] to [x,y,w,h]
        # Return: [{name, confidence, bbox, is_unknown}]

    register(name, jpeg_bytes) -> person_id
        # Acquires _lock
        # 1. Detect face (must be exactly 1 face)
        #    Error: no face → raise ValueError("No face detected")
        #    Error: multiple → raise ValueError("Multiple faces detected")
        # 2. Extract embedding
        # 3. Store in persons table
        # Return person_id

    update_person(person_id, name=None, jpeg_bytes=None) -> bool
        # Update name and/or re-register with new photo

    get_persons() -> list[Person]
    delete_person(person_id)  # SET NULL on face_logs.person_id
    get_logs(limit=50) -> list[FaceLog]
    add_log(person_id, name, confidence, photo)
```

## Background Recognition Loop

In `main.py`, started during lifespan:

```
async face_recognition_loop():
    consecutive_failures = 0
    every FACE_LOOP_INTERVAL seconds:
        1. HTTP GET ESP32_CAM_URL/capture → jpeg_bytes
           - On failure: consecutive_failures += 1
           - If consecutive_failures >= 5: pause 30s, reset counter
           - On success: consecutive_failures = 0
        2. face_service.detect_and_recognize(jpeg_bytes)
        3. If no faces detected: push face_event with empty faces array
           (so frontend clears display)
        4. For each face:
           - known + last seen > 30s ago →
             LLM greeting via run_in_executor (non-blocking!)
             + WebSocket push
           - unknown → push unknown_face event (with snapshot_id)
           - log to face_logs
        5. Push face_event to all WebSocket clients
           (includes both known and unknown faces)
```

**Critical**: LLM greeting MUST use `await loop.run_in_executor(None, llm_service.chat, ...)`
because `ollama.Client.chat()` is synchronous and would block the entire async event loop.

## API Endpoints

### POST /api/face/register?name=小明
- Auto-captures from ESP32 camera, detects face, registers
- Response 200: `{ "id": 1, "name": "小明" }`
- Response 400: `{ "error": "No face detected in image" }`
- Response 400: `{ "error": "Multiple faces detected, please ensure only one face is visible" }`
- Response 502: `{ "error": "Cannot reach ESP32 camera" }`

### POST /api/face/register/upload
- Body: multipart form with `name` field + `file` (JPEG image)
- Same success/error responses as above

### PUT /api/face/persons/{id}
- Body: `{ "name": "new name" }` (update name or re-register photo)
- Response 200: `{ "id": 1, "name": "new name" }`
- Response 404: `{ "error": "Person not found" }`

### GET /api/face/persons
- Response: `[{ "id": 1, "name": "小明", "created_at": "..." }]`

### DELETE /api/face/persons/{id}
- Response: `{ "ok": true }`
- face_logs entries preserved (person_id SET NULL, person_name kept)

### GET /api/face/logs?limit=50
- Response: `[{ "person_name": "小明", "confidence": 0.82, "created_at": "..." }]`

### GET /api/face/persons/{id}/photo
- Response: `image/jpeg`

### GET /api/face/snapshot
- Returns the latest captured frame used for unknown face detection
- Stored temporarily in memory (latest frame only)

## WebSocket Events

### face_event (every recognition cycle, including empty)
```json
{
  "type": "face_event",
  "data": {
    "faces": [
      { "name": "小明", "confidence": 0.82, "bbox": [x, y, w, h], "is_unknown": false },
      { "name": null, "confidence": 0, "bbox": [x, y, w, h], "is_unknown": true }
    ]
  }
}
```
- Sent every cycle. Empty `faces` array = no faces detected (frontend clears display).
- Contains BOTH known and unknown faces in the same event.

### unknown_face (triggers registration prompt)
```json
{
  "type": "unknown_face",
  "data": {
    "bbox": [x, y, w, h],
    "snapshot": "/api/face/snapshot"
  }
}
```
- Sent additionally when an unknown face is detected, to trigger the registration UI.

### chat_response (LLM auto-greeting)
```json
{
  "type": "chat_response",
  "data": { "text": "嗨，小明！好久不见～" }
}
```

## Dashboard FacePanel

File: `frontend/src/components/FacePanel.vue`

Location: center-section, below CameraPanel.

### Sections:
1. **Recognition results** — Current detected faces with name + confidence badge
2. **Unknown face popup** — Shows snapshot + name input when unknown face detected
3. **Manual register button** — Capture from current camera + enter name
4. **Registered persons list** — All persons with delete button
5. **Appearance log** — Recent entries: who appeared when

## LLM Integration

When a known person is recognized (and not seen in last 30s):
1. Call `llm_service.chat(f"你看到了{name}，请跟他打个招呼")`
2. Push reply as `chat_response` via WebSocket
3. ChatPanel displays the greeting naturally

## Memory Management

Realistic estimates (Jetson unified memory, ~6.5GB usable after OS):
- InsightFace buffalo_s + ONNX Runtime GPU: ~400-600MB
- LLM Qwen2.5:3b (Q4_K_M via Ollama): ~2.2GB
- Total: ~3-3.5GB out of ~6.5GB available — viable but monitor usage
- If RAM pressure: face recognition loop can be paused during LLM inference

## useWebSocket.js Changes

Add new reactive refs for face events:
```javascript
const currentFaces = ref([])    // from face_event
const unknownFace = ref(null)   // from unknown_face
```
Handle in onmessage alongside existing heartbeat/sensor/chat_response handlers.

## New Dependencies

```
insightface
onnxruntime-gpu  (Jetson: use JetPack-specific wheel from https://elinux.org/Jetson_Zoo)
                 (or onnxruntime for CPU fallback)
opencv-python
aiohttp  (async HTTP to fetch snapshots)
```

### /api/status update

Add `face_recognition: true/false` to the existing status endpoint response.

## File Structure (new/modified)

```
jetson/
  app/
    face_service.py    (NEW - FaceService class)
    main.py            (MODIFIED - add face endpoints, recognition loop)
  data/
    faces.db           (AUTO-CREATED at runtime)
  requirements.txt     (MODIFIED - add new deps)
frontend/
  src/
    components/
      FacePanel.vue    (NEW)
    App.vue            (MODIFIED - add FacePanel)
    composables/
      useWebSocket.js  (MODIFIED - handle face_event, unknown_face)
```
