import { createRouter, createWebHistory } from 'vue-router'

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
  ],
})

export default router
