import logging
from ollama import Client

logger = logging.getLogger(__name__)

SYSTEM_PROMPT = """你是一个友善的AI陪伴机器人助手。你能感知环境数据（温度、湿度、光照等），
并基于这些数据与用户自然对话。回答简洁、有温度、像朋友一样。用中文回答。"""


class LLMService:
    def __init__(self, model: str, host: str):
        self.model = model
        self.client = Client(host=host)
        self.history: list[dict] = []

    def chat(self, user_message: str, sensor_context: str = "") -> str:
        if sensor_context:
            context_msg = f"[当前环境数据: {sensor_context}]\n{user_message}"
        else:
            context_msg = user_message

        self.history.append({"role": "user", "content": context_msg})

        # Keep history manageable
        messages = [{"role": "system", "content": SYSTEM_PROMPT}]
        messages += self.history[-10:]  # Last 10 turns

        try:
            response = self.client.chat(
                model=self.model,
                messages=messages,
                stream=False,
            )
            reply = response["message"]["content"]
            self.history.append({"role": "assistant", "content": reply})
            return reply
        except Exception as e:
            logger.error(f"LLM error: {e}")
            return f"抱歉，我暂时无法回答。({e})"

    def clear_history(self):
        self.history.clear()
