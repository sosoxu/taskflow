import { ref } from 'vue'

export interface UsePaginationOptions {
  pageSize?: number
}

// Fix #341: 6 个列表视图各写了一份 page/pageSize/total + handleSizeChange
// 样板（含 Fix #175 的「切换每页条数必须回到第一页」），这里收敛为唯一实现。
export function usePagination(fetcher: () => void | Promise<void>, options: UsePaginationOptions = {}) {
  const page = ref(1)
  const pageSize = ref(options.pageSize ?? 10)
  const total = ref(0)

  // Fix #175: 分页 size-change 未重置 page=1
  function handleSizeChange() {
    page.value = 1
    return fetcher()
  }

  /** 筛选条件变化时回到第一页（调用方随后自行重新拉取） */
  function resetToFirstPage() {
    page.value = 1
  }

  return { page, pageSize, total, handleSizeChange, resetToFirstPage }
}
