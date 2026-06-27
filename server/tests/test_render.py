from app.render import (
    PAGE_BYTES,
    PAGE_H,
    PAGE_W,
    ROW_STRIDE,
    _chrome_launch_options,
    _font_candidates,
    _render_html,
    _render_with_playwright,
    _render_with_pillow,
    _slice_and_pack,
    pack_1bpp,
    _plain_text,
    render_answer_pages,
)


def test_page_geometry_is_landscape_screen():
    assert PAGE_W == 384
    assert PAGE_H == 168
    assert ROW_STRIDE == 48
    assert PAGE_BYTES == 8064


def test_pack_1bpp_black_pixel_at_origin_sets_msb():
    pixels = [[0 for _ in range(PAGE_W)] for _ in range(PAGE_H)]
    pixels[0][0] = 1

    packed = pack_1bpp(pixels)

    assert len(packed) == PAGE_BYTES
    assert packed[0] == 0x80
    assert all(byte == 0 for byte in packed[1:])


def test_pack_1bpp_uses_landscape_row_stride():
    pixels = [[0 for _ in range(PAGE_W)] for _ in range(PAGE_H)]
    pixels[1][383] = 1

    packed = pack_1bpp(pixels)

    assert packed[ROW_STRIDE + 47] == 0x01
    assert sum(1 for byte in packed if byte) == 1


def test_render_answer_pages_outputs_8064_byte_pages(monkeypatch):
    from PIL import Image

    monkeypatch.setattr(
        "app.render._render_with_playwright",
        lambda markdown_text: Image.new("L", (PAGE_W, PAGE_H), 255),
    )
    markdown = """
这是一个固定答案。

分数：$\\frac{1}{2}$

积分：$\\int_0^1 x^2 dx$
"""

    pages = render_answer_pages(markdown)

    assert len(pages) >= 1
    assert all(len(page) == PAGE_BYTES for page in pages)


def test_render_answer_pages_propagates_playwright_errors(monkeypatch):
    def fail_render(markdown_text: str):
        raise RuntimeError("latex failed")

    monkeypatch.setattr("app.render._render_with_playwright", fail_render)

    try:
        render_answer_pages("formula: $x^2$")
    except RuntimeError as exc:
        assert "latex failed" in str(exc)
    else:
        assert False, "render_answer_pages should not fall back when LaTeX rendering fails"


def _packed_row_has_black(page: bytes, y: int) -> bool:
    row = page[y * ROW_STRIDE : (y + 1) * ROW_STRIDE]
    return any(byte != 0 for byte in row)


def test_slice_and_pack_does_not_cut_visible_rows_at_page_break():
    from PIL import Image, ImageDraw

    image = Image.new("L", (PAGE_W, PAGE_H + 32), 255)
    draw = ImageDraw.Draw(image)
    draw.rectangle((10, PAGE_H - 4, 80, PAGE_H + 8), fill=0)

    pages = _slice_and_pack(image, max_pages=3)

    assert len(pages) == 2
    assert not any(_packed_row_has_black(pages[0], y) for y in range(PAGE_H - 6, PAGE_H))
    assert any(_packed_row_has_black(pages[1], y) for y in range(0, 16))


def test_render_html_delegates_latex_to_browser_math_engine():
    html = _render_html("分数：$\\frac{1}{2}$\n\n积分：$$\\int_0^1 x^2 dx$$")

    assert "window.MathJax" in html
    assert "\\(\\frac{1}{2}\\)" in html
    assert "\\[\\int_0^1 x^2 dx\\]" in html
    assert '<span class="math-fraction">' not in html


def test_render_html_marks_mathjax_timeout_as_disabled():
    html = _render_html("formula: $x^2$")

    assert "!window.__FJKER_MATH_READY" in html
    assert "MathJax did not finish rendering within 12000ms" in html
    assert "window.__FJKER_MATH_DISABLED = true" in html


