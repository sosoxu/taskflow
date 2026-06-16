export interface WorkerInfo {
  id: string
  name: string
  address: string
  status: 'online' | 'offline'
  cpu_usage: number
  memory_usage: number
  running_tasks: number
  max_tasks: number
  resource_tags: string[]
  last_heartbeat: string
  registered_at: string
}
