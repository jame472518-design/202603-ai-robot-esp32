import io
import logging
import wave
import os

logger = logging.getLogger(__name__)

PIPER_MODEL_DIR = os.path.expanduser("~/.local/share/piper")


class TTSService:
    def __init__(self, voice: str = "zh_CN-huayan-medium"):
        self.voice = voice
        self.model = None
        self._load()

    def _load(self):
        try:
            from piper import PiperVoice
            model_path = os.path.join(PIPER_MODEL_DIR, f"{self.voice}.onnx")
            if not os.path.exists(model_path):
                logger.error(f"Piper model not found: {model_path}")
                return
            self.model = PiperVoice.load(model_path)
            logger.info(f"Piper TTS loaded: {self.voice}")
        except Exception as e:
            logger.error(f"Piper TTS load error: {e}")
            self.model = None

    def synthesize(self, text: str) -> bytes:
        if not self.model:
            return b""
        try:
            buf = io.BytesIO()
            with wave.open(buf, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(self.model.config.sample_rate)
                self.model.synthesize(text, wf)
            return buf.getvalue()
        except Exception as e:
            logger.error(f"TTS error: {e}")
            return b""