def test_render_html_uses_local_mathjax_asset(monkeypatch):
    import app.render as render_module

    monkeypatch.setattr(
        render_module,
        "_optional_asset_uri",
        lambda path: "file:///opt/fj-ker/server/app/templates/mathjax/tex-svg.js"
        if path.name == "tex-svg.js"
        else "",
    )

    html = render_module._render_html("formula: $x^2$")

    assert 'src="file:///opt/fj-ker/server/app/templates/mathjax/tex-svg.js"' in html
    assert "cdn.jsdelivr.net" not in html
    assert "mhchem" not in html


def test_render_html_fails_fast_when_local_mathjax_missing(monkeypatch):
    import app.render as render_module

    monkeypatch.setattr(render_module, "_optional_asset_uri", lambda path: "")

    html = render_module._render_html("formula: $x^2$")

    assert "Local MathJax asset missing" in html
    assert "window.__FJKER_MATH_DISABLED = true" in html
    assert "cdn.jsdelivr.net" not in html


def test_render_with_playwright_opens_file_page_not_networkidle(monkeypatch):
    import sys
    import types

    from PIL import Image

    calls = {}

    class FakePage:
        def goto(self, url, wait_until=None, timeout=None):
            calls["url"] = url
            calls["wait_until"] = wait_until
            calls["timeout"] = timeout

        def wait_for_function(self, expression, timeout=None):
            calls["wait_for_function"] = expression
            calls["wait_timeout"] = timeout

        def evaluate(self, expression):
            if "__FJKER_MATH_DISABLED" in expression:
                return False
            if "scrollHeight" in expression:
                return PAGE_H
            return ""

        def set_viewport_size(self, size):
            calls["viewport"] = size

        def screenshot(self, path, full_page=False):
            calls["full_page"] = full_page
            Image.new("L", (PAGE_W, PAGE_H), 255).save(path)

    class FakeBrowser:
        def new_page(self, viewport):
            calls["initial_viewport"] = viewport
            return FakePage()

        def close(self):
            calls["closed"] = True

    class FakeChromium:
        def launch(self, **options):
            calls["launch_options"] = options
            return FakeBrowser()

    class FakePlaywright:
        chromium = FakeChromium()

    class FakeContext:
        def __enter__(self):
            return FakePlaywright()

        def __exit__(self, exc_type, exc, traceback):
            return False

    def fake_sync_playwright():
        return FakeContext()

    monkeypatch.setitem(sys.modules, "playwright", types.SimpleNamespace())
    monkeypatch.setitem(
        sys.modules,
        "playwright.sync_api",
        types.SimpleNamespace(sync_playwright=fake_sync_playwright),
    )

    image = _render_with_playwright("formula: $x^2$")

    assert image.size == (PAGE_W, PAGE_H)
    assert calls["url"].startswith("file:///")
    assert calls["wait_until"] == "domcontentloaded"
    assert calls["timeout"] == 15000
    assert calls["wait_timeout"] == 20000
    assert calls["closed"] is True


def test_chrome_launch_options_use_google_chrome_executable(monkeypatch):
    monkeypatch.setenv("FJKER_CHROME_EXECUTABLE", "/usr/bin/google-chrome-stable")

    options = _chrome_launch_options()

    assert options["headless"] is True
    assert options["executable_path"] == "/usr/bin/google-chrome-stable"
    assert "channel" not in options


def test_chrome_launch_options_use_writable_runtime_dirs(tmp_path, monkeypatch):
    monkeypatch.setenv("FJKER_CHROME_RUNTIME_DIR", str(tmp_path))

    options = _chrome_launch_options()

    env = options["env"]
    assert env["HOME"] == str(tmp_path / "home")
    assert env["XDG_CONFIG_HOME"] == str(tmp_path / "config")
    assert env["XDG_CACHE_HOME"] == str(tmp_path / "cache")
    assert env["XDG_DATA_HOME"] == str(tmp_path / "data")
    assert (tmp_path / "data" / "applications").is_dir()
    assert (tmp_path / "crashpad").is_dir()
    assert f"--crash-dumps-dir={tmp_path / 'crashpad'}" in options["args"]


