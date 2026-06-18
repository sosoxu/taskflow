<template>
  <div class="instance-detail">
    <el-page-header @back="goBack" title="返回" content="执行实例详情" />

    <div v-loading="loading" class="content-area">
      <!-- Instance Info Card -->
      <el-card v-if="instance" class="info-card">
        <template #header>
          <div class="card-header">
            <span>实例信息</span>
            <div class="control-buttons">
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
          <el-descriptions-item label="创建时间">{{ formatTime(instance.started_at) }}</el-descriptions-item>
          <el-descriptions-item label="更新时间">{{ formatTime(instance.finished_at) }}</el-descriptions-item>
        </el-descriptions>
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
        <el-table :data="instance.tasks" border stripe>
          <el-table-column prop="task_name" label="节点名称" min-width="120" />
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
          <el-table-column label="操作" width="200" fixed="right">
            <template #default="{ row }">
              <el-button
                v-if="row.status === 'SUCCESS' || row.status === 'FAILED' || row.status === 'TIMEOUT' || row.status === 'RUNNING'"
                type="primary"
                link
                size="small"
                @click="openLogDialog(row)"
              >查看日志</el-button>
              <el-button
                v-if="row.status === 'FAILED' || row.status === 'TIMEOUT' || row.status === 'UPSTREAM_FAILED'"
                type="warning"
                link
                size="small"
                @click="handleRetryTask(row)"
              >重试</el-button>
              <el-button
                v-if="row.status === 'RUNNING'"
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
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { getInstance, pauseInstance, resumeInstance, cancelInstance, retryTask, killTask } from '../../api/instance'
import { getTaskLogs, getTaskLogStreamUrl } from '../../api/log'
import { getWorkflow } from '../../api/workflow'
import { formatTime } from '../../utils/format'
import type { WorkflowInstance, TaskInstance, WorkflowInstanceStatus, TaskInstanceStatus } from '../../types/instance'
import type { DagGraph } from '../../types/workflow'

const route = useRoute()
const router = useRouter()

const loading = ref(false)
const instance = ref<WorkflowInstance | null>(null)
const workflowName = ref('')
const dag = ref<DagGraph | null>(null)

const logDialogVisible = ref(false)
const logContent = ref('')
const currentLogTask = ref<TaskInstance | null>(null)
const autoScroll = ref(true)
const logStreaming = ref(false)
const logContainerRef = ref<HTMLElement | null>(null)
let eventSource: EventSource | null = null

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
  const task = instance.value.tasks.find((t) => t.task_id === node.task_id)
  return task?.task_name || node.task_id
}

function nodeFill(nodeId: string): string {
  if (!instance.value) return '#f5f7fa'
  const node = dag.value?.nodes.find((n) => n.id === nodeId)
  if (!node) return '#f5f7fa'
  const task = instance.value.tasks.find((t) => t.task_id === node.task_id)
  if (!task) return '#f5f7fa'
  const colorMap: Record<string, string> = {
    PENDING: '#f5f7fa',
    DISPATCHED: '#ecf5ff',
    RUNNING: '#fdf6ec',
    SUCCESS: '#f0f9eb',
    FAILED: '#fef0f0',
    TIMEOUT: '#fef0f0',
    CANCELLED: '#f5f7fa',
    UPSTREAM_FAILED: '#fdf6ec',
    NODE_OFFLINE: '#fef0f0',
  }
  return colorMap[task.status] || '#f5f7fa'
}

async function fetchInstance() {
  const id = route.params.id as string
  if (!id) return
  loading.value = true
  try {
    const res = await getInstance(id)
    instance.value = res.data.data
    // Fetch workflow for name and DAG (only once, or when workflow_id changes)
    if (instance.value?.workflow_id && !workflowName.value) {
      const wfRes = await getWorkflow(instance.value.workflow_id)
      workflowName.value = wfRes.data?.data?.name || ''
      dag.value = wfRes.data?.data?.dag_json || null
    }
  } catch (err) {
    ElMessage.error('获取实例详情失败')
  } finally {
    loading.value = false
  }
}

async function handlePause() {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm('确定要暂停该实例吗？', '提示', { type: 'warning' })
    await pauseInstance(instance.value.id)
    ElMessage.success('已暂停')
    fetchInstance()
  } catch { /* cancelled */ }
}

async function handleResume() {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm('确定要恢复该实例吗？', '提示', { type: 'warning' })
    await resumeInstance(instance.value.id)
    ElMessage.success('已恢复')
    fetchInstance()
  } catch { /* cancelled */ }
}

async function handleCancel() {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm('确定要取消该实例吗？此操作不可撤销。', '警告', { type: 'warning' })
    await cancelInstance(instance.value.id)
    ElMessage.success('已取消')
    fetchInstance()
  } catch { /* cancelled */ }
}

async function handleRetryInstance() {
  if (!instance.value) return
  // Retry the first failed/timed-out/upstream-failed task
  const failedTask = instance.value.tasks.find(
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
    await retryTask(instance.value.id, task.id)
    ElMessage.success('已提交重试')
    fetchInstance()
  } catch { /* cancelled */ }
}

async function handleKillTask(task: TaskInstance) {
  if (!instance.value) return
  try {
    await ElMessageBox.confirm(`确定要终止任务 "${task.task_name}" 吗？`, '警告', { type: 'warning' })
    await killTask(instance.value.id, task.id)
    ElMessage.success('已终止')
    fetchInstance()
  } catch { /* cancelled */ }
}

async function openLogDialog(task: TaskInstance) {
  currentLogTask.value = task
  logContent.value = ''
  logDialogVisible.value = true
  await fetchLog()
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

  const url = getTaskLogStreamUrl(instance.value.id, currentLogTask.value.id)
  eventSource = new EventSource(url)
  logStreaming.value = true

  eventSource.onmessage = (event) => {
    logContent.value += event.data + '\n'
    if (autoScroll.value) {
      nextTick(() => scrollToBottom())
    }
  }

  eventSource.addEventListener('done', () => {
    stopLogStream()
  })

  eventSource.onerror = () => {
    stopLogStream()
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

function goBack() {
  router.back()
}

// Auto-poll for running instances
function startPolling() {
  pollTimer = setInterval(async () => {
    if (!instance.value) return
    const status = instance.value.status
    // Stop polling if instance reached a terminal state
    if (status !== 'RUNNING' && status !== 'PENDING' && status !== 'PAUSED') {
      if (pollTimer) { clearInterval(pollTimer); pollTimer = null }
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

onMounted(() => {
  fetchInstance()
  startPolling()
})

onUnmounted(() => {
  if (pollTimer) clearInterval(pollTimer)
  stopLogStream()
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
</style>
