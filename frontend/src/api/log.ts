import request from '../utils/request'

export function getTaskLogs(instanceId: string, taskInstanceId: string) {
  return request.get(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/logs`)
}

export function getTaskLogStreamUrl(instanceId: string, taskInstanceId: string): string {
  // Fix #141: Default to empty base (relative URL) so SSE goes through nginx
  // in production, consistent with utils/request.ts. Previously hardcoded
  // 'http://localhost:8080' which only works in local dev.
  const baseUrl = import.meta.env.VITE_API_BASE_URL || ''
  const token = localStorage.getItem('access_token')
  return `${baseUrl}/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/logs/stream?token=${encodeURIComponent(token || '')}`
}
