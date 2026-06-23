<template>
  <div class="instance-detail">
    <el-page-header @back="goBack" title="返回" content="执行实例详情" />

    <div v-loading="loading" class="content-area">
      <!-- Instance Info Card -->
      <el-card v-if="instance" class="info-card">
        <template #header>
          <div class="card-header">
            <span>实例信息</span>
            <!-- Fix #172: viewer 角色隐藏写操作按钮 -->
            <div v-if="userStore.isOperator" class="control-buttons">
              <el-button
                v-if="instance.status === 'RUNNING'"
                type="warning"
                size="small"
                @click="handlePause"
              >暂停</el-button>
              <el-button
                v-if="instance.status === 'PAUSED'"
                type="success"
                size="small"
                @click="handleResume"
              >恢复</el-button>
              <el-button
                v-if="instance.status === 'RUNNING' || instance.status === 'PAUSED'"
                type="danger"
                size="small"
                @click="handleCancel"
              >取消</el-button>
              <el-button
                v-if="instance.status === 'FAILED'"
                type="warning"
                size="small"
                @click="handleRetryInstance"
              >重试</el-button>
            </div>
          </div>
        </template>
        <el-descriptions :column="3" border>
          <el-descriptions-item label="实例 ID">{{ instance.id }}</el-descriptions-item>
          <el-descriptions-item label="工作流名称">{{ workflowName }}</el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="instanceStatusType(instance.status)">{{ instance.status }}</el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="触发类型">
            <el-tag size="small">{{ instance.trigger_type }}</el-tag>
          </el-descriptions-item>
          <!-- Fix #231: "创建时间" should bind to created_at, not started_at
               (which is null until the instance transitions to RUNNING). -->
          <el-descriptions-item label="创建时间">{{ formatTime(instance.created_at) }}</el-descriptions-item>
          <el-descriptions-item label="更新时间">{{ formatTime(instance.finished_at) }}</el-descriptions-item>
        </el-descriptions>
        <!-- Runtime parameter overrides -->
        <template v-if="instanceParamOverrides && Object.keys(instanceParamOverrides).length > 0">
          <el-divider content-position="left">运行时参数覆盖</el-divider>
          <el-descriptions :column="2" border>
            <el-descriptions-item
              v-for="(value, key) in instanceParamOverrides"
              :key="String(key)"
              :label="String(key)"
            >{{ typeof value === 'string' ? value : JSON.stringify(value) }}</el-descriptions-item>
          </el-descriptions>
        </template>
      </el-card>

      <!-- DAG Visualization -->
      <el-card v-if="instance && dag" class="dag-card">
        <template #header>
          <span>DAG 可视化</span>
        </template>
        <div class="dag-container">
          <svg :width="svgWidth" :height="svgHeight" class="dag-svg">
            <!-- Edges -->
            <line
              v-for="edge in dag.edges"
              :key="edge.source + '-' + edge.target"
              :x1="nodePositions[edge.source]?.x"
              :y1="nodePositions[edge.source]?.y"
              :x2="nodePositions[edge.target]?.x"
              :y2="nodePositions[edge.target]?.y"
              stroke="#c0c4cc"
              stroke-width="2"
            />
            <!-- Nodes -->
            <g
              v-for="node in dag.nodes"
              :key="node.id"
              :transform="`translate(${nodePositions[node.id]?.x}, ${nodePositions[node.id]?.y})`"
            >
              <rect
                :x="-60"
                :y="-20"
                width="120"
                height="40"
                rx="6"
                :fill="nodeFill(node.id)"
                stroke="#409eff"
                stroke-width="1.5"
              />
              <text
                text-anchor="middle"
                dominant-baseline="central"
                fill="#303133"
                font-size="12"
              >{{ getNodeName(node.id) }}</text>
            </g>
          </svg>
        </div>
      </el-card>

      <!-- Task Instances Table -->
      <el-card v-if="instance" class="tasks-card">
        <template #header>
          <span>任务实例</span>
        </template>
        <el-table :data="instance.task_instances" border stripe>
          <el-table-column prop="node_id" label="节点 ID" min-width="120" show-overflow-tooltip>
            <template #default="{ row }">{{ row.node_id || '-' }}</template>
          </el-table-column>
          <el-table-column prop="task_name" label="任务名称" min-width="120" />
          <el-table-column label="状态" width="140">
            <template #default="{ row }">
              <el-tag :type="taskStatusType(row.status)" size="small">{{ row.status }}</el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="worker_id" label="Worker" min-width="120">
            <template #default="{ row }">{{ row.worker_id || '-' }}</template>
          </el-table-column>
          <el-table-column prop="retry_count" label="重试次数" width="100" />
          <el-table-column label="开始时间" min-width="160">
            <template #default="{ row }">{{ formatTime(row.started_at) }}</template>
          </el-table-column>
          <el-table-column label="完成时间" min-width="160">
            <template #default="{ row }">{{ formatTime(row.finished_at) }}</template>
          </el-table-column>
          <el-table-column label="操作" width="240" fixed="right">
            <template #default="{ row }">
              <el-button
                v-if="row.status === 'SUCCESS' || row.status === 'FAILED' || row.status === 'TIMEOUT' || row.status === 'RUNNING'"
                type="primary"
                link
                size="small"
                @click="openLogDialog(row)"
              >查看日志</el-button>
              <el-button
                v-if="row.resolved_config && Object.keys(row.resolved_config).length > 0"
                type="info"
                link
                size="small"
                @click="openConfigDialog(row)"
              >执行参数</el-button>
              <!-- Fix #172: viewer 角色隐藏写操作按钮 -->
              <el-button
                v-if="userStore.isOperator && (row.status === 'FAILED' || row.status === 'TIMEOUT' || row.status === 'UPSTREAM_FAILED')"
                type="warning"
                link
                size="small"
                @click="handleRetryTask(row)"
              >重试</el-button>
              <el-button
                v-if="userStore.isOperator && row.status === 'RUNNING'"
                type="danger"
                link
                size="small"
                @click="handleKillTask(row)"
              >终止</el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-card>
    </div>

    <!-- Log Dialog -->
    <el-dialog v-model="logDialogVisible" title="任务日志" width="70%" destroy-on-close>
      <div class="log-toolbar">
        <el-switch
          v-model="autoScroll"
          active-text="自动滚动"
          inactive-text=""
          size="small"
          style="margin-right: 12px"
        />
        <el-button size="small" @click="refreshLog">刷新</el-button>
        <el-button
          v-if="!logStreaming"
          type="primary"
          size="small"
          @click="startLogStream"
        >实时跟踪</el-button>
        <el-button
          v-else
          type="danger"
          size="small"
          @click="stopLogStream"
        >停止跟踪</el-button>
      </div>
      <div ref="logContainerRef" class="log-content">
        <pre><code>{{ logContent }}</code></pre>
      </div>
    </el-dialog>

    <!-- Resolved Config Dialog -->
    <el-dialog v-model="configDialogVisible" title="任务执行参数" width="600px" destroy-on-close>
      <template v-if="currentConfigTask">
        <el-descriptions :column="1" border>
          <el-descriptions-item
            v-for="(value, key) in currentConfigTask.resolved_config"
            :key="String(key)"
            :label="String(key)"
          >
            <template v-if="typeof value === 'object' && value !== null">
              <pre class="config-json">{{ JSON.stringify(value, null, 2) }}</pre>
            </template>
            <template v-else>{{ String(value) }}</template>
          </el-descriptions-item>
        </el-descriptions>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { getInstance, pauseInstance, resumeInstance, cancelInstance, retryTask, killTask } from '../../api/instance'
