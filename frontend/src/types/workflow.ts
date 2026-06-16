export interface WorkflowItem {
  id: string
  name: string
  description: string
  dag: DagGraph
  schedule_strategy: 'random' | 'load_balance' | 'specified'
  target_worker_id: string | null
  cron_expression: string | null
  cron_enabled: boolean
  creator_id: string
  version: number
  created_at: string
  updated_at: string
}

export interface DagNode {
  id: string
  task_id: string
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
  dag: DagGraph
  schedule_strategy?: 'random' | 'load_balance' | 'specified'
  target_worker_id?: string
  cron_expression?: string
  cron_enabled?: boolean
}
