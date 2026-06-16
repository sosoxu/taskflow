<template>
  <div class="workflow-list">
    <div class="page-header">
      <h2>工作流管理</h2>
      <el-button type="primary" @click="handleCreate">创建工作流</el-button>
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
      <el-table-column prop="status" label="状态" width="100" align="center">
        <template #default="{ row }">
          <el-tag :type="row.status === 'active' ? 'success' : 'info'" size="small">
            {{ row.status === 'active' ? '启用' : '停用' }}
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
          <el-button link type="primary" size="small" @click="handleEdit(row)">编辑</el-button>
          <el-button link type="danger" size="small" @click="handleDelete(row)">删除</el-button>
          <el-button link type="success" size="small" @click="handleTrigger(row)">触发</el-button>
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
        @size-change="fetchList"
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

    <el-dialog v-model="triggerDialogVisible" title="确认触发" width="400px">
      <p>确定要手动触发工作流「{{ triggerTarget?.name }}」吗？</p>
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
import { getWorkflows, deleteWorkflow, triggerWorkflow } from '../../api/workflow'
import { formatTime } from '../../utils/format'
import type { WorkflowItem } from '../../types/workflow'

const router = useRouter()

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
    const { data } = await getWorkflows({
      page: page.value,
      page_size: pageSize.value,
      keyword: keyword.value || undefined,
    })
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

function handleTrigger(row: WorkflowItem) {
  triggerTarget.value = row
  triggerDialogVisible.value = true
}

async function confirmTrigger() {
  if (!triggerTarget.value) return
  triggerLoading.value = true
  try {
    await triggerWorkflow(triggerTarget.value.id)
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
</style>
