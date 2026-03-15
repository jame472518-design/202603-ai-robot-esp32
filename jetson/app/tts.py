import io
import logging
import subprocess
import wave

logger = logging.getLogger(__name__)


class TTSService:
    def __init__(self, voice: str = "zh_CN-huayan-medium"):
        self.voice = voice

    def synthesize(self, text: str) -> bytes:
        """Use piper CLI to synthesize speech, return WAV bytes."""
        try:
            result = subprocess.run(
                ["piper", "--model", self.voice, "--output-raw"],
                input=text.encode("utf-8"),
                capture_output=True,
                timeout=30,
            )
            if result.returncode != 0:
                logger.error(f"Piper error: {result.stderr.decode()}")
                return b""

            # Wrap raw PCM in WAV header (16-bit, mono)
            raw_audio = result.stdout
            buf = io.BytesIO()
            with wave.open(buf, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(22050)
                wf.writeframes(raw_audio)
            return buf.getvalue()
        except Exception as e:
            logger.error(f"TTS error: {e}")
            return b""
