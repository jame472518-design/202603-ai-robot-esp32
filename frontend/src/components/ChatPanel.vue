<template>
  <div class="chat-panel">
    <h3>Chat</h3>
    <div class="chat-messages" ref="messagesEl">
      <div v-for="(msg, i) in chatMessages" :key="i"
           :class="['message', msg.role]">
        {{ msg.text }}
      </div>
    </div>
    <div class="chat-input">
      <input v-model="input" @keyup.enter="sendMessage"
             placeholder="Type a message..." />
      <button @click="sendMessage">Send</button>
      <button @click="voiceChat" :disabled="recording" :class="{ recording: recording }">
        {{ recording ? 'Recording...' : 'ESP32 Mic' }}
      </button>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, nextTick } from 'vue'

const props = defineProps({ messages: Array, send: Function })
const input = ref('')
const chatMessages = ref([])
const messagesEl = ref(null)
const recording = ref(false)
const apiBase = `http://${window.location.hostname}:8000`

function sendMessage() {
  if (!input.value.trim()) return
  chatMessages.value.push({ role: 'user', text: input.value })
  props.send({ type: 'chat', text: input.value })
  input.value = ''
  scrollToBottom()
}

async function voiceChat() {
  recording.value = true
  chatMessages.value.push({ role: 'user', text: '(Recording from ESP32 mic...)' })
  scrollToBottom()

  try {
    const res = await fetch(`${apiBase}/api/voice-chat?seconds=3`, { method: 'POST' })

    const inputText = res.headers.get('X-Input-Text')
    const replyText = res.headers.get('X-Reply-Text')

    if (inputText) {
      // TTS audio response
      const lastUserMsg = [...chatMessages.value].reverse().find(m => m.role === 'user')
      if (lastUserMsg) lastUserMsg.text = `🎤 ${inputText}`
      chatMessages.value.push({ role: 'assistant', text: replyText || '...' })

      // Play TTS audio through browser speaker
      const blob = await res.blob()
      if (blob.size > 0) {
        const url = URL.createObjectURL(blob)
        const audio = new Audio(url)
        audio.play()
      }
    } else {
      // JSON response (no TTS)
      const data = await res.json()
      const lastUserMsg = [...chatMessages.value].reverse().find(m => m.role === 'user')
      if (lastUserMsg) {
        lastUserMsg.text = data.input ? `🎤 ${data.input}` : '(no speech detected)'
      }
      if (data.reply) {
        chatMessages.value.push({ role: 'assistant', text: data.reply })
      } else if (data.error) {
        chatMessages.value.push({ role: 'assistant', text: `Error: ${data.error}` })
      }
    }
  } catch (err) {
    chatMessages.value.push({ role: 'assistant', text: 'Voice chat error: ' + err })
  }

  recording.value = false
  scrollToBottom()
}

watch(() => props.messages, (msgs) => {
  const last = msgs[msgs.length - 1]
  if (last?.type === 'chat_response') {
    chatMessages.value.push({ role: 'assistant', text: last.data.text })
    scrollToBottom()
  }
}, { deep: true })

function scrollToBottom() {
  nextTick(() => {
    if (messagesEl.value) {
      messagesEl.value.scrollTop = messagesEl.value.scrollHeight
    }
  })
}
</script>

<style scoped>
.chat-panel { padding: 16px; display: flex; flex-direction: column; height: 100%; }
.chat-messages {
  flex: 1; overflow-y: auto;
  padding: 8px; background: #11111b;
  border-radius: 8px; margin-bottom: 8px;
}
.message {
  padding: 8px 12px; border-radius: 12px;
  margin-bottom: 6px; max-width: 80%;
}
.message.user {
  background: #89b4fa; color: #1e1e2e;
  margin-left: auto;
}
.message.assistant {
  background: #313244; color: #cdd6f4;
}
.chat-input { display: flex; gap: 8px; }
.chat-input input {
  flex: 1; padding: 8px 12px;
  background: #1e1e2e; border: 1px solid #45475a;
  color: #cdd6f4; border-radius: 8px; outline: none;
}
.chat-input button {
  padding: 8px 16px; background: #89b4fa;
  color: #1e1e2e; border: none; border-radius: 8px;
  cursor: pointer; font-weight: bold;
}
.chat-input button:disabled {
  opacity: 0.5; cursor: not-allowed;
}
.chat-input button.recording {
  background: #f38ba8;
  animation: pulse 1s infinite;
}
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.6; }
}
</style>
