import request from '../utils/request'
import type { AxiosResponse } from 'axios'
// Fix #176: typed API responses
import type { ApiResponse } from '../types/api'

export interface DashboardStats {
  // Fix #176: field names match backend snake_case JSON keys
  total_tasks: number
  total_workflows: number
  running_instances: number
  online_workers: number
  today_executions: number
  success_rate: number
  recent_instances: Array<{
    id: string
    workflow_name: string
    status: string
    started_at: string
  }>
}

export function getDashboardStats(): Promise<AxiosResponse<ApiResponse<DashboardStats>>> {
  return request.get('/api/v1/dashboard/stats')
}
