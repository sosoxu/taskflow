<template>
  <div class="instance-list">
    <div class="page-header">
      <h2>执行实例</h2>
    </div>

    <div class="search-bar">
      <el-select
        v-model="statusFilter"
        placeholder="按状态筛选"
        clearable
        style="width: 180px"
        @change="handleSearch"
      >
        <el-option label="PENDING" value="PENDING" />
        <el-option label="RUNNING" value="RUNNING" />
        <el-option label="PAUSED" value="PAUSED" />
        <el-option label="SUCCESS" value="SUCCESS" />
        <el-option label="FAILED" value="FAILED" />
        <el-option label="CANCELLED" value="CANCELLED" />
      </el-select>
      <el-select
        v-model="triggerFilter"
        placeholder="按触发方式筛选"
        clearable
        style="width: 180px; margin-left: 12px"
        @change="handleSearch"
      >
        <el-option label="手动触发" value="manual" />
        <el-option label="定时触发" value="cron" />
      </el-select>
      <el-button style="margin-left: 12px" @click="handleSearch">筛选</el-button>
      <el-button @click="handleReset">重置</el-button>
    </div>

    <el-table :data="filteredInstances" v-loading="loading" stripe>
      <el-table-column label="实例 ID" min-width="120" show-overflow-tooltip>
        <template #default="{ row }">
          <span class="instance-id">{{ row.id.substring(0, 8) }}</span>
        </template>
      </el-table-column>
      <el-table-column label="工作流 ID" min-width="120" show-overflow-tooltip>
        <template #default="{ row }">
          <span>{{ row.workflow_id.substring(0, 8) }}</span>
        </template>
      </el-table-column>
      <el-table-column label="状态" width="120" align="center">
        <template #default="{ row }">
          <el-tag :type="instanceStatusType(row.status)" size="small">{{ row.status }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="触发方式" width="110" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="row.trigger_type === 'cron' ? 'success' : 'info'">
            {{ row.trigger_type === 'cron' ? '定时' : '手动' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="版本" width="80" align="center">
        <template #default="{ row }">
          v{{ row.workflow_version }}
        </template>
      </el-table-column>
      <el-table-column label="创建时间" width="180">
        <template #default="{ row }">
          {{ formatTime(row.created_at) }}
        </template>
      </el-table-column>
      <el-table-column label="开始时间" width="180">
        <template #default="{ row }">
          {{ formatTime(row.started_at) }}
        </template>
      </el-table-column>
      <el-table-column label="结束时间" width="180">
        <template #default="{ row }">
          {{ formatTime(row.finished_at) }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="120" fixed="right">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="handleView(row)">查看详情</el-button>
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
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { getAllInstances } from '../../api/instance'
import { formatTime } from '../../utils/format'
import type { WorkflowInstance, WorkflowInstanceStatus } from '../../types/instance'

const router = useRouter()

const loading = ref(false)
const instances = ref<WorkflowInstance[]>([])
const page = ref(1)
const pageSize = ref(10)
const total = ref(0)

const statusFilter = ref('')
const triggerFilter = ref('')

// Fix #310: Client-side filtering on top of paginated data. The backend
// /api/v1/instances endpoint does not support status/trigger query params,
// so we filter the current page in-memory. This keeps the feature usable
// without a backend change; full server-side filtering can be added later.
const filteredInstances = computed(() => {
  return instances.value.filter((item) => {
    if (statusFilter.value && item.status !== statusFilter.value) return false
    if (triggerFilter.value && item.trigger_type !== triggerFilter.value) return false
    return true
  })
})

async function fetchList() {
  loading.value = true
  try {
    const { data: resp } = await getAllInstances({
      page: page.value,
      page_size: pageSize.value,
    })
    const data = resp.data
    instances.value = data.items || []
    total.value = data.total || 0
  } catch {
    ElMessage.error('获取实例列表失败')
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  page.value = 1
  fetchList()
}

function handleReset() {
  statusFilter.value = ''
  triggerFilter.value = ''
  page.value = 1
  fetchList()
}

function handleSizeChange() {
  page.value = 1
  fetchList()
}

function handleView(row: WorkflowInstance) {
  router.push({ name: 'instance-detail', params: { id: row.id } })
}

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

onMounted(() => {
  fetchList()
})
</script>

<style scoped>
.instance-list {
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
  display: flex;
  align-items: center;
}

.pagination-wrapper {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

.instance-id {
  font-family: monospace;
  color: #606266;
}
</style>
