# Windows Server 公网部署配置指南（FJ-ker）

本文说明如何把一台 Windows Server 作为 FJ-ker 的服务端电脑，并让设备可以从公网访问它。

如果你的服务器是 **Windows Server 2025、无图形界面、只通过 SSH 访问**，优先使用一键脚本说明：[Windows_Server_2025_一键部署.md](Windows_Server_2025_一键部署.md)。

结论先写清楚：**不要把当前项目的 `8080` 端口直接裸露到公网。** 当前项目已经支持 `FJKER_API_TOKEN` 共享 Token，但必须在服务端 `.env` 和固件 `secrets.h` 中同时配置才会生效；如果 Token 为空，接口会按局域网开发模式放行。一旦公网开放且没有 Token、限流或 HTTPS，任何人都可以调用 `/jobs` 上传图片并消耗服务器资源和 DashScope 额度。

推荐方案是：

1. FJ-ker 服务端只监听 `127.0.0.1:8080`。
2. 使用 Cloudflare Tunnel 或反向代理把公网流量转发到本机服务。
3. 真正长期使用前，确认服务端和固件已经配置共享 Token，并继续补充限流、HTTPS/TLS 支持。

## 适用范围

本文适用于当前仓库结构：

- 服务端目录：`E:\陈晨\FJ-ker\server`
- 固件目录：`E:\陈晨\FJ-ker\firmware`
- 服务端启动脚本：`server\run.ps1`
- 固件服务端地址配置：`firmware\include\secrets.h`

当前项目默认端口是 `8080`，默认服务端地址示例是：

```cpp
#define SERVER_BASE_URL "http://192.168.1.42:8080"
```

如果改成公网访问，固件中的 `SERVER_BASE_URL` 要改成公网域名或公网 IP。

## 部署方案选择

| 方案 | 推荐程度 | 适合场景 | 风险 |
| --- | --- | --- | --- |
| Cloudflare Tunnel | 推荐 | 有域名，想避免公网端口转发 | 仍需配置 `FJKER_API_TOKEN`，并建议加限流 |
| Caddy/Nginx/IIS 反向代理 + HTTPS | 可用 | 有公网 IP、能管理证书和防火墙 | 需要维护端口、证书、限流和安全策略 |
| 路由器直接转发 `8080` | 不推荐 | 临时测试 | 即使有 Token，HTTP 仍是明文，且缺少边缘限流 |

如果只是自己在同一个局域网使用，不需要公网部署，继续使用 `docs/使用说明.md` 的局域网方案即可。

## 当前代码的公网风险

在开始配置前，先确认这些限制：

- 服务端当前是 Uvicorn/FastAPI 直跑，没有内建 HTTPS。
- `POST /jobs` 是上传入口，会触发题目识别和内容生成。
- `GET /jobs/{id}`、`GET /jobs/{id}/pages/{index}`、`DELETE /jobs/{id}` 已支持 `X-FJ-KER-TOKEN` 校验，但只有 `FJKER_API_TOKEN` 非空时才启用。
- 上传大小虽然有限制，但项目内仍没有公网限流、队列长度上限或配额控制。
- DashScope API Key 只应该放在 `server\.env`，不要写入固件、截图或公开仓库。

所以公网部署时，最低要求是：不要让外网直接访问未配置 Token 的 `uvicorn --host 0.0.0.0 --port 8080`。更稳妥的做法是让服务端监听 `127.0.0.1`，由 Tunnel 或反向代理提供公网入口。

## 方案 A：Cloudflare Tunnel（推荐）

这个方案的优点是 Windows Server 不需要开放入站端口，路由器也不需要做端口转发。`cloudflared` 会从服务器主动连到 Cloudflare，再由 Cloudflare 把公网域名的请求转发回本机。

### 1. 准备条件

你需要：

- 一个 Cloudflare 账号。
- 一个已经接入 Cloudflare 的域名，例如 `example.com`。
- Windows Server 能访问互联网。
- 本机已经能运行 FJ-ker 服务端。

示例中使用：

- 公网域名：`fjker.example.com`
- 本机服务：`http://127.0.0.1:8080`
- Tunnel 名称：`fjker-server`

