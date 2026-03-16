<template>
  <div class="camera-panel">
    <h3>Camera</h3>
    <div v-if="streamUrl" class="camera-view">
      <img :src="streamUrl" alt="Camera Stream" @error="onStreamError" />
    </div>
    <div v-else class="no-camera">
      Waiting for camera stream...
    </div>
    <div v-if="streamUrl" class="camera-info">
      <span class="stream-badge live">LIVE</span>
      <span class="device-name">{{ deviceId }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, computed } from 'vue'

const props = defineProps({ messages: Array, devices: Object })
const streamError = ref(false)

const streamUrl = computed(() => {
  // Get stream URL from device heartbeat data
  for (const [id, info] of Object.entries(props.devices)) {
    if (info.stream) {
      return info.stream
    }
  }
  return null
})

const deviceId = computed(() => {
  for (const [id, info] of Object.entries(props.devices)) {
    if (info.stream) return id
  }
  return ''
})

function onStreamError() {
  streamError.value = true
  // Retry after 3 seconds
  setTimeout(() => { streamError.value = false }, 3000)
}
</script>

<style scoped>
.camera-panel { padding: 16px; }
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
.device-name { color: #9399b2; font-size: 0.85em; }
.no-camera { color: #6c7086; font-style: italic; margin-top: 8px; }
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.6; }
}
</style>
