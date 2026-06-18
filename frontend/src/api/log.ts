import request from '../utils/request'

export function getTaskLogs(instanceId: string, taskInstanceId: string) {
  return request.get(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/logs`)
}

export function getTaskLogStreamUrl(instanceId: string, taskInstanceId: string): string {
  const baseUrl = import.meta.env.VITE_API_BASE_URL || 'http://localhost:8080'
  const token = localStorage.getItem('access_token')
  return `${baseUrl}/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/logs/stream?token=${encodeURIComponent(token || '')}`
}
