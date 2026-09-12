import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useUserStore = defineStore('user', () => {
  const userId = ref<string>(localStorage.getItem('user_id') || '')
  const username = ref<string>(localStorage.getItem('username') || '')
  const role = ref<string>(localStorage.getItem('role') || '')
  const token = ref<string>(localStorage.getItem('access_token') || '')
  // Fix #335: 本会话是否已完成服务端身份校验
  const serverChecked = ref(false)

  const isLoggedIn = computed(() => !!token.value)
  const isAdmin = computed(() => role.value === 'admin')
  const isOperator = computed(() => role.value === 'operator' || role.value === 'admin')
  // Fix #355: 移除从未被引用的 isViewer（写操作按钮守卫用的是 isOperator 取反语义）

  // Fix #335: 以服务端签发的 JWT 声明刷新本地 role。
  // role 直接读 localStorage 可被手改解锁管理界面，登录/刷新后必须复核。
  // 校验失败（token 无效）则清除本地状态；动态 import 避免 userStore→api→request 的静态循环依赖。
  async function validateFromServer(): Promise<void> {
    try {
      const { getMe } = await import('../api/auth')
      const res = await getMe()
      const d = res.data?.data
      if (res.data?.code === 0 && d) {
        userId.value = d.user_id
        username.value = d.username
        role.value = d.role
        localStorage.setItem('user_id', d.user_id)
        localStorage.setItem('username', d.username)
        localStorage.setItem('role', d.role)
      } else {
        clearUser()
      }
    } catch {
      clearUser()
    } finally {
      serverChecked.value = true
    }
  }

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

  // Fix #198/#213: 多标签页同步。监听 storage 事件，当其他标签页登出
  // （access_token 被清除）时清除本标签页状态；当其他标签页更新 token 时
  // 同步本标签页 token。storage 事件只在其他标签页触发，不会循环。
  // Fix #213: Also sync user_id/username/role so that when another tab logs in
  // as a different user, this tab's role-based UI (isAdmin/isOperator) updates
  // correctly instead of showing the previous user's permissions.
  if (typeof window !== 'undefined') {
    window.addEventListener('storage', (e) => {
      if (e.key === 'access_token') {
        if (e.newValue === null) {
          clearUser()
        } else {
          token.value = e.newValue
        }
      } else if (e.key === 'user_id') {
        userId.value = e.newValue || ''
      } else if (e.key === 'username') {
        username.value = e.newValue || ''
      } else if (e.key === 'role') {
        role.value = e.newValue || ''
      }
    })
  }

  return {
    userId,
    username,
    role,
    token,
    serverChecked,
    isLoggedIn,
    isAdmin,
    isOperator,
    setUser,
    clearUser,
    validateFromServer,
  }
})
