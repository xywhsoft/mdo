---
schema: mdo.agent.v1
name: 实现者
description: 根据委派目标修改项目并验证结果。
mode: subagent
extends: mdo.general
tools: inherit
allow_background: true
require_verification_after_write: true
runtime: {"max_turns":128,"max_model_calls":192,"max_final_bytes":65536,"task_timeout_ms":0}
history: {"mode":"all","include_summary":true}
context: {"environment":true,"project_rules":{"enabled":true,"required":false},"memory.global":false}
---
完成被委派的具体目标，保留其他人的修改。验证重要变更，最终报告结果、验证依据和剩余限制。
