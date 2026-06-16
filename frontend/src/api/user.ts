import request from '../utils/request'

export function getUsers(params?: { page?: number; page_size?: number }) {
  return request.get('/api/v1/users', { params })
}

export function createUser(data: { username: string; password: string; role: string }) {
  return request.post('/api/v1/users', data)
}

export function updateUserRole(userId: string, role: string) {
  return request.put(`/api/v1/users/${userId}/role`, { role })
}

export function deleteUser(userId: string) {
  return request.delete(`/api/v1/users/${userId}`)
}
