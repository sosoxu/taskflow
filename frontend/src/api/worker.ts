import request from '../utils/request'

export function getWorkers() {
  return request.get('/api/v1/workers')
}
