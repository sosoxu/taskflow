-- TaskFlow 数据库 Schema
-- PostgreSQL 15+

-- 启用 UUID 扩展
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ============================================================
-- 1. 用户表
-- ============================================================
CREATE TABLE users (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username        VARCHAR(32) NOT NULL UNIQUE,
    password_hash   VARCHAR(128) NOT NULL,
    role            VARCHAR(16) NOT NULL DEFAULT 'operator'
                        CHECK (role IN ('admin', 'operator', 'viewer')),
    deleted_at      TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_users_username ON users(username);

-- ============================================================
-- 2. 任务表
-- ============================================================
CREATE TABLE tasks (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name            VARCHAR(128) NOT NULL,
    type            VARCHAR(16) NOT NULL CHECK (type IN ('command', 'script', 'sql')),
    config_json     JSONB NOT NULL DEFAULT '{}',
    description     TEXT DEFAULT '',
    timeout         INTEGER NOT NULL DEFAULT 3600,
    max_retries     INTEGER NOT NULL DEFAULT 0,
    retry_interval  INTEGER NOT NULL DEFAULT 60,
    resource_tags   JSONB NOT NULL DEFAULT '[]',
    parameters_json JSONB NOT NULL DEFAULT '{}',
    creator_id      UUID NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    version         INTEGER NOT NULL DEFAULT 1,
    deleted         BOOLEAN NOT NULL DEFAULT FALSE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX idx_tasks_name_active ON tasks(name) WHERE deleted = FALSE;
CREATE INDEX idx_tasks_creator ON tasks(creator_id);
CREATE INDEX idx_tasks_type ON tasks(type) WHERE deleted = FALSE;

-- ============================================================
-- 3. 工作流表
-- ============================================================
CREATE TABLE workflows (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name                VARCHAR(128) NOT NULL,
    description         TEXT DEFAULT '',
    dag_json            JSONB NOT NULL DEFAULT '{"nodes":[],"edges":[]}',
    schedule_strategy   VARCHAR(16) NOT NULL DEFAULT 'random'
                            CHECK (schedule_strategy IN ('random', 'load_balance', 'specified')),
    target_worker_id    UUID,
    cron_expression     VARCHAR(64),
    cron_enabled        BOOLEAN NOT NULL DEFAULT FALSE,
    creator_id          UUID NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    version             INTEGER NOT NULL DEFAULT 1,
    deleted             BOOLEAN NOT NULL DEFAULT FALSE,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX idx_workflows_name_active ON workflows(name) WHERE deleted = FALSE;
CREATE INDEX idx_workflows_creator ON workflows(creator_id);

-- ============================================================
-- 4. 工作流执行实例表
-- ============================================================
CREATE TABLE workflow_instances (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    workflow_id         UUID NOT NULL REFERENCES workflows(id) ON DELETE CASCADE,
    workflow_version    INTEGER NOT NULL,
    status              VARCHAR(16) NOT NULL DEFAULT 'PENDING'
                            CHECK (status IN ('PENDING', 'RUNNING', 'PAUSED', 'SUCCESS', 'FAILED', 'CANCELLED')),
    trigger_type        VARCHAR(8) NOT NULL DEFAULT 'manual'
                            CHECK (trigger_type IN ('manual', 'cron')),
    started_at          TIMESTAMPTZ,
    finished_at         TIMESTAMPTZ,
    param_overrides   JSONB NOT NULL DEFAULT '{}',
    creator_id          UUID NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_wf_instances_workflow ON workflow_instances(workflow_id);
CREATE INDEX idx_wf_instances_status ON workflow_instances(status);
CREATE INDEX idx_wf_instances_created ON workflow_instances(created_at DESC);

-- ============================================================
-- 5. 执行节点表（必须在 task_instances 之前，因为 FK 引用）
-- ============================================================
CREATE TABLE workers (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name            VARCHAR(64) NOT NULL,
    address         VARCHAR(256) NOT NULL UNIQUE,
    status          VARCHAR(8) NOT NULL DEFAULT 'online'
                        CHECK (status IN ('online', 'offline')),
    cpu_usage       DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    memory_usage    DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    running_tasks   INTEGER NOT NULL DEFAULT 0,
    max_tasks       INTEGER NOT NULL DEFAULT 10,
    resource_tags   JSONB NOT NULL DEFAULT '[]',
    last_heartbeat  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    registered_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_workers_status ON workers(status);
CREATE UNIQUE INDEX idx_workers_name ON workers(name);

-- ============================================================
-- 6. 任务执行实例表
-- ============================================================
CREATE TABLE task_instances (
    id                      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    workflow_instance_id    UUID NOT NULL REFERENCES workflow_instances(id) ON DELETE CASCADE,
    task_id                 UUID NOT NULL REFERENCES tasks(id) ON DELETE RESTRICT,
    task_version            INTEGER NOT NULL,
    task_name               VARCHAR(128) NOT NULL,
    status                  VARCHAR(16) NOT NULL DEFAULT 'PENDING'
                                CHECK (status IN ('PENDING', 'DISPATCHED', 'RUNNING', 'SUCCESS',
                                                  'FAILED', 'UPSTREAM_FAILED', 'TIMEOUT',
                                                  'CANCELLED', 'NODE_OFFLINE')),
    worker_id               UUID REFERENCES workers(id) ON DELETE SET NULL,
    node_id                 VARCHAR(64) NOT NULL DEFAULT '',
    retry_count             INTEGER NOT NULL DEFAULT 0,
    started_at              TIMESTAMPTZ,
    finished_at             TIMESTAMPTZ,
    exit_code               INTEGER,
    error_message           TEXT,
    resolved_config         JSONB,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_task_instances_wf_instance ON task_instances(workflow_instance_id);
CREATE INDEX idx_task_instances_status ON task_instances(status);
CREATE INDEX idx_task_instances_worker ON task_instances(worker_id);

-- ============================================================
-- 7. 定时任务表
-- ============================================================
CREATE TABLE cron_jobs (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    workflow_id         UUID NOT NULL REFERENCES workflows(id) ON DELETE CASCADE,
    cron_expression     VARCHAR(64) NOT NULL,
    enabled             BOOLEAN NOT NULL DEFAULT TRUE,
    next_trigger_time   TIMESTAMPTZ,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_cron_jobs_enabled ON cron_jobs(enabled) WHERE enabled = TRUE;
CREATE INDEX idx_cron_jobs_next_trigger ON cron_jobs(next_trigger_time) WHERE enabled = TRUE;

-- ============================================================
-- 初始管理员用户（密码: admin123，bcrypt hash）
-- ============================================================
INSERT INTO users (username, password_hash, role)
VALUES ('admin', '$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy', 'admin');
