import request from '../utils/request'
import type { AxiosResponse } from 'axios'
import type { TaskItem, TaskCreateRequest, TaskUpdateRequest } from '../types/task'
// Fix #176: typed API responses
import type { ApiResponse, PaginatedData } from '../types/api'

export function getTasks(params?: { page?: number; page_size?: number; type?: string; keyword?: string }): Promise<AxiosResponse<ApiResponse<PaginatedData<TaskItem>>>> {
  return request.get('/api/v1/tasks', { params })
}

export function getTask(id: string): Promise<AxiosResponse<ApiResponse<TaskItem>>> {
  return request.get(`/api/v1/tasks/${id}`)
}

// Fix #176: use typed request body instead of `unknown`
export function createTask(data: TaskCreateRequest): Promise<AxiosResponse<ApiResponse<TaskItem>>> {
  return request.post('/api/v1/tasks', data)
}

export function updateTask(id: string, data: TaskUpdateRequest): Promise<AxiosResponse<ApiResponse<TaskItem>>> {
  return request.put(`/api/v1/tasks/${id}`, data)
}

export function deleteTask(id: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.delete(`/api/v1/tasks/${id}`)
}
