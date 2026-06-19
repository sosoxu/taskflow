export type WorkflowInstanceStatus =
  | 'PENDING'
  | 'RUNNING'
  | 'PAUSED'
  | 'SUCCESS'
  | 'FAILED'
  | 'CANCELLED'

export type TaskInstanceStatus =
  | 'PENDING'
  | 'DISPATCHED'
  | 'RUNNING'
  | 'SUCCESS'
  | 'FAILED'
  | 'UPSTREAM_FAILED'
  | 'TIMEOUT'
  | 'CANCELLED'
  | 'NODE_OFFLINE'

export interface WorkflowInstance {
  id: string
  workflow_id: string
  workflow_version: number
  status: WorkflowInstanceStatus
  trigger_type: 'manual' | 'cron'
  started_at: string | null
  finished_at: string | null
  creator_id: string
  // Fix #231: Backend WorkflowInstance model (workflow_instance.h) includes
  // created_at and toJson() returns it. The "创建时间" columns in list views
  // should bind to created_at (not started_at, which is null until RUNNING).
  created_at: string | null
  // Fix #224: Backend returns "task_instances" (instance_service.cpp:423),
  // not "tasks". The old field name caused instance.tasks to be undefined at
  // runtime, breaking the task table, DAG visualization, and retry button.
  task_instances: TaskInstance[]
}

export interface TaskInstance {
  id: string
  workflow_instance_id: string
  task_id: string
  // Fix #164/#165: node_id is returned by the backend but was missing from
  // the type, forcing callers to use untyped access.
  node_id: string
  task_version: number
  task_name: string
  status: TaskInstanceStatus
  worker_id: string | null
  retry_count: number
  started_at: string | null
  finished_at: string | null
  exit_code: number | null
  error_message: string | null
}
