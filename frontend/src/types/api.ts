// Fix #176: Shared API response types so all api/*.ts modules can use proper
// typing instead of untyped payloads.

export interface ApiResponse<T> {
  code: number
  message: string
  data: T
}

export interface PaginatedData<T> {
  items: T[]
  total: number
  page: number
  page_size: number
}
