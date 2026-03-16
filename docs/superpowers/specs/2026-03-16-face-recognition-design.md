# Face Recognition System Design

## Overview

Add face recognition to the AI Companion Robot demo. Jetson fetches snapshots from ESP32-S3-CAM, runs InsightFace (ArcFace) for detection and recognition, stores a database of known persons, and pushes results to the Web Dashboard in real-time.

## Constraints

- Jetson Orin 8GB RAM (shared with LLM Qwen2.5:3b)
- ESP32-S3-CAM OV2640, VGA 640x480 JPEG
- 5-10 persons for demo use
- ESP32 captures only, all AI processing on Jetson

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

## FaceService Module

File: `jetson/app/face_service.py`

```python
class FaceService:
    __init__(db_path="data/faces.db", model_name="buffalo_s")
        # Load InsightFace model
        # Connect SQLite, create tables

    detect_and_recognize(jpeg_bytes) -> list[FaceResult]
        # 1. Decode JPEG to numpy
        # 2. FaceAnalysis.get() to detect faces
        # 3. Extract 512-dim embedding per face
        # 4. Compare cosine similarity with all persons in DB
        # 5. similarity > 0.4 = match, else unknown
        # Return: [{name, confidence, bbox, is_unknown}]

    register(name, jpeg_bytes) -> person_id
        # 1. Detect face (must be exactly 1 face)
        # 2. Extract embedding
        # 3. Store in persons table
        # Return person_id

    get_persons() -> list[Person]
    delete_person(person_id)
    get_logs(limit=50) -> list[FaceLog]
    add_log(person_id, name, confidence, photo)
```

## Background Recognition Loop

In `main.py`, started during lifespan:

```
async face_recognition_loop():
    every 1.5 seconds:
        1. HTTP GET http://ESP32_IP/capture → jpeg_bytes
        2. face_service.detect_and_recognize(jpeg_bytes)
        3. For each face:
           - known + last seen > 30s ago → LLM greeting + WebSocket push
           - unknown → push unknown_face event
           - log to face_logs
        4. Push face_event to all WebSocket clients
```

## API Endpoints

### POST /api/face/register
- Body: `{ "name": "小明" }` (auto-capture from ESP32)
- Or: multipart form with uploaded photo
- Response: `{ "id": 1, "name": "小明" }`

### GET /api/face/persons
- Response: `[{ "id": 1, "name": "小明", "created_at": "..." }]`

### DELETE /api/face/persons/{id}
- Response: `{ "ok": true }`

### GET /api/face/logs?limit=50
- Response: `[{ "person_name": "小明", "confidence": 0.82, "created_at": "..." }]`

### GET /api/face/persons/{id}/photo
- Response: `image/jpeg`

## WebSocket Events

### face_event (known face detected)
```json
{
  "type": "face_event",
  "data": {
    "faces": [{ "name": "小明", "confidence": 0.82, "bbox": [x, y, w, h] }]
  }
}
```

### unknown_face (unregistered face detected)
```json
{
  "type": "unknown_face",
  "data": {
    "bbox": [x, y, w, h],
    "snapshot": "/api/face/snapshot"
  }
}
```

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

- InsightFace buffalo_s: ~300-500MB
- LLM Qwen2.5:3b: ~2-3GB
- Both can coexist within 8GB (total ~3-3.5GB)
- If RAM pressure: face recognition loop can be paused during LLM inference

## New Dependencies

```
insightface
onnxruntime-gpu  (or onnxruntime for CPU fallback)
opencv-python
aiohttp  (async HTTP to fetch snapshots)
```

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