实际使用时把 `fjker.example.com` 换成你的域名。

### 2. 让 FJ-ker 服务端只监听本机

进入服务端目录：

```powershell
cd E:\陈晨\FJ-ker\server
```

首次部署先安装依赖：

```powershell
.\install.ps1
```

复制环境变量文件并填写 DashScope Key：

```powershell
Copy-Item .env.example .env
notepad .env
```

`.env` 中至少确认：

```env
DASHSCOPE_API_KEY=你的真实 DashScope API Key
QWEN_MODEL=qwen3.7-plus
SERVER_PORT=8080
MAX_PAGES=20
```

然后用本机监听方式启动：

```powershell
$env:SERVER_HOST = "127.0.0.1"
$env:SERVER_PORT = "8080"
.\run.ps1
```

验证本机服务：

```powershell
curl.exe http://127.0.0.1:8080/health
```

如果返回健康状态，说明本机服务正常。

### 3. 创建一个本机启动脚本

为了后面用计划任务或服务长期运行，建议新建：

```text
E:\陈晨\FJ-ker\server\run_public_local.ps1
```

内容如下：

```powershell
$ErrorActionPreference = "Stop"
$env:SERVER_HOST = "127.0.0.1"
$env:SERVER_PORT = "8080"
& "E:\陈晨\FJ-ker\server\run.ps1"
```

这个脚本的作用是固定让 FJ-ker 只监听本机地址。这样即使 Windows 防火墙或路由器配置错误，`8080` 也不会直接变成公网服务。

### 4. 用计划任务开机启动 FJ-ker 服务端

用管理员 PowerShell 执行：

```powershell
$Action = New-ScheduledTaskAction `
  -Execute "powershell.exe" `
  -Argument '-NoProfile -ExecutionPolicy Bypass -File "E:\陈晨\FJ-ker\server\run_public_local.ps1"' `
  -WorkingDirectory "E:\陈晨\FJ-ker\server"

$Trigger = New-ScheduledTaskTrigger -AtStartup
$Principal = New-ScheduledTaskPrincipal -UserId "NT AUTHORITY\SYSTEM" -RunLevel Highest

Register-ScheduledTask `
  -TaskName "FJ-ker Server" `
  -Action $Action `
  -Trigger $Trigger `
  -Principal $Principal
```

手动启动任务：

```powershell
Start-ScheduledTask -TaskName "FJ-ker Server"
```

查看任务状态：

```powershell
Get-ScheduledTask -TaskName "FJ-ker Server"
```

查看 `8080` 是否在监听：

```powershell
netstat -ano | findstr :8080
```

如果你已经用 PowerShell 窗口手动运行了 `run.ps1`，先关闭那个窗口，避免端口占用。

### 5. 安装 cloudflared

下载 Windows 版 `cloudflared`，重命名为：

```text
cloudflared.exe
```

建议放到：

```text
C:\Cloudflared\bin\cloudflared.exe
```

验证版本：

```powershell
cd C:\Cloudflared\bin
.\cloudflared.exe --version
```

### 6. 登录 Cloudflare 并创建 Tunnel

在 `C:\Cloudflared\bin` 下执行：

```powershell
.\cloudflared.exe tunnel login
```

浏览器会打开 Cloudflare 登录页面。登录后选择你的域名。

创建 Tunnel：

```powershell
.\cloudflared.exe tunnel create fjker-server
```

查看 Tunnel：

```powershell
.\cloudflared.exe tunnel list
```

记录输出里的 Tunnel UUID，例如：

```text
aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
```

后文用 `<Tunnel-UUID>` 表示这个值。

### 7. 创建 cloudflared 配置文件

如果要把 cloudflared 作为 Windows 服务运行，建议把配置放到 LocalSystem 用户目录：

```text
C:\Windows\System32\config\systemprofile\.cloudflared\config.yml
```

先创建目录：

```powershell
New-Item -ItemType Directory -Force "C:\Windows\System32\config\systemprofile\.cloudflared"
```

把当前用户目录下生成的 Tunnel 凭据复制过去：

```powershell
Copy-Item "$env:USERPROFILE\.cloudflared\<Tunnel-UUID>.json" `
  "C:\Windows\System32\config\systemprofile\.cloudflared\<Tunnel-UUID>.json"
```

