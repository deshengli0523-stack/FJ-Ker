# FJ-ker — 基于拍照的题目解答设备
**Codex 实施计划**

本文档是 Codex 搭建和实现本项目的唯一事实来源。它说明硬件接线、固件架构、PC 端服务器、两者之间的通信协议，以及构建/运行步骤。

最终用户技术背景较少。Codex 应产出开箱即用的代码：用户只需填写三个密钥/配置项（`WIFI_SSID`、`WIFI_PASS`、`DASHSCOPE_API_KEY`）以及 PC 服务器的局域网 IP。

---

## 1. 系统概览

```
                  Wi-Fi (LAN)
 [ESP32-S3-CAM] ─────────────────► [PC 服务端 (Windows)]
       ▲                                 │
       │  1-bpp 页面位图                 │ Qwen3.7-Plus (DashScope) 视觉 API
       └─────────────────────────────────┘
       (HTTP/REST, polling)
```

### 角色

| 端 | 职责 |
|------|----------------|
| **ESP32-S3-CAM** | UI 状态机、相机预览、JPEG 拍摄、Wi-Fi 上传、页面导航、对收到的位图进行单色显示 |
| **PC 服务端 (Windows + Python)** | 接收 JPEG，调用 **Qwen3.7-Plus**（Alibaba DashScope）生成答案，将 Markdown + LaTeX + 中文渲染为与屏幕尺寸完全匹配的 **1-bpp 页面位图**，并把页面提供给设备 |

### 关键设计决策（以及我调整部分内容的原因）

1. **发送给设备的是页面位图，而不是文本字符串。**
   ESP32 难以良好渲染中文 + LaTeX，而 YDP290H001 是 1-bit 单色屏。把所有排版、数学公式渲染和中文字体处理放到 PC 端（使用 headless Chromium + KaTeX + Noto Sans CJK），再发送预分页的 1-bpp 位图，可以把最困难的问题从微控制器上移走。每页正好是 `168 × 384 bits = 8 064 bytes`。

2. **使用 HTTP 轮询，而不是 WebSocket。**
   对 ESP32 更简单、更可靠，也更容易调试。设备 POST JPEG，拿到 `job_id`，然后每约 1.5 s 轮询 `/jobs/{id}`，直到页面准备好。

3. **相机预览使用抖动灰度，而不是彩色。**
   OV2640 以 QQVGA (160×120) 灰度输出 → Floyd–Steinberg dither → 居中 blit 到 168×384 屏幕。约 5 fps 足够用来对准题目。

4. **按钮消抖放在固件中，而不是硬件中。**
   节省 BOM。使用 20 ms 软件消抖，并为 Page Up/Down 提供长按重复。

5. **Wi-Fi 只在需要时连接。**
   节省电量。设备在空闲/预览状态下不连接 Wi-Fi，直到用户按下确认键（Confirm）。

6. **不在设备端运行 LLM，也不在设备端做字体渲染。**
   ESP32-S3 + PSRAM 有足够空间放缓冲区，但放不下 CJK 字体集和 LaTeX 渲染器。让设备保持简单，把重活交给 PC。

7. **电源开关是位于升压转换器输入侧的硬开关**，因此关机时电池会完全断开。TP4056 充电模块仍通过 USB-C OTG 口保持可充电，所以关机状态下也能充电。

8. **欠压 / 电池保护：** 固件在启动时读取 VBAT（通过分压器接入 ADC 引脚）；如果低于约 3.3 V，就显示“电量低”警告并停止拍摄，以保护电芯。

---

## 2. 硬件

### 2.1 物料清单（已在你手上）

