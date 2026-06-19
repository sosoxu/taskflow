import request from '../utils/request'
import type { AxiosResponse } from 'axios'
import type { UserItem } from '../types/user'
// Fix #176: typed API responses
import type { ApiResponse, PaginatedData } from '../types/api'

export function getUsers(params?: { page?: number; page_size?: number }): Promise<AxiosResponse<ApiResponse<PaginatedData<UserItem>>>> {
  return request.get('/api/v1/users', { params })
}

export function createUser(data: { username: string; password: string; role: string }): Promise<AxiosResponse<ApiResponse<UserItem>>> {
  return request.post('/api/v1/users', data)
}

export function updateUserRole(userId: string, role: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.put(`/api/v1/users/${userId}/role`, { role })
}

export function deleteUser(userId: string): Promise<AxiosResponse<ApiResponse<null>>> {
  return request.delete(`/api/v1/users/${userId}`)
}
