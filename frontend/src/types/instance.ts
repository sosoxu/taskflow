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
  tasks: TaskInstance[]
}

export interface TaskInstance {
  id: string
  workflow_instance_id: string
  task_id: string
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
