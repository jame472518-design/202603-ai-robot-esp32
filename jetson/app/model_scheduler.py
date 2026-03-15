import gc
import logging

logger = logging.getLogger(__name__)


class ModelScheduler:
    """Manages model loading/unloading to fit in 8GB RAM."""

    def __init__(self, stt, llm, tts):
        self.stt = stt
        self.llm = llm
        self.tts = tts

    def clear_gpu(self):
        gc.collect()
        try:
            import torch
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except ImportError:
            pass
        logger.info("GPU memory cleared")