def test_render_html_auto_formats_common_physics_formulas():
    html = _render_html("速度：v_0=at，能量：E=mc^2，位移：s=v_0t+\\frac{1}{2}at^2。")

    assert "\\(v_0=at\\)" in html
    assert "\\(E=mc^2\\)" in html
    assert "\\(s=v_0t+\\frac{1}{2}at^2\\)" in html
    assert '<span class="math-fraction">' not in html


def test_render_html_auto_formats_scientific_notation_with_unicode_symbols():
    html = _render_html("原子数密度：3 × 6.022 × 10^23 / 55.9 × 10^-3 ≈ 8.47 × 10^28 m^-3。磁矩 μ：")

    assert "\\(3 \\times 6.022 \\times 10^23 / 55.9 \\times 10^-3 \\approx 8.47 \\times 10^28 m^-3\\)" in html
    assert "\\(\\mu\\)" in html
    assert "×" not in html
    assert "≈" not in html
    assert "μ" not in html
    assert '<span class="math-fraction">' not in html


def test_render_html_formats_vector_physics_commands():
    html = _render_html("受力：$\\vec{F}=m\\vec{a}$")

    assert "\\(\\vec{F}=m\\vec{a}\\)" in html
    assert '<span class="math-vector">' not in html
    assert "vecF" not in html


def test_pillow_fallback_plain_text_simplifies_latex():
    text = _plain_text("分数：$\\frac{1}{2}$，根号：$\\sqrt{x^2+1}$")

    assert "\\frac" not in text
    assert "\\sqrt" not in text
    assert "1/2" in text
    assert "√" in text


def test_font_candidates_include_ubuntu_cjk_fonts():
    candidates = {str(path).replace("\\", "/") for path in _font_candidates()}

    assert "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc" in candidates


def test_pillow_fallback_uses_full_page_width():
    image = _render_with_pillow(
        "This fallback line should use the whole 384 pixel display width instead of wrapping after 12 chars."
    )
    gray = image.convert("L")

    right_half_has_ink = any(
        gray.getpixel((x, y)) < 128
        for y in range(min(PAGE_H, gray.height))
        for x in range(PAGE_W // 2, PAGE_W - 4)
    )

    assert right_half_has_ink


def test_render_html_normalizes_ion_charge_symbols():
    html = _render_html(
        "矿泉水中 K⁺、Na⁺、Cl⁻、Na+、Cl-、Na^+、Cl^- 较多，"
        "$\\mathrm{Na}^+$、$\\text{Cl}^-$、$\\ce{Ca^{2+}}$、$\\ce{Mg^2+}$ 也可能存在。"
    )

    assert "\\(\\mathrm{Na}^+\\)" in html
    assert "\\(\\text{Cl}^-\\)" in html
    assert "\\(\\ce{Ca^{2+}}\\)" in html
    assert "\\(\\ce{Mg^2+}\\)" in html
    assert "chem-formula" in html
    assert ".chem-formula sup" in html
    assert "K<sup>+</sup>" in html
    assert "Na<sup>+</sup>" in html
    assert "Cl<sup>-</sup>" in html
    assert "⁺" not in html
    assert "⁻" not in html


def test_plain_text_normalizes_ion_charge_symbols():
    text = _plain_text(
        "矿泉水中 K⁺、Na⁺、Cl⁻、Ca^{2+}、Mg^2+、$\\mathrm{Na}^+$、"
        "$\\text{Cl}^-$、$\\ce{Ca^{2+}}$ 和 $\\ce{Mg^2+}$。"
    )

    assert "K+" in text
    assert "Na+" in text
    assert "Cl-" in text
    assert "Ca2+" in text
    assert "Mg2+" in text
    assert "mathrm" not in text
    assert "ce" not in text
    assert "⁺" not in text
    assert "⁻" not in text
