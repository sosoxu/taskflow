import request from '../utils/request'
import type { AxiosResponse } from 'axios'
import type { WorkflowInstance } from '../types/instance'
// Fix #176: typed API responses
import type { ApiResponse, PaginatedData } from '../types/api'

export function getWorkflowInstances(workflowId: string, params?: { page?: number; page_size?: number }): Promise<AxiosResponse<ApiResponse<PaginatedData<WorkflowInstance>>>> {
  return request.get(`/api/v1/workflows/${workflowId}/instances`, { params })
}

export function getInstance(id: string): Promise<AxiosResponse<ApiResponse<WorkflowInstance>>> {
  return request.get(`/api/v1/instances/${id}`)
}

export function getAllInstances(params?: { page?: number; page_size?: number; task_id?: string }): Promise<AxiosResponse<ApiResponse<PaginatedData<WorkflowInstance>>>> {
  return request.get('/api/v1/instances', { params })
}

export function pauseInstance(id: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.post(`/api/v1/instances/${id}/pause`)
}

export function resumeInstance(id: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.post(`/api/v1/instances/${id}/resume`)
}

export function cancelInstance(id: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.post(`/api/v1/instances/${id}/cancel`)
}

export function retryTask(instanceId: string, taskInstanceId: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.post(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/retry`)
}

export function killTask(instanceId: string, taskInstanceId: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.post(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/kill`)
}
