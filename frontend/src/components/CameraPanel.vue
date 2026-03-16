<template>
  <div class="camera-panel">
    <h3>Camera</h3>
    <div class="mode-switch" v-if="streamUrl">
      <button :class="{ active: mode === 'stream' }" @click="mode = 'stream'">Stream</button>
      <button :class="{ active: mode === 'snapshot' }" @click="mode = 'snapshot'">Snapshot</button>
    </div>
    <div v-if="streamUrl" class="camera-view">
      <img v-if="mode === 'stream'" :src="streamUrl" alt="Camera Stream" @error="onStreamError" />
      <img v-else :src="snapshotSrc" alt="Camera Snapshot" @load="onSnapshotLoad" @error="onSnapshotError" />
    </div>
    <div v-else class="no-camera">
      Waiting for camera stream...
    </div>
    <div v-if="streamUrl" class="camera-info">
      <span :class="['stream-badge', mode === 'stream' ? 'live' : 'snap']">
        {{ mode === 'stream' ? 'LIVE' : 'SNAPSHOT' }}
      </span>
      <span class="device-name">{{ deviceId }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onUnmounted } from 'vue'

const props = defineProps({ messages: Array, devices: Object })
const mode = ref('stream')
let snapshotTimer = null

const baseUrl = computed(() => {
  for (const [id, info] of Object.entries(props.devices)) {
    if (info.stream) {
      // Extract base IP from stream URL like http://x.x.x.x:81/stream
      const match = info.stream.match(/http:\/\/([\d.]+)/)
      return match ? match[1] : null
    }
  }
  return null
})

const streamUrl = computed(() => {
  if (!baseUrl.value) return null
  return `http://${baseUrl.value}:81/stream`
})

const captureUrl = computed(() => {
  if (!baseUrl.value) return null
  return `http://${baseUrl.value}/capture`
})

const deviceId = computed(() => {
  for (const [id, info] of Object.entries(props.devices)) {
    if (info.stream) return id
  }
  return ''
})

const snapshotSrc = ref('')

function refreshSnapshot() {
  if (captureUrl.value) {
    snapshotSrc.value = `${captureUrl.value}?t=${Date.now()}`
  }
}

function onSnapshotLoad() {
  // Refresh every 500ms for near-real-time
  snapshotTimer = setTimeout(refreshSnapshot, 500)
}

function onSnapshotError() {
  snapshotTimer = setTimeout(refreshSnapshot, 2000)
}

function onStreamError() {
  // Auto-fallback to snapshot mode
  mode.value = 'snapshot'
}

watch(mode, (newMode) => {
  clearTimeout(snapshotTimer)
  if (newMode === 'snapshot') {
    refreshSnapshot()
  }
})

watch(captureUrl, (url) => {
  if (url && mode.value === 'snapshot') {
    refreshSnapshot()
  }
})

onUnmounted(() => clearTimeout(snapshotTimer))
</script>

<style scoped>
.camera-panel { padding: 16px; }
.mode-switch {
  display: flex;
  gap: 4px;
  margin-top: 8px;
}
.mode-switch button {
  padding: 4px 12px;
  border: 1px solid #313244;
  background: #1e1e2e;
  color: #9399b2;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.8em;
}
.mode-switch button.active {
  background: #313244;
  color: #cdd6f4;
}
.camera-view {
  background: #11111b;
  border-radius: 8px;
  overflow: hidden;
  margin-top: 8px;
}
.camera-view img {
  width: 100%;
  display: block;
  border-radius: 8px;
}
.camera-info {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 8px;
}
.stream-badge {
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 0.75em;
  font-weight: bold;
}
.stream-badge.live {
  background: #f38ba8;
  color: #1e1e2e;
  animation: pulse 2s infinite;
}
.stream-badge.snap {
  background: #a6e3a1;
  color: #1e1e2e;
}
.device-name { color: #9399b2; font-size: 0.85em; }
.no-camera { color: #6c7086; font-style: italic; margin-top: 8px; }
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.6; }
}
</style>
