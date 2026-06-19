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

export function logout(accessToken: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.post('/api/v1/auth/logout', { access_token: accessToken })
}
