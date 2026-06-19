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
    // Fix #226: backend recent_instances SQL selects wi.created_at (not started_at);
    // align the type so the DashboardView binding `row.created_at` type-checks.
    created_at: string
  }>
}

export function getDashboardStats(): Promise<AxiosResponse<ApiResponse<DashboardStats>>> {
  return request.get('/api/v1/dashboard/stats')
}
