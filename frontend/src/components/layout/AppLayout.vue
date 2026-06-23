<template>
  <el-container class="app-layout">
    <el-header class="app-header">
      <div class="header-left">
        <span class="logo">TaskFlow</span>
      </div>
      <div class="header-right">
        <span class="username">{{ username }}</span>
        <el-button text @click="handleLogout">登出</el-button>
      </div>
    </el-header>
    <el-container>
      <el-aside width="220px" class="app-sidebar">
        <el-menu :default-active="activeMenu" router>
          <el-menu-item index="/">
            <span>仪表盘</span>
          </el-menu-item>
          <el-menu-item index="/tasks">
            <span>任务管理</span>
          </el-menu-item>
          <el-menu-item index="/workflows">
            <span>工作流管理</span>
          </el-menu-item>
          <!-- Fix #310: Add nav entry for the instance list page so users can
               browse all workflow instances, not just via workflow detail. -->
          <el-menu-item index="/instances">
            <span>执行实例</span>
          </el-menu-item>
          <el-menu-item index="/workers">
            <span>执行节点</span>
          </el-menu-item>
          <el-menu-item v-if="userStore.isAdmin" index="/users">
            <span>用户管理</span>
          </el-menu-item>
        </el-menu>
      </el-aside>
      <el-main class="app-main">
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useUserStore } from '../../stores/userStore'
import { logout as logoutApi } from '../../api/auth'

const route = useRoute()
const router = useRouter()
const userStore = useUserStore()

const activeMenu = computed(() => {
  const path = route.path
  // Fix #230: Use prefix matching so that detail/sub routes (e.g.
  // /tasks/123, /workflows/create) keep their parent menu item highlighted.
  // '/' is matched exactly so it doesn't shadow every other route.
  if (path === '/') return '/'
  const menuIndices = ['/tasks', '/workflows', '/instances', '/workers', '/users']
  for (const idx of menuIndices) {
    if (path === idx || path.startsWith(idx + '/')) {
      return idx
    }
  }
  return path
})

const username = computed(() => {
  return userStore.username || 'unknown'
})

async function handleLogout() {
  try {
    if (userStore.token) {
      await logoutApi(userStore.token)
    }
  } catch (e) {
    // 即使后端登出失败，前端仍需清理本地状态
    console.warn('Logout API failed, clearing local state anyway', e)
  } finally {
    userStore.clearUser()
    router.push('/login')
  }
}
</script>

<style scoped>
.app-layout {
  height: 100vh;
}

.app-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid #e6e6e6;
  background: #fff;
}

.header-left {
  display: flex;
  align-items: center;
}

.logo {
  font-size: 20px;
  font-weight: bold;
  color: #409eff;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.username {
  color: #606266;
}

.app-sidebar {
  border-right: 1px solid #e6e6e6;
  background: #fff;
}

.app-main {
  background: #f5f7fa;
  overflow-y: auto;
}
</style>
