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

  return {
    userId,
    username,
    role,
    token,
    isLoggedIn,
    isAdmin,
    isOperator,
    setUser,
    clearUser,
  }
})