- **MCU 开发板：** GOOUUU ESP32-S3-CAM (ESP32-S3 + OV2640 + 8 MB PSRAM)
- **显示屏：** YDP290H001 — 2.9" 单色 SPI LCD，**168 × 384**，ST7305 控制器
  ([driver/example](https://gitee.com/osptek/2.9-lcd-168x384-spi-st7305/tree/main/))
- **5 × 轻触按钮：** 上一页（Page Up）、下一页（Page Down）、拍摄（Capture）、取消（Cancel）、确认（Confirm）
- **1 × SPDT 滑动开关：** 主电源
- **充电：** TP4056 模块（USB-C 输入，BAT+ / BAT-，OUT+ / OUT-）
- **电池：** Li-ion 602030（约 250 mAh，标称 3.7 V）
- **升压：** TPS61088 模块，设置为 **5.0 V 输出**（通过 ESP32-S3-CAM 的 5 V/VIN 引脚供电；板载 LDO 产生稳定的 3.3 V）

### 2.2 电源接线

```
Battery(+) ─── TP4056 BAT+
Battery(−) ─── TP4056 BAT−
TP4056 OUT+ ─── MAIN SWITCH ─── TPS61088 VIN+
TP4056 OUT− ─────────────────── TPS61088 VIN−
TPS61088 VOUT+ (5 V) ─── ESP32-S3-CAM 5V 引脚
TPS61088 VOUT− ─────── ESP32-S3-CAM GND
```

显示屏的 `VCC` 使用 **ESP32-S3-CAM 的 3V3 引脚输出的 3.3 V**。**不要**用 5 V 给显示屏供电，ST7305 是 3.3 V 器件。

用于 ADC 的**电池监测分压器**：
```
BAT+ ─[100 kΩ]─┬─[100 kΩ]─ GND
               └── ADC 引脚 (GPIO1)
```
这会把 ½·VBAT（满电时约 1.85 V）送入 ADC，处于安全范围内。

### 2.3 GPIO 分配

OV2640 相机占用 GPIO 4–18（以及 3、46）。PSRAM 使用 35–37。USB native 使用 19/20。UART0 是 43/44。因此可用如下引脚。

| 信号 | GPIO | 说明 |
|---|---|---|
| **Display SCLK** | 47 | 连接 ST7305 的 SPI |
| **Display MOSI** | 21 | 连接 ST7305 的 SPI |
| **Display CS**   | 42 | 低电平有效 |
| **Display DC**   | 41 | 数据/命令选择 |
| **Display RST**  | 40 | 复位 |
| **Btn: Capture** | 45 | 启动绑定位（strapping pin）— 可作为输入并外接上拉到 3.3V；启动时**不要**拉低。使用 10 kΩ 上拉 + 按钮到 GND。 |
| **Btn: Confirm** | 48 | （板上也接 WS2812；板载 LED 可在固件中禁用） |
| **Btn: Cancel**  | 39 | |
| **Btn: Page Up** | 38 | （SD_CMD 标注 — 不使用 SD 卡，因此可用） |
| **Btn: Page Dn** | 2  | |
| **VBAT sense**   | 1  | ADC1_CH0 |

> Codex：把所有引脚编号都放在 `firmware/include/pins.h` 中，这样未来换板时只需要改一个文件。

### 2.4 按钮电路

五个按钮都采用：GPIO ─ button ─ GND，并启用内部上拉（`gpio_pulldown_dis` + `gpio_pullup_en`）。每个按钮的 GPIO 到 GND 加一个 100 nF 电容，用于硬件抗噪。

---

## 3. 固件（ESP32-S3）

### 3.1 工具链

- **PlatformIO + Arduino-ESP32 core (v3.0+)** — 对用户来说最容易，可在 VS Code 中一键烧录。Codex 也应提交 `platformio.ini`。
- C++17.
- 必需库：`esp32-camera`（内置）、`WiFi`、`HTTPClient`、一个 ST7305 driver port（见 §3.4）、`ArduinoJson`。

### 3.2 项目结构

```
firmware/
├── platformio.ini
├── include/
│   ├── pins.h               # 所有 GPIO + ADC 通道分配
│   ├── config.h             # WIFI_SSID, WIFI_PASS, SERVER_BASE_URL (defines)
│   └── secrets.h.example    # 模板；用户复制为 secrets.h（gitignored）
├── src/
│   ├── main.cpp             # setup(), loop() — 只调度状态机 tick
│   ├── app.cpp / app.h      # AppState enum + transitions
│   ├── ui_splash.cpp        # "FJ-ker" 启动画面
│   ├── ui_preview.cpp       # 相机预览渲染循环
│   ├── ui_uploading.cpp     # 加载动画 + 进度文本
│   ├── ui_answer.cpp        # 页面查看器
│   ├── ui_error.cpp         # 网络错误、低电量
│   ├── camera.cpp/.h        # 相机初始化、取帧（预览 + 拍摄）
│   ├── dither.cpp/.h        # Floyd–Steinberg 灰度 → 1bpp
│   ├── display.cpp/.h       # ST7305 driver wrapper
│   ├── buttons.cpp/.h       # 消抖 + event queue
│   ├── net_client.cpp/.h    # HTTP 上传 + 轮询 + 页面获取
│   ├── page_store.cpp/.h    # PSRAM 中的 N 页环形缓存
│   └── battery.cpp/.h       # ADC 读取 + 低电量检查
└── data/                    # 嵌入资源（如启动 logo）
```

### 3.3 状态机

```
                ┌──────────────────────────────────────┐
                │            BOOT (splash 1.5 s)       │
                └─────────────┬────────────────────────┘
                              ▼
                ┌──────────────────────────┐  ── 任意按钮 ──►
                │       IDLE（启动画面）   │   除拍摄键（Capture）外，其他按钮进入 PREVIEW
                └──────────────┬───────────┘
                               │ 拍摄键（Capture）
                               ▼
       ┌────────────────────────────────────────────┐
       │            CAMERA_PREVIEW                  │ ← Cancel ─┐
       │  - 160×120 灰度图，约 5fps                 │            │
       │  - 抖动到 168×~126，并居中贴到屏幕         │            │
       │  - 小型“取景后确认”提示                    │            │
       └─────────┬──────────────────────────────────┘            │
                 │ 确认键（Confirm）                             │
                 ▼                                                │
       ┌────────────────────────────────────────────┐            │
       │  CAPTURE → UPLOADING                       │            │
       │  - JPEG UXGA，质量 12                     │            │
       │  - 若尚未连接，则连接 Wi-Fi                │            │
       │  - POST /jobs                              │            │
       └─────────┬──────────────────────────────────┘            │
                 │ 已收到 job_id                                  │
                 ▼                                                │
       ┌────────────────────────────────────────────┐            │
       │  WAITING_FOR_ANSWER                        │            │
       │  - 每 1.5 s 轮询 /jobs/{id}                │── Cancel ──┤
       │  - 显示“正在思考”加载动画                  │            │
       └─────────┬──────────────────────────────────┘            │
                 │ status = "ready"                               │
                 ▼                                                │
       ┌────────────────────────────────────────────┐            │
       │  ANSWER_VIEW                               │            │
       │  - 下载所有页面到 PSRAM                    │            │
       │  - PageUp / PageDown 导航                  │── Cancel ──┘
       │  - 右下角页码指示 "3 / 7"                  │
       │  - 确认键（Confirm）= 重新进入 PREVIEW     │
       └────────────────────────────────────────────┘
```

任何非 idle 状态下按 `Cancel` 都返回 `CAMERA_PREVIEW`（按规格）。在 `IDLE` 中，Cancel 不执行操作。

### 3.4 显示驱动

把 gitee 仓库中的 C driver
（`2.9-lcd-168x384-spi-st7305`）移植到 `display.cpp`。用以下接口封装：

```cpp
namespace display {
    void init();
    void clear();
    void blitPage(const uint8_t* page8064);   // 正好 168*384/8 bytes，MSB-first
    void drawText(int x, int y, const char* utf8, Font font);  // 仅用于 splash + UI chrome
    void present();                            // 提交到 LCD
}
```

gitee 的 driver 面向通用 MCU；需要把其中的 GPIO/SPI 调用替换为 `SPI3_HOST` 上 20 MHz 的 ESP-IDF SPI master。

> **重要：** ST7305 是反射式双稳态 LCD，刷新非常慢（全屏约 ~1 Hz）。相机预览时，只对预览区域做 **partial-window refresh**（Codex：见 ST7305 datasheet §"Partial Update" command 0x12）。否则预览会严重撕裂/残影。

### 3.5 相机配置

```cpp
camera_config_t cfg = {};
cfg.pin_pwdn  = -1;
cfg.pin_reset = -1;
// （其他 camera pins 按 GOOUUU ESP32-S3-CAM 原理图设置 — Codex：可从
//  Espressif 示例 "CameraWebServer" 的 AI-Thinker pinout 复制，但
//  需要对照 GOOUUU 丝印验证 — pins 4..18 inclusive。）
cfg.xclk_freq_hz = 20_000_000;
cfg.pixel_format = PIXFORMAT_JPEG;     // 预览时会重新初始化为 GRAYSCALE
cfg.frame_size   = FRAMESIZE_UXGA;     // 1600×1200 用于拍摄
cfg.jpeg_quality = 12;
cfg.fb_count     = 2;
cfg.fb_location  = CAMERA_FB_IN_PSRAM;
cfg.grab_mode    = CAMERA_GRAB_LATEST;
```

预览时，重新配置为 `PIXFORMAT_GRAYSCALE` + `FRAMESIZE_QQVGA`（160×120），并在循环中使用 `esp_camera_fb_get()`。正式拍摄前再切回 JPEG/UXGA。

### 3.6 抖动（预览路径）

`dither.cpp`:

```cpp
void floydSteinberg(
    const uint8_t* gray, int gw, int gh,         // 源 160×120
    uint8_t*       mono, int mw, int mh,         // 目标 packed 1bpp 168×~126
    int            dst_x_offset, int dst_y_offset);
```

阈值为 128。误差扩散到右侧（7/16）、左下（3/16）、下方（5/16）、右下（1/16）。在 PSRAM 中的工作缓冲区上原地运行。

### 3.7 按钮

单个 FreeRTOS task 以 200 Hz 读取全部 5 个 GPIO，进行消抖（连续 3 次同状态样本），并入队 `ButtonEvent { which, kind }`，其中 `kind ∈ {Down, Up, Repeat}`。PageUp/PageDown 长按时每 200 ms 发出一次 `Repeat`。

### 3.8 网络客户端

```cpp
struct UploadResult { String job_id; int http_status; };

UploadResult postJobJpeg(const uint8_t* jpeg, size_t len);
JobStatus    getJobStatus(const String& job_id);    // {queued|processing|ready|error}
bool         getPage(const String& job_id, int index, uint8_t out[8064]);
int          getPageCount(const String& job_id);
```

所有 HTTP 调用都使用基于普通 HTTP（LAN）的 `HTTPClient`。失败后重试 3×，每次 500 ms backoff。失败要浮现到 `ui_error` 状态。

### 3.9 页面存储

在 PSRAM 中放一个最多 20 页的环形缓冲区（20 × 8 064 ≈ 160 KB）。按需下载：先立即下载第 0 页，再预取第 1 + 第 2 页，之后随用户导航继续下载。缺页时，`page_store::get(i)` 最多阻塞 2 s。

### 3.10 启动流程

```
1. display::init()       (~60 ms)
2. 居中显示大字号 `FJ-ker` 启动画面
3. battery::read() — if VBAT < 3.3 V → ui_error("电量不足"), halt
4. camera::init() (kept warm)
5. buttons::start()
6. 进入 IDLE
```

启动时**不**连接 Wi-Fi。第一次拍摄时再懒加载连接。

---

## 4. PC 服务端 (Windows)

### 4.1 技术栈

- **Python 3.11**
- **FastAPI + Uvicorn** — HTTP API
- **Playwright (Chromium headless)** — 将 HTML 渲染成图片
- **Pillow** — 最终缩放、阈值化、打包为 1-bpp
- **dashscope**（Alibaba 官方 Python SDK）— Qwen3.7-Plus 视觉调用
- **python-dotenv** — 配置

### 4.2 项目结构

```
server/
├── pyproject.toml
├── .env.example
├── run.ps1                  # PowerShell 启动脚本
├── install.ps1              # 一次性创建 venv + 安装依赖 + playwright install
├── app/
│   ├── main.py              # FastAPI app + routes
│   ├── settings.py          # pydantic-settings，读取 .env
│   ├── jobs.py              # 内存 job registry（dict + asyncio locks）
│   ├── llm_qwen.py          # Qwen3.7-Plus (DashScope) 视觉调用
│   ├── render.py            # HTML build → Playwright → 1bpp pages
│   ├── templates/
│   │   ├── answer.html.j2   # KaTeX + Marked.js + Noto Sans CJK
│   │   └── style.css        # 168px 宽 CSS 布局
│   └── fonts/
│       └── NotoSansSC-Regular.otf
└── tests/
    ├── test_render.py       # 示例答案的快照测试
    └── samples/             # PNG 黄金样本
```

### 4.3 HTTP API（通信协议）

| 方法 | 路径 | 请求体 | 响应 |
|---|---|---|---|
| `POST` | `/jobs` | 带有 `image` 字段（JPEG）的 `multipart/form-data` | `{"job_id":"ab12","status":"queued"}` |
| `GET`  | `/jobs/{id}` | – | `{"status":"queued|processing|ready|error","pages":N,"error":"…"}` |
| `GET`  | `/jobs/{id}/pages/{index}` | – | `application/octet-stream`，**正好 8064 bytes**，1-bpp packed，MSB-first，row-major，top-left origin |
| `GET`  | `/health` | – | `{"ok":true,"version":"…"}` |

固件必须一致使用的常量：
- **PAGE_W = 168**, **PAGE_H = 384**, **PAGE_BYTES = 8064**
- Bit packing：像素 `(x,y)` 是字节 `y*21 + x/8` 中的 bit `7 - (x % 8)`。
  `1 = ink (black)`，`0 = white`。

### 4.4 LLM 调用（`llm_qwen.py`）

```python
async def answer_question(image_jpeg: bytes) -> str:
    """返回包含内嵌 LaTeX 的 Markdown（行内 $...$，独立公式 $$...$$）。"""
```

使用官方 **DashScope** SDK（`pip install dashscope`）。图片以 base64 data-URL 形式放入 `image_url` content part。伪代码：

```python
import base64, dashscope
from dashscope import MultiModalConversation

def call_qwen(jpeg: bytes) -> str:
    b64 = base64.b64encode(jpeg).decode()
    messages = [
        {"role": "system", "content": [{"text": SYSTEM_PROMPT}]},
        {"role": "user",   "content": [
            {"image": f"data:image/jpeg;base64,{b64}"},
            {"text":  "请解答图片中的题目。"},
        ]},
    ]
    resp = MultiModalConversation.call(
        model=settings.QWEN_MODEL,        # default: "qwen3.7-plus"
        messages=messages,
        temperature=0.2,
    )
    return resp.output.choices[0].message.content[0]["text"]
```

System prompt（固定；不要暴露给用户）：

> 你是一个面向中小学生的解题助手。请阅读图片中的题目,然后用简洁、清晰的中文给出解题过程
> 与最终答案。要求:
> 1. 先复述题目要点(一行),然后分步骤推导;
> 2. 公式用 LaTeX,行内用 `$...$`,独立公式用 `$$...$$`;
> 3. 不要使用表格、不要使用图片链接、不要使用 HTML;
> 4. 最后一行以 `**答案:** …` 收尾;
> 5. 若图片不含题目或无法识别,只回复 `无法识别题目,请重新拍照`。

Model: **`qwen3.7-plus`**（multimodal）。Temperature 0.2。

> **关于模型名称的说明：** 用户指定了 `qwen3.7-plus`。如果 DashScope 拒绝这个精确标识符，本文写作时最接近的有效替代项是 `qwen-vl-plus` 或 `qwen-vl-max`。Codex 应把模型名保留为单一设置项（`.env` 中的 `QWEN_MODEL`），这样用户只需改一行即可切换，不需要改代码。

### 4.5 渲染流水线（`render.py`）

```
markdown ── Jinja2 ──► HTML doc (预加载 KaTeX、Noto Sans SC，168px 宽列，
                                 16px 基础字号，line-height 1.4，12px padding)
                              │
                              ▼
                       Playwright headless Chromium
                              │
                              ▼
              设备像素比为 1 的整页 PNG
              (宽度固定为 168 px CSS，高度自动)
                              │
                              ▼
             Pillow:  convert L → threshold ≤ 128 → 1-bit
                              │
                              ▼
             切成 N 个 384 px 高的条带；
             最后一个条带用白色补齐；
             打包为 8 064-byte buffers，MSB-first
```

HTML 模板：

```html
<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8">
<link rel="stylesheet" href="katex.min.css">
<script defer src="katex.min.js"></script>
<script defer src="auto-render.min.js"
        onload="renderMathInElement(document.body,{
          delimiters:[{left:'$$',right:'$$',display:true},
                      {left:'$',right:'$',display:false}]});"></script>
<style>
  @font-face { font-family:'NSSC'; src:url('NotoSansSC-Regular.otf'); }
  html,body { margin:0; padding:0; }
  body { width:168px; padding:8px; font:14px/1.45 'NSSC',sans-serif;
         color:#000; background:#fff; }
  .katex { font-size:1em; }
  pre,code { font-family:'NSSC',monospace; }
</style></head>
<body>{{ html_from_markdown }}</body></html>
```

KaTeX + Noto fonts 会 vendored 到 `app/fonts/`，因此渲染器可以离线工作。

### 4.6 设置（`.env.example`）

```
DASHSCOPE_API_KEY=sk-...
QWEN_MODEL=qwen3.7-plus
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
MAX_PAGES=20
```

在 <https://dashscope.console.aliyun.com/> 获取 DashScope API key。该 key 以 `sk-` 开头。

固件中的 `SERVER_BASE_URL` 必须指向 Windows 机器的局域网 IP，例如 `http://192.168.1.42:8080`。用户可用 `ipconfig` 查找。

### 4.7 并发

Jobs 通过单个 `asyncio.Queue` worker 顺序处理。设备同一时间只会发起一个 job，而 Qwen 调用足够慢，使用并行不值得增加复杂度。Job 记录在完成 10 分钟后过期，以释放内存。

---

## 5. 端到端流程（时间预算）

| 步骤 | 时间 |
|---|---|
| 按下 Confirm → 捕获 JPEG | ~200 ms |
| Wi-Fi 连接（已缓存凭据） | 第一次约 ~1.5 s，之后约 ~600 ms |
| 上传 UXGA JPEG（2.4 GHz 下约 ~150 KB） | ~500 ms |
| Qwen3.7-Plus 视觉调用 | 4–10 s |
| 渲染 + 抖动 + 切片（Playwright） | 1–2 s |
| 从 PC 下载 N 页 | 50 ms × N |
| **用户感知到的总“正在思考”时间** | **7–14 s** |

这决定了加载动画的节奏。“正在思考”页面不应看起来卡住，应每 500 ms 动画显示一次三点省略号。

---

## 6. 配置与密钥

`firmware/include/secrets.h.example`:

```c
#pragma once
#define WIFI_SSID        "YOUR_SSID"
#define WIFI_PASS        "YOUR_PASS"
#define SERVER_BASE_URL  "http://192.168.1.42:8080"
```

用户将它复制为 `secrets.h`（gitignored），填入三个字符串，然后点击 PlatformIO "Upload"。无需改代码。

`server/.env`：同 §4.6。

---

## 7. 构建与运行

### 7.1 PC 服务端（首次）

```powershell
cd server
.\install.ps1          # 创建 venv，安装依赖，运行 `playwright install chromium`
copy .env.example .env
notepad .env           # 粘贴 DASHSCOPE_API_KEY（必要时调整 QWEN_MODEL）
.\run.ps1              # 在 0.0.0.0:8080 启动 uvicorn
```

`run.ps1` 还应打印 LAN IP，方便用户把它填入 `secrets.h`。

### 7.2 固件

```
1. 安装 VS Code + PlatformIO extension。
2. 打开 `firmware/` 文件夹。
3. 复制 include/secrets.h.example → include/secrets.h，并填入三个值。
4. 将设备接入 TTL USB-C port（右侧那个）。
5. 点击 PlatformIO "Upload"。
```

---

## 8. 测试计划

Codex 应提供以下测试：

**固件（host-side，可选 Unity tests，位于 `firmware/test/`）：**
- Dithering：golden 160×120 PNG → 期望的 168×126 packed bytes。
- State machine：脚本化按钮事件序列 → 期望的状态迁移。
- Bit-packing：pixel `(0,0)=ink` → byte 0 = `0x80`。

**Server（`pytest server/tests/`）：**
- `test_render.py`：输入固定 markdown answer（含中文、分数和积分）→ 断言 (a) page count，以及 (b) page 0 的 hash 与保存的 golden 在容差内匹配。
- `test_protocol.py`：启动 FastAPI app，使用模拟 LLM POST 一个 fixture JPEG，轮询，获取 page 0，并断言 size == 8064。

**端到端手动 checklist（位于 `docs/SMOKE_TEST.md`）：**
- [ ] 开机后 2 s 内出现 Splash "FJ-ker"。
- [ ] 拍摄键（Capture）→ 预览显示可识别场景，帧率 ≥4 fps。
- [ ] 确认键（Confirm）→“正在思考”加载动画会动。
- [ ] 出现答案，中文和 LaTeX 公式都清晰可读。
- [ ] PageUp/PageDown 到首尾页时停止（决定：**停止，不循环**）。
- [ ] 答案视图中按取消键（Cancel）→ 预览。
- [ ] “正在思考”期间按取消键（Cancel）→ 预览，服务端任务被取消。
- [ ] 低电量：VBAT < 3.3 V → 显示 "电量不足" 画面。

---

## 9. 相比原始规格的改进 — 请阅读

这些是我替你做出的设计调整。它们不会改变你描述的用户可见行为；它们让设备真正可构建。

1. **PC 负责所有渲染并发送位图。** 你的规格已经隐含了这一点；我只是明确写出来。如果不这样做，在 ESP32 上放入中文字体和 LaTeX 渲染器并不现实。

2. **使用 HTTP 轮询而不是 push。** 固件更简单，不需要持久 socket，也更能承受 Wi-Fi 短暂中断。用户感知不到差异。

3. **相机预览为灰度 + Floyd–Steinberg dither。** 在 1-bit 单色屏上，简单阈值化会很难用；抖动能让预览足够可读，以便对准。

4. **懒连接 Wi-Fi。** 只有按下确认键（Confirm）后设备才会使用 Wi-Fi，可节省约 ~80 mA 持续电流，并大致让小型 602030 电芯的待机时间翻倍。

5. **电池检测 + 低电量锁定。** 602030 电芯容量小（约 ~250 mAh），深度放电很容易损坏。固件会在低于 3.3 V 时拒绝拍摄，并显示清晰信息。

6. **升压转换器输入侧硬电源开关。** 关机时干净切断负载；TP4056 位于开关上游，因此充电仍然有效。

7. **PageUp / PageDown 到首尾页时停止，不循环。** 答案较长时循环会让人困惑；停止更符合电子阅读器的行为。

8. **“正在思考”期间按取消键（Cancel）会通知服务器。** 固件发送 `DELETE /jobs/{id}`，让服务器停止 LLM 调用，避免浪费 DashScope 额度。

如果你不同意其中任何一点，请告诉我，Codex 会在写代码前调整。

---

## 10. 范围外（扩展目标）

这些内容有意不在 v1 实现。Codex 应留下清晰 hook。

- 通过 Wi-Fi 进行 OTA firmware update。
- 将历史 questions/answers 存到 SD card。
- 答案的 pinned "favorite" list。
- 可调 display contrast / refresh rate。
- 一个简单的 captive-portal Wi-Fi setup（这样用户无需重新烧录即可更换 SSID）。
- 在同一 adapter 后接入第二个 LLM backend（OpenAI / Anthropic / Gemini）。

---

## 11. 术语表

- **1-bpp** — 每像素 1 bit，每个像素要么黑要么白。
- **Dither** — 把灰度转换为 1-bit 的方法：将亮度误差扩散到相邻像素，让人眼看到层次。
- **ST7305** — YDP290H001 显示屏上的 LCD 控制芯片。
- **PSRAM** — ESP32-S3 模块上的额外 RAM，用于相机帧和页面缓冲区。
- **KaTeX** — 在浏览器中渲染 LaTeX 数学公式的 JavaScript 库。
- **Playwright** — 从 Python 驱动 headless browser 的库；本项目用它给渲染后的答案页面截图。

---

*计划结束。Codex：按以下顺序实现：§4 server → §3 firmware。在接线设备前，先用 `curl` 验证 §4.3 wire protocol。*