import { getTaskLogs, getTaskLogStreamUrl } from '../../api/log'
import { getWorkflow } from '../../api/workflow'
import { formatTime } from '../../utils/format'
import type { WorkflowInstance, TaskInstance, WorkflowInstanceStatus, TaskInstanceStatus } from '../../types/instance'
import type { DagGraph } from '../../types/workflow'
import { useUserStore } from '../../stores/userStore'

const route = useRoute()
const router = useRouter()
// Fix #172: viewer 角色隐藏写操作按钮
const userStore = useUserStore()

const loading = ref(false)
const instance = ref<WorkflowInstance | null>(null)
const workflowName = ref('')
const dag = ref<DagGraph | null>(null)

// Computed: extract param_overrides from the workflow instance
const instanceParamOverrides = computed(() => {
  if (!instance.value?.param_overrides) return null
  const po = instance.value.param_overrides as Record<string, unknown>
  if (typeof po !== 'object' || Object.keys(po).length === 0) return null
  return po
})

const logDialogVisible = ref(false)
const logContent = ref('')
const currentLogTask = ref<TaskInstance | null>(null)
const autoScroll = ref(true)
const logStreaming = ref(false)
const logContainerRef = ref<HTMLElement | null>(null)

// Resolved config dialog state
const configDialogVisible = ref(false)
const currentConfigTask = ref<TaskInstance | null>(null)
let eventSource: EventSource | null = null
// Fix #192: SSE 重连计数器，最多重连 3 次（间隔 2s/4s/6s）
let sseReconnectCount = 0
const SSE_MAX_RECONNECT = 3
// Fix #194: logContent 最大长度限制（约 100KB），避免无限增长导致卡顿
const LOG_MAX_LENGTH = 100000

