import { ref } from 'vue'

export interface UseSseLogOptions {
  /** 日志内容上限，超出则截断保留尾部（默认约 100KB，见 Fix #194） */
  maxLength?: number
  /** 最大重连次数（默认 3） */
  maxReconnect?: number
  /** 收到一条日志后触发，用于自动滚动 */
  onMessage?: () => void
  /** 收到 done 事件（服务端正常结束流） */
  onDone?: () => void
  /** 重连次数用尽，需要提示用户 */
  onReconnectExhausted?: () => void
}

// Fix #341: InstanceDetailView 内联的 SSE 日志流逻辑（Fix #192 的重连、
// Fix #194 的长度上限、Fix #355 的重连定时器清理）抽成 composable。
export function useSseLog(options: UseSseLogOptions = {}) {
  const { maxLength = 100000, maxReconnect = 3, onMessage, onDone, onReconnectExhausted } = options

  const content = ref('')
  const streaming = ref(false)

  let eventSource: EventSource | null = null
  // Fix #192: SSE 重连计数器，最多 3 次（间隔 2s/4s/6s）
  let reconnectCount = 0
  // Fix #355: 保存重连 setTimeout 句柄——关闭弹窗/卸载时清除，
  // 避免旧重连回调在新会话中再建孤儿 EventSource
  let reconnectTimer: ReturnType<typeof setTimeout> | null = null

  // Fix #194: 限制内容长度，避免无限增长导致卡顿
  function appendText(chunk: string) {
    const next = content.value + chunk + '\n'
    if (next.length > maxLength) {
      content.value = `... (log truncated, showing last ${Math.round(maxLength / 1000)}KB) ...\n` + next.slice(-maxLength)
    } else {
      content.value = next
    }
  }

  function clear() {
    content.value = ''
  }

  function close() {
    if (eventSource) {
      eventSource.close()
      eventSource = null
    }
    // Fix #355: 同时取消挂起的重连定时器
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
    streaming.value = false
  }

  // Fix #192: 抽取 EventSource 创建逻辑，支持错误后手动重连（2s/4s/6s 递增），
  // 最多重连 3 次，超限则停止并提示用户。手动关闭当前连接以避免原生自动重连
  // 与 setTimeout 重连堆叠产生多个连接。
  function open(url: string) {
    eventSource = new EventSource(url)
    streaming.value = true

    eventSource.onmessage = (event) => {
      appendText(event.data)
      onMessage?.()
    }

    eventSource.addEventListener('done', () => {
      // 正常结束，标记不再重连
      reconnectCount = maxReconnect
      onDone?.()
      close()
    })

    eventSource.onerror = () => {
      // Fix #192: 不立即放弃，先关闭当前连接再按递增间隔重连
      if (eventSource) {
        eventSource.close()
        eventSource = null
      }
      reconnectCount++
      if (reconnectCount > maxReconnect) {
        onReconnectExhausted?.()
        close()
        return
      }
      const delay = reconnectCount * 2000 // 2s, 4s, 6s
      if (reconnectTimer) clearTimeout(reconnectTimer)
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null
        // 仅在用户未主动停止时重连，避免死循环
        if (streaming.value) {
          open(url)
        }
      }, delay)
    }
  }

  /** 开始跟踪日志流：重置重连计数后建立新连接 */
  function start(url: string) {
    close()
    reconnectCount = 0
    open(url)
  }

  return { content, streaming, start, close, clear, appendText }
}
