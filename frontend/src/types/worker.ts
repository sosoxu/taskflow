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

// 部署（新建 + 启动）worker 的请求参数
export interface DeployWorkerRequest {
  name: string
  host: string
  // 0 = 自动分配（默认）：worker 启动时由内核挑空闲端口，注册时上报实际地址
  grpc_port: number
  ssh_port?: number
  ssh_username: string
  ssh_password: string
  max_tasks?: number
  resource_tags?: string[]
  worker_binary_path?: string
  remote_dir?: string
  scheduler_address: string
  log_level?: string
}

export interface DeployStepLog {
  step: string
  success: boolean
  message: string
}

export interface DeployWorkerResult {
  success: boolean
  worker_name: string
  address: string
  config_path: string
  steps: DeployStepLog[]
  message: string
}