let pollTimer: ReturnType<typeof setInterval> | null = null

function instanceStatusType(status: WorkflowInstanceStatus): string {
  const map: Record<WorkflowInstanceStatus, string> = {
    PENDING: 'info',
    RUNNING: 'warning',
    SUCCESS: 'success',
    FAILED: 'danger',
    CANCELLED: 'info',
    PAUSED: 'warning',
  }
  return map[status] || 'info'
}

function taskStatusType(status: TaskInstanceStatus): string {
  const map: Record<TaskInstanceStatus, string> = {
    PENDING: 'info',
    DISPATCHED: 'info',
    RUNNING: 'warning',
    SUCCESS: 'success',
    FAILED: 'danger',
    TIMEOUT: 'danger',
    CANCELLED: 'info',
    UPSTREAM_FAILED: 'warning',
    NODE_OFFLINE: 'danger',
  }
  return map[status] || 'info'
}

// DAG layout computation
const nodeSpacingX = 180
const nodeSpacingY = 100
const svgWidth = 800
const svgHeight = 400

const nodePositions = computed(() => {
  if (!dag.value) return {} as Record<string, { x: number; y: number }>
  const positions: Record<string, { x: number; y: number }> = {}
  const nodes = dag.value.nodes
  const edges = dag.value.edges

  // Build adjacency and in-degree
  const inDegree: Record<string, number> = {}
  const children: Record<string, string[]> = {}
  nodes.forEach((n) => {
    inDegree[n.id] = 0
    children[n.id] = []
  })
  edges.forEach((e) => {
    inDegree[e.target] = (inDegree[e.target] || 0) + 1
    if (!children[e.source]) children[e.source] = []
    children[e.source]!.push(e.target)
  })

  // BFS for levels
  const levels: Record<string, number> = {}
  const queue: string[] = []
  nodes.forEach((n) => {
    if (inDegree[n.id] === 0) {
      queue.push(n.id)
      levels[n.id] = 0
    }
  })
  while (queue.length > 0) {
    const curr = queue.shift()!
    for (const child of children[curr] || []) {
      levels[child] = Math.max(levels[child] || 0, (levels[curr] || 0) + 1)
      inDegree[child] = (inDegree[child] || 0) - 1
      if (inDegree[child] === 0) {
        queue.push(child)
      }
    }
  }

  // Assign unvisited nodes
  nodes.forEach((n) => {
    if (levels[n.id] === undefined) levels[n.id] = 0
  })

  // Group by level
  const levelGroups: Record<number, string[]> = {}
  nodes.forEach((n) => {
    const lvl = levels[n.id] ?? 0
    if (!levelGroups[lvl]) levelGroups[lvl] = []
    levelGroups[lvl]!.push(n.id)
  })

  const maxLevel = Math.max(...Object.keys(levelGroups).map(Number), 0)
  const offsetX = 100
  const offsetY = 60

  for (let lvl = 0; lvl <= maxLevel; lvl++) {
    const group = levelGroups[lvl] || []
    const totalWidth = group.length * nodeSpacingX
    const startX = (svgWidth - totalWidth) / 2 + nodeSpacingX / 2
    group.forEach((nodeId, idx) => {
      positions[nodeId] = {
        x: startX + idx * nodeSpacingX,
        y: offsetY + lvl * nodeSpacingY,
      }
    })
  }

  return positions
})

