<template>
  <div class="worker-list">
    <div class="page-header">
      <h2>执行节点</h2>
    </div>

    <div class="toolbar">
      <el-button type="primary" :icon="Plus" @click="openDeployDialog">部署 Worker</el-button>
      <el-button :icon="Refresh" @click="fetchWorkers()">刷新</el-button>
    </div>

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

    <!-- 部署 Worker 对话框 -->
    <el-dialog
      v-model="deployDialogVisible"
      title="部署 Worker"
      width="640px"
      destroy-on-close
      :close-on-click-modal="false"
    >
      <!-- 部署结果 -->
      <div v-if="deployResult" class="deploy-result">
        <el-alert
          :title="deployResult.success ? '部署成功' : '部署失败'"
          :type="deployResult.success ? 'success' : 'error'"
          :description="deployResult.message"
          show-icon
          :closable="false"
          style="margin-bottom: 16px"
        />
        <el-timeline>
          <el-timeline-item
            v-for="(step, idx) in deployResult.steps"
            :key="idx"
            :type="step.success ? 'success' : 'danger'"
            :timestamp="step.step"
            placement="top"
          >
            <pre class="step-message">{{ step.message }}</pre>
          </el-timeline-item>
        </el-timeline>
        <div class="dialog-footer">
          <el-button @click="closeDeployDialog">关闭</el-button>
          <el-button v-if="!deployResult.success" type="primary" @click="backToDeployForm">重新填写</el-button>
        </div>
      </div>

      <!-- 部署表单 -->
      <el-form
        v-else
        ref="deployFormRef"
        :model="deployForm"
        :rules="deployRules"
        label-width="120px"
        v-loading="deploying"
      >
        <el-divider content-position="left">节点信息</el-divider>
        <el-form-item label="Worker 名称" prop="name">
          <el-input v-model="deployForm.name" placeholder="例如 worker-1" />
        </el-form-item>
        <el-form-item label="机器 IP" prop="host">
          <el-input v-model="deployForm.host" placeholder="远程节点 IP，如 192.168.1.100" />
        </el-form-item>
        <el-form-item label="gRPC 端口" prop="grpc_port">
          <el-input-number v-model="deployForm.grpc_port" :min="0" :max="65535" />
          <span class="form-hint">0 = 自动分配（推荐，同一环境重复部署不会撞端口）</span>
        </el-form-item>

        <el-divider content-position="left">SSH 登录</el-divider>
        <el-form-item label="SSH 端口" prop="ssh_port">
          <el-input-number v-model="deployForm.ssh_port" :min="1" :max="65535" />
        </el-form-item>
        <el-form-item label="SSH 用户名" prop="ssh_username">
          <el-input v-model="deployForm.ssh_username" placeholder="SSH 登录用户名" />
        </el-form-item>
        <el-form-item label="SSH 密码" prop="ssh_password">
          <el-input v-model="deployForm.ssh_password" type="password" show-password placeholder="SSH 登录密码" />
        </el-form-item>

        <el-divider content-position="left">Worker 配置</el-divider>
        <el-form-item label="Scheduler 地址" prop="scheduler_address">
          <el-input v-model="deployForm.scheduler_address" placeholder="worker 回连 scheduler 的地址，如 10.0.0.1:50051" />
        </el-form-item>
        <el-form-item label="最大任务数">
          <el-input-number v-model="deployForm.max_tasks" :min="1" :max="1000" />
        </el-form-item>
        <el-form-item label="资源标签">
          <el-select
            v-model="deployForm.resource_tags"
            multiple
            filterable
            allow-create
            default-first-option
            placeholder="输入标签后回车"
            style="width: 100%"
          />
        </el-form-item>
        <el-form-item label="日志级别">
          <el-select v-model="deployForm.log_level" style="width: 100%">
            <el-option label="debug" value="debug" />
            <el-option label="info" value="info" />
            <el-option label="warn" value="warn" />
            <el-option label="error" value="error" />
          </el-select>
        </el-form-item>

        <el-divider content-position="left">远程路径（高级）</el-divider>
        <el-form-item label="worker 路径">
          <el-input v-model="deployForm.worker_binary_path" placeholder="远程机器上 worker 可执行文件路径" />
        </el-form-item>
        <el-form-item label="工作目录">
          <el-input v-model="deployForm.remote_dir" placeholder="远程工作目录（配置与日志存放）" />
        </el-form-item>

        <div class="dialog-footer">
          <el-button @click="closeDeployDialog">取消</el-button>
          <el-button type="primary" :loading="deploying" @click="submitDeploy">部署并启动</el-button>
        </div>
      </el-form>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { Plus, Refresh } from '@element-plus/icons-vue'
