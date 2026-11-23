# 版本发布

Ku 使用递增的 `v主版本.次版本.修订号` 标签。修复递增修订号，新增兼容功能递增次版本，
需要迁移的破坏性变更递增主版本。Alpha 状态在主页与发布说明中明确标注。

每次发布完成以下步骤。

1. 更新 CMake、Python 项目、uv lock 和 Rust 绑定的版本，保留独立的模块 ABI 与标准库版本。
2. 新建 `docs/releases/v<版本>.md`，记录功能、修复、验证和适用范围。
3. 通过 PR 合入 `main`。CI 构建三平台内核、运行功能门禁，打包 ZIP 并验证解压后的 SDK。
4. 在通过验证的主干提交创建并推送版本标签。

```sh
git tag -a v1.1.0 <已验证提交> -m "Ku v1.1.0"
git push origin v1.1.0
```

标签触发的 CI 会再次构建和验证版本。所有任务通过后，发布任务核对版本号，
上传三个 SDK ZIP 及 SHA-256 校验文件，并用对应的版本说明创建最新 GitHub Release。
已经发布的标签保留原内容，后续修复使用新版本标签。

本地可提前验证安装包。

## 先发布 Windows

需要先提供 Windows 附件时，使用独立的 `Windows Release` 工作流，不先手工推送标签：

```sh
gh workflow run windows-release.yml --ref main -f version=1.2.0
```

先按上述版本准备步骤通过 PR 合入主干。该工作流固定检出触发时的主干提交，核对共享
版本，重新构建 Windows、执行功能 CTest 和 Python 集成测试、打包并验证解压 SDK。
通过后用工作流 token 在该提交创建版本标签和 Release，仅上传 Windows ZIP 与 SHA-256。
由工作流 token 创建标签不触发另一轮标签发布工作流；原三平台 CI 与发布入口仍保留。
发布说明必须明确本次只包含 Windows，其他平台不得以旧包冒充新版本。

```powershell
. .\tools\activate_toolchain.ps1
cmake --build kernel/out/cmake --parallel 2
cpack --config kernel/out/cmake/CPackConfig.cmake -B kernel/out/packages
python tools/verify_installed_sdk.py <生成的ZIP路径>
```