function getNodeName(nodeId: string): string {
  if (!dag.value || !instance.value) return nodeId
  const node = dag.value.nodes.find((n) => n.id === nodeId)
  if (!node) return nodeId
  const task = instance.value.task_instances.find((t) => t.task_id === node.task_id)
  return task?.task_name || node.task_id
}

function nodeFill(nodeId: string): string {
  if (!instance.value) return '#f5f7fa'
  const node = dag.value?.nodes.find((n) => n.id === nodeId)
  if (!node) return '#f5f7fa'
  const task = instance.value.task_instances.find((t) => t.task_id === node.task_id)
  if (!task) return '#f5f7fa'
  // Fix #143: Colors per completed-features.md section 9.8:
  // PENDING-灰色, RUNNING-蓝色, SUCCESS-绿色, FAILED-红色, UPSTREAM_FAILED-橙色
  const colorMap: Record<string, string> = {
    PENDING: '#e9e9eb',          // 灰色
    DISPATCHED: '#ecf5ff',       // 浅蓝 (派发中，接近 RUNNING)
    RUNNING: '#ecf5ff',          // 蓝色
    SUCCESS: '#f0f9eb',          // 绿色
    FAILED: '#fef0f0',           // 红色
    TIMEOUT: '#fef0f0',          // 红色 (超时视为失败)
    CANCELLED: '#f5f7fa',        // 灰色 (取消视为待定/终止)
    UPSTREAM_FAILED: '#fdf6ec',  // 橙色
    NODE_OFFLINE: '#fef0f0',     // 红色 (节点离线视为失败)
  }
  return colorMap[task.status] || '#f5f7fa'
}

async function fetchInstance() {
  const id = route.params.id as string
  if (!id) return
  loading.value = true
  // Fix #232: Clear stale workflow name and DAG at the start of each fetch.
  // Without this, navigating from instance A (workflow X) to instance B
  // (workflow Y) briefly shows A's workflow name and DAG until B's workflow
  // fetch completes; if B has no workflow_id or the fetch fails, A's stale
  // data persists indefinitely.
  workflowName.value = ''
  dag.value = null
  try {
    const res = await getInstance(id)
    instance.value = res.data.data
    // Fetch workflow for name and DAG. Re-fetch when the workflow_id changes
    // (e.g. navigating to a different instance) so stale data isn't shown.
    if (instance.value?.workflow_id) {
      const wfRes = await getWorkflow(instance.value.workflow_id)
      workflowName.value = wfRes.data?.data?.name || ''
      dag.value = wfRes.data?.data?.dag_json || null
    }
  } catch {
    ElMessage.error('获取实例详情失败')
  } finally {
    loading.value = false
  }
}

// Fix #162: Distinguish ElMessageBox cancellation (no error message) from
// actual API failures (show error message). Previously all errors were
// silently swallowed, so the user got no feedback when the API call failed.
async function handlePause() {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm('确定要暂停该实例吗？', '提示', { type: 'warning' })
  } catch {
    return
  }
  try {
    await pauseInstance(instance.value.id)
    ElMessage.success('已暂停')
    fetchInstance()
  } catch {
    ElMessage.error('暂停失败')
  }
}

async function handleResume() {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm('确定要恢复该实例吗？', '提示', { type: 'warning' })
  } catch {
    return
  }
  try {
    await resumeInstance(instance.value.id)
    ElMessage.success('已恢复')
    fetchInstance()
  } catch {
    ElMessage.error('恢复失败')
  }
}

async function handleCancel() {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm('确定要取消该实例吗？此操作不可撤销。', '警告', { type: 'warning' })
  } catch {
    return
  }
  try {
    await cancelInstance(instance.value.id)
    ElMessage.success('已取消')
    fetchInstance()
  } catch {
    ElMessage.error('取消失败')
  }
}

