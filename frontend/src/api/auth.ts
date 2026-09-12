import request from '../utils/request'
import type { AxiosResponse } from 'axios'
// Fix #176: typed API responses
import type { ApiResponse } from '../types/api'

export interface LoginResponse {
  access_token: string
  refresh_token: string
  expires_in: number
  user_id: string
  username: string
  role: string
}

export function login(username: string, password: string): Promise<AxiosResponse<ApiResponse<LoginResponse>>> {
  return request.post('/api/v1/auth/login', { username, password })
}

export function register(username: string, password: string): Promise<AxiosResponse<ApiResponse<{ id: string; username: string; role: string }>>> {
  return request.post('/api/v1/auth/register', { username, password })
}

export function refreshToken(refreshToken: string): Promise<AxiosResponse<ApiResponse<{ access_token: string; expires_in: number }>>> {
  return request.post('/api/v1/auth/refresh', { refresh_token: refreshToken })
}

// Fix #330: logout 同时携带 refresh_token，后端将其一并加入黑名单
export function logout(accessToken: string, refreshToken?: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.post('/api/v1/auth/logout', {
    access_token: accessToken,
    refresh_token: refreshToken || undefined,
  })
}

// Fix #335: 服务端身份校验——以签发的 JWT 声明为准刷新本地 role，
// 防止篡改 localStorage 的 role 解锁管理界面
export function getMe(): Promise<AxiosResponse<ApiResponse<{ user_id: string; username: string; role: string }>>> {
  return request.get('/api/v1/auth/me')
}