创建配置文件：

```yaml
tunnel: <Tunnel-UUID>
credentials-file: C:\Windows\System32\config\systemprofile\.cloudflared\<Tunnel-UUID>.json

ingress:
  - hostname: fjker.example.com
    service: http://127.0.0.1:8080
  - service: http_status:404

logfile: C:\Cloudflared\cloudflared.log
```

注意：

- `hostname` 改成你的真实域名。
- `service` 必须指向 `http://127.0.0.1:8080`，不要写 `0.0.0.0`。
- 最后一条 `http_status:404` 是兜底规则，避免其他未知域名也被转发到服务端。

校验配置：

```powershell
cd C:\Cloudflared\bin
.\cloudflared.exe tunnel --config C:\Windows\System32\config\systemprofile\.cloudflared\config.yml ingress validate
```

### 8. 配置 DNS 路由

把 `fjker.example.com` 指向这个 Tunnel：

```powershell
cd C:\Cloudflared\bin
.\cloudflared.exe tunnel route dns fjker-server fjker.example.com
```

如果你使用 UUID，也可以：

```powershell
.\cloudflared.exe tunnel route dns <Tunnel-UUID> fjker.example.com
```

### 9. 先手动运行 Tunnel 测试

```powershell
cd C:\Cloudflared\bin
.\cloudflared.exe tunnel --config C:\Windows\System32\config\systemprofile\.cloudflared\config.yml run fjker-server
```

另开一个 PowerShell 测试公网域名：

```powershell
curl.exe http://fjker.example.com/health
```

如果你准备让固件暂时继续使用 HTTP，要确认 Cloudflare 没有强制把 HTTP 跳转到 HTTPS。否则固件访问 `http://fjker.example.com` 时可能只拿到重定向，而不是服务端响应。

长期安全方案应该改造固件支持 HTTPS/TLS 和认证头，而不是长期依赖公网明文 HTTP。

### 10. 安装 cloudflared 为 Windows 服务

管理员 CMD 或管理员 PowerShell：

```powershell
cd C:\Cloudflared\bin
.\cloudflared.exe service install
```

Cloudflare 官方 Windows 服务文档要求服务能找到配置文件。若服务没有读取到配置，需要检查注册表中的服务启动参数：

```text
Computer\HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\Cloudflared
```

`ImagePath` 建议包含：

```text
C:\Cloudflared\bin\cloudflared.exe --config=C:\Windows\System32\config\systemprofile\.cloudflared\config.yml tunnel run
```

启动服务：

```powershell
sc.exe start cloudflared
```

重启服务：

```powershell
sc.exe stop cloudflared
sc.exe start cloudflared
```

查看日志：

```powershell
Get-Content C:\Cloudflared\cloudflared.log -Tail 100
```

### 11. 修改固件服务端地址

编辑：

```text
E:\陈晨\FJ-ker\firmware\include\secrets.h
```

临时 HTTP 测试写法：

```cpp
#define SERVER_BASE_URL "http://fjker.example.com"
```

如果你已经改造固件并验证 HTTPS 可用，再改成：

```cpp
#define SERVER_BASE_URL "https://fjker.example.com"
```

重要限制：

- 当前固件代码没有专门配置证书校验或 Cloudflare Access 登录。
- 不要给这个设备入口套浏览器登录型 Cloudflare Access；ESP32 不会处理网页登录、MFA 或 OAuth 流程。
- 如果要用 Cloudflare Access Service Token，需要固件能发送对应请求头，服务端或边缘规则也要配套。

### 12. 外网验证

用不在同一个局域网内的网络测试，例如手机热点或移动数据：

```powershell
curl.exe http://fjker.example.com/health
```

然后再用设备拍照提交一张测试图片，确认：