async function handleRetryInstance() {
  if (!instance.value) return
  // Retry the first failed/timed-out/upstream-failed task
  const failedTask = instance.value.task_instances.find(
    (t) => t.status === 'FAILED' || t.status === 'TIMEOUT' || t.status === 'UPSTREAM_FAILED'
  )
  if (failedTask) {
    await handleRetryTask(failedTask)
  }
}

async function handleRetryTask(task: TaskInstance) {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm(`确定要重试任务 "${task.task_name}" 吗？`, '提示', { type: 'warning' })
  } catch {
    return
  }
  try {
    await retryTask(instance.value.id, task.id)
    ElMessage.success('已提交重试')
    await fetchInstance()
    // Fix #228: After a successful retry the instance transitions from a
    // terminal state (FAILED/etc.) back to RUNNING/PENDING. Polling was
    // stopped when the terminal state was reached, so it must be restarted
    // here; otherwise the UI will never refresh to show progress.
    if (
      instance.value &&
      (instance.value.status === 'RUNNING' ||
        instance.value.status === 'PENDING' ||
        instance.value.status === 'PAUSED') &&
      !pollTimer
    ) {
      startPolling()
    }
  } catch {
    ElMessage.error('重试失败')
  }
}

async function handleKillTask(task: TaskInstance) {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm(`确定要终止任务 "${task.task_name}" 吗？`, '警告', { type: 'warning' })
  } catch {
    return
  }
  try {
    await killTask(instance.value.id, task.id)
    ElMessage.success('已终止')
    fetchInstance()
  } catch {
    ElMessage.error('终止失败')
  }
}

async function openLogDialog(task: TaskInstance) {
  currentLogTask.value = task
  logContent.value = ''
  logDialogVisible.value = true
  await fetchLog()
}

function openConfigDialog(task: TaskInstance) {
  currentConfigTask.value = task
  configDialogVisible.value = true
}

async function fetchLog() {
  if (!instance.value || !currentLogTask.value) return
  try {
    const res = await getTaskLogs(instance.value.id, currentLogTask.value.id)
    const logData = res.data?.data?.log || res.data?.data || ''
    logContent.value = typeof logData === 'string' ? logData : JSON.stringify(logData, null, 2)
    if (autoScroll.value) {
      await nextTick()
      scrollToBottom()
    }
  } catch {
    logContent.value = '获取日志失败'
  }
}

function startLogStream() {
  if (!instance.value || !currentLogTask.value) return
  stopLogStream()
  // Fix #192: 重置重连计数器
  sseReconnectCount = 0
  createEventSource()
}

// Fix #192: 抽取 EventSource 创建逻辑，支持错误后手动重连（2s/4s/6s 递增），
// 最多重连 3 次，超限则停止并提示用户。手动关闭当前连接以避免原生自动重连
// 与 setTimeout 重连堆叠产生多个连接。
function createEventSource() {
  if (!instance.value || !currentLogTask.value) return
  const url = getTaskLogStreamUrl(instance.value.id, currentLogTask.value.id)
  eventSource = new EventSource(url)
  logStreaming.value = true

  eventSource.onmessage = (event) => {
    // Fix #194: 通过 appendLog 限制日志长度，避免无限增长
    appendLog(event.data)
    if (autoScroll.value) {
      nextTick(() => scrollToBottom())
    }
  }

  eventSource.addEventListener('done', () => {
    // 正常结束，标记不再重连
    sseReconnectCount = SSE_MAX_RECONNECT
    stopLogStream()
  })

  eventSource.onerror = () => {
    // Fix #192: 不立即放弃，先关闭当前连接再按递增间隔重连
    if (eventSource) {
      eventSource.close()
      eventSource = null
    }
    sseReconnectCount++
    if (sseReconnectCount > SSE_MAX_RECONNECT) {
      ElMessage.warning('日志实时跟踪连接失败，已停止重连，请点击"实时跟踪"重试')
      stopLogStream()
      return
    }
    const delay = sseReconnectCount * 2000 // 2s, 4s, 6s
    setTimeout(() => {
      // 仅在用户未主动停止时重连，避免死循环
      if (logStreaming.value) {
        createEventSource()
      }
    }, delay)
  }
}