import { getWorkers, deployWorker } from '../../api/worker'
import { usePolling } from '../../composables/usePolling'
import { formatTime } from '../../utils/format'
import type { WorkerInfo, DeployWorkerRequest, DeployWorkerResult } from '../../types/worker'

const loading = ref(false)
const workers = ref<WorkerInfo[]>([])

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

// Fix #180: 轮询调用时传 silent=true 跳过 loading 设置，避免每 10 秒闪烁
async function fetchWorkers(silent = false) {
  if (!silent) {
    loading.value = true
  }
  try {
    const res = await getWorkers()
    workers.value = res.data?.data?.items || res.data?.data || []
  } catch {
    ElMessage.error('获取 Worker 列表失败')
  } finally {
    if (!silent) {
      loading.value = false
    }
  }
}

// Fix #197/#341: 每 10 秒静默刷新（silent 跳过 loading 闪烁），
// 标签页隐藏时暂停、可见时恢复——统一由 usePolling 处理。
usePolling(() => fetchWorkers(true), { interval: 10000 })

// ===== 部署 Worker =====
const deployDialogVisible = ref(false)
const deploying = ref(false)
const deployResult = ref<DeployWorkerResult | null>(null)
const deployFormRef = ref<FormInstance>()

const deployForm = reactive<DeployWorkerRequest>({
  name: '',
  host: '',
  // 0 = 自动分配，由 worker 启动时向内核申请空闲端口
  grpc_port: 0,
  ssh_port: 22,
  ssh_username: '',
  ssh_password: '',
  max_tasks: 10,
  resource_tags: [],
  worker_binary_path: '/opt/taskflow/bin/worker',
  remote_dir: '/opt/taskflow/worker',
  scheduler_address: '',
  log_level: 'info'
})

const deployRules: FormRules = {
  name: [{ required: true, message: '请输入 Worker 名称', trigger: 'blur' }],
  host: [{ required: true, message: '请输入机器 IP', trigger: 'blur' }],
  grpc_port: [{ required: true, message: '请输入 gRPC 端口', trigger: 'blur' }],
  ssh_username: [{ required: true, message: '请输入 SSH 用户名', trigger: 'blur' }],
  ssh_password: [{ required: true, message: '请输入 SSH 密码', trigger: 'blur' }],
  scheduler_address: [{ required: true, message: '请输入 Scheduler 地址', trigger: 'blur' }]
}

function openDeployDialog() {
  deployResult.value = null
  deployDialogVisible.value = true
}

function closeDeployDialog() {
  deployDialogVisible.value = false
  deployResult.value = null
}

function backToDeployForm() {
  deployResult.value = null
}

async function submitDeploy() {
  if (!deployFormRef.value) return
  await deployFormRef.value.validate(async (valid) => {
    if (!valid) return
    deploying.value = true
    try {
      const res = await deployWorker({ ...deployForm })
      const data = res.data?.data
      if (data) {
        deployResult.value = data
        if (data.success) {
          ElMessage.success('Worker 部署成功')
          // 部署成功后刷新列表（worker 通过 gRPC 自注册，可能需要几秒）
          setTimeout(() => fetchWorkers(true), 3000)
        } else {
          ElMessage.warning('Worker 部署未完全成功，请查看步骤详情')
        }
      } else {
        ElMessage.error(res.data?.message || '部署失败')
      }
    } catch (err: any) {
      ElMessage.error(err?.response?.data?.message || '部署请求失败')
    } finally {
      deploying.value = false
    }
  })
}

onMounted(() => {
  fetchWorkers()
})
</script>

<style scoped>
.worker-list {
  padding: 20px;
}

.page-header {
  margin-bottom: 16px;
}

.page-header h2 {
  color: #303133;
}

.toolbar {
  margin-bottom: 16px;
  display: flex;
  gap: 8px;
}

.table-card {
  margin-bottom: 20px;
}

.resource-tag {
  margin-right: 4px;
  margin-bottom: 2px;
}

.form-hint {
  margin-left: 12px;
  color: #909399;
  font-size: 12px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 8px;
}

.deploy-result .step-message {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-all;
  font-family: inherit;
  font-size: 13px;
  color: #606266;
}
</style>
