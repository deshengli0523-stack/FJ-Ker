# Windows Server 2025 无图形界面一键部署程序

本文面向这类服务器：

- Windows Server 2025
- 有公网 IP
- 没有图形界面
- 只能通过 SSH 登录
- 用作 FJ-ker 的公网服务端电脑

一键部署程序位于：

```text
server\deploy_windows_server_2025.ps1
```

它会自动完成：

1. 检查管理员权限。
2. 复制项目到固定安装目录。
3. 检查 Python 3.11；没有就静默下载安装。
4. 创建 Python 虚拟环境。
5. 安装 FJ-ker 服务端依赖。
6. 写入 `server\.env`。
7. 自动生成 `FJKER_API_TOKEN`。
8. 注册开机自启计划任务。
9. 启动服务并做本机健康检查。
10. 可选开放 Windows 防火墙 TCP 端口。
11. 输出固件需要填写的 `SERVER_BASE_URL` 和 `FJKER_API_TOKEN`。

## 部署结论

如果你只有公网 IP、没有域名和 HTTPS，最简单的一键命令是直接开放：

```text
http://你的公网IP:8080
```

但是这仍然是公网明文 HTTP。脚本会强制你加 `-AcceptPublicHttpRisk` 才能打开防火墙，避免误把接口暴露出去。

脚本会启用 Token 校验。部署完成后，固件必须同时配置：

```cpp
#define SERVER_BASE_URL "http://你的公网IP:8080"
#define FJKER_API_TOKEN "部署脚本输出的 Token"
```

否则设备会收到 `401 Unauthorized`。

## 第一步：把项目上传到服务器

在本机 PowerShell 中执行：

```powershell
scp -r E:\陈晨\FJ-ker Administrator@你的公网IP:C:\fjker-src
```

如果你的 SSH 用户不是 `Administrator`，把用户名换成实际用户。

登录服务器：

```powershell
ssh Administrator@你的公网IP
```

进入项目目录：

```powershell
cd C:\fjker-src
```

## 第二步：执行一键部署

在 SSH 里的 PowerShell 执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File C:\fjker-src\server\deploy_windows_server_2025.ps1 `
  -DashscopeApiKey "你的 DashScope API Key" `
  -InstallRoot "C:\FJ-ker" `
  -Port 8080 `
  -BindHost "0.0.0.0" `
  -PublicBaseUrl "http://你的公网IP:8080" `
  -OpenFirewall `
  -AcceptPublicHttpRisk
```

参数说明：

| 参数 | 作用 |
| --- | --- |
| `-DashscopeApiKey` | 写入服务端 `.env` 的 DashScope Key，必填 |
| `-InstallRoot` | 最终安装目录，默认 `C:\FJ-ker` |
| `-Port` | 服务端端口，默认 `8080` |
| `-BindHost` | 监听地址。公网直连时显式传 `0.0.0.0`，默认是更安全的 `127.0.0.1` |
| `-PublicBaseUrl` | 写给固件用的公网地址 |
| `-OpenFirewall` | 创建 Windows 防火墙入站规则 |
| `-AcceptPublicHttpRisk` | 明确确认你接受公网 HTTP 风险 |

如果你只想先在服务器本机部署，不开放公网端口，可以去掉：

```powershell
-OpenFirewall `
-AcceptPublicHttpRisk
```

## 第三步：确认部署成功

脚本成功后会显示：

```text
Deployment complete.
Local health: http://127.0.0.1:8080/health
Public base URL for firmware: http://你的公网IP:8080
Deployment info: C:\FJ-ker\server\DEPLOYMENT_INFO.txt
```

在服务器上检查本机健康状态：

```powershell
curl.exe http://127.0.0.1:8080/health
```

在你自己的电脑上检查公网访问：

```powershell
curl.exe http://你的公网IP:8080/health
```

`/health` 不要求 Token，所以应该直接返回健康状态。

## 第四步：修改固件配置

打开：

```text
firmware\include\secrets.h
```

写入脚本输出的两行：

```cpp
#define SERVER_BASE_URL "http://你的公网IP:8080"
#define FJKER_API_TOKEN "部署脚本输出的 Token"
```

完整示例：

```cpp
#pragma once

#define WIFI_SSID "你的 Wi-Fi 名称"
#define WIFI_PASS "你的 Wi-Fi 密码"
#define SERVER_BASE_URL "http://203.0.113.10:8080"
#define FJKER_API_TOKEN "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

