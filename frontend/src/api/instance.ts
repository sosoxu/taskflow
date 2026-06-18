import request from '../utils/request'

export function getWorkflowInstances(workflowId: string, params?: { page?: number; page_size?: number }) {
  return request.get(`/api/v1/workflows/${workflowId}/instances`, { params })
}

export function getInstance(id: string) {
  return request.get(`/api/v1/instances/${id}`)
}

export function getAllInstances(params?: { page?: number; page_size?: number }) {
  return request.get('/api/v1/instances', { params })
}

export function pauseInstance(id: string) {
  return request.post(`/api/v1/instances/${id}/pause`)
}

export function resumeInstance(id: string) {
  return request.post(`/api/v1/instances/${id}/resume`)
}

export function cancelInstance(id: string) {
  return request.post(`/api/v1/instances/${id}/cancel`)
}

export function retryTask(instanceId: string, taskInstanceId: string) {
  return request.post(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/retry`)
}

export function killTask(instanceId: string, taskInstanceId: string) {
  return request.post(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/kill`)
}
