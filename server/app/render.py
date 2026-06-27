import html
import logging
import math
import os
import re
import tempfile
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont

try:
    import markdown as markdown_lib
except ImportError:  # pragma: no cover - 仅在精简环境中触发。
    markdown_lib = None

try:
    from jinja2 import Environment, FileSystemLoader, select_autoescape
except ImportError:  # pragma: no cover
    Environment = None
    FileSystemLoader = None
    select_autoescape = None


PAGE_W = 384
PAGE_H = 168
ROW_STRIDE = 48
PAGE_BYTES = PAGE_H * ROW_STRIDE
THRESHOLD = 128
LOGGER = logging.getLogger(__name__)
DEFAULT_CHROME_CHANNEL = "chrome"
_MATH_TOKEN = "FJKERMATH"
_PAGE_CUT_SEARCH_ROWS = 48
_FORMULA_CHARS = set(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789\\{}^_+=*/().- "
    "×÷·≈≤≥≠±∓∞→←⇒⇐∫∑∏√αβγδεθλμπρσφωΓΔΘΛΠΣΦΩ"
)
_GREEK_SYMBOLS = set("αβγδεθλμπρσφωΓΔΘΛΠΣΦΩ")
_UNICODE_MATH_TO_LATEX = {
    "α": r"\alpha",
    "β": r"\beta",
    "γ": r"\gamma",
    "δ": r"\delta",
    "ε": r"\epsilon",
    "θ": r"\theta",
    "λ": r"\lambda",
    "μ": r"\mu",
    "π": r"\pi",
    "ρ": r"\rho",
    "σ": r"\sigma",
    "φ": r"\phi",
    "ω": r"\omega",
    "Γ": r"\Gamma",
    "Δ": r"\Delta",
    "Θ": r"\Theta",
    "Λ": r"\Lambda",
    "Π": r"\Pi",
    "Σ": r"\Sigma",
    "Φ": r"\Phi",
    "Ω": r"\Omega",
    "×": r"\times",
    "÷": r"\div",
    "·": r"\cdot",
    "≈": r"\approx",
    "≤": r"\le",
    "≥": r"\ge",
    "≠": r"\ne",
    "±": r"\pm",
    "∓": r"\mp",
    "∞": r"\infty",
    "→": r"\rightarrow",
    "←": r"\leftarrow",
    "⇒": r"\Rightarrow",
    "⇐": r"\Leftarrow",
    "∫": r"\int",
    "∑": r"\sum",
    "∏": r"\prod",
    "√": r"\sqrt",
}
_SUPERSCRIPT_TO_ASCII = str.maketrans(
    {
        "⁰": "0",
        "¹": "1",
        "²": "2",
        "³": "3",
        "⁴": "4",
        "⁵": "5",
        "⁶": "6",
        "⁷": "7",
        "⁸": "8",
        "⁹": "9",
        "⁺": "+",
        "⁻": "-",
    }
)
_SUBSCRIPT_TO_ASCII = str.maketrans(
    {
        "₀": "0",
        "₁": "1",
        "₂": "2",
        "₃": "3",
        "₄": "4",
        "₅": "5",
        "₆": "6",
        "₇": "7",
        "₈": "8",
        "₉": "9",
        "₊": "+",
        "₋": "-",
    }
)
_LATEX_COMMANDS = {
    "alpha": "α",
    "beta": "β",
    "gamma": "γ",
    "delta": "δ",
    "epsilon": "ε",
    "theta": "θ",
    "lambda": "λ",
    "mu": "μ",
    "pi": "π",
    "rho": "ρ",
    "sigma": "σ",
    "phi": "φ",
    "omega": "ω",
    "Gamma": "Γ",
    "Delta": "Δ",
    "Theta": "Θ",
    "Lambda": "Λ",
    "Pi": "Π",
    "Sigma": "Σ",
    "Phi": "Φ",
    "Omega": "Ω",
    "times": "×",
    "div": "÷",
    "cdot": "·",
    "le": "≤",
    "leq": "≤",
    "ge": "≥",
    "geq": "≥",
    "ne": "≠",
    "neq": "≠",
    "approx": "≈",
    "pm": "±",
    "mp": "∓",
    "infty": "∞",
    "rightarrow": "→",
    "to": "→",
    "Rightarrow": "⇒",
    "leftarrow": "←",
    "Leftarrow": "⇐",
    "int": "∫",
    "sum": "∑",
    "prod": "∏",
    "lim": "lim",
    "sin": "sin",
    "cos": "cos",
    "tan": "tan",
    "ln": "ln",
    "log": "log",
}


