<template>
  <div class="task-detail-view">
    <div class="page-header">
      <el-button :icon="ArrowLeft" @click="goBack">返回</el-button>
      <h2>任务详情</h2>
    </div>

    <div v-loading="loading">
      <template v-if="task">
        <el-card class="info-card" shadow="never">
          <template #header>
            <span class="card-title">基本信息</span>
          </template>
          <el-descriptions :column="2" border>
            <el-descriptions-item label="名称">{{ task.name }}</el-descriptions-item>
            <el-descriptions-item label="类型">
              <el-tag :type="typeTagMap[task.type]?.type || 'info'" effect="plain">
                {{ typeTagMap[task.type]?.label || task.type }}
              </el-tag>
            </el-descriptions-item>
            <el-descriptions-item label="描述" :span="2">{{ task.description || '-' }}</el-descriptions-item>
            <el-descriptions-item label="超时(s)">{{ task.timeout }}</el-descriptions-item>
            <el-descriptions-item label="最大重试">{{ task.max_retries }}</el-descriptions-item>
            <el-descriptions-item label="重试间隔(s)">{{ task.retry_interval }}</el-descriptions-item>
            <el-descriptions-item label="版本">{{ task.version }}</el-descriptions-item>
            <el-descriptions-item label="资源标签" :span="2">
              <template v-if="task.resource_tags?.length">
                <el-tag v-for="tag in task.resource_tags" :key="tag" size="small" style="margin-right: 6px">{{ tag }}</el-tag>
              </template>
              <span v-else>-</span>
            </el-descriptions-item>
            <el-descriptions-item label="创建者">{{ task.creator_id }}</el-descriptions-item>
            <el-descriptions-item label="创建时间">{{ formatTime(task.created_at) }}</el-descriptions-item>
            <el-descriptions-item label="更新时间">{{ formatTime(task.updated_at) }}</el-descriptions-item>
          </el-descriptions>
        </el-card>

        <el-card class="config-card" shadow="never">
          <template #header>
            <span class="card-title">任务配置</span>
          </template>

          <!-- Command config -->
          <template v-if="task.type === 'command'">
            <el-descriptions :column="1" border>
              <el-descriptions-item label="命令">{{ task.config_json?.command || '-' }}</el-descriptions-item>
              <el-descriptions-item label="工作目录">{{ task.config_json?.working_dir || '-' }}</el-descriptions-item>
              <el-descriptions-item label="环境变量">
                <template v-if="task.config_json?.env_vars && Object.keys(task.config_json.env_vars).length">
                  <div v-for="(val, key) in task.config_json.env_vars" :key="key" class="env-item">
                    <span class="env-key">{{ key }}</span>={{ val }}
                  </div>
                </template>
                <span v-else>-</span>
              </el-descriptions-item>
            </el-descriptions>
          </template>

          <!-- Script config -->
          <template v-else-if="task.type === 'script'">
            <el-descriptions :column="1" border>
              <el-descriptions-item label="脚本内容">
                <pre class="script-content">{{ task.config_json?.script_content || '-' }}</pre>
              </el-descriptions-item>
            </el-descriptions>
          </template>

          <!-- SQL config -->
          <template v-else-if="task.type === 'sql'">
            <el-descriptions :column="2" border>
              <el-descriptions-item label="数据库地址">{{ task.config_json?.db_host || '-' }}</el-descriptions-item>
              <el-descriptions-item label="端口">{{ task.config_json?.db_port || '-' }}</el-descriptions-item>
              <el-descriptions-item label="数据库名">{{ task.config_json?.db_name || '-' }}</el-descriptions-item>
              <el-descriptions-item label="用户名">{{ task.config_json?.db_user || '-' }}</el-descriptions-item>
              <el-descriptions-item label="SQL 语句" :span="2">
                <pre class="script-content">{{ task.config_json?.sql_statement || '-' }}</pre>
              </el-descriptions-item>
            </el-descriptions>
          </template>

          <!-- Fallback: raw JSON -->
          <template v-else>
            <pre class="config-json">{{ JSON.stringify(task.config_json, null, 2) }}</pre>
          </template>
        </el-card>
      </template>

      <el-empty v-else-if="!loading" description="任务不存在" />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { ArrowLeft } from '@element-plus/icons-vue'
import { getTask } from '../../api/task'
import type { TaskItem } from '../../types/task'
import { formatTime } from '../../utils/format'

const route = useRoute()
const router = useRouter()
const taskId = route.params.id as string

const typeTagMap: Record<string, { type: string; label: string }> = {
  command: { type: 'primary', label: 'Command' },
  script: { type: 'success', label: 'Script' },
  sql: { type: 'warning', label: 'SQL' },
}

const loading = ref(false)
const task = ref<TaskItem | null>(null)

async function fetchTask() {
  loading.value = true
  try {
    const { data: resp } = await getTask(taskId)
    const data = resp.data
    task.value = data
  } catch {
    ElMessage.error('获取任务详情失败')
  } finally {
    loading.value = false
  }
}

function goBack() {
  router.push({ name: 'task-list' })
}

onMounted(() => {
  fetchTask()
})
</script>

<style scoped>
.task-detail-view {
  padding: 20px;
}

.page-header {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 20px;
}

.page-header h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
}

.info-card {
  margin-bottom: 20px;
}

.config-card {
  margin-bottom: 20px;
}

.card-title {
  font-weight: 600;
  font-size: 15px;
}

.script-content {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-all;
  font-family: 'Menlo', 'Monaco', 'Courier New', monospace;
  font-size: 13px;
  line-height: 1.6;
  background: #f5f7fa;
  padding: 12px;
  border-radius: 4px;
}

.config-json {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-all;
  font-family: 'Menlo', 'Monaco', 'Courier New', monospace;
  font-size: 13px;
  line-height: 1.6;
  background: #f5f7fa;
  padding: 12px;
  border-radius: 4px;
}

.env-item {
  line-height: 1.8;
}

.env-key {
  font-weight: 600;
  color: #409eff;
}
</style>
