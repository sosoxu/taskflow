import request from '../utils/request'

export function getTasks(params?: { page?: number; page_size?: number; type?: string; keyword?: string }) {
  return request.get('/api/v1/tasks', { params })
}

export function getTask(id: string) {
  return request.get(`/api/v1/tasks/${id}`)
}

export function createTask(data: unknown) {
  return request.post('/api/v1/tasks', data)
}

export function updateTask(id: string, data: unknown) {
  return request.put(`/api/v1/tasks/${id}`, data)
}

export function deleteTask(id: string) {
  return request.delete(`/api/v1/tasks/${id}`)
}
