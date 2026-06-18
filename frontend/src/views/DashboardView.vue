<template>
  <div class="dashboard">
    <h2 class="page-title">仪表盘</h2>

    <el-row :gutter="20" class="stat-row">
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <div class="stat-info">
              <div class="stat-label">任务总数</div>
              <div class="stat-value">{{ stats.totalTasks }}</div>
            </div>
            <el-icon class="stat-icon" :size="48" color="#409eff"><Document /></el-icon>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <div class="stat-info">
              <div class="stat-label">工作流总数</div>
              <div class="stat-value">{{ stats.totalWorkflows }}</div>
            </div>
            <el-icon class="stat-icon" :size="48" color="#67c23a"><Share /></el-icon>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <div class="stat-info">
              <div class="stat-label">今日执行数</div>
              <div class="stat-value">{{ stats.todayExecutions }}</div>
            </div>
            <el-icon class="stat-icon" :size="48" color="#e6a23c"><TrendCharts /></el-icon>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <div class="stat-info">
              <div class="stat-label">在线节点</div>
              <div class="stat-value">{{ stats.onlineWorkers }}</div>
            </div>
            <el-icon class="stat-icon" :size="48" color="#f56c6c"><Monitor /></el-icon>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" class="stat-row">
      <el-col :span="12">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <div class="stat-info">
              <div class="stat-label">运行中实例</div>
              <div class="stat-value">{{ stats.runningInstances }}</div>
            </div>
            <el-icon class="stat-icon" :size="40" color="#409eff"><VideoPlay /></el-icon>
          </div>
        </el-card>
      </el-col>
      <el-col :span="12">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <div class="stat-info">
              <div class="stat-label">成功率</div>
              <div class="stat-value">{{ stats.successRate }}%</div>
            </div>
            <el-icon class="stat-icon" :size="40" color="#67c23a"><CircleCheck /></el-icon>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" class="content-row">
      <el-col :span="16">
        <el-card shadow="hover">
          <template #header>
            <div class="card-header">
              <span>最近工作流实例</span>
            </div>
          </template>
          <el-table :data="recentInstances" stripe style="width: 100%">
            <el-table-column prop="id" label="实例 ID" width="100">
              <template #default="{ row }">
                <router-link :to="`/instances/${row.id}`" class="link">{{ row.id.substring(0, 8) }}...</router-link>
              </template>
            </el-table-column>
            <el-table-column prop="workflow_id" label="工作流" />
            <el-table-column prop="status" label="状态" width="120">
              <template #default="{ row }">
                <el-tag :type="statusTagType(row.status)" size="small">
                  {{ row.status }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="trigger_type" label="触发类型" width="100" />
            <el-table-column label="创建时间" width="180">
              <template #default="{ row }">{{ formatTime(row.created_at) }}</template>
            </el-table-column>
          </el-table>
          <el-empty v-if="recentInstances.length === 0" description="暂无实例数据" />
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="hover">
          <template #header>
            <div class="card-header">
              <span>快捷操作</span>
            </div>
          </template>
          <div class="quick-actions">
            <el-button type="primary" size="large" @click="$router.push('/tasks')">
              <el-icon><Plus /></el-icon>
              创建任务
            </el-button>
            <el-button type="success" size="large" @click="$router.push('/workflows/create')">
              <el-icon><Plus /></el-icon>
              创建工作流
            </el-button>
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref, onMounted } from 'vue'
import { Document, Share, VideoPlay, Monitor, Plus, TrendCharts, CircleCheck } from '@element-plus/icons-vue'
import { getDashboardStats } from '../api/dashboard'
import { formatTime } from '../utils/format'

const stats = reactive({
  totalTasks: 0,
  totalWorkflows: 0,
  runningInstances: 0,
  onlineWorkers: 0,
  todayExecutions: 0,
  successRate: 0,
})

interface InstanceItem {
  id: string
  workflow_id: string
  status: string
  trigger_type: string
  created_at: string
}

const recentInstances = ref<InstanceItem[]>([])

function statusTagType(status: string): '' | 'success' | 'warning' | 'danger' | 'info' {
  const map: Record<string, '' | 'success' | 'warning' | 'danger' | 'info'> = {
    RUNNING: '',
    SUCCESS: 'success',
    FAILED: 'danger',
    PAUSED: 'warning',
    PENDING: 'info',
    CANCELLED: 'info',
  }
  return map[status] || 'info'
}

async function loadDashboardData() {
  try {
    const res = await getDashboardStats()
    const data = res.data?.data
    if (!data) return
    stats.totalTasks = data.total_tasks || 0
    stats.totalWorkflows = data.total_workflows || 0
    stats.runningInstances = data.running_instances || 0
    stats.onlineWorkers = data.online_workers || 0
    stats.todayExecutions = data.today_executions || 0
    stats.successRate = Number(data.success_rate?.toFixed(2) || 0)
    recentInstances.value = data.recent_instances || []
  } catch {
    // Silently fail - dashboard shows zeros
  }
}

onMounted(() => {
  loadDashboardData()
})
</script>

<style scoped>
.dashboard {
  padding: 0;
}

.page-title {
  margin: 0 0 20px;
  font-size: 22px;
  color: #303133;
}

.stat-row {
  margin-bottom: 20px;
}

.stat-card {
  height: 100%;
}

.stat-content {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.stat-info {
  display: flex;
  flex-direction: column;
}

.stat-label {
  font-size: 14px;
  color: #909399;
  margin-bottom: 8px;
}

.stat-value {
  font-size: 32px;
  font-weight: 600;
  color: #303133;
}

.stat-icon {
  opacity: 0.8;
}

.content-row {
  margin-bottom: 20px;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 16px;
  font-weight: 500;
}

.quick-actions {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.quick-actions .el-button {
  width: 100%;
  justify-content: center;
}

.link {
  color: #409eff;
  text-decoration: none;
}

.link:hover {
  text-decoration: underline;
}
</style>
