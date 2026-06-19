import axios from 'axios'
import type { AxiosInstance, InternalAxiosRequestConfig, AxiosResponse } from 'axios'
import router from '../router'
import { useUserStore } from '../stores/userStore'

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || ''

const request: AxiosInstance = axios.create({
  baseURL: API_BASE_URL,
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
})

// 请求拦截器
request.interceptors.request.use(
  (config: InternalAxiosRequestConfig) => {
    const token = localStorage.getItem('access_token')
    if (token && config.headers) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  (error) => {
    return Promise.reject(error)
  }
)

// 是否正在刷新token
let isRefreshing = false
// 等待token刷新的请求队列
// Fix #190: 回调签名改为 (token: string | null) => void，null 表示刷新失败
let refreshSubscribers: Array<(token: string | null) => void> = []

function onTokenRefreshed(token: string) {
  refreshSubscribers.forEach((cb) => cb(token))
  refreshSubscribers = []
}

// Fix #190: 刷新失败时通知所有排队请求（传 null 让它们 reject），
// 否则队列中的 Promise 永远 pending，导致 UI 卡死。
function onTokenRefreshFailed() {
  refreshSubscribers.forEach((cb) => cb(null))
  refreshSubscribers = []
}

// Fix #144: Redirect to login via Vue Router (SPA navigation, preserves
// redirect query) instead of window.location.href (full page reload).
// Also clear the Pinia user store so stale role state doesn't linger.
function redirectToLogin() {
  const userStore = useUserStore()
  userStore.clearUser()
  const currentPath = router.currentRoute.value.fullPath
  if (currentPath !== '/login') {
    router.push({ path: '/login', query: { redirect: currentPath } })
  }
}

// 响应拦截器
request.interceptors.response.use(
  (response: AxiosResponse) => {
    return response
  },
  async (error) => {
    const originalRequest = error.config

    if (error.response?.status === 401 && !originalRequest._retry) {
      const refreshToken = localStorage.getItem('refresh_token')

      if (!refreshToken) {
        // 没有refresh token，直接跳转登录
        redirectToLogin()
        return Promise.reject(error)
      }

      if (isRefreshing) {
        // 正在刷新token，将请求加入队列等待
        return new Promise((resolve, reject) => {
          // Fix #190: subscriber 接收 token | null，null 表示刷新失败需 reject
          refreshSubscribers.push((token: string | null) => {
            if (!token) {
              reject(new Error('token refresh failed'))
              return
            }
            originalRequest.headers.Authorization = `Bearer ${token}`
            resolve(request(originalRequest))
          })
        })
      }

      originalRequest._retry = true
      isRefreshing = true

      try {
        // 尝试刷新token
        const response = await axios.post(`${API_BASE_URL}/api/v1/auth/refresh`, {
          refresh_token: refreshToken,
        })

        if (response.data?.code === 0 && response.data?.data?.access_token) {
          const newAccessToken = response.data.data.access_token
          localStorage.setItem('access_token', newAccessToken)

          if (response.data.data.refresh_token) {
            localStorage.setItem('refresh_token', response.data.data.refresh_token)
          }

          onTokenRefreshed(newAccessToken)
          originalRequest.headers.Authorization = `Bearer ${newAccessToken}`
          return request(originalRequest)
        } else {
          // 刷新失败，跳转登录
          // Fix #190: 通知排队请求 reject，避免永远 pending
          onTokenRefreshFailed()
          redirectToLogin()
          return Promise.reject(error)
        }
      } catch (refreshError) {
        // 刷新失败，跳转登录
        // Fix #190: 通知排队请求 reject，避免永远 pending
        onTokenRefreshFailed()
        redirectToLogin()
        return Promise.reject(refreshError)
      } finally {
        isRefreshing = false
      }
    }

    return Promise.reject(error)
  }
)

export default request
