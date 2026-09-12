// Fix #355: 仅保留实际被使用的 setToken（其余 token 存取统一走
// userStore/localStorage，避免两套 key 常量体系并存）。
const TOKEN_KEY = 'access_token'

export function setToken(token: string): void {
  localStorage.setItem(TOKEN_KEY, token)
}
