# Ubuntu 26.04 一键部署指南

本文适用于把 FJ-ker 服务端部署到 Ubuntu 26.04 服务器。

部署脚本：

```text
server/deploy_ubuntu_26_04.sh
```

它会自动完成：

1. 安装系统依赖：`python3`、`python3-venv`、`python3-pip`、`curl`、`fonts-noto-cjk` 等。
2. 复制项目到 `/opt/fj-ker`。
3. 创建 Python 虚拟环境。
4. 安装服务端依赖。
5. 可选安装 Playwright Chromium。
6. 写入 `/opt/fj-ker/server/.env`。
7. 生成或复用 `FJKER_API_TOKEN`。
8. 创建 `fj-ker.service` systemd 服务。
9. 启动服务并检查 `/health`。
10. 可选添加 UFW 防火墙规则。

## 服务器前提

Ubuntu 服务器需要：

- Ubuntu 26.04
- SSH 可登录
- 有公网 IP
- 能访问 Python/PyPI/DashScope 网络
- 云厂商安全组放行 TCP `8080`

如果你继续使用当前公网地址，固件地址是：

```text
http://115.28.131.113:8080
```

如果公网 IP 已变化，请替换成新的 IP。

## 上传并解压项目

在本机把 zip 上传到服务器，例如：

```powershell
scp E:\陈晨\FJ-ker.zip root@115.28.131.113:/root/fjker.zip
```

登录服务器：

```bash
ssh root@115.28.131.113
```

安装 unzip 并解压：

```bash
apt-get update
apt-get install -y unzip
rm -rf /root/fjker-src
mkdir -p /root/fjker-src
unzip /root/fjker.zip -d /root/fjker-src
```

如果 zip 解压后多了一层目录，先找到脚本：

```bash
find /root/fjker-src -name deploy_ubuntu_26_04.sh
```

假设脚本路径是：

```text
/root/fjker-src/server/deploy_ubuntu_26_04.sh
```

## 执行一键部署

推荐用公网直连 `8080` 的部署命令：

```bash
chmod +x /root/fjker-src/server/deploy_ubuntu_26_04.sh

sudo /root/fjker-src/server/deploy_ubuntu_26_04.sh \
  --dashscope-api-key "你的 DashScope API Key" \
  --install-root "/opt/fj-ker" \
  --port 8080 \
  --bind-host "0.0.0.0" \
  --public-base-url "http://115.28.131.113:8080" \
  --open-firewall \
  --accept-public-http-risk
```

如果你想复用旧固件里的 Token，加：

```bash
--api-token "旧的 FJKER_API_TOKEN"
```

如果不传 `--api-token`，脚本会自动生成新 Token。新 Token 生成后，你必须重新修改并烧录固件。

## 如果 Chromium 下载太慢

Playwright Chromium 下载比较大。如果网络太慢，可以跳过：

```bash
sudo /root/fjker-src/server/deploy_ubuntu_26_04.sh \
  --dashscope-api-key "你的 DashScope API Key" \
  --install-root "/opt/fj-ker" \
  --port 8080 \
  --bind-host "0.0.0.0" \
  --public-base-url "http://115.28.131.113:8080" \
  --open-firewall \
  --accept-public-http-risk \
  --skip-playwright-browser-install
```

跳过后服务仍可运行，但会更多依赖 Pillow fallback 渲染。以后网络好时再执行：

```bash
sudo PLAYWRIGHT_BROWSERS_PATH=/opt/fj-ker/.cache/ms-playwright \
  /opt/fj-ker/server/.venv/bin/python -m playwright install chromium

sudo systemctl restart fj-ker
```

## 检查服务状态

部署完成后检查：

```bash
curl http://127.0.0.1:8080/health
curl http://115.28.131.113:8080/health
```

查看 systemd 服务：

```bash
sudo systemctl status fj-ker --no-pager
```

查看日志：

```bash
sudo journalctl -u fj-ker -f
```

或：

