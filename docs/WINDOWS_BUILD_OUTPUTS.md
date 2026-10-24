# Windows 构建目录

首次使用原生 Visual Studio 工程时，将根目录的 `FlorrBt.Dependencies.example.props` 复制为 `FlorrBt.Dependencies.local.props`，填写 `SfmlRoot` 和 `MySqlRoot`。本地文件不进入 Git；也可通过环境变量或 MSBuild 参数 `FLORRBT_SFML_ROOT`、`FLORRBT_MYSQL_ROOT` 指定 SDK 路径。CMake 构建继续使用自身的依赖配置。

正常 `Release | x64` 的服务端仍输出到：

```text
x64\Release\FlorrBt.Server.exe
```

Server 和 EngineShared 共同导入 `tools/msbuild/isolated_outputs.props`。中间产物现在按输出目录、项目、平台和配置隔离：

```text
$(OutDir)\obj\$(MSBuildProjectName)\$(Platform)\$(Configuration)\
```

例如，正常 Release 与 `/p:OutDir=...\artifacts\compile-check\trial\` 的验证构建不会再共用 `.obj`、PDB 和 `.tlog`，Server 与引用的 EngineShared 也不会互相覆盖。不要再用一个全局 `/p:IntDir=...` 让所有引用项目共用中间目录。

此前只覆盖 OutDir 的验证命令曾共用正常 Release 的中间目录；源码没有丢失，但链接记录包含多个输出路径。旧中间目录本次不手动删除，新构建不会再使用它们。

在 Visual Studio 中选择 **Release / x64**，对 **FlorrBt.Server 重新生成**；或者在 Developer PowerShell 中：

```powershell
msbuild .\FlorrBt.Server.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m:2
```

生成前正常退出占用目标 EXE 的服务器。完成后核对 `MSBUILD_EXIT` / Visual Studio 构建结果、输出中的目标路径以及该 EXE 的更新时间，不能只根据 F5 成功启动判断是否用了新版本。构建不自动启动服务器或操作数据库。
