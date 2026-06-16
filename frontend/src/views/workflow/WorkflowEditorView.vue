<template>
  <div class="workflow-editor">
    <div class="page-header">
      <h2>{{ isEdit ? '编辑工作流' : '创建工作流' }}</h2>
      <div>
        <el-button @click="handleCancel">取消</el-button>
        <el-button type="primary" :loading="saving" @click="handleSave">保存</el-button>
      </div>
    </div>

    <el-card class="section-card">
      <template #header>基本信息</template>
      <el-form :model="form" label-width="120px">
        <el-form-item label="名称" required>
          <el-input v-model="form.name" placeholder="请输入工作流名称" />
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" type="textarea" :rows="3" placeholder="请输入工作流描述" />
        </el-form-item>
        <el-form-item label="调度策略">
          <el-select v-model="form.schedule_strategy" placeholder="请选择调度策略">
            <el-option label="随机" value="random" />
            <el-option label="负载均衡" value="load_balance" />
            <el-option label="指定节点" value="specified" />
          </el-select>
        </el-form-item>
        <el-form-item v-if="form.schedule_strategy === 'specified'" label="目标 Worker">
          <el-select v-model="form.target_worker_id" placeholder="请选择目标 Worker" :loading="workersLoading">
            <el-option
              v-for="w in workers"
              :key="w.id"
              :label="w.name"
              :value="w.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="启用 Cron">
          <el-switch v-model="form.cron_enabled" />
        </el-form-item>
        <el-form-item v-if="form.cron_enabled" label="Cron 表达式">
          <el-input v-model="form.cron_expression" placeholder="例如: 0 8 * * *" />
        </el-form-item>
      </el-form>
    </el-card>

    <el-card class="section-card">
      <template #header>DAG 编辑器</template>
      <div class="dag-editor">
        <div class="dag-tasks-panel">
          <h4>可用任务</h4>
          <div v-loading="tasksLoading" class="task-list">
            <div
              v-for="task in availableTasks"
              :key="task.id"
              class="task-item"
              @click="addNode(task)"
            >
              <span class="task-name">{{ task.name }}</span>
              <el-tag size="small" :type="taskTypeTag(task.type)">{{ task.type }}</el-tag>
            </div>
            <el-empty v-if="!tasksLoading && availableTasks.length === 0" description="暂无可用任务" />
          </div>
        </div>

        <div
          ref="canvasRef"
          class="dag-canvas"
          @click="handleCanvasClick"
          @contextmenu.prevent
        >
          <svg class="dag-edges">
            <defs>
              <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="10" refY="3.5" orient="auto">
                <polygon points="0 0, 10 3.5, 0 7" fill="#409EFF" />
              </marker>
            </defs>
            <line
              v-for="(edge, idx) in dagEdges"
              :key="'e-' + idx"
              :x1="getNodeCenter(edge.source).x"
              :y1="getNodeCenter(edge.source).y"
              :x2="getNodeCenter(edge.target).x"
              :y2="getNodeCenter(edge.target).y"
              :class="{ 'edge-selected': selectedEdgeIndex === idx }"
              class="dag-edge"
              marker-end="url(#arrowhead)"
              @click.stop="selectEdge(idx)"
            />
          </svg>

          <div
            v-for="node in dagNodes"
            :key="node.id"
            :class="['dag-node', { 'node-selected': selectedNodeId === node.id, 'node-connecting': connectingFromId === node.id }]"
            :style="{ left: node.x + 'px', top: node.y + 'px' }"
            @mousedown.stop="startDrag(node, $event)"
            @click.stop="handleNodeClick(node)"
          >
            <div class="node-content">
              <span class="node-name">{{ node.task_name }}</span>
              <el-tag size="small" :type="taskTypeTag(node.task_type)">{{ node.task_type }}</el-tag>
            </div>
            <el-button
              class="node-delete"
              type="danger"
              :icon="Delete"
              circle
              size="small"
              @click.stop="removeNode(node.id)"
            />
          </div>

          <div v-if="dagNodes.length === 0" class="canvas-empty">
            点击左侧任务添加节点到画布
          </div>
        </div>
      </div>

      <div class="dag-toolbar">
        <el-button size="small" :disabled="selectedEdgeIndex < 0" @click="removeSelectedEdge">
          删除选中连线
        </el-button>
        <span v-if="connectingFromId" class="connecting-hint">
          正在连线 — 点击目标节点完成连线，点击空白处取消
        </span>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Delete } from '@element-plus/icons-vue'
import { getWorkflow, createWorkflow, updateWorkflow } from '../../api/workflow'
import { getTasks } from '../../api/task'
import { getWorkers } from '../../api/worker'
import type { TaskItem } from '../../types/task'
import type { WorkerInfo } from '../../types/worker'

interface DagNodeData {
  id: string
  task_id: string
  task_name: string
  task_type: string
  x: number
  y: number
}

interface DagEdgeData {
  source: string
  target: string
}

const route = useRoute()
const router = useRouter()

