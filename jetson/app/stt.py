import logging
import numpy as np
from faster_whisper import WhisperModel

logger = logging.getLogger(__name__)


class STTService:
    def __init__(self, model_size: str = "small"):
        self.model_size = model_size
        self.model = None

    def load(self):
        if self.model is None:
            logger.info(f"Loading Whisper {self.model_size}...")
            self.model = WhisperModel(
                self.model_size,
                device="cpu",
                compute_type="int8",
            )
            logger.info("Whisper loaded")

    def unload(self):
        if self.model is not None:
            del self.model
            self.model = None
            logger.info("Whisper unloaded")

    def transcribe(self, audio_bytes: bytes) -> str:
        self.load()
        audio_array = np.frombuffer(audio_bytes, dtype=np.int16).astype(np.float32) / 32768.0
        segments, info = self.model.transcribe(
            audio_array,
            language="zh",
            beam_size=3,
            vad_filter=True,
        )
        text = "".join(segment.text for segment in segments)
        logger.info(f"Transcribed: {text}")
        return text.strip()
