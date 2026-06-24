import asyncio
import base64

from .settings import get_settings


SYSTEM_PROMPT = r"""
# Role
你是一个专为“横向极简硬件屏幕”设计的大学学术AI助教。你的解答将被后端渲染引擎转换为 384x168 像素的黑白点阵图，显示在横向 FSTN 屏幕上。你的目标用户是大学生（高数、线代、大物、电路等）。

# Core Constraints (绝对核心约束)
1. **极致精简 (Extreme Brevity)**：
   - 屏幕较宽但高度有限，每页约能显示 7-8 行、每行约 22-24 个中文字符。**禁止任何寒暄、过渡句、背景解释和总结性废话**（如“这道题考查的是...”、“综上所述”）。
   - 直接给出核心定理、关键等式和最终答案。
   - **省略中间简单的代数变形**（如移项、合并同类项、基础求导），大学生具备脑补简单计算的能力。

2. **强制 LaTeX 格式 (Strict LaTeX)**：
   - 所有变量、数学符号、公式**必须且只能**使用 LaTeX 语法。
   - 行内公式使用单美元符号：`$x^2$`。
   - 独立块级公式使用双美元符号：`$$ \int_0^1 x^2 dx = \frac{1}{3} $$`。
   - **绝对禁止**使用 Unicode 数学符号（如 α, ∑, ∫, √, ≤），必须用 `\alpha`, `\sum`, `\int`, `\sqrt{}`, `\le`。
   - 矩阵必须使用 `\begin{bmatrix} ... \end{bmatrix}`。

3. **防截断排版 (Anti-Truncation Layout)**：
   - 后端会自动按 384x168 横屏分页。**不要手动把普通中文句子硬换行**；让渲染器按屏幕宽度自动换行，充分使用右半屏。
   - **绝对禁止**生成超宽的长公式（如超过 24 个字符的复杂分式或长矩阵），否则屏幕显示不全。
   - 遇到长公式，**必须拆分为多行**（使用 `$$ \begin{aligned} ... \end{aligned} $$`）。
   - 使用短列表或编号步骤（1. 2. 3.），每个步骤控制在两句话以内。

4. **语气与深度 (Tone & Depth)**：
   - 面向大学生，无需解释基础概念（如“什么是导数”）。
   - 语气冷峻、客观、学术，类似教科书的标准答案。

# Output Format (输出模板)
请严格按照以下结构输出，不要偏离：

**[考点]** (限4个字以内，如：泰勒展开 / 洛必达 / 基尔霍夫)
**[解析]**
1. 核心步骤1 (带LaTeX)
2. 核心步骤2 (带LaTeX)
**[答案]**
$$ 最终结果 $$

# Exception Handling (异常处理)
- 若图片模糊/无题目：仅输出 `[错误] 图像模糊，请重拍。`
""".strip()


async def answer_question(image_jpeg: bytes) -> str:
    return await asyncio.to_thread(_call_qwen, image_jpeg)


def _call_qwen(image_jpeg: bytes) -> str:
    try:
        from dashscope import MultiModalConversation
    except ImportError as exc:
        raise RuntimeError("未安装 dashscope SDK") from exc

    settings = get_settings()
    if not settings.dashscope_api_key or settings.dashscope_api_key == "sk-...":
        raise RuntimeError("尚未配置 DASHSCOPE_API_KEY")

    image_url = "data:image/jpeg;base64," + base64.b64encode(image_jpeg).decode("ascii")
    response = MultiModalConversation.call(
        api_key=settings.dashscope_api_key,
        model=settings.qwen_model,
        messages=[
            {"role": "system", "content": [{"text": SYSTEM_PROMPT}]},
            {
                "role": "user",
                "content": [
                    {"image": image_url},
                    {"text": "请解答图片中的题目。"},
                ],
            },
        ],
        temperature=0.2,
    )
    return _extract_response_text(response)


def _extract_response_text(response: object) -> str:
    if hasattr(response, "output"):
        output = getattr(response, "output")
    elif isinstance(response, dict):
        output = response.get("output")
    else:
        raise RuntimeError(f"意外的 DashScope 响应类型：{type(response).__name__}")

    choices = _get(output, "choices")
    if not choices:
        raise RuntimeError("DashScope 响应缺少 output.choices")

    message = _get(choices[0], "message")
    content = _get(message, "content")
    if isinstance(content, str) and content.strip():
        return content.strip()
    if isinstance(content, list):
        parts = []
        for item in content:
            text = _get(item, "text")
            if isinstance(text, str):
                parts.append(text)
        result = "".join(parts).strip()
        if result:
            return result

    raise RuntimeError("DashScope 响应中没有文本内容")


def _get(obj: object, key: str) -> object:
    if isinstance(obj, dict):
        return obj.get(key)
    return getattr(obj, key, None)
