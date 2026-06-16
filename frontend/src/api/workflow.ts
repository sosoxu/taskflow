import request from '../utils/request'

export function getWorkflows(params?: { page?: number; page_size?: number; keyword?: string }) {
  return request.get('/api/v1/workflows', { params })
}

export function getWorkflow(id: string) {
  return request.get(`/api/v1/workflows/${id}`)
}

export function createWorkflow(data: unknown) {
  return request.post('/api/v1/workflows', data)
}

export function updateWorkflow(id: string, data: unknown) {
  return request.put(`/api/v1/workflows/${id}`, data)
}

export function deleteWorkflow(id: string) {
  return request.delete(`/api/v1/workflows/${id}`)
}

export function triggerWorkflow(id: string) {
  return request.post(`/api/v1/workflows/${id}/trigger`)
}