const workflowId = computed(() => route.params.id as string)
const isEdit = computed(() => !!workflowId.value)

const saving = ref(false)

const form = reactive({
  name: '',
  description: '',
  schedule_strategy: 'random' as 'random' | 'load_balance' | 'specified',
  target_worker_id: '',
  cron_enabled: false,
  cron_expression: '',
})

// Workers
const workers = ref<WorkerInfo[]>([])
const workersLoading = ref(false)

async function fetchWorkers() {
  workersLoading.value = true
  try {
    const { data } = await getWorkers()
    workers.value = data.items || data || []
  } catch {
    // silently fail
  } finally {
    workersLoading.value = false
  }
}

// Available tasks
const availableTasks = ref<TaskItem[]>([])
const tasksLoading = ref(false)

async function fetchTasks() {
  tasksLoading.value = true
  try {
    const { data } = await getTasks({ page: 1, page_size: 200 })
    availableTasks.value = data.items || []
  } catch {
    // silently fail
  } finally {
    tasksLoading.value = false
  }
}

// DAG state
const dagNodes = ref<DagNodeData[]>([])
const dagEdges = ref<DagEdgeData[]>([])
const selectedNodeId = ref<string | null>(null)
const selectedEdgeIndex = ref(-1)
const connectingFromId = ref<string | null>(null)
const canvasRef = ref<HTMLElement | null>(null)

// Drag state
let dragNode: DagNodeData | null = null
let dragOffsetX = 0
let dragOffsetY = 0

function taskTypeTag(type: string) {
  const map: Record<string, string> = {
    command: '',
    script: 'success',
    sql: 'warning',
  }
  return map[type] || 'info'
}

let nodeCounter = 0
function addNode(task: TaskItem) {
  nodeCounter++
  const id = `node_${nodeCounter}_${Date.now()}`
  // Place new nodes with some offset based on existing count
  const offset = dagNodes.value.length * 30
  dagNodes.value.push({
    id,
    task_id: task.id,
    task_name: task.name,
    task_type: task.type,
    x: 60 + offset,
    y: 60 + offset,
  })
}

function removeNode(nodeId: string) {
  dagNodes.value = dagNodes.value.filter((n) => n.id !== nodeId)
  dagEdges.value = dagEdges.value.filter((e) => e.source !== nodeId && e.target !== nodeId)
  if (selectedNodeId.value === nodeId) selectedNodeId.value = null
  if (connectingFromId.value === nodeId) connectingFromId.value = null
}

function handleNodeClick(node: DagNodeData) {
  if (connectingFromId.value) {
    // Complete the connection
    if (connectingFromId.value !== node.id) {
      const exists = dagEdges.value.some(
        (e) => e.source === connectingFromId.value && e.target === node.id,
      )
      if (!exists) {
        dagEdges.value.push({ source: connectingFromId.value!, target: node.id })
      }
    }
    connectingFromId.value = null
  } else {
    // Start connecting
    selectedNodeId.value = node.id
    selectedEdgeIndex.value = -1
    connectingFromId.value = node.id
  }
}

function handleCanvasClick() {
  // Cancel connecting mode
  connectingFromId.value = null
  selectedNodeId.value = null
  selectedEdgeIndex.value = -1
}

function selectEdge(idx: number) {
  selectedEdgeIndex.value = idx
  selectedNodeId.value = null
  connectingFromId.value = null
}

function removeSelectedEdge() {
  if (selectedEdgeIndex.value >= 0) {
    dagEdges.value.splice(selectedEdgeIndex.value, 1)
    selectedEdgeIndex.value = -1
  }
}

function getNodeCenter(nodeId: string): { x: number; y: number } {
  const node = dagNodes.value.find((n) => n.id === nodeId)
  if (!node) return { x: 0, y: 0 }
  // Node width ~180, height ~50
  return { x: node.x + 90, y: node.y + 25 }
}

// Drag
function startDrag(node: DagNodeData, event: MouseEvent) {
  // Only drag with left button and not in connecting mode
  if (connectingFromId.value) return
  dragNode = node
  dragOffsetX = event.clientX - node.x
  dragOffsetY = event.clientY - node.y

  const onMouseMove = (e: MouseEvent) => {
    if (!dragNode) return
    dragNode.x = e.clientX - dragOffsetX
    dragNode.y = e.clientY - dragOffsetY
  }

  const onMouseUp = () => {
    dragNode = null
    document.removeEventListener('mousemove', onMouseMove)
    document.removeEventListener('mouseup', onMouseUp)
  }

  document.addEventListener('mousemove', onMouseMove)
  document.addEventListener('mouseup', onMouseUp)
}

