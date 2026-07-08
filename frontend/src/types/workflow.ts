export interface WorkflowItem {
  id: string
  name: string
  description: string
  dag_json: DagGraph
  schedule_strategy: 'random' | 'load_balance' | 'specified'
  target_worker_id: string | null
  cron_expression: string | null
  cron_enabled: boolean
  creator_id: string
  creator_name?: string
  version: number
  // Fix #164: deleted is returned by the backend but was missing from the type,
  // causing untyped access and potential runtime confusion in list views.
  deleted: boolean
  created_at: string
  updated_at: string
}

export interface DagNode {
  id: string
  task_id: string
  // Fix #164: these fields are persisted by the backend and used by the editor
  // / detail views, but were missing from the type (callers used `any` casts).
  task_name?: string
  task_type?: string
  x?: number
  y?: number
  param_overrides?: Record<string, unknown>
}

export interface DagEdge {
  source: string
  target: string
}

export interface DagGraph {
  nodes: DagNode[]
  edges: DagEdge[]
}

export interface WorkflowCreateRequest {
  name: string
  description?: string
  dag_json: DagGraph
  schedule_strategy?: 'random' | 'load_balance' | 'specified'
  target_worker_id?: string
  cron_expression?: string
  cron_enabled?: boolean
}
