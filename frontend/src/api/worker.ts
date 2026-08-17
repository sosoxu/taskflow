import request from '../utils/request'
import type { AxiosResponse } from 'axios'
import type { WorkerInfo, DeployWorkerRequest, DeployWorkerResult } from '../types/worker'
// Fix #176: typed API responses
import type { ApiResponse } from '../types/api'

// Backend returns { items: [...], total: N } (no page/page_size for workers)
export interface WorkerListResponse {
  items: WorkerInfo[]
  total: number
}

export function getWorkers(): Promise<AxiosResponse<ApiResponse<WorkerListResponse>>> {
  return request.get('/api/v1/workers')
}

// 部署（新建 + 启动）worker：通过 SSH 登录远程节点，写入配置并以参数方式启动 worker
// 部署涉及多次 SSH 往返（连接测试/建目录/写配置/检查二进制/启动/存活检查），
// 远程节点上每步 SSH 建连可能耗时数秒，全局 30s 超时不够，这里单独放宽到 120s。
export function deployWorker(data: DeployWorkerRequest): Promise<AxiosResponse<ApiResponse<DeployWorkerResult>>> {
  return request.post('/api/v1/workers/deploy', data, { timeout: 120000 })
}