// Load workflow for editing
async function loadWorkflow() {
  if (!workflowId.value) return
  try {
    const { data } = await getWorkflow(workflowId.value)
    form.name = data.name || ''
    form.description = data.description || ''
    form.schedule_strategy = data.schedule_strategy || 'random'
    form.target_worker_id = data.target_worker_id || ''
    form.cron_enabled = data.cron_enabled || false
    form.cron_expression = data.cron_expression || ''

    if (data.dag) {
      // Map DAG nodes from API format to editor format
      const apiNodes = data.dag.nodes || []
      dagNodes.value = apiNodes.map((n: any, i: number) => ({
        id: n.id,
        task_id: n.task_id,
        task_name: n.task_name || n.task_id,
        task_type: n.task_type || 'command',
        x: n.x ?? 60 + i * 200,
        y: n.y ?? 60,
      }))
      dagEdges.value = (data.dag.edges || []).map((e: any) => ({
        source: e.source,
        target: e.target,
      }))
      // Update counter to avoid id collisions
      nodeCounter = dagNodes.value.length + 1
    }
  } catch {
    ElMessage.error('加载工作流失败')
  }
}

async function handleSave() {
  if (!form.name.trim()) {
    ElMessage.warning('请输入工作流名称')
    return
  }

  saving.value = true
  try {
    const dag = {
      nodes: dagNodes.value.map((n) => ({
        id: n.id,
        task_id: n.task_id,
        task_name: n.task_name,
        task_type: n.task_type,
        x: n.x,
        y: n.y,
      })),
      edges: dagEdges.value.map((e) => ({
        source: e.source,
        target: e.target,
      })),
    }

    const payload: any = {
      name: form.name,
      description: form.description,
      dag,
      schedule_strategy: form.schedule_strategy,
      cron_enabled: form.cron_enabled,
      cron_expression: form.cron_enabled ? form.cron_expression : undefined,
      target_worker_id: form.schedule_strategy === 'specified' ? form.target_worker_id : undefined,
    }

    if (isEdit.value) {
      await updateWorkflow(workflowId.value, payload)
      ElMessage.success('更新成功')
    } else {
      await createWorkflow(payload)
      ElMessage.success('创建成功')
    }
    router.push({ name: 'workflow-list' })
  } catch {
    ElMessage.error(isEdit.value ? '更新失败' : '创建失败')
  } finally {
    saving.value = false
  }
}

function handleCancel() {
  router.push({ name: 'workflow-list' })
}

onMounted(() => {
  fetchTasks()
  fetchWorkers()
  if (isEdit.value) {
    loadWorkflow()
  }
})
</script>

<style scoped>
.workflow-editor {
  padding: 20px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.page-header h2 {
  margin: 0;
  font-size: 20px;
  color: #303133;
}

.section-card {
  margin-bottom: 20px;
}

.dag-editor {
  display: flex;
  gap: 16px;
  height: 500px;
}

.dag-tasks-panel {
  width: 220px;
  flex-shrink: 0;
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.dag-tasks-panel h4 {
  margin: 0;
  padding: 12px;
  background: #f5f7fa;
  border-bottom: 1px solid #e4e7ed;
  font-size: 14px;
  color: #303133;
}

.task-list {
  flex: 1;
  overflow-y: auto;
  padding: 8px;
}

.task-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 10px;
  margin-bottom: 4px;
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  cursor: pointer;
  transition: background 0.2s;
}

.task-item:hover {
  background: #ecf5ff;
  border-color: #b3d8ff;
}

.task-name {
  font-size: 13px;
  color: #303133;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 120px;
}

.dag-canvas {
  flex: 1;
  position: relative;
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  background: #fafafa;
  background-image:
    radial-gradient(circle, #dcdfe6 1px, transparent 1px);
  background-size: 20px 20px;
  overflow: hidden;
}

.dag-edges {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
}

.dag-edges line {
  pointer-events: stroke;
}

.dag-edge {
  stroke: #409EFF;
  stroke-width: 2;
  cursor: pointer;
}

.dag-edge:hover {
  stroke-width: 3;
}

.edge-selected {
  stroke: #F56C6C;
  stroke-width: 3;
}

.dag-node {
  position: absolute;
  width: 180px;
  background: #fff;
  border: 2px solid #409EFF;
  border-radius: 6px;
  padding: 10px 12px;
  cursor: grab;
  user-select: none;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
  transition: box-shadow 0.2s;
}

.dag-node:hover {
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.dag-node:active {
  cursor: grabbing;
}

.node-selected {
  border-color: #E6A23C;
  box-shadow: 0 0 0 3px rgba(230, 162, 60, 0.2);
}

.node-connecting {
  border-color: #67C23A;
  box-shadow: 0 0 0 3px rgba(103, 194, 78, 0.2);
}

.node-content {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}

.node-name {
  font-size: 13px;
  font-weight: 500;
  color: #303133;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  flex: 1;
}

.node-delete {
  position: absolute;
  top: -8px;
  right: -8px;
  width: 20px !important;
  height: 20px !important;
  opacity: 0;
  transition: opacity 0.2s;
}

.dag-node:hover .node-delete {
  opacity: 1;
}

.canvas-empty {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  color: #c0c4cc;
  font-size: 14px;
}

.dag-toolbar {
  margin-top: 12px;
  display: flex;
  align-items: center;
  gap: 12px;
}

.connecting-hint {
  font-size: 13px;
  color: #67C23A;
}
</style>
