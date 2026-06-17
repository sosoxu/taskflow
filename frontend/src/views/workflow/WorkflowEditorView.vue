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
      <template #header>DAG 编辑器（Vue Flow）</template>
      <div class="dag-editor">
        <div class="dag-tasks-panel">
          <h4>可用任务</h4>
          <div v-loading="tasksLoading" class="task-list">
            <div
              v-for="task in availableTasks"
              :key="task.id"
              class="task-item"
              draggable="true"
              @dragstart="onDragStart($event, task)"
              @click="addNode(task)"
            >
              <span class="task-name">{{ task.name }}</span>
              <el-tag size="small" :type="taskTypeTag(task.type)">{{ task.type }}</el-tag>
            </div>
            <el-empty v-if="!tasksLoading && availableTasks.length === 0" description="暂无可用任务" />
          </div>
        </div>

        <div class="dag-canvas" @drop="onDrop" @dragover="onDragOver">
          <VueFlow
            v-model:nodes="vfNodes"
            v-model:edges="vfEdges"
            :default-viewport="{ zoom: 1, x: 0, y: 0 }"
            :min-zoom="0.3"
            :max-zoom="2"
            fit-view-on-init
            @node-click="onNodeClick"
            @edge-click="onEdgeClick"
            @connect="onConnect"
          >
            <Background :gap="20" />
            <Controls />
            <template #node-custom="customNodeProps">
              <div :class="['custom-node', { 'node-selected': selectedNodeId === customNodeProps.id }]">
                <div class="node-header">
                  <span class="node-name">{{ customNodeProps.data.label }}</span>
                  <el-tag size="small" :type="taskTypeTag(customNodeProps.data.taskType)">
                    {{ customNodeProps.data.taskType }}
                  </el-tag>
                </div>
                <Handle type="target" :position="Position.Left" />
                <Handle type="source" :position="Position.Right" />
                <el-button
                  class="node-delete"
                  type="danger"
                  :icon="Delete"
                  circle
                  size="small"
                  @click.stop="removeNode(customNodeProps.id)"
                />
              </div>
            </template>
          </VueFlow>
        </div>
      </div>

      <div class="dag-toolbar">
        <el-button size="small" :disabled="!selectedEdgeId" @click="removeSelectedEdge">
          删除选中连线
        </el-button>
        <span class="toolbar-hint">拖拽任务到画布或点击添加 | 拖拽连接点创建依赖</span>
      </div>

      <!-- Node Parameter Overrides Panel -->
      <el-card v-if="selectedNodeId" class="section-card">
        <template #header>
          <span>节点参数覆盖 - {{ selectedNodeName }}</span>
          <el-button style="float: right" size="small" @click="selectedNodeId = null">关闭</el-button>
        </template>
        <el-form label-width="120px">
          <el-form-item label="超时覆盖">
            <el-input-number v-model="nodeOverrides.timeout" :min="0" :max="86400" placeholder="留空使用默认" clearable />
          </el-form-item>
          <el-form-item label="最大重试覆盖">
            <el-input-number v-model="nodeOverrides.max_retries" :min="0" :max="10" placeholder="留空使用默认" clearable />
          </el-form-item>
          <el-form-item label="重试间隔覆盖">
            <el-input-number v-model="nodeOverrides.retry_interval" :min="0" :max="3600" placeholder="留空使用默认" clearable />
          </el-form-item>
          <el-form-item>
            <el-button size="small" type="primary" @click="applyOverrides">应用</el-button>
            <el-button size="small" @click="clearOverrides">清除覆盖</el-button>
          </el-form-item>
        </el-form>
      </el-card>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Delete } from '@element-plus/icons-vue'
import { VueFlow, Handle, Position, type Node, type Edge } from '@vue-flow/core'
import { Background } from '@vue-flow/background'
import { Controls } from '@vue-flow/controls'
import '@vue-flow/core/dist/style.css'
import '@vue-flow/core/dist/theme-default.css'
import '@vue-flow/controls/dist/style.css'
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
  param_overrides?: Record<string, unknown>
}

interface DagEdgeData {
  source: string
  target: string
}

interface ApiDagNode {
  id: string
  task_id: string
  task_name?: string
  task_type?: string
  x?: number
  y?: number
  param_overrides?: Record<string, unknown>
}

interface ApiDagEdge {
  source: string
  target: string
}

interface WorkflowPayload {
  name: string
  description: string
  dag: { nodes: unknown[]; edges: unknown[] }
  schedule_strategy: string
  cron_enabled: boolean
  cron_expression?: string
  target_worker_id?: string
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

// Vue Flow nodes and edges
const vfNodes = ref<Node[]>([])
const vfEdges = ref<Edge[]>([])

const selectedNodeId = ref<string | null>(null)
const selectedEdgeId = ref<string | null>(null)

const nodeOverrides = reactive({
  timeout: undefined as number | undefined,
  max_retries: undefined as number | undefined,
  retry_interval: undefined as number | undefined,
})

// Internal data store for DAG nodes (to track task_id, param_overrides etc.)
const dagNodeDataMap = ref<Map<string, DagNodeData>>(new Map())

const selectedNodeName = computed(() => {
  if (!selectedNodeId.value) return ''
  const data = dagNodeDataMap.value.get(selectedNodeId.value)
  return data?.task_name || ''
})

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
  const offset = vfNodes.value.length * 50

  dagNodeDataMap.value.set(id, {
    id,
    task_id: task.id,
    task_name: task.name,
    task_type: task.type,
    x: 100 + offset,
    y: 100 + offset,
    param_overrides: undefined,
  })

  vfNodes.value.push({
    id,
    type: 'custom',
    position: { x: 100 + offset, y: 100 + offset },
    data: { label: task.name, taskType: task.type },
  })
}

