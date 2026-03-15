<template>
  <div class="sensor-panel">
    <h3>Sensor Data</h3>
    <div v-if="latest" class="sensor-grid">
      <div class="sensor-card">
        <div class="sensor-label">Temperature</div>
        <div class="sensor-value">{{ latest.temperature?.toFixed(1) }} C</div>
      </div>
      <div class="sensor-card">
        <div class="sensor-label">Humidity</div>
        <div class="sensor-value">{{ latest.humidity?.toFixed(1) }} %</div>
      </div>
      <div class="sensor-card">
        <div class="sensor-label">Light</div>
        <div class="sensor-value">{{ latest.light }} lux</div>
      </div>
    </div>
    <div v-else class="no-data">Waiting for sensor data...</div>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({ messages: Array })

const latest = computed(() => {
  const sensorMsgs = props.messages.filter(m => m.type === 'sensor')
  if (sensorMsgs.length === 0) return null
  return sensorMsgs[sensorMsgs.length - 1].data
})
</script>

<style scoped>
.sensor-panel { padding: 16px; }
.sensor-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
.sensor-card {
  background: #1e1e2e; border-radius: 8px;
  padding: 16px; text-align: center;
}
.sensor-label { color: #9399b2; font-size: 0.85em; margin-bottom: 4px; }
.sensor-value { color: #cdd6f4; font-size: 1.5em; font-weight: bold; }
.no-data { color: #6c7086; font-style: italic; }
</style>
