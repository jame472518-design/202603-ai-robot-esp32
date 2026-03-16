<template>
  <div class="app">
    <header>
      <h1>AI Companion Robot</h1>
      <span :class="['conn-status', connected ? 'on' : 'off']">
        {{ connected ? 'Connected' : 'Disconnected' }}
      </span>
    </header>
    <main>
      <aside>
        <DevicePanel :devices="devices" />
        <SensorPanel :messages="messages" />
      </aside>
      <section class="center-section">
        <CameraPanel :messages="messages" :devices="devices" />
      </section>
      <section class="chat-section">
        <ChatPanel :messages="messages" :send="send" />
      </section>
    </main>
  </div>
</template>

<script setup>
import DevicePanel from './components/DevicePanel.vue'
import ChatPanel from './components/ChatPanel.vue'
import SensorPanel from './components/SensorPanel.vue'
import CameraPanel from './components/CameraPanel.vue'
import { useWebSocket } from './composables/useWebSocket'

const wsUrl = `ws://${window.location.hostname}:8000/ws`
const { messages, devices, connected, send } = useWebSocket(wsUrl)
</script>

<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { background: #181825; color: #cdd6f4; font-family: system-ui, sans-serif; }
.app { height: 100vh; display: flex; flex-direction: column; }
header {
  display: flex; justify-content: space-between; align-items: center;
  padding: 12px 24px; background: #1e1e2e; border-bottom: 1px solid #313244;
}
header h1 { font-size: 1.2em; }
.conn-status {
  padding: 4px 12px; border-radius: 12px; font-size: 0.8em;
}
.conn-status.on { background: #a6e3a1; color: #1e1e2e; }
.conn-status.off { background: #f38ba8; color: #1e1e2e; }
main {
  flex: 1; display: flex; overflow: hidden;
}
aside {
  width: 350px; border-right: 1px solid #313244;
  overflow-y: auto;
}
.center-section { flex: 1; border-right: 1px solid #313244; overflow-y: auto; }
.chat-section { flex: 1; }
</style>
