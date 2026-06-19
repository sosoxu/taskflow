import request from '../utils/request'
import type { AxiosResponse } from 'axios'
import type { WorkerInfo } from '../types/worker'
// Fix #176: typed API responses
import type { ApiResponse } from '../types/api'

// Backend returns { items: [...], total: N } (no page/page_size for workers)
export interface WorkerListResponse {
  items: WorkerInfo[]
  total: number
}

export function getWorkers(): Promise<AxiosResponse<ApiResponse<WorkerListResponse>>> {
  return request.get('/api/v1/workers')
}