function removeNode(nodeId: string) {
  vfNodes.value = vfNodes.value.filter((n) => n.id !== nodeId)
  vfEdges.value = vfEdges.value.filter((e) => e.source !== nodeId && e.target !== nodeId)
  dagNodeDataMap.value.delete(nodeId)
  if (selectedNodeId.value === nodeId) selectedNodeId.value = null
}

function onNodeClick({ node }: { node: Node }) {
  selectedNodeId.value = node.id
  selectedEdgeId.value = null
  // Load existing overrides
  const data = dagNodeDataMap.value.get(node.id)
  if (data && data.param_overrides) {
    nodeOverrides.timeout = data.param_overrides.timeout
    nodeOverrides.max_retries = data.param_overrides.max_retries
    nodeOverrides.retry_interval = data.param_overrides.retry_interval
  } else {
    clearOverrides()
  }
}

function onEdgeClick({ edge }: { edge: Edge }) {
  selectedEdgeId.value = edge.id
  selectedNodeId.value = null
}

function onConnect(params: { source: string; target: string }) {
  // Check for duplicate
  const exists = vfEdges.value.some(
    (e) => e.source === params.source && e.target === params.target,
  )
  if (!exists && params.source !== params.target) {
    vfEdges.value.push({
      id: `e-${params.source}-${params.target}`,
      source: params.source,
      target: params.target,
      type: 'smoothstep',
      animated: true,
    })
  }
}

function removeSelectedEdge() {
  if (selectedEdgeId.value) {
    vfEdges.value = vfEdges.value.filter((e) => e.id !== selectedEdgeId.value)
    selectedEdgeId.value = null
  }
}

function applyOverrides() {
  if (!selectedNodeId.value) return
  const data = dagNodeDataMap.value.get(selectedNodeId.value)
  if (!data) return
  const overrides: Record<string, number> = {}
  if (nodeOverrides.timeout !== undefined) overrides.timeout = nodeOverrides.timeout
  if (nodeOverrides.max_retries !== undefined) overrides.max_retries = nodeOverrides.max_retries
  if (nodeOverrides.retry_interval !== undefined) overrides.retry_interval = nodeOverrides.retry_interval
  data.param_overrides = Object.keys(overrides).length > 0 ? overrides : undefined
  ElMessage.success('参数覆盖已应用')
}

function clearOverrides() {
  nodeOverrides.timeout = undefined
  nodeOverrides.max_retries = undefined
  nodeOverrides.retry_interval = undefined
  if (selectedNodeId.value) {
    const data = dagNodeDataMap.value.get(selectedNodeId.value)
    if (data) data.param_overrides = undefined
  }
}

// Drag and drop from task panel
function onDragStart(event: DragEvent, task: TaskItem) {
  event.dataTransfer?.setData('application/taskflow-task', JSON.stringify(task))
  event.dataTransfer!.effectAllowed = 'move'
}

function onDragOver(event: DragEvent) {
  event.preventDefault()
  event.dataTransfer!.dropEffect = 'move'
}

function onDrop(event: DragEvent) {
  event.preventDefault()
  const data = event.dataTransfer?.getData('application/taskflow-task')
  if (!data) return
  try {
    const task = JSON.parse(data) as TaskItem
    addNode(task)
  } catch { /* ignore */ }
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
      const apiNodes = data.dag.nodes || []
      const apiEdges = data.dag.edges || []

      // Build Vue Flow nodes
      vfNodes.value = apiNodes.map((n: ApiDagNode, i: number) => {
        const nodeData: DagNodeData = {
          id: n.id,
          task_id: n.task_id,
          task_name: n.task_name || n.task_id,
          task_type: n.task_type || 'command',
          x: n.x ?? 100 + i * 200,
          y: n.y ?? 100,
          param_overrides: n.param_overrides || undefined,
        }
        dagNodeDataMap.value.set(n.id, nodeData)

        return {
          id: n.id,
          type: 'custom',
          position: { x: nodeData.x, y: nodeData.y },
          data: { label: nodeData.task_name, taskType: nodeData.task_type },
        }
      })

      // Build Vue Flow edges
      vfEdges.value = apiEdges.map((e: ApiDagEdge) => ({
        id: `e-${e.source}-${e.target}`,
        source: e.source,
        target: e.target,
        type: 'smoothstep',
        animated: true,
      }))

      nodeCounter = apiNodes.length + 1
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
    // Convert Vue Flow nodes/edges back to API format
    const nodes = vfNodes.value.map((n) => {
      const data = dagNodeDataMap.value.get(n.id)
      return {
        id: n.id,
        task_id: data?.task_id || '',
        task_name: data?.task_name || '',
        task_type: data?.task_type || 'command',
        x: n.position.x,
        y: n.position.y,
        ...(data?.param_overrides ? { param_overrides: data.param_overrides } : {}),
      }
    })

    const edges = vfEdges.value.map((e) => ({
      source: e.source,
      target: e.target,
    }))

    const dag = { nodes, edges }

    const payload: WorkflowPayload = {
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
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  overflow: hidden;
}

.custom-node {
  position: relative;
  background: #fff;
  border: 2px solid #409EFF;
  border-radius: 6px;
  padding: 10px 16px;
  min-width: 160px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
}

.custom-node.node-selected {
  border-color: #E6A23C;
  box-shadow: 0 0 0 3px rgba(230, 162, 60, 0.2);
}

.node-header {
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

.custom-node:hover .node-delete {
  opacity: 1;
}

.dag-toolbar {
  margin-top: 12px;
  display: flex;
  align-items: center;
  gap: 12px;
}

.toolbar-hint {
  font-size: 12px;
  color: #909399;
}
</style>
