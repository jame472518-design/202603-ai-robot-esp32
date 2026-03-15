<template>
  <div class="device-panel">
    <h3>Device Status</h3>
    <div v-if="Object.keys(devices).length === 0" class="no-devices">
      No devices connected
    </div>
    <div v-for="(info, id) in devices" :key="id" class="device-card">
      <div class="device-header">
        <span class="device-id">{{ id }}</span>
        <span :class="['status-dot', info.status === 'online' ? 'online' : 'offline']"></span>
      </div>
      <div class="device-info">
        <div>Uptime: {{ info.uptime }}s</div>
        <div>Heap: {{ (info.heap / 1024).toFixed(1) }}KB</div>
        <div>RSSI: {{ info.rssi }}dBm</div>
        <div class="last-seen">{{ info.last_seen }}</div>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({ devices: Object })
</script>

<style scoped>
.device-panel { padding: 16px; }
.device-card {
  background: #1e1e2e;
  border-radius: 8px;
  padding: 12px;
  margin-bottom: 8px;
}
.device-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.device-id { font-weight: bold; color: #cdd6f4; }
.status-dot {
  width: 10px; height: 10px;
  border-radius: 50%;
}
.status-dot.online { background: #a6e3a1; }
.status-dot.offline { background: #f38ba8; }
.device-info { font-size: 0.85em; color: #9399b2; margin-top: 8px; }
.no-devices { color: #6c7086; font-style: italic; }
</style>
