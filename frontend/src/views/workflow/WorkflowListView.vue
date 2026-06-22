<template>
  <div class="workflow-list">
    <div class="page-header">
      <h2>工作流管理</h2>
      <!-- Fix #172: viewer 角色隐藏写操作按钮 -->
      <el-button v-if="userStore.isOperator" type="primary" @click="handleCreate">创建工作流</el-button>
    </div>

    <div class="search-bar">
      <el-input
        v-model="keyword"
        placeholder="搜索工作流名称"
        clearable
        style="width: 300px"
        @keyup.enter="handleSearch"
      >
        <template #append>
          <el-button @click="handleSearch">搜索</el-button>
        </template>
      </el-input>
    </div>

    <el-table :data="workflows" v-loading="loading" stripe>
      <el-table-column prop="name" label="名称" min-width="140" />
      <el-table-column prop="description" label="描述" min-width="180" show-overflow-tooltip />
      <el-table-column label="调度策略" width="120" align="center">
        <template #default="{ row }">
          <el-tag :type="strategyTagType(row.schedule_strategy)">
            {{ strategyLabel(row.schedule_strategy) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="Cron" min-width="140">
        <template #default="{ row }">
          {{ row.cron_enabled ? row.cron_expression : '-' }}
        </template>
      </el-table-column>
      <el-table-column label="定时调度" width="100" align="center">
        <template #default="{ row }">
          <el-tag :type="row.cron_enabled ? 'success' : 'info'" size="small">
            {{ row.cron_enabled ? '启用' : '停用' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="creator_id" label="创建者" width="120" />
      <el-table-column label="创建时间" width="180">
        <template #default="{ row }">
          {{ formatTime(row.created_at) }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="240" fixed="right">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="handleView(row)">查看</el-button>
          <!-- Fix #172: viewer 角色隐藏写操作按钮 -->
          <el-button v-if="userStore.isOperator" link type="primary" size="small" @click="handleEdit(row)">编辑</el-button>
          <el-button v-if="userStore.isOperator" link type="danger" size="small" @click="handleDelete(row)">删除</el-button>
          <el-button v-if="userStore.isOperator" link type="success" size="small" @click="handleTrigger(row)">触发</el-button>
        </template>
      </el-table-column>
    </el-table>

    <div class="pagination-wrapper">
      <el-pagination
        v-model:current-page="page"
        v-model:page-size="pageSize"
        :total="total"
        :page-sizes="[10, 20, 50]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="fetchList"
      />
    </div>

    <el-dialog v-model="deleteDialogVisible" title="确认删除" width="400px">
      <p>确定要删除工作流「{{ deleteTarget?.name }}」吗？此操作不可恢复。</p>
      <template #footer>
        <el-button @click="deleteDialogVisible = false">取消</el-button>
        <el-button type="danger" :loading="deleteLoading" @click="confirmDelete">确定</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="triggerDialogVisible" title="触发工作流" width="600px">
      <p style="margin-bottom: 12px">确定要手动触发工作流「{{ triggerTarget?.name }}」吗？</p>
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
        <el-button type="primary" :loading="triggerLoading" @click="confirmTrigger">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { getWorkflows, getWorkflow, deleteWorkflow, triggerWorkflow } from '../../api/workflow'
import { getTask } from '../../api/task'
import { formatTime } from '../../utils/format'
import type { WorkflowItem } from '../../types/workflow'
import { useUserStore } from '../../stores/userStore'

const router = useRouter()
// Fix #172: viewer 角色隐藏写操作按钮
const userStore = useUserStore()

const loading = ref(false)
const workflows = ref<WorkflowItem[]>([])
const keyword = ref('')
const page = ref(1)
const pageSize = ref(10)
const total = ref(0)

const deleteDialogVisible = ref(false)
const deleteLoading = ref(false)
const deleteTarget = ref<WorkflowItem | null>(null)

const triggerDialogVisible = ref(false)
const triggerLoading = ref(false)
const triggerTarget = ref<WorkflowItem | null>(null)

// Trigger parameter entries
interface TriggerParamEntry { key: string; value: string; defaultValue: string }
const triggerParamEntries = ref<TriggerParamEntry[]>([])

async function loadTriggerParams(workflowId: string) {
  triggerParamEntries.value = []
  try {
    const { data: resp } = await getWorkflow(workflowId)
    const wf = resp.data
    if (!wf?.dag_json?.nodes) return

    // Collect all unique parameter names from all task nodes
    const paramMap = new Map<string, string>() // key -> defaultValue
    for (const node of wf.dag_json.nodes) {
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
  } catch {
    // If we can't load params, just show empty form
  }
}

function strategyTagType(strategy: string) {
  const map: Record<string, string> = {
    random: 'info',
    load_balance: 'success',
    specified: 'warning',
  }
  return map[strategy] || 'info'
}

function strategyLabel(strategy: string) {
  const map: Record<string, string> = {
    random: '随机',
    load_balance: '负载均衡',
    specified: '指定节点',
  }
  return map[strategy] || strategy
}

async function fetchList() {
  loading.value = true
  try {
    const { data: resp } = await getWorkflows({
      page: page.value,
      page_size: pageSize.value,
      keyword: keyword.value || undefined,
    })
    const data = resp.data
    workflows.value = data.items || []
    total.value = data.total || 0
  } catch {
    ElMessage.error('获取工作流列表失败')
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  page.value = 1
  fetchList()
}

// Fix #175: 分页 size-change 未重置 page=1
function handleSizeChange() {
  page.value = 1
  fetchList()
}

function handleCreate() {
  router.push({ name: 'workflow-create' })
}

function handleView(row: WorkflowItem) {
  router.push({ name: 'workflow-detail', params: { id: row.id } })
}

function handleEdit(row: WorkflowItem) {
  router.push({ name: 'workflow-edit', params: { id: row.id } })
}

function handleDelete(row: WorkflowItem) {
  deleteTarget.value = row
  deleteDialogVisible.value = true
}

async function confirmDelete() {
  if (!deleteTarget.value) return
  deleteLoading.value = true
  try {
    await deleteWorkflow(deleteTarget.value.id)
    ElMessage.success('删除成功')
    deleteDialogVisible.value = false
    fetchList()
  } catch {
    ElMessage.error('删除失败')
  } finally {
    deleteLoading.value = false
  }
}

async function handleTrigger(row: WorkflowItem) {
  triggerTarget.value = row
  triggerParamEntries.value = []
  triggerDialogVisible.value = true
  await loadTriggerParams(row.id)
}

async function confirmTrigger() {
  if (!triggerTarget.value) return
  triggerLoading.value = true
  try {
    // Build param_overrides from trigger param entries (only non-empty values)
    const paramOverrides: Record<string, string> = {}
    for (const entry of triggerParamEntries.value) {
      if (entry.value.trim()) {
        paramOverrides[entry.key] = entry.value.trim()
      }
    }
    await triggerWorkflow(triggerTarget.value.id, Object.keys(paramOverrides).length > 0 ? paramOverrides : undefined)
    ElMessage.success('触发成功')
    triggerDialogVisible.value = false
  } catch {
    ElMessage.error('触发失败')
  } finally {
    triggerLoading.value = false
  }
}

onMounted(() => {
  fetchList()
})
</script>

<style scoped>
.workflow-list {
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

.search-bar {
  margin-bottom: 16px;
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
