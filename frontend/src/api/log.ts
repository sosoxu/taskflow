import request from '../utils/request'
import type { AxiosResponse } from 'axios'
// Fix #176: typed API responses
import type { ApiResponse } from '../types/api'

export function getTaskLogs(instanceId: string, taskInstanceId: string): Promise<AxiosResponse<ApiResponse<{ log: string }>>> {
  return request.get(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/logs`)
}

// Fix #343: SSE 不再把 access_token 放进 URL query（会落进 nginx/代理访问日志
// 与浏览器历史）。改为先用带 Authorization 头的请求换发一次性 ticket（30s、
// 单次有效、绑定实例与任务），再用 ticket 建立 EventSource。
export function getTaskLogStreamTicket(
  instanceId: string,
  taskInstanceId: string,
): Promise<AxiosResponse<ApiResponse<{ ticket: string; expires_in: number }>>> {
  return request.get(`/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/logs/ticket`)
}

export async function getTaskLogStreamUrl(instanceId: string, taskInstanceId: string): Promise<string> {
  const { data: resp } = await getTaskLogStreamTicket(instanceId, taskInstanceId)
  const ticket = resp.data?.ticket || ''
  // Fix #141: Default to empty base (relative URL) so SSE goes through nginx
  // in production, consistent with utils/request.ts. Previously hardcoded
  // 'http://localhost:8080' which only works in local dev.
  const baseUrl = import.meta.env.VITE_API_BASE_URL || ''
  return `${baseUrl}/api/v1/instances/${instanceId}/tasks/${taskInstanceId}/logs/stream?ticket=${encodeURIComponent(ticket)}`
}
