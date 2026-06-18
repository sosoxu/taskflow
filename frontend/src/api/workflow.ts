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

export function triggerWorkflow(id: string, paramOverrides?: Record<string, unknown>) {
  const data: Record<string, unknown> = {}
  if (paramOverrides && Object.keys(paramOverrides).length > 0) {
    data.param_overrides = paramOverrides
  }
  return request.post(`/api/v1/workflows/${id}/trigger`, data)
}
