<template>
  <div class="task-list-view">
    <div class="page-header">
      <h2>任务管理</h2>
    </div>

    <div class="toolbar">
      <div class="search-bar">
        <el-input
          v-model="keyword"
          placeholder="搜索任务名称"
          clearable
          style="width: 220px"
          @keyup.enter="handleSearch"
        />
        <el-select v-model="typeFilter" placeholder="任务类型" clearable style="width: 140px; margin-left: 12px">
          <el-option label="全部" value="" />
          <el-option label="Command" value="command" />
          <el-option label="Script" value="script" />
          <el-option label="SQL" value="sql" />
        </el-select>
        <el-button type="primary" :icon="Search" style="margin-left: 12px" @click="handleSearch">搜索</el-button>
      </div>
      <el-button v-if="userStore.isOperator" type="primary" :icon="Plus" @click="handleCreate">创建任务</el-button>
    </div>

    <el-table :data="taskList" v-loading="loading" stripe style="width: 100%">
      <el-table-column prop="name" label="名称" min-width="140" />
      <el-table-column prop="type" label="类型" width="120">
        <template #default="{ row }">
          <el-tag :type="taskTypeTag(row.type)" effect="plain">
            {{ taskTypeLabel(row.type) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="description" label="描述" min-width="180" show-overflow-tooltip />
      <el-table-column prop="timeout" label="超时(s)" width="100" />
      <el-table-column prop="max_retries" label="最大重试" width="100" />
      <el-table-column prop="creator_name" label="创建者" width="120" show-overflow-tooltip />
      <el-table-column label="创建时间" width="180">
        <template #default="{ row }">{{ formatTime(row.created_at) }}</template>
      </el-table-column>
      <el-table-column label="操作" width="200" fixed="right">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="handleView(row)">查看</el-button>
          <!-- Fix #172: viewer 角色隐藏写操作按钮 -->
          <el-button v-if="userStore.isOperator" link type="primary" size="small" @click="handleEdit(row)">编辑</el-button>
          <el-popconfirm v-if="userStore.isOperator" title="确认删除该任务？" @confirm="handleDelete(row)">
            <template #reference>
              <el-button link type="danger" size="small">删除</el-button>
            </template>
          </el-popconfirm>
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

    <el-dialog v-model="dialogVisible" :title="isEdit ? '编辑任务' : '创建任务'" width="640px" destroy-on-close>
      <el-form ref="formRef" :model="form" :rules="formRules" label-width="100px">
        <el-form-item label="名称" prop="name">
          <el-input v-model="form.name" placeholder="请输入任务名称" />
        </el-form-item>
        <el-form-item label="类型" prop="type">
          <el-select v-model="form.type" placeholder="请选择任务类型" style="width: 100%" :disabled="isEdit" @change="handleTypeChange">
            <el-option label="Command" value="command" />
            <el-option label="Script" value="script" />
            <el-option label="SQL" value="sql" />
          </el-select>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" type="textarea" :rows="3" placeholder="请输入描述" />
        </el-form-item>
        <el-form-item label="超时(s)">
          <el-input-number v-model="form.timeout" :min="1" :max="86400" />
        </el-form-item>
        <el-form-item label="最大重试">
          <el-input-number v-model="form.max_retries" :min="0" :max="100" />
        </el-form-item>
        <el-form-item label="重试间隔(s)">
          <el-input-number v-model="form.retry_interval" :min="1" :max="86400" />
        </el-form-item>
        <el-form-item label="资源标签">
          <el-select
            v-model="form.resource_tags"
            multiple
            filterable
            allow-create
            default-first-option
            placeholder="输入标签后回车"
            style="width: 100%"
          />
        </el-form-item>

        <!-- Command config -->
        <template v-if="form.type === 'command'">
          <el-divider content-position="left">Command 配置</el-divider>
          <el-form-item label="命令">
            <el-input v-model="form.config.command" placeholder="请输入命令，支持 ${var} 或 {var} 占位符" />
          </el-form-item>
          <el-form-item label="工作目录">
            <el-input v-model="form.config.working_dir" placeholder="留空使用默认目录" />
          </el-form-item>
          <el-form-item label="环境变量">
            <EnvVarEditor ref="envEditorRef" v-model="form.config.env_vars" />
          </el-form-item>
        </template>

        <!-- Script config -->
        <template v-if="form.type === 'script'">
          <el-divider content-position="left">Script 配置</el-divider>
          <el-form-item label="脚本内容">
            <div ref="scriptEditorRef" class="code-editor"></div>
          </el-form-item>
          <el-form-item label="工作目录">
            <el-input v-model="form.config.working_dir" placeholder="留空使用默认目录" />
          </el-form-item>
          <el-form-item label="环境变量">
            <EnvVarEditor ref="envEditorRef" v-model="form.config.env_vars" />
          </el-form-item>
        </template>

        <!-- SQL config -->
        <template v-if="form.type === 'sql'">
          <el-divider content-position="left">SQL 配置</el-divider>
          <el-form-item label="数据库地址">
            <el-input v-model="form.config.db_host" placeholder="请输入数据库地址，支持 ${var} 或 {var} 占位符" />
          </el-form-item>
          <el-form-item label="端口">
            <el-input-number v-model="form.config.db_port" :min="1" :max="65535" />
          </el-form-item>
          <el-form-item label="数据库名">
            <el-input v-model="form.config.db_name" placeholder="请输入数据库名，支持 ${var} 或 {var} 占位符" />
          </el-form-item>
          <el-form-item label="用户名">
            <el-input v-model="form.config.db_user" placeholder="请输入用户名，支持 ${var} 或 {var} 占位符" />
          </el-form-item>
          <el-form-item label="密码">
            <el-input v-model="form.config.db_password" type="password" show-password placeholder="请输入密码，支持 ${var} 或 {var} 占位符" />
          </el-form-item>
          <el-form-item label="SQL 语句">
            <div ref="sqlEditorRef" class="code-editor"></div>
          </el-form-item>
        </template>

        <!-- Parameters definition -->
        <el-divider content-position="left">参数定义</el-divider>
        <el-alert
          type="info"
          :closable="false"
          show-icon
          style="margin-bottom: 12px"
        >
          <template #title>
            在配置字段中使用 <code>${"{"}var_name{"}"}</code> 或 <code>{"{"}var_name{"}"}</code> 占位符，然后在下方定义参数默认值。执行时可通过工作流参数覆盖传入实际值。
          </template>
        </el-alert>
        <div class="param-editor">
          <div v-for="(param, index) in paramEntries" :key="index" class="param-row">
            <el-input v-model="param.key" placeholder="参数名" style="width: 160px" @change="onParamKeyChange" />
            <span class="param-eq">=</span>
            <el-input v-model="param.value" placeholder="默认值（留空则执行时必须传入）" style="flex: 1" />
            <el-button :icon="Delete" circle size="small" @click="removeParam(index)" />
          </div>
          <el-button size="small" :icon="Plus" @click="addParam">添加参数</el-button>
        </div>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="submitting" @click="handleSubmit">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, nextTick, watch, onBeforeUnmount } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Search, Plus, Delete } from '@element-plus/icons-vue'
