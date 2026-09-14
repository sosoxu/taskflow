<template>
  <div class="env-editor">
    <div v-for="(_, key) in model" :key="key" class="env-row">
      <el-input v-model="keyTemp[key]" placeholder="变量名" style="width: 160px" @change="updateKey(key)" />
      <span class="env-eq">=</span>
      <el-input v-model="model[key]" placeholder="变量值" style="flex: 1" />
      <el-button :icon="Delete" circle size="small" @click="removeVar(key)" />
    </div>
    <el-button size="small" :icon="Plus" @click="addVar">添加环境变量</el-button>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { Delete, Plus } from '@element-plus/icons-vue'

// Fix #341: TaskListView 的 command / script 两段「环境变量」编辑器逐字重复，
// 连同 keyTemp 状态与增删改逻辑一起收敛到这里，父组件只保留表单与提交。
const model = defineModel<Record<string, string>>({ default: () => ({}) })

// 变量名输入框的暂存值：key 是配置里的旧名，value 是用户正在编辑的新名
const keyTemp = ref<Record<string, string>>({})
let newKeyCounter = 0

/** 编辑已有任务时，把配置里的变量名填进输入框 */
function loadKeys(envVars?: Record<string, string>) {
  keyTemp.value = {}
  if (!envVars) return
  for (const key of Object.keys(envVars)) {
    keyTemp.value[key] = key
  }
}

function reset() {
  keyTemp.value = {}
  model.value = {}
}

function addVar() {
  const placeholder = `__new_${++newKeyCounter}`
  model.value = { ...model.value, [placeholder]: '' }
  keyTemp.value[placeholder] = ''
}

function removeVar(key: string) {
  const next = { ...model.value }
  delete next[key]
  model.value = next
  delete keyTemp.value[key]
}

function updateKey(oldKey: string) {
  const newKey = keyTemp.value[oldKey]?.trim()
  if (!newKey || newKey === oldKey) return
  // Fix #229: Reject duplicate env var keys — otherwise renaming would
  // silently overwrite the value of an existing variable with the same name.
  if (Object.prototype.hasOwnProperty.call(model.value, newKey)) {
    ElMessage.warning(`环境变量 "${newKey}" 已存在，请使用其他名称`)
    // Restore the input to the old key so the user can correct it
    keyTemp.value[oldKey] = oldKey
    return
  }
  const value = model.value[oldKey] ?? ''
  const next = { ...model.value }
  delete next[oldKey]
  next[newKey] = value
  model.value = next
  delete keyTemp.value[oldKey]
  keyTemp.value[newKey] = newKey
}

/** 是否还有只填了占位名、没改成真实变量名的条目（Fix #196） */
function hasPendingKeys(): boolean {
  return Object.keys(model.value ?? {}).some((k) => k.startsWith('__new_'))
}

/** 提交前把占位 key 换成用户输入的真实名字；无有效变量时返回 undefined */
function resolveKeys(): Record<string, string> | undefined {
  const cleaned: Record<string, string> = {}
  for (const [k, v] of Object.entries(model.value ?? {})) {
    const realKey = keyTemp.value[k]?.trim() || k
    if (realKey && !realKey.startsWith('__new_')) {
      cleaned[realKey] = v
    }
  }
  return Object.keys(cleaned).length > 0 ? cleaned : undefined
}

defineExpose({ loadKeys, reset, hasPendingKeys, resolveKeys })
</script>

<style scoped>
.env-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.env-eq {
  color: #909399;
}
</style>
