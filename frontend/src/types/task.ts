export interface TaskItem {
  id: string
  name: string
  type: 'command' | 'script' | 'sql'
  config: TaskConfig
  description: string
  timeout: number
  max_retries: number
  retry_interval: number
  resource_tags: string[]
  creator_id: string
  version: number
  created_at: string
  updated_at: string
}

export interface TaskConfig {
  // Command
  command?: string
  work_dir?: string
  env_vars?: Record<string, string>
  // Script
  script_content?: string
  // SQL
  db_host?: string
  db_port?: number
  db_name?: string
  db_user?: string
  db_password?: string
  sql_statement?: string
}

export interface TaskCreateRequest {
  name: string
  type: 'command' | 'script' | 'sql'
  config: TaskConfig
  description?: string
  timeout?: number
  max_retries?: number
  retry_interval?: number
  resource_tags?: string[]
}

export interface TaskUpdateRequest extends Partial<TaskCreateRequest> {}