def pack_1bpp(pixels: list[list[int]]) -> bytes:
    if len(pixels) != PAGE_H or any(len(row) != PAGE_W for row in pixels):
        raise ValueError(f"像素矩阵必须是 {PAGE_W}x{PAGE_H}")

    data = bytearray(PAGE_BYTES)
    for y, row in enumerate(pixels):
        base = y * ROW_STRIDE
        for x, value in enumerate(row):
            if value:
                data[base + x // 8] |= 1 << (7 - (x % 8))
    return bytes(data)


def image_to_1bpp_page(image: Image.Image) -> bytes:
    gray = image.convert("L")
    if gray.size != (PAGE_W, PAGE_H):
        gray = gray.resize((PAGE_W, PAGE_H))

    pixels: list[list[int]] = []
    for y in range(PAGE_H):
        row = []
        for x in range(PAGE_W):
            row.append(1 if gray.getpixel((x, y)) <= THRESHOLD else 0)
        pixels.append(row)
    return pack_1bpp(pixels)


def render_answer_pages(markdown_text: str, max_pages: int = 20) -> list[bytes]:
    screenshot = _render_with_playwright(markdown_text)
    return _slice_and_pack(screenshot, max_pages=max_pages)


def _slice_and_pack(image: Image.Image, max_pages: int) -> list[bytes]:
    gray = image.convert("L")
    if gray.width != PAGE_W:
        target_h = max(1, math.ceil(gray.height * PAGE_W / gray.width))
        gray = gray.resize((PAGE_W, target_h), Image.Resampling.LANCZOS)

    cuts = _page_cuts(gray, max_pages=max_pages)
    pages = []
    start_y = 0
    for end_y in cuts:
        page = Image.new("L", (PAGE_W, PAGE_H), 255)
        crop = gray.crop((0, start_y, PAGE_W, end_y))
        page.paste(crop, (0, 0))
        pages.append(image_to_1bpp_page(page))
        start_y = end_y
    return pages


def _page_cuts(gray: Image.Image, max_pages: int) -> list[int]:
    cuts: list[int] = []
    start_y = 0
    while start_y < gray.height and len(cuts) < max_pages:
        ideal_y = min(start_y + PAGE_H, gray.height)
        if ideal_y >= gray.height:
            end_y = gray.height
        else:
            end_y = _find_safe_page_cut(gray, start_y, ideal_y)
        if end_y <= start_y:
            end_y = ideal_y
        cuts.append(end_y)
        start_y = end_y
    return cuts or [gray.height]


def _find_safe_page_cut(gray: Image.Image, start_y: int, ideal_y: int) -> int:
    min_y = max(start_y + 1, ideal_y - _PAGE_CUT_SEARCH_ROWS)
    for y in range(ideal_y, min_y - 1, -1):
        if _row_ink_count(gray, y) == 0:
            return y

    return min(
        range(ideal_y, min_y - 1, -1),
        key=lambda y: (_row_ink_count(gray, y), abs(ideal_y - y)),
    )


def _row_ink_count(gray: Image.Image, y: int) -> int:
    if y < 0 or y >= gray.height:
        return 0
    row = gray.crop((0, y, gray.width, y + 1))
    return sum(1 for value in row.tobytes() if value <= THRESHOLD)


def _render_with_playwright(markdown_text: str) -> Image.Image:
    try:
        from playwright.sync_api import sync_playwright
    except ImportError as exc:
        raise RuntimeError(
            "Playwright is required for LaTeX rendering. Install playwright and Chromium."
        ) from exc

    html_text = _render_html(markdown_text)
    try:
        with tempfile.TemporaryDirectory() as temp_dir:
            html_path = Path(temp_dir) / "answer.html"
            out_path = Path(temp_dir) / "answer.png"
            html_path.write_text(html_text, encoding="utf-8")
            with sync_playwright() as p:
                browser = p.chromium.launch(**_chrome_launch_options())
                page = browser.new_page(viewport={"width": PAGE_W, "height": PAGE_H})
                page.goto(html_path.resolve().as_uri(), wait_until="domcontentloaded", timeout=15000)
                page.wait_for_function(
                    "() => window.__FJKER_MATH_READY === true || window.__FJKER_MATH_DISABLED === true",
                    timeout=20000,
                )
                math_disabled = page.evaluate("() => window.__FJKER_MATH_DISABLED === true")
                if math_disabled:
                    math_error = page.evaluate("() => window.__FJKER_MATH_ERROR || ''")
                    detail = f": {math_error}" if math_error else ": MathJax did not load before timeout"
                    raise RuntimeError(f"LaTeX engine did not finish rendering{detail}")
                content_height = page.evaluate(
                    "() => Math.max(document.body.scrollHeight, document.documentElement.scrollHeight)"
                )
                page.set_viewport_size({"width": PAGE_W, "height": max(PAGE_H, int(content_height))})
                page.screenshot(path=str(out_path), full_page=True)
                browser.close()
            return Image.open(out_path).convert("L")
    except Exception as exc:
        LOGGER.exception("Playwright/LaTeX rendering failed.")
        raise RuntimeError(f"Playwright/LaTeX rendering failed: {exc}") from exc


def _render_html(markdown_text: str) -> str:
    templates = Path(__file__).parent / "templates"
    css = (templates / "style.css").read_text(encoding="utf-8")
    font_url = _local_font_url()
    if font_url:
        css = css.replace("../fonts/NotoSansSC-Regular.otf", font_url)
    katex_css = _optional_text(templates / "katex" / "katex.min.css")
    katex_js = _optional_text(templates / "katex" / "katex.min.js")
    katex_auto_render_js = _optional_text(templates / "katex" / "auto-render.min.js")
    mathjax_src = _optional_asset_uri(templates / "mathjax" / "tex-svg.js")
    body = _markdown_to_html(markdown_text)
    if Environment is None:
        return f"<!doctype html><html><head><style>{css}</style></head><body><main>{body}</main></body></html>"

    env = Environment(
        loader=FileSystemLoader(str(templates)),
        autoescape=select_autoescape(["html", "j2"]),
    )
    template = env.get_template("answer.html.j2")
    return template.render(
        answer_html=body,
        css=css,
        katex_css=katex_css,
        katex_js=katex_js,
        katex_auto_render_js=katex_auto_render_js,
        mathjax_src=mathjax_src,
    )


def _optional_text(path: Path) -> str:
    if path.exists():
        return path.read_text(encoding="utf-8")
    return ""


def _optional_asset_uri(path: Path) -> str:
    if path.exists():
        return path.resolve().as_uri()
    return ""


def _chrome_launch_options() -> dict[str, object]:
    options: dict[str, object] = {
        "headless": True,
        "args": _chrome_args(),
        "env": _chrome_env(),
    }
    executable_path = os.environ.get("FJKER_CHROME_EXECUTABLE", "").strip()
    if executable_path:
        options["executable_path"] = executable_path
    else:
        options["channel"] = (
            os.environ.get("FJKER_CHROME_CHANNEL", DEFAULT_CHROME_CHANNEL).strip()
            or DEFAULT_CHROME_CHANNEL
        )
    return options


def _chrome_args() -> list[str]:
    args = [
        "--disable-dev-shm-usage",
    ]
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        args.append("--no-sandbox")
    return args


def _chrome_env() -> dict[str, str]:
    root = _chrome_runtime_root()
    home = root / "home"
    config = root / "config"
    cache = root / "cache"
    data = root / "data"
    runtime = root / "runtime"
    for path in (home, config, cache, data / "applications", runtime):
        path.mkdir(parents=True, exist_ok=True)
    runtime.chmod(0o700)

    env = {key: str(value) for key, value in os.environ.items()}
    env["HOME"] = str(home)
    env["XDG_CONFIG_HOME"] = str(config)
    env["XDG_CACHE_HOME"] = str(cache)
    env["XDG_DATA_HOME"] = str(data)
    env["XDG_RUNTIME_DIR"] = str(runtime)
    return env


def _chrome_runtime_root() -> Path:
    configured = os.environ.get("FJKER_CHROME_RUNTIME_DIR", "").strip()
    if configured:
        return Path(configured)
    return Path(tempfile.gettempdir()) / "fjker-chrome"


def _markdown_to_html(markdown_text: str) -> str:
    markdown_text, math_fragments = _extract_math_fragments(markdown_text)
    markdown_text = _extract_auto_physics_fragments(markdown_text, math_fragments)
    if markdown_lib is not None:
        body = markdown_lib.markdown(markdown_text, extensions=["extra"])
        body = _render_chemistry_html(body)
        return _restore_math_fragments(body, math_fragments)

    blocks = [block.strip() for block in markdown_text.split("\n\n") if block.strip()]
    body = "\n".join(f"<p>{html.escape(block)}</p>" for block in blocks)
    body = _render_chemistry_html(body)
    return _restore_math_fragments(body, math_fragments)


def _extract_math_fragments(markdown_text: str) -> tuple[str, list[str]]:
    fragments: list[str] = []
    out: list[str] = []
    i = 0
    while i < len(markdown_text):
        if markdown_text.startswith("$$", i):
            end = markdown_text.find("$$", i + 2)
            if end != -1:
                out.append(
                    _math_placeholder(
                        fragments,
                        markdown_text[i + 2 : end],
                        display=True,
                    )
                )
                i = end + 2
                continue
        if markdown_text.startswith("\\[", i):
            end = markdown_text.find("\\]", i + 2)
            if end != -1:
                out.append(
                    _math_placeholder(
                        fragments,
                        markdown_text[i + 2 : end],
                        display=True,
                    )
                )
                i = end + 2
                continue
        if markdown_text.startswith("\\(", i):
            end = markdown_text.find("\\)", i + 2)
            if end != -1:
                out.append(
                    _math_placeholder(
                        fragments,
                        markdown_text[i + 2 : end],
                        display=False,
                    )
                )
                i = end + 2
                continue
        if markdown_text[i] == "$" and not markdown_text.startswith("$$", i):
            end = _find_inline_math_end(markdown_text, i + 1)
            if end != -1:
                out.append(
                    _math_placeholder(
                        fragments,
                        markdown_text[i + 1 : end],
                        display=False,
                    )
                )
                i = end + 1
                continue
        out.append(markdown_text[i])
        i += 1
    return "".join(out), fragments


def _find_inline_math_end(text: str, start: int) -> int:
    i = start
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == "$":
            return i
        if text[i] == "\n":
            return -1
        i += 1
    return -1


def _extract_auto_physics_fragments(markdown_text: str, fragments: list[str]) -> str:
    out: list[str] = []
    i = 0
    while i < len(markdown_text):
        if _is_formula_start(markdown_text[i]):
            end = i + 1
            while end < len(markdown_text) and markdown_text[end] in _FORMULA_CHARS:
                end += 1
            candidate = markdown_text[i:end].strip()
            if _looks_like_physics_formula(candidate):
                out.append(
                    _math_placeholder(
                        fragments,
                        _normalize_unicode_math(candidate),
                        display=False,
                    )
                )
                i = end
                continue
        out.append(markdown_text[i])
        i += 1
    return "".join(out)


def _is_formula_start(ch: str) -> bool:
    return (ch.isascii() and (ch.isalpha() or ch.isdigit() or ch == "\\")) or ch in _GREEK_SYMBOLS


def _looks_like_physics_formula(candidate: str) -> bool:
    if candidate in _GREEK_SYMBOLS:
        return True
    if len(candidate) < 2:
        return False
    if not re.search(r"[A-Za-z0-9\\]", candidate) and not any(ch in _GREEK_SYMBOLS for ch in candidate):
        return False
    if not re.search(r"(=|\\frac|\\sqrt|\^|_|[×÷·≈≤≥≠±∓∞→←⇒⇐∫∑∏√])", candidate):
        return False
    return not candidate.startswith(_MATH_TOKEN)


def _normalize_unicode_math(source: str) -> str:
    return "".join(_UNICODE_MATH_TO_LATEX.get(ch, ch) for ch in source.strip())


def _math_placeholder(fragments: list[str], latex: str, display: bool) -> str:
    index = len(fragments)
    fragments.append(_math_engine_fragment(latex, display=display))
    return f"@@{_MATH_TOKEN}{index}@@"


def _math_engine_fragment(latex: str, display: bool) -> str:
    escaped = html.escape(latex.strip(), quote=False)
    if display:
        return f"\\[{escaped}\\]"
    return f"\\({escaped}\\)"


def _restore_math_fragments(body: str, fragments: list[str]) -> str:
    for index, fragment in enumerate(fragments):
        token = f"@@{_MATH_TOKEN}{index}@@"
        body = body.replace(token, fragment)
    return body


def _render_latex_fragment(latex: str, display: bool) -> str:
    body, _ = _parse_latex(latex.strip(), 0, stop_char="")
    if display:
        return f'<div class="math math-display">{body}</div>'
    return f'<span class="math math-inline">{body}</span>'


def _parse_latex(source: str, index: int, stop_char: str) -> tuple[str, int]:
    parts: list[str] = []
    while index < len(source):
        ch = source[index]
        if stop_char and ch == stop_char:
            return "".join(parts), index + 1
        if ch == "{":
            inner, index = _parse_latex(source, index + 1, "}")
            parts.append(inner)
            continue
        if ch in "^_":
            tag = "sup" if ch == "^" else "sub"
            inner, index = _parse_script_argument(source, index + 1)
            parts.append(f"<{tag}>{inner}</{tag}>")
            continue
        if ch == "\\":
            rendered, index = _parse_latex_command(source, index + 1)
            parts.append(rendered)
            continue
        if ch == "&":
            index += 1
            continue
        if ch in "\r\n":
            parts.append(" ")
            index += 1
            continue
        if ch.isspace():
            parts.append(" ")
            index += 1
            continue
        parts.append(html.escape(ch))
        index += 1
    return "".join(parts), index


def _parse_script_argument(source: str, index: int) -> tuple[str, int]:
    index = _skip_latex_spaces(source, index)
    if index >= len(source):
        return "", index
    if source[index] == "{":
        return _parse_latex(source, index + 1, "}")
    if source[index] == "\\":
        return _parse_latex_command(source, index + 1)
    number = re.match(r"[+-]?\d+", source[index:])
    if number:
        value = number.group(0)
        return html.escape(value), index + len(value)
    return html.escape(source[index]), index + 1


def _parse_latex_command(source: str, index: int) -> tuple[str, int]:
    command, index = _read_latex_command(source, index)
    if command in {"frac", "dfrac", "tfrac"}:
        numerator, index = _parse_required_group(source, index)
        denominator, index = _parse_required_group(source, index)
        return (
            '<span class="math-fraction">'
            f'<span class="math-num">{numerator}</span>'
            f'<span class="math-den">{denominator}</span>'
            "</span>",
            index,
        )
    if command == "sqrt":
        radicand, index = _parse_required_group(source, index)
        return (
            '<span class="math-sqrt"><span class="math-radical">√</span>'
            f'<span class="math-radicand">{radicand}</span></span>',
            index,
        )
    if command in {"vec", "overrightarrow"}:
        inner, index = _parse_required_group(source, index)
        return f'<span class="math-vector"><span class="math-vector-arrow">→</span><span>{inner}</span></span>', index
    if command in {"bar", "overline"}:
        inner, index = _parse_required_group(source, index)
        return f'<span class="math-overline">{inner}</span>', index
    if command in {"mathrm", "text"}:
        return _parse_required_group(source, index)
    if command == "ce":
        formula, index = _read_raw_group(source, index)
        return _render_chemistry_content(formula), index
    if command in {"left", "right"}:
        return "", index
    if command == "begin":
        _, index = _read_raw_group(source, index)
        return "", index
    if command == "end":
        _, index = _read_raw_group(source, index)
        return "", index
    if command in {"\\", "newline"}:
        return "<br>", index
    if command in {",", ";", ":", "!", "quad", "qquad"}:
        return " ", index
    mapped = _LATEX_COMMANDS.get(command)
    if mapped is not None:
        suffix = " " if mapped.isascii() and mapped.isalpha() else ""
        return html.escape(mapped) + suffix, index
    if len(command) == 1:
        return html.escape(command), index
    return html.escape(command), index


def _read_latex_command(source: str, index: int) -> tuple[str, int]:
    if index < len(source) and source[index].isalpha():
        start = index
        while index < len(source) and source[index].isalpha():
            index += 1
        return source[start:index], index
    if index < len(source):
        return source[index], index + 1
    return "", index


def _parse_required_group(source: str, index: int) -> tuple[str, int]:
    index = _skip_latex_spaces(source, index)
    if index < len(source) and source[index] == "{":
        return _parse_latex(source, index + 1, "}")
    if index < len(source):
        return _parse_script_argument(source, index)
    return "", index


def _read_raw_group(source: str, index: int) -> tuple[str, int]:
    index = _skip_latex_spaces(source, index)
    if index >= len(source) or source[index] != "{":
        return "", index
    depth = 1
    start = index + 1
    index += 1
    while index < len(source) and depth:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    return source[start : index - 1], index


def _skip_latex_spaces(source: str, index: int) -> int:
    while index < len(source) and source[index].isspace():
        index += 1
    return index


def _render_chemistry_html(body: str) -> str:
    body = re.sub(
        r"(?<![A-Za-z])([A-Z][A-Za-z0-9₀-₉]*)([⁰¹²³⁴⁵⁶⁷⁸⁹]*[⁺⁻])",
        _unicode_ion_to_html,
        body,
    )
    body = re.sub(
        r"(?<![A-Za-z])([A-Z][A-Za-z0-9]*)(?:\^\{([0-9]*[+-])\}|\^([0-9]*[+-]))",
        _caret_ion_to_html,
        body,
    )
    body = re.sub(
        r"(?<![A-Za-z])([A-Z][a-z]?)([+-])(?=$|[^A-Za-z0-9])",
        _ascii_single_ion_to_html,
        body,
    )
    return body


def _render_chemistry_content(source: str) -> str:
    source = re.sub(r"_\{([0-9]+)\}", r"\1", source)
    source = re.sub(r"_([0-9]+)", r"\1", source)
    return _render_chemistry_html(html.escape(source))


def _unicode_ion_to_html(match: re.Match[str]) -> str:
    formula = _chemical_formula_to_html(match.group(1))
    charge = html.escape(match.group(2).translate(_SUPERSCRIPT_TO_ASCII))
    return f'<span class="chem-formula">{formula}<sup>{charge}</sup></span>'


def _caret_ion_to_html(match: re.Match[str]) -> str:
    formula = _chemical_formula_to_html(match.group(1))
    charge = html.escape(match.group(2) or match.group(3) or "")
    return f'<span class="chem-formula">{formula}<sup>{charge}</sup></span>'


def _ascii_single_ion_to_html(match: re.Match[str]) -> str:
    formula = _chemical_formula_to_html(match.group(1))
    charge = html.escape(match.group(2))
    return f'<span class="chem-formula">{formula}<sup>{charge}</sup></span>'


def _chemical_formula_to_html(formula: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(formula):
        ch = formula[i]
        if ch.isdigit() or ch in "₀₁₂₃₄₅₆₇₈₉":
            start = i
            while i < len(formula) and (formula[i].isdigit() or formula[i] in "₀₁₂₃₄₅₆₇₈₉"):
                i += 1
            subscript = formula[start:i].translate(_SUBSCRIPT_TO_ASCII)
            out.append(f"<sub>{html.escape(subscript)}</sub>")
            continue
        out.append(html.escape(ch))
        i += 1
    return "".join(out)


def _render_with_pillow(markdown_text: str) -> Image.Image:
    font = _load_font(16)
    lines = _wrap_text_to_width(_plain_text(markdown_text), font, max_width=PAGE_W - 8)
    line_height = 20
    height = max(PAGE_H, 16 + len(lines) * line_height)
    image = Image.new("L", (PAGE_W, height), 255)
    draw = ImageDraw.Draw(image)

    y = 8
    for line in lines:
        draw.text((4, y), line, fill=0, font=font)
        y += line_height
    return image


def _wrap_text_to_width(text: str, font: ImageFont.ImageFont, max_width: int) -> list[str]:
    lines: list[str] = []
    for raw_line in text.splitlines():
        raw_line = raw_line.strip()
        if not raw_line:
            lines.append("")
            continue
        current = ""
        for ch in raw_line:
            candidate = current + ch
            if current and _text_width(font, candidate) > max_width:
                lines.append(current)
                current = ch
            else:
                current = candidate
        if current:
            lines.append(current)
    return lines or [""]


def _text_width(font: ImageFont.ImageFont, text: str) -> float:
    if hasattr(font, "getlength"):
        return float(font.getlength(text))
    bbox = font.getbbox(text)
    return float(bbox[2] - bbox[0])


def _plain_text(markdown_text: str) -> str:
    markdown_text = _latex_to_plain_text(markdown_text)
    text = re.sub(r"`([^`]+)`", r"\1", markdown_text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"\1", text)
    text = re.sub(r"\*([^*]+)\*", r"\1", text)
    text = text.replace("$", "")
    return text.strip()


def _latex_to_plain_text(text: str) -> str:
    text = _normalize_chemistry_plain_text(text)
    previous = None
    while previous != text:
        previous = text
        text = re.sub(r"\\(?:dfrac|tfrac|frac)\{([^{}]+)\}\{([^{}]+)\}", r"\1/\2", text)
        text = re.sub(r"\\sqrt\{([^{}]+)\}", r"√(\1)", text)
        text = re.sub(r"\^\{([^{}]+)\}", r"^\1", text)
        text = re.sub(r"_\{([^{}]+)\}", r"_\1", text)

    text = text.replace("$$", "").replace("$", "")
    text = text.replace("\\(", "").replace("\\)", "")
    text = text.replace("\\[", "").replace("\\]", "")
    return re.sub(r"\\([A-Za-z]+|.)", _plain_latex_command, text)


def _normalize_chemistry_plain_text(text: str) -> str:
    text = re.sub(r"\\(?:mathrm|text)\{([^{}]+)\}", r"\1", text)
    text = re.sub(r"\\ce\{([A-Za-z0-9_]+)\^\{([0-9]*[+-])\}\}", r"\1\2", text)
    text = re.sub(r"\\ce\{([^{}]+)\}", r"\1", text)
    text = re.sub(r"_\{([0-9]+)\}", r"\1", text)
    text = re.sub(r"_([0-9]+)", r"\1", text)
    text = text.translate(_SUPERSCRIPT_TO_ASCII).translate(_SUBSCRIPT_TO_ASCII)
    text = re.sub(r"([A-Z][A-Za-z0-9]*)\^\{([0-9]*[+-])\}", r"\1\2", text)
    text = re.sub(r"([A-Z][A-Za-z0-9]*)\^([0-9]*[+-])", r"\1\2", text)
    return text


def _plain_latex_command(match: re.Match[str]) -> str:
    command = match.group(1)
    if command in {"left", "right"}:
        return ""
    if command in {",", ";", ":", "!", "quad", "qquad"}:
        return " "
    return _LATEX_COMMANDS.get(command, command)


def _wrap_text(text: str, max_chars: int) -> list[str]:
    lines: list[str] = []
    for raw_line in text.splitlines():
        raw_line = raw_line.strip()
        if not raw_line:
            lines.append("")
            continue
        while len(raw_line) > max_chars:
            lines.append(raw_line[:max_chars])
            raw_line = raw_line[max_chars:]
        lines.append(raw_line)
    return lines or [""]


def _load_font(size: int) -> ImageFont.ImageFont:
    for candidate in _font_candidates():
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size=size)
    return ImageFont.load_default()


def _font_candidates() -> tuple[Path, ...]:
    return (
        Path(__file__).parent / "fonts" / "NotoSansSC-Regular.otf",
        Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
        Path("/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf"),
        Path("/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf"),
        Path("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"),
        Path("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"),
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/simhei.ttf"),
        Path("C:/Windows/Fonts/simsun.ttc"),
    )


def _local_font_url() -> str:
    font_path = Path(__file__).parent / "fonts" / "NotoSansSC-Regular.otf"
    if not font_path.exists():
        return ""
    return font_path.resolve().as_uri()
