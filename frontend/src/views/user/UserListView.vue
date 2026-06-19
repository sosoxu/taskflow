<template>
  <div class="user-list">
    <div class="page-header">
      <h2>用户管理</h2>
      <!-- Fix #172: 仅管理员可创建用户 -->
      <el-button v-if="userStore.isAdmin" type="primary" @click="openCreateDialog">创建用户</el-button>
    </div>

    <el-card class="table-card">
      <el-table v-loading="loading" :data="users" border stripe>
        <el-table-column prop="username" label="用户名" min-width="150" />
        <el-table-column label="角色" width="120">
          <template #default="{ row }">
            <el-tag :type="roleTagType(row.role)" size="small">{{ roleLabel(row.role) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="创建时间" min-width="160">
          <template #default="{ row }">{{ formatTime(row.created_at) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="200" fixed="right">
          <template #default="{ row }">
            <el-button type="primary" link size="small" @click="openRoleDialog(row)">修改角色</el-button>
            <el-button type="danger" link size="small" @click="handleDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="pagination-wrapper">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="total"
          :page-sizes="[10, 20, 50]"
          layout="total, sizes, prev, pager, next"
          @size-change="handleSizeChange"
          @current-change="fetchUsers"
        />
      </div>
    </el-card>

    <!-- Create User Dialog -->
    <el-dialog v-model="createDialogVisible" title="创建用户" width="450px" destroy-on-close>
      <el-form ref="formRef" :model="createForm" :rules="formRules" label-width="80px">
        <el-form-item label="用户名" prop="username">
          <el-input v-model="createForm.username" placeholder="请输入用户名" />
        </el-form-item>
        <el-form-item label="密码" prop="password">
          <el-input v-model="createForm.password" type="password" placeholder="请输入密码" show-password />
        </el-form-item>
        <el-form-item label="角色">
          <el-select v-model="createForm.role" placeholder="请选择角色">
            <el-option label="管理员" value="admin" />
            <el-option label="操作员" value="operator" />
            <el-option label="观察者" value="viewer" />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="createDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="submitting" @click="handleCreate">确定</el-button>
      </template>
    </el-dialog>

    <!-- Change Role Dialog -->
    <el-dialog v-model="roleDialogVisible" title="修改角色" width="400px" destroy-on-close>
      <el-form label-width="80px">
        <el-form-item label="用户名">
          <span>{{ currentUser?.username }}</span>
        </el-form-item>
        <el-form-item label="角色">
          <el-select v-model="selectedRole" placeholder="请选择角色">
            <el-option label="管理员" value="admin" />
            <el-option label="操作员" value="operator" />
            <el-option label="观察者" value="viewer" />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="roleDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="submitting" @click="handleChangeRole">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { getUsers, createUser, updateUserRole, deleteUser } from '../../api/user'
import { formatTime } from '../../utils/format'
import { useUserStore } from '../../stores/userStore'

// Fix #172: 仅管理员可创建用户
const userStore = useUserStore()

interface UserItem {
  id: string
  username: string
  role: string
  created_at: string
}

const loading = ref(false)
const users = ref<UserItem[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(10)

const submitting = ref(false)

// Create dialog
const createDialogVisible = ref(false)
const formRef = ref()
const createForm = reactive({
  username: '',
  password: '',
  role: 'viewer',
})
// Fix #174: 用户表单校验规则
const formRules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 3, max: 32, message: '用户名长度为 3-32 个字符', trigger: 'blur' },
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 8, max: 64, message: '密码长度为 8-64 个字符', trigger: 'blur' },
  ],
}

// Role dialog
const roleDialogVisible = ref(false)
const currentUser = ref<UserItem | null>(null)
const selectedRole = ref('')

function roleTagType(role: string): string {
  const map: Record<string, string> = {
    admin: 'danger',
    operator: 'warning',
    viewer: 'info',
  }
  return map[role] || 'info'
}

function roleLabel(role: string): string {
  const map: Record<string, string> = {
    admin: '管理员',
    operator: '操作员',
    viewer: '观察者',
  }
  return map[role] || role
}

async function fetchUsers() {
  loading.value = true
  try {
    const res = await getUsers({ page: page.value, page_size: pageSize.value })
    users.value = res.data?.data?.items || res.data?.data || []
    total.value = res.data?.data?.total || users.value.length
  } catch {
    ElMessage.error('获取用户列表失败')
  } finally {
    loading.value = false
  }
}

// Fix #175: 分页 size-change 未重置 page=1
function handleSizeChange() {
  page.value = 1
  fetchUsers()
}

function openCreateDialog() {
  createForm.username = ''
  createForm.password = ''
  createForm.role = 'viewer'
  createDialogVisible.value = true
}

async function handleCreate() {
  // Fix #174: 提交前调用表单校验
  try {
    await formRef.value?.validate()
  } catch {
    return
  }
  submitting.value = true
  try {
    await createUser({
      username: createForm.username,
      password: createForm.password,
      role: createForm.role,
    })
    ElMessage.success('创建成功')
    createDialogVisible.value = false
    fetchUsers()
  } catch {
    ElMessage.error('创建用户失败')
  } finally {
    submitting.value = false
  }
}

function openRoleDialog(user: UserItem) {
  currentUser.value = user
  selectedRole.value = user.role
  roleDialogVisible.value = true
}

async function handleChangeRole() {
  if (!currentUser.value) return
  submitting.value = true
  try {
    await updateUserRole(currentUser.value.id, selectedRole.value)
    ElMessage.success('角色已更新')
    roleDialogVisible.value = false
    fetchUsers()
  } catch {
    ElMessage.error('更新角色失败')
  } finally {
    submitting.value = false
  }
}

// Fix #179: 分离 ElMessageBox 取消与 API 错误，避免错误处理混淆
async function handleDelete(user: UserItem) {
  try {
    await ElMessageBox.confirm(
      `确定要删除用户 "${user.username}" 吗？此操作不可撤销。`,
      '警告',
      { type: 'warning' }
    )
  } catch {
    return  // 用户取消
  }
  try {
    await deleteUser(user.id)
    ElMessage.success('删除成功')
    fetchUsers()
  } catch {
    ElMessage.error('删除失败')
  }
}

onMounted(() => {
  fetchUsers()
})
</script>

<style scoped>
.user-list {
  padding: 20px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.page-header h2 {
  margin: 0;
  color: #303133;
}

.table-card {
  margin-bottom: 20px;
}

.pagination-wrapper {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}
</style>
