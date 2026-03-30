import asyncio
import logging
import os
import sqlite3

import cv2
import numpy as np

logger = logging.getLogger(__name__)


class FaceResult:
    __slots__ = ("name", "confidence", "bbox", "is_unknown")

    def __init__(self, name, confidence, bbox, is_unknown):
        self.name = name
        self.confidence = confidence
        self.bbox = bbox
        self.is_unknown = is_unknown


class FaceService:
    def __init__(self, db_path="data/faces.db", model_name="buffalo_s",
                 similarity_threshold=0.5):
        self.db_path = db_path
        self.model_name = model_name
        self.threshold = similarity_threshold
        self._lock = asyncio.Lock()
        self._last_snapshot = None

        os.makedirs(os.path.dirname(db_path) or "data", exist_ok=True)
        self._init_db()

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
            self._face_app.prepare(ctx_id=0, det_size=(320, 240))
            logger.info(f"InsightFace model '{self.model_name}' loaded")
        except Exception as e:
            logger.error(f"Failed to load InsightFace: {e}")
            self._face_app = None

    @property
    def available(self):
        return self._face_app is not None

    @property
    def last_snapshot(self):
        return self._last_snapshot

    def _decode_jpeg(self, jpeg_bytes):
        arr = np.frombuffer(jpeg_bytes, dtype=np.uint8)
        img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if img is None:
            raise ValueError("Failed to decode JPEG image")
        return img

    def _cosine_similarity(self, a, b):
        return float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)))

    def _get_all_embeddings(self):
        conn = sqlite3.connect(self.db_path)
        rows = conn.execute("SELECT id, name, embedding FROM persons").fetchall()
        conn.close()
        result = []
        for row in rows:
            emb = np.frombuffer(row[2], dtype=np.float32)
            result.append((row[0], row[1], emb))
        return result

    async def detect_and_recognize(self, jpeg_bytes):
        async with self._lock:
            self._last_snapshot = jpeg_bytes
            if not self.available:
                return []

            img = self._decode_jpeg(jpeg_bytes)
            # Resize to 320x240 for faster detection
            h, w = img.shape[:2]
            if w > 320:
                scale = 320 / w
                img = cv2.resize(img, (320, int(h * scale)))
            faces = self._face_app.get(img)
            if not faces:
                return []

            known_embeddings = self._get_all_embeddings()
            results = []

            for face in faces:
                bbox_raw = face.bbox.astype(int)
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

    async def register(self, name, jpeg_bytes):
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

    async def update_person(self, person_id, name=None, jpeg_bytes=None):
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

    def get_persons(self):
        conn = sqlite3.connect(self.db_path)
        rows = conn.execute("SELECT id, name, created_at FROM persons ORDER BY id").fetchall()
        conn.close()
        return [{"id": r[0], "name": r[1], "created_at": r[2]} for r in rows]

    def get_person_photo(self, person_id):
        conn = sqlite3.connect(self.db_path)
        row = conn.execute("SELECT photo FROM persons WHERE id = ?", (person_id,)).fetchone()
        conn.close()
        return row[0] if row else None

    def delete_person(self, person_id):
        conn = sqlite3.connect(self.db_path)
        conn.execute("UPDATE face_logs SET person_id = NULL WHERE person_id = ?", (person_id,))
        cursor = conn.execute("DELETE FROM persons WHERE id = ?", (person_id,))
        conn.commit()
        deleted = cursor.rowcount > 0
        conn.close()
        return deleted

    def add_log(self, person_id, person_name, confidence, photo=None):
        conn = sqlite3.connect(self.db_path)
        conn.execute(
            "INSERT INTO face_logs (person_id, person_name, confidence, photo) VALUES (?, ?, ?, ?)",
            (person_id, person_name, confidence, photo),
        )
        conn.execute("""
            DELETE FROM face_logs WHERE id NOT IN (
                SELECT id FROM face_logs ORDER BY id DESC LIMIT 500
            )
        """)
        conn.commit()
        conn.close()

    def get_logs(self, limit=50):
        conn = sqlite3.connect(self.db_path)
        rows = conn.execute(
            "SELECT id, person_id, person_name, confidence, created_at FROM face_logs ORDER BY id DESC LIMIT ?",
            (limit,),
        ).fetchall()
        conn.close()
        return [{"id": r[0], "person_id": r[1], "person_name": r[2],
                 "confidence": r[3], "created_at": r[4]} for r in rows]
