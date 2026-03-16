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