1. 设备能连上 Wi-Fi。
2. 设备能访问 `SERVER_BASE_URL`。
3. 服务端能收到 `/jobs` 请求。
4. DashScope Key 正常。
5. 设备能下载 `/jobs/{id}/pages/{index}` 页面数据。

## 方案 B：公网 IP + 反向代理

如果你有固定公网 IP，或者可以用 DDNS，也可以使用 Caddy、Nginx、IIS ARR 做反向代理。

推荐结构：

```text
公网用户或设备
  -> 80/443
  -> Caddy/Nginx/IIS ARR
  -> http://127.0.0.1:8080
  -> FJ-ker FastAPI 服务
```

FJ-ker 仍然只监听本机：

```powershell
$env:SERVER_HOST = "127.0.0.1"
$env:SERVER_PORT = "8080"
.\run.ps1
```

Caddy 示例：

```caddyfile
fjker.example.com {
  reverse_proxy 127.0.0.1:8080
}
```

注意：

- 如果固件还不能使用 HTTPS，就不要假设 `https://fjker.example.com` 一定能用。
- 如果为了兼容当前固件而开放 HTTP，请理解这是明文传输，公网链路上不适合传敏感图片。
- 反向代理必须配置请求体大小、访问日志、限流或 WAF，否则 `/jobs` 仍然会被公开刷接口。

## 方案 C：直接端口转发（只适合临时测试）

这个方案不推荐长期使用。

只有在你明确知道风险、只是短时间测试时，才考虑这样做：

```text
公网用户或设备
  -> 公网 IP:8080
  -> 路由器端口转发
  -> Windows Server:8080
  -> FJ-ker FastAPI 服务
```

### 1. 服务端监听所有网卡

```powershell
cd E:\陈晨\FJ-ker\server
$env:SERVER_HOST = "0.0.0.0"
$env:SERVER_PORT = "8080"
.\run.ps1
```

### 2. 放行 Windows 防火墙

管理员 PowerShell：

```powershell
New-NetFirewallRule `
  -DisplayName "FJ-ker API 8080" `
  -Direction Inbound `
  -Action Allow `
  -Protocol TCP `
  -LocalPort 8080
```

### 3. 路由器端口转发

在路由器管理页面添加：

```text
外部端口：8080
内部 IP：Windows Server 的局域网 IP
内部端口：8080
协议：TCP
```

然后固件写：

```cpp
#define SERVER_BASE_URL "http://你的公网IP或域名:8080"
```

### 4. 测试完成后关闭暴露

删除防火墙规则：

```powershell
Remove-NetFirewallRule -DisplayName "FJ-ker API 8080"
```

关闭路由器端口转发。

## 必须做的安全加固

如果计划长期公网使用，至少确认下面几项：

### 1. 启用共享 Token

服务端 `.env` 配置：

```env
FJKER_API_TOKEN=一串足够长的随机字符串
```

固件 `secrets.h` 配置同一串 Token：

```cpp
#define FJKER_API_TOKEN "同一串随机字符串"
```

当前固件网络层会在 `FJKER_API_TOKEN` 非空时自动加请求头：

```http
X-FJ-KER-TOKEN: 同一串随机字符串
```

服务端会对所有 `/jobs` 接口校验这个请求头，不匹配就返回 `401 Unauthorized`。`/health` 保持公开，方便公网健康检查。

### 2. 增加限流

至少限制：

- 每分钟 `POST /jobs` 次数。
- 单个来源 IP 的上传频率。
- 总并发任务数。
- 单个任务最大处理时间。

如果用 Cloudflare，可以在 Cloudflare 侧给 `/jobs` 配置 WAF 或 Rate Limiting。即使加了 Token，也建议做限流，因为 Token 泄露后仍可能被刷。

### 3. 使用 HTTPS

公网长期使用时，不建议传明文 HTTP。

可选路线：

- 反向代理或 Cloudflare 提供公网 HTTPS。
- 固件改为 HTTPS 请求，并正确处理证书校验。
- 如果设备资源有限，至少在公网入口加 Token，并把明文 HTTP 限制在临时测试。

### 4. 不要开放 RDP 到全网

Windows Server 远程管理不要直接暴露 `3389` 到公网。

更安全的做法：

- 只通过 VPN、Zero Trust、堡垒机或固定 IP 白名单访问 RDP。
- 使用强密码和 MFA。
- 禁用默认管理员弱口令。
- 定期安装 Windows Update。

### 5. 保护 `.env`

`server\.env` 里有 DashScope Key。建议：

- 不提交到 Git。
- 不截图发给别人。
- 只允许服务运行账户和管理员读取。
- 定期检查 DashScope 用量。
- 泄露后立即轮换 Key。

## 运维检查清单

### 每次部署后检查

```powershell
curl.exe http://127.0.0.1:8080/health
```

如果使用 Cloudflare Tunnel：

```powershell
curl.exe http://fjker.example.com/health
sc.exe query cloudflared
Get-Content C:\Cloudflared\cloudflared.log -Tail 50
```

如果使用计划任务：

```powershell
Get-ScheduledTask -TaskName "FJ-ker Server"
Get-ScheduledTaskInfo -TaskName "FJ-ker Server"
```

如果使用端口转发：

```powershell
Get-NetFirewallRule -DisplayName "FJ-ker API 8080"
netstat -ano | findstr :8080
```

### 常见问题

#### 访问域名没有响应

检查：

1. `curl.exe http://127.0.0.1:8080/health` 是否正常。
2. `cloudflared` 服务是否运行。
3. `config.yml` 里的 `hostname` 是否是正确域名。
4. Cloudflare DNS 是否已创建 Tunnel 路由。
5. 服务器是否能访问 Cloudflare。