// Fix #194: 限制 logContent 最大长度为 100KB，超过则截断保留后半部分并加提示
function appendLog(chunk: string) {
  const next = logContent.value + chunk + '\n'
  if (next.length > LOG_MAX_LENGTH) {
    logContent.value = '... (log truncated, showing last 100KB) ...\n' + next.slice(-LOG_MAX_LENGTH)
  } else {
    logContent.value = next
  }
}

function stopLogStream() {
  if (eventSource) {
    eventSource.close()
    eventSource = null
  }
  logStreaming.value = false
}

function scrollToBottom() {
  if (logContainerRef.value) {
    logContainerRef.value.scrollTop = logContainerRef.value.scrollHeight
  }
}

function refreshLog() {
  stopLogStream()
  fetchLog()
}

// Fix #162: Stop the SSE log stream when the log dialog is closed, otherwise
// the EventSource keeps consuming resources in the background.
watch(logDialogVisible, (visible) => {
  if (!visible) {
    stopLogStream()
  }
})

function goBack() {
  router.back()
}

// Auto-poll for running instances
// Fix #192/#197: 停止轮询辅助函数
function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

function startPolling() {
  pollTimer = setInterval(async () => {
    if (!instance.value) return
    const status = instance.value.status
    // Stop polling if instance reached a terminal state
    if (status !== 'RUNNING' && status !== 'PENDING' && status !== 'PAUSED') {
      stopPolling()
      return
    }
    // Poll without loading spinner
    const id = route.params.id as string
    if (!id) return
    try {
      const res = await getInstance(id)
      instance.value = res.data.data
    } catch { /* ignore poll errors */ }
  }, 5000)
}

// Fix #197: 标签页隐藏时停止轮询，可见时若实例运行中则恢复轮询
function handleVisibilityChange() {
  if (document.hidden) {
    stopPolling()
  } else {
    if (instance.value && (instance.value.status === 'RUNNING' || instance.value.status === 'PENDING' || instance.value.status === 'PAUSED')) {
      if (!pollTimer) startPolling()
    }
  }
}

onMounted(() => {
  fetchInstance()
  startPolling()
  // Fix #197: 监听标签页可见性变化
  document.addEventListener('visibilitychange', handleVisibilityChange)
})

// Fix #161/#192: Re-fetch when the route param changes (e.g. navigating from one
// instance detail to another without unmounting). 切换实例时先停止旧轮询与日志流，
// 拉取新实例后根据其状态决定是否重启轮询（从已结束实例导航到运行中实例时轮询需重启）。
watch(() => route.params.id, async (newId, oldId) => {
  if (newId && newId !== oldId) {
    stopLogStream()
    stopPolling()
    await fetchInstance()
    if (instance.value && (instance.value.status === 'RUNNING' || instance.value.status === 'PENDING' || instance.value.status === 'PAUSED')) {
      startPolling()
    }
  }
})

onUnmounted(() => {
  stopPolling()
  stopLogStream()
  // Fix #197: 移除可见性监听
  document.removeEventListener('visibilitychange', handleVisibilityChange)
})
</script>

<style scoped>
.instance-detail {
  padding: 20px;
}

.content-area {
  margin-top: 20px;
}

.info-card {
  margin-bottom: 20px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.control-buttons {
  display: flex;
  gap: 8px;
}

.dag-card {
  margin-bottom: 20px;
}

.dag-container {
  overflow-x: auto;
}

.dag-svg {
  display: block;
  margin: 0 auto;
}

.tasks-card {
  margin-bottom: 20px;
}

.log-toolbar {
  margin-bottom: 12px;
  text-align: right;
}

.log-content {
  background: #1e1e1e;
  border-radius: 4px;
  padding: 16px;
  max-height: 500px;
  overflow-y: auto;
}

.log-content pre {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-all;
}

.log-content code {
  color: #d4d4d4;
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
  font-size: 13px;
  line-height: 1.6;
}

.config-json {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-all;
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
  font-size: 13px;
  line-height: 1.5;
  background: #f5f7fa;
  padding: 8px;
  border-radius: 4px;
}
</style>
