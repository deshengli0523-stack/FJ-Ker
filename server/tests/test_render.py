from app.render import (
    PAGE_BYTES,
    PAGE_H,
    PAGE_W,
    ROW_STRIDE,
    _font_candidates,
    _markdown_to_html,
    _render_html,
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


def test_render_answer_pages_outputs_8064_byte_pages():
    markdown = """
这是一个固定答案。

分数：$\\frac{1}{2}$

积分：$\\int_0^1 x^2 dx$
"""

    pages = render_answer_pages(markdown)

    assert len(pages) >= 1
    assert all(len(page) == PAGE_BYTES for page in pages)


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


def test_render_html_uses_local_math_when_browser_math_assets_are_missing():
    html = _render_html("分数：$\\frac{1}{2}$\n\n积分：$$\\int_0^1 x^2 dx$$")

    assert '<span class="math-fraction">' in html
    assert "\\(\\frac{1}{2}\\)" not in html
    assert "cdn.jsdelivr.net" not in html
    assert "window.__FJKER_MATH_DISABLED = true" in html


def test_markdown_to_html_can_delegate_latex_to_browser_math_engine():
    html = _markdown_to_html("fraction: $\\frac{1}{2}$\n\nintegral: $$\\int_0^1 x^2 dx$$")

    assert "\\(\\frac{1}{2}\\)" in html
    assert "\\[\\int_0^1 x^2 dx\\]" in html
    assert '<span class="math-fraction">' not in html


def test_render_html_auto_formats_common_physics_formulas():
    html = _render_html("速度：v_0=at，能量：E=mc^2，位移：s=v_0t+\\frac{1}{2}at^2。")

    assert "<sub>0</sub>" in html
    assert "<sup>2</sup>" in html
    assert '<span class="math-fraction">' in html


def test_render_html_auto_formats_scientific_notation_with_unicode_symbols():
    html = _render_html("原子数密度：3 × 6.022 × 10^23 / 55.9 × 10^-3 ≈ 8.47 × 10^28 m^-3。磁矩 μ：")

    assert "\\times" not in html
    assert "\\mu" not in html
    assert "<sup>23</sup>" in html
    assert "<sup>-3</sup>" in html
    assert "<sup>28</sup>" in html
    assert '<span class="math-fraction">' not in html


def test_render_html_formats_vector_physics_commands():
    html = _render_html("受力：$\\vec{F}=m\\vec{a}$")

    assert '<span class="math-vector">' in html
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

    assert "\\(\\mathrm{Na}^+\\)" not in html
    assert "\\(\\text{Cl}^-\\)" not in html
    assert "\\(\\ce{Ca^{2+}}\\)" not in html
    assert "\\(\\ce{Mg^2+}\\)" not in html
    assert "Ca<sup>2+</sup>" in html
    assert "Mg<sup>2+</sup>" in html
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
