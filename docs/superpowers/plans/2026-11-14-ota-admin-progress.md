# OTA 后台执行账本 — plan: docs/superpowers/plans/2026-11-14-ota-admin-autocheck.md

- 基线17b6bc0；用户已确认并授权完成代码/测试/编译，明确禁止烧录。
- Task1～5 完成；本地部署，不增加硬件，不发布云端、不烧录。
- 三个独立文件范围并行实现后台、脚本和App；主执行者完成Boot固定目标、总集成、真实浏览器验收与四种构建。最终独立审阅无重要问题遗留。

## 实现与修复证据

- Boot目标：先在空日志选择断言观察RED，再完成HMAC认证、自动严格增版、INSTALLING固定目标、强制KEY1、完整包头匹配与156/192路径边界；既有Boot网络/策略测试仍通过。
- 后台：真实HTTP和临时文件系统验证上传/发布分离、认证/同源、限额/并发/中断/超时、原子对象/指针及磁盘故障、重启恢复与不可变Range；保留原init/pack/serve。
- 脚本：显式私有配置副本、签名键/令牌摘要一致性、硬件选项透传、实际App秘密扫描、原子目录交付。新增不同版本并发RED证实共享编译目录风险，工作区进程锁使回归GREEN，退出后可再次构建。
- App：28个原服务场景及28个新自动场景、复位快照缓存、无更新零写、队列持有完整头、worker重查、忙碌延期、软件回退不循环、AUTO=0及无管理令牌兼容。
- 审阅发现HTTPCGET组合长度问题：新配置测试先2项RED，实际生产格式化器与App/配置校验统一后GREEN；URL224/225及命令255/256均覆盖。实际CMake预设边界接受224、拒绝225。
- 浏览器发现发布后旧“尚未发布”提示；修复后v3上传仍保持v2，确认发布后卡片和上传提示均显示v3已发布。错误令牌、坏包、重启保持、刷新登录、桌面/窄屏也已验证。截图 `build/ota-admin-preview/final-published.jpg`，测试服务已停止。

## 最终验证

- `scripts/test.ps1 -IncludeLegacy`：exit0，日志 `build/ota-admin-final-tests.log`；跟踪C测试、Node50/50与本地补充遗留测试通过。
- 普通 Debug/Release：exit0；日志 `build/ota-admin-final-debug.log`、`build/ota-admin-final-release.log`。
- 新脚本真实OTA Debug v4和Release v5（含Factory）：exit0；日志 `build/ota-admin-final-ota-debug.log`、`build/ota-admin-final-ota-release.log`。
- 最终App Debug328284B、Release272540B，Boot17604B；版本/签名/向量/容量/下载秘密隔离通过，`build/ota-admin-final-artifacts.log`。
- 产物仅验证用途，使用 `build/ota-validation/` 的隔离密钥与示例地址；未创建或覆盖用户实际OTA配置。正式使用按 `docs/OTA_ADMIN_TESTS.md` 配置IP、备份密钥后重编译。
- 独立报告：`build/ota-admin-backend-report.md`、`build/ota-admin-build-report.md`、`build/ota-app-autocheck-report.md`；根集成记录补充脚本锁与最终编译证据。

## 已记录边界

- 软件/物理混合或未知复位原因保守不授权旧失败请求重试；下次明确物理正常开机可重试。
- 配额满时HTTP上传预留可拒绝重复包（507）；已接收包和当前指针不受影响，需配置更大配额。
- 不提供本地旧版回滚；首次新版Boot安装、真实UART/网络、断电安装恢复和长期任务时序仍需实板验收。
- 保留现有 `codex/migrate-weather-clock` 分支，不推送、不合并、不清理用户工作区。
