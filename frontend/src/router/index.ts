import { createRouter, createWebHistory } from 'vue-router'
import { useUserStore } from '../stores/userStore'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/login',
      name: 'login',
      component: () => import('../views/LoginView.vue'),
    },
    {
      path: '/',
      component: () => import('../components/layout/AppLayout.vue'),
      children: [
        {
          path: '',
          name: 'dashboard',
          component: () => import('../views/DashboardView.vue'),
        },
        {
          path: 'tasks',
          name: 'task-list',
          component: () => import('../views/task/TaskListView.vue'),
        },
        {
          path: 'tasks/:id',
          name: 'task-detail',
          component: () => import('../views/task/TaskDetailView.vue'),
        },
        {
          path: 'workflows',
          name: 'workflow-list',
          component: () => import('../views/workflow/WorkflowListView.vue'),
        },
        {
          path: 'workflows/create',
          name: 'workflow-create',
          component: () => import('../views/workflow/WorkflowEditorView.vue'),
          // Fix #211: Write-operation routes require operator+ role. Without
          // this, a viewer could navigate directly to the editor URL even
          // though the list-view buttons are hidden.
          meta: { requireOperator: true },
        },
        {
          path: 'workflows/:id',
          name: 'workflow-detail',
          component: () => import('../views/workflow/WorkflowDetailView.vue'),
        },
        {
          path: 'workflows/:id/edit',
          name: 'workflow-edit',
          component: () => import('../views/workflow/WorkflowEditorView.vue'),
          // Fix #211: Same as workflow-create — editing requires operator+.
          meta: { requireOperator: true },
        },
        {
          path: 'instances/:id',
          name: 'instance-detail',
          component: () => import('../views/instance/InstanceDetailView.vue'),
        },
        {
          path: 'workers',
          name: 'worker-list',
          component: () => import('../views/worker/WorkerListView.vue'),
        },
        {
          path: 'users',
          name: 'user-list',
          component: () => import('../views/user/UserListView.vue'),
        },
      ],
    },
    // Fix #173: 缺少 404 页面和 catch-all 路由
    {
      path: '/:pathMatch(.*)*',
      name: 'not-found',
      component: () => import('../views/NotFoundView.vue'),
      meta: { title: '页面不存在' },
    },
  ],
})

router.beforeEach((to, from, next) => {
  const userStore = useUserStore()
  if (to.path === '/login') {
    if (userStore.isLoggedIn) {
      next('/')
    } else {
      next()
    }
  } else {
    if (!userStore.isLoggedIn) {
      next({ path: '/login', query: { redirect: to.fullPath } })
    } else if (to.path === '/users' && !userStore.isAdmin) {
      // Only admin can access user management page
      next('/')
    } else if ((to.meta as { requireOperator?: boolean })?.requireOperator && !userStore.isOperator) {
      // Fix #211: Block viewers from write-operation routes (create/edit).
      // The list-view buttons are already hidden, but without this guard a
      // viewer could type the URL directly.
      next('/')
    } else {
      next()
    }
  }
})

export default router