import { getTasks, getTask, createTask, updateTask, deleteTask } from '../../api/task'
import EnvVarEditor from '../../components/EnvVarEditor.vue'
import type { TaskItem, TaskConfig } from '../../types/task'
import { usePagination } from '../../composables/usePagination'
import { formatTime } from '../../utils/format'
import { taskTypeLabel, taskTypeTag } from '../../utils/mappings'
import { useUserStore } from '../../stores/userStore'
import { basicSetup } from 'codemirror'
import { EditorView } from '@codemirror/view'
import { EditorState } from '@codemirror/state'
import { sql, PostgreSQL } from '@codemirror/lang-sql'
import { javascript } from '@codemirror/lang-javascript'
import { oneDark } from '@codemirror/theme-one-dark'

const router = useRouter()
// Fix #172: viewer 角色隐藏写操作按钮
const userStore = useUserStore()

// List state
const loading = ref(false)
const taskList = ref<TaskItem[]>([])
const keyword = ref('')
const typeFilter = ref('')

// Fix #341: 分页样板改用 usePagination
const { page, pageSize, total, handleSizeChange, resetToFirstPage } = usePagination(fetchList)

async function fetchList() {
  loading.value = true
  try {
    const params: Record<string, unknown> = {
      page: page.value,
      page_size: pageSize.value,
    }
    if (keyword.value) params.keyword = keyword.value
    if (typeFilter.value) params.type = typeFilter.value
    const { data: resp } = await getTasks(params as Parameters<typeof getTasks>[0])
    const data = resp.data
    taskList.value = data?.items || []
    total.value = data?.total || 0
  } catch {
    ElMessage.error('获取任务列表失败')
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  resetToFirstPage()
  fetchList()
}

function handleView(row: TaskItem) {
  router.push({ name: 'task-detail', params: { id: row.id } })
}

// Dialog state
const dialogVisible = ref(false)
const isEdit = ref(false)
const editingId = ref('')
const submitting = ref(false)
const formRef = ref()

const defaultConfig = (): TaskConfig => ({
  env_vars: {},
})

// Fix #341: 环境变量编辑器的状态与增删改逻辑移入 EnvVarEditor 组件
const envEditorRef = ref<InstanceType<typeof EnvVarEditor> | null>(null)

const form = reactive({
  name: '',
  type: 'command' as 'command' | 'script' | 'sql',
  description: '',
  timeout: 3600,
  max_retries: 0,
  retry_interval: 60,
  resource_tags: [] as string[],
  config: defaultConfig(),
})

// Parameters editor state
interface ParamEntry { key: string; value: string }
const paramEntries = ref<ParamEntry[]>([])

function addParam() {
  paramEntries.value.push({ key: '', value: '' })
}

function removeParam(index: number) {
  paramEntries.value.splice(index, 1)
}

function onParamKeyChange() {
  // Trigger reactivity
  paramEntries.value = [...paramEntries.value]
}

function paramsFromEntries(): Record<string, unknown> {
  const params: Record<string, unknown> = {}
  for (const entry of paramEntries.value) {
    const key = entry.key.trim()
    if (key) {
      params[key] = entry.value
    }
  }
  return params
}

function entriesFromParams(params: Record<string, unknown> | undefined) {
  if (!params || typeof params !== 'object') {
    paramEntries.value = []
    return
  }
  paramEntries.value = Object.entries(params).map(([key, value]) => ({
    key,
    value: typeof value === 'string' ? value : JSON.stringify(value),
  }))
}

const formRules = {
  name: [{ required: true, message: '请输入任务名称', trigger: 'blur' }],
  type: [{ required: true, message: '请选择任务类型', trigger: 'change' }],
}

function resetForm() {
  form.name = ''
  form.type = 'command'
  form.description = ''
  form.timeout = 3600
  form.max_retries = 0
  form.retry_interval = 60
  form.resource_tags = []
  form.config = defaultConfig()
  envEditorRef.value?.reset()
  paramEntries.value = []
}

function handleTypeChange() {
  form.config = defaultConfig()
  // Fix #236/#341: 切换类型时重置环境变量编辑器。旧实现是清空 envKeyTemp，
  // 否则上个类型遗留的占位 key（如 __new_1）会卡住提交前的
  // 「请先完成环境变量名输入」校验——即便新配置根本没有环境变量。
  envEditorRef.value?.reset()
  destroyEditors()
  nextTick(() => {
    initEditorForType()
  })
}

// CodeMirror editors
const sqlEditorRef = ref<HTMLElement | null>(null)
const scriptEditorRef = ref<HTMLElement | null>(null)
let sqlEditorView: EditorView | null = null
let scriptEditorView: EditorView | null = null

function destroyEditors() {
  if (sqlEditorView) {
    sqlEditorView.destroy()
    sqlEditorView = null
  }
  if (scriptEditorView) {
    scriptEditorView.destroy()
    scriptEditorView = null
  }
}

function initSqlEditor() {
  if (sqlEditorRef.value && !sqlEditorView) {
    sqlEditorView = new EditorView({
      state: EditorState.create({
        doc: form.config.sql_statement || '',
        extensions: [
          basicSetup,
          sql({ dialect: PostgreSQL }),
          oneDark,
          EditorView.updateListener.of((update) => {
            if (update.docChanged) {
              form.config.sql_statement = update.state.doc.toString()
            }
          }),
        ],
      }),
      parent: sqlEditorRef.value,
    })
  }
}

function initScriptEditor() {
  if (scriptEditorRef.value && !scriptEditorView) {
    scriptEditorView = new EditorView({
      state: EditorState.create({
        doc: form.config.script_content || '',
        extensions: [
          basicSetup,
          javascript(),
          oneDark,
          EditorView.updateListener.of((update) => {
            if (update.docChanged) {
              form.config.script_content = update.state.doc.toString()
            }
          }),
        ],
      }),
      parent: scriptEditorRef.value,
    })
  }
}

function initEditorForType() {
  if (form.type === 'sql') {
    initSqlEditor()
  } else if (form.type === 'script') {
    initScriptEditor()
  }
}

function handleCreate() {
  isEdit.value = false
  editingId.value = ''
  resetForm()
  destroyEditors()
  dialogVisible.value = true
  nextTick(() => {
    initEditorForType()
  })
}

async function handleEdit(row: TaskItem) {
  isEdit.value = true
  editingId.value = row.id
  try {
    const { data: resp } = await getTask(row.id)
    const data = resp.data
    const task = data
    form.name = task.name
    form.type = task.type
    form.description = task.description || ''
    form.timeout = task.timeout ?? 3600
    form.max_retries = task.max_retries ?? 0
    form.retry_interval = task.retry_interval ?? 60
    form.resource_tags = task.resource_tags || []
    form.config = { ...defaultConfig(), ...(task.config_json || {}) }
    // Populate parameters from task
    entriesFromParams(task.parameters_json)
    destroyEditors()
    dialogVisible.value = true
    nextTick(() => {
      // Populate env key temp for editing
      envEditorRef.value?.loadKeys(form.config.env_vars)
      initEditorForType()
    })
  } catch {
    ElMessage.error('获取任务详情失败')
  }
}

async function handleSubmit() {
  try {
    await formRef.value?.validate()
  } catch {
    return
  }
  // Fix #196: 提交前检查是否存在未完成的环境变量名输入（占位符 key 仍存在），
  // 避免用户输入 key 后未失焦直接提交导致该变量被静默丢弃。
  if (envEditorRef.value?.hasPendingKeys()) {
    ElMessage.warning('请先完成环境变量名输入')
    return
  }
  // Fix #174: 按任务类型校验必填字段
  if (form.type === 'command') {
    if (!form.config.command?.trim()) {
      ElMessage.error('请输入命令')
      return
    }
  } else if (form.type === 'script') {
    if (!form.config.script_content?.trim()) {
      ElMessage.error('请输入脚本内容')
      return
    }
  } else if (form.type === 'sql') {
    if (!form.config.db_host?.trim()) {
      ElMessage.error('请输入数据库地址')
      return
    }
    if (!form.config.db_name?.trim()) {
      ElMessage.error('请输入数据库名')
      return
    }
    if (!form.config.db_user?.trim()) {
      ElMessage.error('请输入用户名')
      return
    }
    if (!form.config.db_password?.trim()) {
      ElMessage.error('请输入密码')
      return
    }
    if (!form.config.sql_statement?.trim()) {
      ElMessage.error('请输入 SQL 语句')
      return
    }
  }
  submitting.value = true
  try {
    // Clean up env_vars: remove placeholder keys (keys starting with __new_)
    const cleanConfig = { ...form.config }
    if (cleanConfig.env_vars) {
      // 占位 key 换成用户输入的真实变量名；无有效变量时整体移除
      const cleaned = envEditorRef.value?.resolveKeys()
      if (cleaned) {
        cleanConfig.env_vars = cleaned
      } else {
        delete cleanConfig.env_vars
      }
    }

    const payload = {
      name: form.name,
      type: form.type,
      description: form.description,
      timeout: form.timeout,
      max_retries: form.max_retries,
      retry_interval: form.retry_interval,
      resource_tags: form.resource_tags,
      config_json: cleanConfig,
      parameters_json: paramsFromEntries(),
    }
    if (isEdit.value) {
      await updateTask(editingId.value, payload)
      ElMessage.success('更新成功')
    } else {
      await createTask(payload)
      ElMessage.success('创建成功')
    }
    dialogVisible.value = false
    fetchList()
  } catch {
    ElMessage.error(isEdit.value ? '更新失败' : '创建失败')
  } finally {
    submitting.value = false
  }
}

async function handleDelete(row: TaskItem) {
  try {
    await deleteTask(row.id)
    ElMessage.success('删除成功')
    fetchList()
  } catch {
    ElMessage.error('删除失败')
  }
}

onMounted(() => {
  fetchList()
})

// Fix #193: 对话框关闭时销毁 CodeMirror 编辑器，避免 EditorView 引用泄漏
watch(dialogVisible, (visible) => {
  if (!visible) {
    destroyEditors()
  }
})

onBeforeUnmount(() => {
  destroyEditors()
})
</script>

<style scoped>
.task-list-view {
  padding: 20px;
}

.page-header {
  margin-bottom: 20px;
}

.page-header h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
}

.toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.search-bar {
  display: flex;
  align-items: center;
}

.pagination-wrapper {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

.code-editor {
  border: 1px solid #dcdfe6;
  border-radius: 4px;
  overflow: hidden;
  width: 100%;
}

.code-editor :deep(.cm-editor) {
  height: 200px;
  font-size: 13px;
}

.code-editor :deep(.cm-scroller) {
  overflow: auto;
}

.param-editor {
  width: 100%;
}

.param-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.param-eq {
  color: #909399;
  font-weight: 600;
}
</style>
