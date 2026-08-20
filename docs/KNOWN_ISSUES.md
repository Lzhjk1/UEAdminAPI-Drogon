# 已知问题

> 本文件记录项目当前已知、但不影响主流程修复或需要外部环境配合的问题。

## GitLab 创建 Impersonation Token 偶发失败

- **现象**：运行 pytest 时，创建测试用户接口偶发返回错误码 `-604`（`ApiError_GitLabAccountCreationFailure` / 创建 GitLab Impersonation Token 失败）。复现用例示例：`tests/test_user_manage.py::test_update_email_already_bound`。
- **原因**：用户注册流程会联动调用 GitLab API 创建账号及 Impersonation Token。该操作依赖外部 GitLab 服务（`config.yaml` 中 `GitLab.ApiHost`，当前为 `pidgin.uesoft.com`）的可用性、凭据有效性及限流状态。偶发失败通常由外部 GitLab 瞬时不可用、超时或接口限流导致，与后端业务代码无直接关系。
- **影响**：受影响测试可能偶发失败；重跑同一用例通常可恢复。
- **处理建议**：
  - 重跑失败的测试用例，确认是否为瞬时环境问题。
  - 检查 GitLab 服务状态、`AdminToken` 是否有效、是否触发限流。
  - 若频繁出现，可考虑将测试账号创建与 GitLab 联动解耦，或在测试环境关闭 GitLab 联动。
- **状态**：观察中。
