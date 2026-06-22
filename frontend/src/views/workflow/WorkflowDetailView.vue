<template>
  <div class="workflow-detail">
    <div class="page-header">
      <h2>工作流详情</h2>
      <div>
        <el-button @click="goBack">返回</el-button>
        <!-- Fix #178: WorkflowDetailView 缺少编辑入口 -->
        <el-button type="warning" @click="handleEdit">编辑</el-button>
        <el-button type="primary" :loading="triggering" @click="handleTrigger">触发</el-button>
      </div>
    </div>

    <el-card v-loading="loading" class="section-card">
      <template #header>基本信息</template>
      <el-descriptions :column="2" border>
        <el-descriptions-item label="名称">{{ workflow?.name }}</el-descriptions-item>
        <el-descriptions-item label="描述">{{ workflow?.description || '-' }}</el-descriptions-item>
        <el-descriptions-item label="调度策略">
          <el-tag :type="strategyTagType(workflow?.schedule_strategy)">
            {{ strategyLabel(workflow?.schedule_strategy) }}
          </el-tag>
        </el-descriptions-item>
        <el-descriptions-item label="目标 Worker">{{ workflow?.target_worker_id || '-' }}</el-descriptions-item>
        <el-descriptions-item label="Cron 启用">
          <el-tag :type="workflow?.cron_enabled ? 'success' : 'info'" size="small">
            {{ workflow?.cron_enabled ? '是' : '否' }}
          </el-tag>
        </el-descriptions-item>
        <el-descriptions-item label="Cron 表达式">{{ workflow?.cron_enabled ? (workflow?.cron_expression || '-') : '-' }}</el-descriptions-item>
        <el-descriptions-item label="创建者">{{ workflow?.creator_id }}</el-descriptions-item>
        <el-descriptions-item label="创建时间">{{ formatTime(workflow?.created_at) }}</el-descriptions-item>
      </el-descriptions>
    </el-card>

    <el-card class="section-card">
      <template #header>DAG 可视化</template>
      <div class="dag-canvas">
        <svg class="dag-edges">
          <defs>
            <marker id="arrowhead-detail" markerWidth="10" markerHeight="7" refX="10" refY="3.5" orient="auto">
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
            class="dag-edge"
            marker-end="url(#arrowhead-detail)"
          />
        </svg>

        <div
          v-for="node in dagNodes"
          :key="node.id"
          class="dag-node"
          :style="{ left: node.x + 'px', top: node.y + 'px' }"
        >
          <div class="node-content">
            <span class="node-name">{{ node.task_name }}</span>
            <el-tag size="small" :type="taskTypeTag(node.task_type)">{{ node.task_type }}</el-tag>
          </div>
        </div>

        <div v-if="dagNodes.length === 0" class="canvas-empty">
          暂无 DAG 节点
        </div>
      </div>
    </el-card>

    <el-card class="section-card">
      <template #header>最近实例</template>
      <el-table :data="instances" v-loading="instancesLoading" stripe>
        <el-table-column prop="id" label="实例 ID" min-width="180" show-overflow-tooltip />
        <el-table-column label="状态" width="120" align="center">
          <template #default="{ row }">
            <el-tag :type="instanceStatusType(row.status)" size="small">
              {{ row.status }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="触发类型" width="120" align="center">
          <template #default="{ row }">
            <el-tag :type="row.trigger_type === 'manual' ? '' : 'success'" size="small">
              {{ row.trigger_type === 'manual' ? '手动' : '定时' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="创建时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.created_at) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="140">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click="viewInstance(row)">查看详情</el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="pagination-wrapper">
        <el-pagination
          v-model:current-page="instancePage"
          v-model:page-size="instancePageSize"
          :total="instanceTotal"
          :page-sizes="[10, 20, 50]"
          layout="total, sizes, prev, pager, next"
          @size-change="handleSizeChange"
          @current-change="fetchInstances"
        />
      </div>
    </el-card>

    <!-- Trigger dialog with param_overrides -->
    <el-dialog v-model="triggerDialogVisible" title="触发工作流" width="600px">
      <p style="margin-bottom: 12px">确定要手动触发此工作流吗？</p>
      <template v-if="triggerParamEntries.length > 0">
        <el-divider content-position="left">运行时参数</el-divider>
        <el-alert
          type="info"
          :closable="false"
          show-icon
          style="margin-bottom: 12px"
        >
          <template #title>
            以下参数来自任务定义中的占位符变量，请输入本次执行的值。留空则使用默认值。
          </template>
        </el-alert>
        <div class="trigger-param-editor">
          <div v-for="(param, index) in triggerParamEntries" :key="index" class="trigger-param-row">
            <el-input :model-value="param.key" disabled style="width: 160px" />
            <span class="trigger-param-eq">=</span>
            <el-input v-model="param.value" :placeholder="param.defaultValue ? `默认值: ${param.defaultValue}` : '请输入参数值'" style="flex: 1" />
          </div>
        </div>
      </template>
      <template v-else>
        <el-alert
          type="info"
          :closable="false"
          show-icon
          style="margin-top: 8px"
        >
          <template #title>此工作流无需运行时参数</template>
        </el-alert>
      </template>
      <template #footer>
        <el-button @click="triggerDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="triggering" @click="confirmTrigger">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { getWorkflow, triggerWorkflow } from '../../api/workflow'
import { getWorkflowInstances } from '../../api/instance'
import { getTask } from '../../api/task'
import { formatTime } from '../../utils/format'
import type { WorkflowItem } from '../../types/workflow'
import type { WorkflowInstance } from '../../types/instance'

interface ApiDagNode {
  id: string
  task_id: string
  task_name?: string
  task_type?: string
  x?: number
  y?: number
}

interface ApiDagEdge {
  source: string
  target: string
}

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

const loading = ref(false)
const workflow = ref<WorkflowItem | null>(null)

const dagNodes = ref<DagNodeData[]>([])
const dagEdges = ref<DagEdgeData[]>([])

const instances = ref<WorkflowInstance[]>([])
const instancesLoading = ref(false)
const instancePage = ref(1)
const instancePageSize = ref(10)
const instanceTotal = ref(0)

const triggering = ref(false)

// Trigger parameter entries
interface TriggerParamEntry { key: string; value: string; defaultValue: string }
const triggerParamEntries = ref<TriggerParamEntry[]>([])
const triggerDialogVisible = ref(false)

async function loadTriggerParams() {
  triggerParamEntries.value = []
  if (!workflow.value?.dag_json?.nodes) return
  const paramMap = new Map<string, string>()
  for (const node of workflow.value.dag_json.nodes) {
    if (!node.task_id) continue
    try {
      const { data: taskResp } = await getTask(node.task_id)
      const task = taskResp.data
      if (task?.parameters_json && typeof task.parameters_json === 'object') {
        for (const [key, val] of Object.entries(task.parameters_json)) {
          if (!paramMap.has(key)) {
            paramMap.set(key, typeof val === 'string' ? val : JSON.stringify(val))
          }
        }
      }
    } catch {
      // Skip tasks that can't be loaded
    }
  }
  triggerParamEntries.value = Array.from(paramMap.entries()).map(([key, defaultValue]) => ({
    key,
    value: '',
    defaultValue,
  }))
}

function strategyTagType(strategy?: string) {
  const map: Record<string, string> = {
    random: 'info',
    load_balance: 'success',
    specified: 'warning',
  }
  return map[strategy || ''] || 'info'
}

function strategyLabel(strategy?: string) {
  const map: Record<string, string> = {
    random: '随机',
    load_balance: '负载均衡',
    specified: '指定节点',
  }
  return map[strategy || ''] || strategy || '-'
}

function taskTypeTag(type: string) {
  const map: Record<string, string> = {
    command: '',
    script: 'success',
    sql: 'warning',
  }
  return map[type] || 'info'
}

function instanceStatusType(status: string) {
  const map: Record<string, string> = {
    PENDING: 'info',
    RUNNING: '',
    PAUSED: 'warning',
    SUCCESS: 'success',
    FAILED: 'danger',
    CANCELLED: 'info',
  }
  return map[status] || 'info'
}

function getNodeCenter(nodeId: string): { x: number; y: number } {
  const node = dagNodes.value.find((n) => n.id === nodeId)
  if (!node) return { x: 0, y: 0 }
  return { x: node.x + 90, y: node.y + 25 }
}

async function fetchWorkflow() {
  loading.value = true
  try {
    const { data: resp } = await getWorkflow(workflowId.value)
    const data = resp.data
    workflow.value = data

    if (data.dag_json) {
      const apiNodes = data.dag_json.nodes || []
      dagNodes.value = apiNodes.map((n: ApiDagNode, i: number) => ({
        id: n.id,
        task_id: n.task_id,
        task_name: n.task_name || n.task_id,
        task_type: n.task_type || 'command',
        x: n.x ?? 60 + i * 200,
        y: n.y ?? 60,
      }))
      dagEdges.value = (data.dag_json.edges || []).map((e: ApiDagEdge) => ({
        source: e.source,
        target: e.target,
      }))
    }
  } catch {
    ElMessage.error('加载工作流失败')
  } finally {
    loading.value = false
  }
}

async function fetchInstances() {
  instancesLoading.value = true
  try {
    const { data: resp } = await getWorkflowInstances(workflowId.value, {
      page: instancePage.value,
      page_size: instancePageSize.value,
    })
    const data = resp.data
    instances.value = data.items || []
    instanceTotal.value = data.total || 0
  } catch {
    ElMessage.error('获取实例列表失败')
  } finally {
    instancesLoading.value = false
  }
}

// Fix #175: 分页 size-change 未重置 page=1
function handleSizeChange() {
  instancePage.value = 1
  fetchInstances()
}

async function handleTrigger() {
  // Load params and show dialog
  triggerParamEntries.value = []
  triggerDialogVisible.value = true
  await loadTriggerParams()
}

async function confirmTrigger() {
  triggering.value = true
  try {
    const paramOverrides: Record<string, string> = {}
    for (const entry of triggerParamEntries.value) {
      if (entry.value.trim()) {
        paramOverrides[entry.key] = entry.value.trim()
      }
    }
    await triggerWorkflow(workflowId.value, Object.keys(paramOverrides).length > 0 ? paramOverrides : undefined)
    ElMessage.success('触发成功')
    triggerDialogVisible.value = false
    fetchInstances()
  } catch {
    ElMessage.error('触发失败')
  } finally {
    triggering.value = false
  }
}

function viewInstance(row: WorkflowInstance) {
  router.push({ name: 'instance-detail', params: { id: row.id } })
}

function goBack() {
  router.push({ name: 'workflow-list' })
}

// Fix #178: WorkflowDetailView 缺少编辑入口
function handleEdit() {
  router.push({ name: 'workflow-edit', params: { id: workflowId.value } })
}

onMounted(() => {
  fetchWorkflow()
  fetchInstances()
})

// Fix #161: Re-fetch when the route param changes (e.g. navigating from one
// workflow detail to another without unmounting). Without this watch the page
// shows stale data from the previous workflow.
watch(workflowId, (newId, oldId) => {
  if (newId && newId !== oldId) {
    fetchWorkflow()
    instancePage.value = 1
    fetchInstances()
  }
})
</script>

<style scoped>
.workflow-detail {
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

.dag-canvas {
  position: relative;
  height: 400px;
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

.dag-edge {
  stroke: #409EFF;
  stroke-width: 2;
}

.dag-node {
  position: absolute;
  width: 180px;
  background: #fff;
  border: 2px solid #409EFF;
  border-radius: 6px;
  padding: 10px 12px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
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

.canvas-empty {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  color: #c0c4cc;
  font-size: 14px;
}

.pagination-wrapper {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

.trigger-param-editor {
  width: 100%;
}

.trigger-param-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.trigger-param-eq {
  color: #909399;
  font-weight: 600;
}
</style>
