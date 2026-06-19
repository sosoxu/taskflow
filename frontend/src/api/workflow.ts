import request from '../utils/request'
import type { AxiosResponse } from 'axios'
import type { WorkflowItem, WorkflowCreateRequest } from '../types/workflow'
// Fix #176: import shared API response types from types/api.ts
import type { ApiResponse, PaginatedData } from '../types/api'

// Re-export for backward compatibility (callers that imported from workflow.ts)
export type { ApiResponse, PaginatedData }

export function getWorkflows(params?: { page?: number; page_size?: number; keyword?: string }) {
  return request.get('/api/v1/workflows', { params })
}

export function getWorkflow(id: string): Promise<AxiosResponse<ApiResponse<WorkflowItem>>> {
  return request.get(`/api/v1/workflows/${id}`)
}

// Fix #166: use the typed WorkflowCreateRequest instead of `unknown`.
export function createWorkflow(data: WorkflowCreateRequest) {
  return request.post('/api/v1/workflows', data)
}

export function updateWorkflow(id: string, data: WorkflowCreateRequest) {
  return request.put(`/api/v1/workflows/${id}`, data)
}

export function deleteWorkflow(id: string) {
  return request.delete(`/api/v1/workflows/${id}`)
}

export function triggerWorkflow(id: string, paramOverrides?: Record<string, unknown>) {
  const data: Record<string, unknown> = {}
  if (paramOverrides && Object.keys(paramOverrides).length > 0) {
    data.param_overrides = paramOverrides
  }
  return request.post(`/api/v1/workflows/${id}/trigger`, data)
}
