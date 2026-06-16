<template>
  <div class="worker-list">
    <h2>执行节点</h2>

    <el-card class="table-card">
      <el-table v-loading="loading" :data="workers" border stripe>
        <el-table-column prop="name" label="名称" min-width="120" />
        <el-table-column prop="address" label="地址" min-width="150" />
        <el-table-column label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="row.status === 'online' ? 'success' : 'danger'" size="small">
              {{ row.status === 'online' ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="max_tasks" label="最大任务数" width="110" />
        <el-table-column prop="running_tasks" label="运行中任务" width="110" />
        <el-table-column label="CPU 使用率" width="120">
          <template #default="{ row }">
            <el-progress
              :percentage="Math.round(row.cpu_usage)"
              :color="cpuColor(row.cpu_usage)"
              :stroke-width="14"
              :text-inside="true"
            />
          </template>
        </el-table-column>
        <el-table-column label="内存使用率" width="120">
          <template #default="{ row }">
            <el-progress
              :percentage="Math.round(row.memory_usage)"
              :color="memColor(row.memory_usage)"
              :stroke-width="14"
              :text-inside="true"
            />
          </template>
        </el-table-column>
        <el-table-column label="最后心跳" min-width="160">
          <template #default="{ row }">{{ formatTime(row.last_heartbeat) }}</template>
        </el-table-column>
        <el-table-column label="资源标签" min-width="150">
          <template #default="{ row }">
            <el-tag
              v-for="tag in row.resource_tags"
              :key="tag"
              size="small"
              class="resource-tag"
            >{{ tag }}</el-tag>
            <span v-if="!row.resource_tags || row.resource_tags.length === 0">-</span>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import { getWorkers } from '../../api/worker'
import { formatTime } from '../../utils/format'
import type { WorkerInfo } from '../../types/worker'

const loading = ref(false)
const workers = ref<WorkerInfo[]>([])

let refreshTimer: ReturnType<typeof setInterval> | null = null

function cpuColor(value: number): string {
  if (value >= 90) return '#f56c6c'
  if (value >= 70) return '#e6a23c'
  return '#67c23a'
}

function memColor(value: number): string {
  if (value >= 90) return '#f56c6c'
  if (value >= 70) return '#e6a23c'
  return '#67c23a'
}

async function fetchWorkers() {
  loading.value = true
  try {
    const res = await getWorkers()
    workers.value = res.data || []
  } catch {
    ElMessage.error('获取 Worker 列表失败')
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  fetchWorkers()
  refreshTimer = setInterval(fetchWorkers, 10000)
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
})
</script>

<style scoped>
.worker-list {
  padding: 20px;
}

.worker-list h2 {
  margin-bottom: 20px;
  color: #303133;
}

.table-card {
  margin-bottom: 20px;
}

.resource-tag {
  margin-right: 4px;
  margin-bottom: 2px;
}
</style>