```bash
sudo tail -n 100 /opt/fj-ker/logs/server.log
sudo tail -n 100 /opt/fj-ker/logs/server.err.log
```

重启服务：

```bash
sudo systemctl restart fj-ker
```

停止服务：

```bash
sudo systemctl stop fj-ker
```

开机自启状态：

```bash
sudo systemctl is-enabled fj-ker
```

## 查看服务器配置

配置文件：

```text
/opt/fj-ker/server/.env
```

查看：

```bash
sudo cat /opt/fj-ker/server/.env
```

典型内容：

```env
DASHSCOPE_API_KEY=你的 DashScope API Key
QWEN_MODEL=qwen3.7-plus
FJKER_API_TOKEN=设备访问 Token
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
MAX_PAGES=20
```

不要截图或公开 `.env`，里面有 Qwen API Key 和设备 Token。

部署信息文件：

```bash
sudo cat /opt/fj-ker/server/DEPLOYMENT_INFO.txt
```

里面会输出固件需要填写的两行：

```cpp
#define SERVER_BASE_URL "http://115.28.131.113:8080"
#define FJKER_API_TOKEN "脚本生成或你传入的 Token"
```

## 固件需要同步修改

固件文件：

```text
firmware/include/secrets.h
```

应配置为：

```cpp
#pragma once

#define WIFI_SSID "你的 Wi-Fi 名称"
#define WIFI_PASS "你的 Wi-Fi 密码"
#define SERVER_BASE_URL "http://115.28.131.113:8080"
#define FJKER_API_TOKEN "和服务器 .env 完全相同的 Token"
```

注意：

- `SERVER_BASE_URL` 必须带 `:8080`。
- `FJKER_API_TOKEN` 必须和服务器 `.env` 完全一致。
- `DASHSCOPE_API_KEY` 只放服务器，不要写进固件。

## 防火墙和云安全组

脚本的 `--open-firewall` 只会尝试添加 UFW 规则：

```bash
sudo ufw allow 8080/tcp
```

很多云服务器还有“安全组”。你必须在云厂商控制台放行：

```text
入站 TCP 8080
来源：0.0.0.0/0
目标：当前 Ubuntu 服务器
```

如果本机能访问但公网不能访问，通常是安全组没放行。

## API 测试

健康检查：

```bash
curl http://115.28.131.113:8080/health
```

不带 Token 测试 `/jobs`，应返回 `401`：

```bash
curl -i -X POST http://115.28.131.113:8080/jobs
```

带 Token 但不传图片，应返回 `422`，说明 Token 已通过，只是缺少图片：

```bash
curl -i -X POST http://115.28.131.113:8080/jobs \
  -H "X-FJ-KER-TOKEN: 你的 Token"
```

上传 JPEG：

```bash
curl -X POST http://115.28.131.113:8080/jobs \
  -H "X-FJ-KER-TOKEN: 你的 Token" \
  -F "image=@/root/test.jpg;type=image/jpeg"
```

返回示例：

```json
{"job_id":"abcd","status":"queued"}
```

查询状态：

```bash
curl http://115.28.131.113:8080/jobs/abcd \
  -H "X-FJ-KER-TOKEN: 你的 Token"
```

下载页面：

```bash
curl http://115.28.131.113:8080/jobs/abcd/pages/0 \
  -H "X-FJ-KER-TOKEN: 你的 Token" \
  --output page0.bin
```

横屏页面仍是：

```text
384 × 168 × 1-bit = 8064 bytes
```

## 重新部署

上传新 zip 后重新解压，再执行同一个部署脚本即可。

如果你不想重新烧录固件，一定要传入旧 Token：

```bash
--api-token "旧的 FJKER_API_TOKEN"
```

否则脚本会生成新 Token，固件旧 Token 会失效。

## 卸载

```bash
sudo systemctl stop fj-ker
sudo systemctl disable fj-ker
sudo rm -f /etc/systemd/system/fj-ker.service
sudo systemctl daemon-reload
sudo rm -rf /opt/fj-ker
```
