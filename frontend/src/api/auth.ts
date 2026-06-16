import request from '../utils/request'

export function login(username: string, password: string) {
  return request.post('/api/v1/auth/login', { username, password })
}

export function register(username: string, password: string) {
  return request.post('/api/v1/auth/register', { username, password })
}

export function refreshToken(refreshToken: string) {
  return request.post('/api/v1/auth/refresh', { refresh_token: refreshToken })
}