重新编译并烧录固件后，设备请求 `/jobs`、`/jobs/{id}`、`/jobs/{id}/pages/{index}`、`DELETE /jobs/{id}` 时都会自动带上：

```http
X-FJ-KER-TOKEN: 部署脚本输出的 Token
```

## 常用运维命令

### 查看服务任务

```powershell
Get-ScheduledTask -TaskName "FJ-ker Server"
Get-ScheduledTaskInfo -TaskName "FJ-ker Server"
```

### 重启服务

```powershell
Stop-ScheduledTask -TaskName "FJ-ker Server"
Start-ScheduledTask -TaskName "FJ-ker Server"
```

### 查看日志

```powershell
Get-Content C:\FJ-ker\logs\server.log -Tail 100
```

### 查看部署信息

```powershell
Get-Content C:\FJ-ker\server\DEPLOYMENT_INFO.txt
```

注意：这个文件包含设备 Token，不要发给别人。

### 查看防火墙规则

```powershell
Get-NetFirewallRule -DisplayName "FJ-ker API 8080"
```

### 删除公网防火墙规则

```powershell
Remove-NetFirewallRule -DisplayName "FJ-ker API 8080"
```

### 删除计划任务

```powershell
Stop-ScheduledTask -TaskName "FJ-ker Server"
Unregister-ScheduledTask -TaskName "FJ-ker Server" -Confirm:$false
```

## 重新部署

如果你更新了项目代码，重新上传后再运行同一条部署命令即可。

脚本会：

- 覆盖复制项目文件。
- 复用或重建虚拟环境。
- 重写 `.env`。
- 重新注册计划任务。
- 重新启动服务。

如果你想沿用旧 Token，就从 `C:\FJ-ker\server\DEPLOYMENT_INFO.txt` 里复制旧 Token，并在部署时显式传入：

```powershell
-ApiToken "旧 Token"
```

否则脚本会生成新的 Token，你需要重新烧录固件。

## 限制和建议

当前“一键公网 IP”方案是可用的，但不是最理想的生产安全方案：

- 公网 HTTP 是明文传输。
- Token 能防止随便调用接口，但不能加密图片内容。
- 如果 Token 泄露，别人仍然可以调用 `/jobs`。
- Windows 防火墙默认对全网开放该端口，除非你传入 `-AllowedRemoteAddress` 限制来源。

如果你的设备访问来源 IP 固定，建议这样部署：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File C:\fjker-src\server\deploy_windows_server_2025.ps1 `
  -DashscopeApiKey "你的 DashScope API Key" `
  -InstallRoot "C:\FJ-ker" `
  -Port 8080 `
  -BindHost "0.0.0.0" `
  -PublicBaseUrl "http://你的公网IP:8080" `
  -OpenFirewall `
  -AcceptPublicHttpRisk `
  -AllowedRemoteAddress "你的出口公网IP"
```

更长期的方案仍然是：

```text
域名 + HTTPS 反向代理或 Cloudflare Tunnel + Token + 限流
```

## 故障排查

### Python 安装失败

脚本默认从 Python 官网下载：

```text
https://www.python.org/ftp/python/3.11.9/python-3.11.9-amd64.exe
```

如果服务器无法访问该地址，可以手动下载 Python 3.11 安装包并上传到服务器，然后先安装 Python，再运行：

```powershell
-SkipPythonInstall
```

### 健康检查失败

查看日志：

```powershell
Get-Content C:\FJ-ker\logs\server.log -Tail 100
```

常见原因：

1. DashScope Key 写错。
2. 端口被其他程序占用。
3. Python 依赖安装失败。
4. 计划任务没有启动。

### 公网能打开 `/health`，设备提交失败

检查：

1. 固件 `SERVER_BASE_URL` 是否是公网地址。
2. 固件 `FJKER_API_TOKEN` 是否和部署输出一致。
3. 服务器日志中是否有 `401`。
4. 设备 Wi-Fi 是否能访问公网 IP。
5. 云厂商安全组是否放行 TCP `8080`。

### 云服务器安全组

很多云服务器除了 Windows 防火墙，还有云厂商安全组。你需要在云厂商控制台放行：

```text
入站 TCP 8080
来源：设备所在网络，或 0.0.0.0/0
```

如果安全组没放行，即使脚本打开了 Windows 防火墙，公网也访问不到。
