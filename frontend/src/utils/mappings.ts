import type { TaskInstanceStatus, WorkflowInstanceStatus } from '../types/instance'

// Fix #341: 状态/策略/任务类型的标签映射此前散落在 8 个视图里各写一份
// （instanceStatusType ×4、taskStatusType、strategyTagType/strategyLabel ×2、
// typeTagMap ×2、taskTypeTag ×2）。这里收敛为唯一实现，视图只做渲染。

export type TagType = '' | 'primary' | 'success' | 'warning' | 'danger' | 'info'

// 收敛说明：此前 instanceStatusType 的 4 份拷贝对 RUNNING 有两种写法——
// 'warning'（InstanceListView / InstanceDetailView）与 ''（WorkflowDetailView /
// TaskDetailView / DashboardView）。统一取 ''（Element Plus 默认色，蓝色）：
// 各版本里 PAUSED 都已是 'warning'，RUNNING 也用 'warning' 会让两者同色，
// 反而分不清「正在跑」和「已暂停」。
const INSTANCE_STATUS_TAG: Record<WorkflowInstanceStatus, TagType> = {
  PENDING: 'info',
  RUNNING: '',
  PAUSED: 'warning',
  SUCCESS: 'success',
  FAILED: 'danger',
  CANCELLED: 'info',
}

export function instanceStatusType(status: WorkflowInstanceStatus | string): TagType {
  return INSTANCE_STATUS_TAG[status as WorkflowInstanceStatus] ?? 'info'
}

const TASK_STATUS_TAG: Record<TaskInstanceStatus, TagType> = {
  PENDING: 'info',
  DISPATCHED: 'info',
  RUNNING: '',
  SUCCESS: 'success',
  FAILED: 'danger',
  UPSTREAM_FAILED: 'warning',
  TIMEOUT: 'danger',
  CANCELLED: 'info',
  NODE_OFFLINE: 'danger',
}

export function taskStatusType(status: TaskInstanceStatus | string): TagType {
  return TASK_STATUS_TAG[status as TaskInstanceStatus] ?? 'info'
}

const STRATEGY_TAG: Record<string, TagType> = {
  random: 'info',
  load_balance: 'success',
  specified: 'warning',
}

export function strategyTagType(strategy?: string): TagType {
  return STRATEGY_TAG[strategy ?? ''] ?? 'info'
}

const STRATEGY_LABEL: Record<string, string> = {
  random: '随机',
  load_balance: '负载均衡',
  specified: '指定节点',
}

export function strategyLabel(strategy?: string): string {
  return STRATEGY_LABEL[strategy ?? ''] ?? (strategy || '-')
}

// 任务类型 → 标签。command 用 'primary'（与 Element Plus 默认色一致，
// 此前 taskTypeTag 各版本用空串表示同一效果）。
const TASK_TYPE_TAG: Record<string, { type: TagType; label: string }> = {
  command: { type: 'primary', label: 'Command' },
  script: { type: 'success', label: 'Script' },
  sql: { type: 'warning', label: 'SQL' },
}

export function taskTypeTag(type: string): TagType {
  return TASK_TYPE_TAG[type]?.type ?? 'info'
}

export function taskTypeLabel(type: string): string {
  return TASK_TYPE_TAG[type]?.label ?? type
}
