import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useUserStore = defineStore('user', () => {
  const userId = ref<string>(localStorage.getItem('user_id') || '')
  const username = ref<string>(localStorage.getItem('username') || '')
  const role = ref<string>(localStorage.getItem('role') || '')
  const token = ref<string>(localStorage.getItem('access_token') || '')

  const isLoggedIn = computed(() => !!token.value)
  const isAdmin = computed(() => role.value === 'admin')
  const isOperator = computed(() => role.value === 'operator' || role.value === 'admin')
  // Fix #172: isViewer for hiding write-operation buttons
  const isViewer = computed(() => role.value === 'viewer')

  function setUser(data: { userId: string; username: string; role: string; token: string; refreshToken: string }) {
    userId.value = data.userId
    username.value = data.username
    role.value = data.role
    token.value = data.token

    localStorage.setItem('user_id', data.userId)
    localStorage.setItem('username', data.username)
    localStorage.setItem('role', data.role)
    localStorage.setItem('access_token', data.token)
    localStorage.setItem('refresh_token', data.refreshToken)
  }

  function clearUser() {
    userId.value = ''
    username.value = ''
    role.value = ''
    token.value = ''

    localStorage.removeItem('user_id')
    localStorage.removeItem('username')
    localStorage.removeItem('role')
    localStorage.removeItem('access_token')
    localStorage.removeItem('refresh_token')
  }

  // Fix #198: 多标签页同步。监听 storage 事件，当其他标签页登出
  // （access_token 被清除）时清除本标签页状态；当其他标签页更新 token 时
  // 同步本标签页 token。storage 事件只在其他标签页触发，不会循环。
  if (typeof window !== 'undefined') {
    window.addEventListener('storage', (e) => {
      if (e.key === 'access_token') {
        if (e.newValue === null) {
          clearUser()
        } else {
          token.value = e.newValue
        }
      }
    })
  }

  return {
    userId,
    username,
    role,
    token,
    isLoggedIn,
    isAdmin,
    isOperator,
    isViewer,
    setUser,
    clearUser,
  }
})
