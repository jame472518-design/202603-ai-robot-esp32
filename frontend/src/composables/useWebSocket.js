import { ref, onMounted, onUnmounted } from 'vue'

export function useWebSocket(url) {
  const messages = ref([])
  const devices = ref({})
  const connected = ref(false)
  let ws = null
  let reconnectTimer = null

  function connect() {
    ws = new WebSocket(url)

    ws.onopen = () => {
      connected.value = true
      console.log('WebSocket connected')
    }

    ws.onclose = () => {
      connected.value = false
      console.log('WebSocket disconnected, reconnecting...')
      reconnectTimer = setTimeout(connect, 3000)
    }

    ws.onmessage = (event) => {
      const data = JSON.parse(event.data)
      if (data.type === 'heartbeat') {
        devices.value = { ...devices.value, [data.device_id]: data.data }
      } else if (data.type === 'sensor') {
        messages.value = [...messages.value.slice(-199), data]
      } else if (data.type === 'chat_response') {
        messages.value = [...messages.value, data]
      }
    }
  }

  function send(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(data))
    }
  }

  onMounted(connect)
  onUnmounted(() => {
    clearTimeout(reconnectTimer)
    if (ws) ws.close()
  })

  return { messages, devices, connected, send }
}
