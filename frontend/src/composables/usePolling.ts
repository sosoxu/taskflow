import { onMounted, onUnmounted, ref } from 'vue'

export interface UsePollingOptions {
  /** 轮询间隔（毫秒） */
  interval: number
  /** 标签页隐藏时暂停轮询，重新可见时恢复（默认 true） */
  pauseOnHidden?: boolean
  /** 挂载后自动开始轮询（默认 true） */
  autoStart?: boolean
  /**
   * 返回 false 时自动停止轮询，重新可见时也不会恢复。
   * 用于「实例已进入终态就不再轮询」这类场景。
   */
  isActive?: () => boolean
}

// Fix #341: DashboardView / WorkerListView / InstanceDetailView 各写了一份
// setInterval + clearInterval + visibilitychange 样板（含 Fix #197 的
// 「标签页隐藏暂停轮询」逻辑），这里收敛为唯一实现。
export function usePolling(task: () => void | Promise<void>, options: UsePollingOptions) {
  const { interval, pauseOnHidden = true, autoStart = true, isActive } = options

  const isRunning = ref(false)
  let timer: ReturnType<typeof setInterval> | null = null

  async function run() {
    // 已进入终态（或不再满足条件）时自行停止，避免无意义请求
    if (isActive && !isActive()) {
      stop()
      return
    }
    await task()
  }

  function stop() {
    if (timer) {
      clearInterval(timer)
      timer = null
    }
    isRunning.value = false
  }

  function start() {
    if (timer) return
    timer = setInterval(run, interval)
    isRunning.value = true
  }

  // Fix #197: 标签页隐藏时停止轮询，可见时按 isActive 决定是否恢复
  function handleVisibilityChange() {
    if (document.hidden) {
      if (pauseOnHidden) stop()
      return
    }
    if (!isActive || isActive()) {
      start()
    }
  }

  if (pauseOnHidden) {
    onMounted(() => document.addEventListener('visibilitychange', handleVisibilityChange))
    onUnmounted(() => document.removeEventListener('visibilitychange', handleVisibilityChange))
  }

  onMounted(() => {
    if (autoStart) start()
  })
  onUnmounted(stop)

  return { isRunning, start, stop }
}