#### 固件访问失败，但电脑浏览器能打开

检查：

1. 固件 `SERVER_BASE_URL` 是否写错。
2. Cloudflare 是否把 HTTP 强制跳转 HTTPS。
3. 固件是否支持当前 URL 的协议。
4. 域名是否能被设备所在网络解析。
5. 设备 Wi-Fi 是否能访问公网。

#### 服务端启动后外网仍访问不到

如果用 Cloudflare Tunnel，不需要开 Windows 入站防火墙，也不需要路由器转发。重点检查 Tunnel。

如果用端口转发，需要同时满足：

1. 服务端监听 `0.0.0.0:8080`。
2. Windows 防火墙放行 TCP 8080。
3. 路由器转发到正确的 Windows Server 内网 IP。
4. 运营商没有 CGNAT 或封锁入站端口。
5. 公网 IP 或 DDNS 指向当前网络。

#### DashScope 额度异常消耗

立即：

1. 关闭 Cloudflare Tunnel 或删除端口转发。
2. 停止 FJ-ker 服务端。
3. 检查访问日志。
4. 轮换 DashScope API Key。
5. 增加 Token 和限流后再重新开放。

## 最终推荐配置

长期公网使用建议采用这个组合：

```text
ESP32 设备
  -> HTTPS 域名
  -> Cloudflare Tunnel / 反向代理
  -> http://127.0.0.1:8080
  -> FJ-ker 服务端
  -> DashScope
```

并满足：

- 服务端不直接监听公网地址。
- Windows Server 不开放 `8080` 到公网。
- `/jobs` 全部接口都有共享 Token。
- `/jobs` 上传入口有限流。
- DashScope Key 只在服务端 `.env`。
- 远程桌面不直接暴露到全网。

在当前未改认证代码前，最务实的上线方式是：

```text
Cloudflare Tunnel + SERVER_HOST=127.0.0.1 + 临时 HTTP 测试 + 严格控制使用范围
```

真正长期运行前，先补 Token、限流和 HTTPS/TLS 固件适配。

## 参考资料

- [Cloudflare Tunnel：创建本地管理 Tunnel](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/do-more-with-tunnels/local-management/create-local-tunnel/)
- [Cloudflare Tunnel：Windows 上作为服务运行](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/do-more-with-tunnels/local-management/as-a-service/windows/)
- [Microsoft Learn：New-NetFirewallRule](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule?view=windowsserver2025)
- [Caddy 文档：reverse_proxy 指令](https://caddyserver.com/docs/caddyfile/directives/reverse_proxy)
