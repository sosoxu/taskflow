import { getTask } from '../api/task'
import type { DagNode } from '../types/workflow'

export interface TriggerParamEntry {
  key: string
  value: string
  defaultValue: string
}

// Fix #341: WorkflowListView 与 WorkflowDetailView 各实现了一遍「遍历 DAG 节点、
// 逐个 await getTask()」来收集触发参数——节点越多等待越久（N+1 串行请求）。
// 这里收敛为唯一实现，并对去重后的 task_id 并发拉取，结果与原先一致：
// 同一个 task_id 只查一次，参数按首次出现的顺序保留。
export async function loadTriggerParamEntries(nodes?: DagNode[]): Promise<TriggerParamEntry[]> {
  if (!nodes?.length) return []

  const taskIds = [...new Set(nodes.map((node) => node.task_id).filter((id) => !!id))]
  const tasks = await Promise.all(
    taskIds.map(async (id) => {
      try {
        const { data: resp } = await getTask(id)
        return resp.data
      } catch {
        // Skip tasks that can't be loaded
        return null
      }
    }),
  )

  // key -> defaultValue
  const paramMap = new Map<string, string>()
  for (const task of tasks) {
    if (!task?.parameters_json || typeof task.parameters_json !== 'object') continue
    for (const [key, val] of Object.entries(task.parameters_json)) {
      if (!paramMap.has(key)) {
        paramMap.set(key, typeof val === 'string' ? val : JSON.stringify(val))
      }
    }
  }

  return [...paramMap.entries()].map(([key, defaultValue]) => ({ key, value: '', defaultValue }))
}
