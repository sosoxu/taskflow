# 前端构建 - Nginx 托管
# 阶段1: 构建
FROM node:20-alpine AS builder

WORKDIR /app

COPY package.json package-lock.json* ./
# Fix #331 配套: 有 package-lock.json 时用 npm ci 保证依赖可复现
RUN npm ci || npm install

COPY . .
RUN npm run build

# 阶段2: 运行
FROM nginx:alpine

COPY --from=builder /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf

# Fix #331: 以非 root 用户运行 nginx。
# - /run chown 给 nginx（默认 pid 路径 /var/run/nginx.pid 可写，无需覆盖 pid 指令）
# - 缓存目录 chown 给 nginx 用户
# - 监听 8080（非特权端口，见 nginx.conf），compose 侧映射 80:8080
# Fix #344: conf.d 也 chown 给 nginx——官方镜像的
# /docker-entrypoint.d/20-envsubst-on-templates.sh 负责把 /etc/nginx/templates/*.template
# 渲染进 conf.d（TLS 形态就是靠它生成配置）。非 root 运行时它原本无权写入，
# 模板会被静默跳过，只剩镜像里烘焙的非 TLS 配置生效。
RUN chown -R nginx:nginx /var/cache/nginx /etc/nginx/conf.d && chown nginx:nginx /run
USER nginx

EXPOSE 8080

CMD ["nginx", "-g", "daemon off;"]
