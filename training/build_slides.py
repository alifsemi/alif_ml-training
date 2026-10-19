"""Build the training PowerPoint from the Markdown slide source.

Usage:
    python build_slides.py [alif_ml_training.md] [-o alif_ml_training.pptx]

The Markdown file is the single source of truth, so the .md and the .pptx stay in sync.
Slide syntax (see the header of alif_ml_training.md):
    ---                       slide separator
    <!-- layout: handson; time: 0:25-0:45 -->   optional, first line of a slide
    # Title                   slide title
    ## Subtitle               subtitle (title and section slides) or sub-heading
    - bullet / 1. numbered    lists, two spaces of indent per level
    | a | b |                 tables, the first row is the header
    ```lang                   code blocks
    ![caption](path.png)      images, path relative to the Markdown file
    > text                    callout box
    ::: notes ... :::         speaker notes
Requires python-pptx (pip install python-pptx).
"""
import argparse
import math
import os
import re
import sys

from PIL import Image
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

# Theme
NAVY = RGBColor(0x0B, 0x1F, 0x3A)
BLUE = RGBColor(0x00, 0x67, 0xB9)
TEAL = RGBColor(0x00, 0xA3, 0xAD)
GREEN = RGBColor(0x2E, 0x9E, 0x5B)
TEXT = RGBColor(0x26, 0x32, 0x40)
MUTED = RGBColor(0x6B, 0x77, 0x85)
LIGHT = RGBColor(0xF2, 0xF4, 0xF7)
ROW_ALT = RGBColor(0xE8, 0xEE, 0xF5)
CODE_BG = RGBColor(0xF4, 0xF5, 0xF7)
CODE_BORDER = RGBColor(0xD0, 0xD5, 0xDD)
CALLOUT_BG = RGBColor(0xE3, 0xF2, 0xFB)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)

FONT = "Calibri"
MONO = "Consolas"
FOOTER = "Alif Semiconductor · Analog Devices  |  Edge AI hands-on training"

SLIDE_W = 13.333
SLIDE_H = 7.5
MARGIN = 0.55
CONTENT_TOP = 1.35
CONTENT_BOTTOM = 6.85
BLOCK_GAP = 0.14
CHAR_W = 0.47  # average character width relative to the font size, Calibri


# --------------------------------------------------------------------------------------
# Markdown parsing
# --------------------------------------------------------------------------------------

def split_slides(md):
    """Split on '---' lines outside code fences, dropping the file header comment."""
    slides, current, in_code = [], [], False
    for line in md.splitlines():
        if line.startswith("```"):
            in_code = not in_code
        if line.strip() == "---" and not in_code:
            slides.append(current)
            current = []
        else:
            current.append(line)
    slides.append(current)
    return [s for s in slides if any(l.strip() for l in s)]


def parse_slide(lines):
    slide = {"layout": "default", "time": None, "title": "", "subtitle": None,
             "blocks": [], "notes": ""}
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        stripped = line.strip()

        if stripped.startswith("<!--"):
            comment = [stripped]
            while not comment[-1].endswith("-->") and i + 1 < n:
                i += 1
                comment.append(lines[i].strip())
            text = " ".join(comment)[4:-3].strip()
            if text.startswith("layout:") or text.startswith("time:"):
                for part in text.split(";"):
                    key, _, value = part.partition(":")
                    if key.strip() in ("layout", "time"):
                        slide[key.strip()] = value.strip()
            i += 1
            continue

        if stripped.startswith("```"):
            code = []
            i += 1
            while i < n and not lines[i].strip().startswith("```"):
                code.append(lines[i])
                i += 1
            slide["blocks"].append(("code", "\n".join(code)))
            i += 1
            continue

        if stripped.startswith("::: notes"):
            notes = []
            i += 1
            while i < n and lines[i].strip() != ":::":
                notes.append(lines[i])
                i += 1
            slide["notes"] = "\n".join(notes).strip()
            i += 1
            continue

        if stripped.startswith("# "):
            slide["title"] = stripped[2:].strip()
            i += 1
            continue

        if stripped.startswith("## "):
            if slide["layout"] in ("title", "section") and slide["subtitle"] is None:
                slide["subtitle"] = stripped[3:].strip()
            else:
                slide["blocks"].append(("subhead", stripped[3:].strip()))
            i += 1
            continue

        if stripped.startswith("|"):
            rows = []
            while i < n and lines[i].strip().startswith("|"):
                cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-{2,}:?", c) for c in cells if c):
                    rows.append(cells)
                i += 1
            slide["blocks"].append(("table", rows))
            continue

        m = re.fullmatch(r"!\[([^\]]*)\]\(([^)]+)\)", stripped)
        if m:
            slide["blocks"].append(("image", m.group(1), m.group(2)))
            i += 1
            continue

        if re.match(r"\s*([-*]|\d+\.)\s+", line):
            items = []
            while i < n and re.match(r"\s*([-*]|\d+\.)\s+", lines[i]):
                m = re.match(r"(\s*)([-*]|\d+\.)\s+(.*)", lines[i])
                level = min(len(m.group(1)) // 2, 2)
                number = int(m.group(2)[:-1]) if m.group(2)[0].isdigit() else None
                items.append((level, m.group(3).strip(), number))
                i += 1
            slide["blocks"].append(("list", items))
            continue

        if stripped.startswith(">"):
            quote = []
            while i < n and lines[i].strip().startswith(">"):
                quote.append(lines[i].strip()[1:].strip())
                i += 1
            slide["blocks"].append(("callout", " ".join(quote)))
            continue

        if stripped:
            para = [stripped]
            i += 1
            while i < n and lines[i].strip() and not re.match(r"\s*([-*]|\d+\.|#|\||!\[|>|```|:::|<!--)", lines[i]):
                para.append(lines[i].strip())
                i += 1
            slide["blocks"].append(("para", " ".join(para)))
            continue

        i += 1
    return slide


INLINE = re.compile(r"(\*\*[^*]+\*\*|`[^`]+`|\[[^\]]+\]\([^)]+\)|(?<![\w*])\*[^*\s][^*]*\*(?![\w*]))")


def inline_runs(text):
    """Split inline Markdown into (text, style, url) runs."""
    runs = []
    for part in INLINE.split(text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            runs.append((part[2:-2], "bold", None))
        elif part.startswith("`") and part.endswith("`"):
            runs.append((part[1:-1], "code", None))
        elif len(part) > 2 and part.startswith("*") and part.endswith("*"):
            runs.append((part[1:-1], "italic", None))
        elif re.fullmatch(r"\[([^\]]+)\]\(([^)]+)\)", part):
            m = re.fullmatch(r"\[([^\]]+)\]\(([^)]+)\)", part)
            runs.append((m.group(1), "link", m.group(2)))
        else:
            runs.append((part, None, None))
    return runs


def plain(text):
    return "".join(r[0] for r in inline_runs(text))


def block_chars(block):
    kind = block[0]
    if kind == "list":
        return sum(len(plain(t)) for _, t, _ in block[1])
    if kind == "table":
        return sum(len(plain(c)) for row in block[1] for c in row)
    if kind in ("para", "subhead", "callout", "code"):
        return len(plain(block[1]))
    return 0


# --------------------------------------------------------------------------------------
# Size estimation
# --------------------------------------------------------------------------------------

NARROW = set("iljtfrI.,:;'|!()[]{} ")


def char_em(ch, style):
    """Approximate glyph advance in em: Calibri for text, Consolas for code."""
    if style == "code":
        return 0.55 * 0.92
    if ch in NARROW:
        em = 0.26
    elif ch.isupper() or ch in "mwMW%&@_":
        em = 0.60
    elif ch.isdigit():
        em = 0.51
    else:
        em = 0.47
    return em * (1.05 if style == "bold" else 1.0)


def text_em(text):
    return sum(char_em(ch, style) for value, style, _ in inline_runs(text) for ch in value)


def text_width_chars(text):
    """Text width expressed in average characters (CHAR_W em each)."""
    return text_em(text) / CHAR_W


def text_lines(text, width_in, font_pt):
    width_needed = text_em(text) * font_pt / 72
    if width_needed <= width_in * 0.97:
        return 1
    return max(1, math.ceil(width_needed * 1.1 / width_in))


def list_height(items, width_in, font_pt):
    total = 0.0
    for level, text, _ in items:
        size = font_pt if level == 0 else font_pt * 0.9
        indent = 0.3 + 0.35 * level
        total += text_lines(text, width_in - indent, size) * size * 1.2 / 72 + size * 0.35 / 72
    return total + 0.1


def para_height(text, width_in, font_pt):
    return text_lines(text, width_in, font_pt) * font_pt * 1.2 / 72 + 0.12


def code_font(code, width_in, base_pt):
    longest = max((len(l) for l in code.splitlines()), default=1)
    fit = width_in * 72 / (max(longest, 1) * 0.55) - 0.5
    return max(8.0, min(base_pt * 0.72, 14.0, fit))


def code_height(code, font_pt):
    return len(code.splitlines()) * font_pt * 1.18 / 72 + 0.3


def table_col_widths(rows, width_in, font_pt):
    """Column widths: at least the longest unbreakable token, the rest by content length."""
    cols = max(len(r) for r in rows)
    minimum, weights = [], []
    for c in range(cols):
        cells = [r[c] if c < len(r) else "" for r in rows]
        longest_token = 0.0
        for cell in cells:
            for value, style, _ in inline_runs(cell):
                for token in value.split():
                    longest_token = max(longest_token, sum(char_em(ch, style) for ch in token))
        minimum.append(longest_token * font_pt / 72 + 0.18)
        weights.append(min(max(max(text_width_chars(x) for x in cells), 4), 60) ** 0.8)
    if sum(minimum) >= width_in:
        return [width_in * m / sum(minimum) for m in minimum]
    spare = width_in - sum(minimum)
    return [m + spare * w / sum(weights) for m, w in zip(minimum, weights)]


def table_font(base_pt):
    return max(10.0, min(base_pt * 0.8, 18.0))


def table_height(rows, width_in, font_pt):
    widths = table_col_widths(rows, width_in, font_pt)
    total = 0.0
    for r in rows:
        lines = 1
        for c, cell in enumerate(r):
            if c < len(widths):
                lines = max(lines, text_lines(cell, widths[c] - 0.14, font_pt))
        total += lines * font_pt * 1.25 / 72 + 0.12
    return total


def block_height(block, width_in, base_pt):
    kind = block[0]
    if kind == "list":
        return list_height(block[1], width_in, base_pt)
    if kind == "para":
        return para_height(block[1], width_in, base_pt)
    if kind == "subhead":
        return para_height(block[1], width_in, base_pt * 1.05)
    if kind == "callout":
        return para_height(block[1], width_in - 0.4, base_pt) + 0.25
    if kind == "code":
        return code_height(block[1], code_font(block[1], width_in - 0.3, base_pt))
    if kind == "table":
        return table_height(block[1], width_in, table_font(base_pt))
    return 0.0


def fit_font(blocks, width_in, height_in, start=22.0, minimum=10.0):
    size = start
    while size > minimum:
        total = sum(block_height(b, width_in, size) for b in blocks) + BLOCK_GAP * (len(blocks) - 1)
        if total <= height_in:
            return size, total
        size -= 0.5
    total = sum(block_height(b, width_in, minimum) for b in blocks) + BLOCK_GAP * (len(blocks) - 1)
    return minimum, total


# --------------------------------------------------------------------------------------
# Rendering helpers
# --------------------------------------------------------------------------------------

def set_bullet(paragraph, level, number=None):
    pPr = paragraph._p.get_or_add_pPr()
    indent = Emu(Inches(0.28))
    pPr.set("marL", str(int(Inches(0.3 + 0.35 * level))))
    pPr.set("indent", str(-int(indent)))
    for tag in ("a:buNone", "a:buChar", "a:buAutoNum"):
        for el in pPr.findall(qn(tag)):
            pPr.remove(el)
    font = pPr.makeelement(qn("a:buFont"), {"typeface": FONT})
    pPr.append(font)
    if number is not None:
        bu = pPr.makeelement(qn("a:buAutoNum"), {"type": "arabicPeriod", "startAt": str(number)})
    else:
        bu = pPr.makeelement(qn("a:buChar"), {"char": "•" if level == 0 else "–"})
    pPr.append(bu)


def add_runs(paragraph, text, size, color=TEXT, bold=False, link_color=BLUE):
    for value, style, url in inline_runs(text):
        run = paragraph.add_run()
        run.text = value
        f = run.font
        f.size = Pt(size)
        f.name = FONT
        f.bold = bold or style == "bold"
        f.italic = style == "italic"
        f.color.rgb = color
        if style == "code":
            f.name = MONO
            f.size = Pt(size * 0.92)
            f.color.rgb = NAVY if color == TEXT else color
        elif style == "link":
            run.hyperlink.address = url
            f.color.rgb = link_color
            f.underline = True


def new_textbox(slide, x, y, w, h):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = Inches(0.02)
    tf.margin_top = tf.margin_bottom = Inches(0.02)
    return box, tf


def render_list(slide, items, x, y, w, size):
    h = list_height(items, w, size)
    _, tf = new_textbox(slide, x, y, w, h)
    for idx, (level, text, number) in enumerate(items):
        p = tf.paragraphs[0] if idx == 0 else tf.add_paragraph()
        item_size = size if level == 0 else size * 0.9
        set_bullet(p, level, number)
        p.space_after = Pt(item_size * 0.35)
        p.line_spacing = 1.05
        add_runs(p, text, item_size)
    return h


def render_para(slide, text, x, y, w, size, color=TEXT, bold=False):
    h = para_height(text, w, size)
    _, tf = new_textbox(slide, x, y, w, h)
    p = tf.paragraphs[0]
    add_runs(p, text, size, color=color, bold=bold)
    return h


def render_callout(slide, text, x, y, w, size):
    h = para_height(text, w - 0.4, size) + 0.25
    shape = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    shape.fill.solid()
    shape.fill.fore_color.rgb = CALLOUT_BG
    shape.line.fill.background()
    bar = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(0.08), Inches(h))
    bar.fill.solid()
    bar.fill.fore_color.rgb = BLUE
    bar.line.fill.background()
    tf = shape.text_frame
    tf.word_wrap = True
    tf.margin_left = Inches(0.25)
    tf.margin_right = Inches(0.15)
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.LEFT
    add_runs(p, text, size, color=NAVY, bold=True)
    return h


def render_code(slide, code, x, y, w, base):
    size = code_font(code, w - 0.3, base)
    h = code_height(code, size)
    shape = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    shape.fill.solid()
    shape.fill.fore_color.rgb = CODE_BG
    shape.line.color.rgb = CODE_BORDER
    shape.line.width = Pt(0.75)
    shape.shadow.inherit = False
    tf = shape.text_frame
    tf.word_wrap = False
    tf.margin_left = tf.margin_right = Inches(0.15)
    tf.margin_top = tf.margin_bottom = Inches(0.1)
    tf.vertical_anchor = MSO_ANCHOR.TOP
    for idx, line in enumerate(code.splitlines()):
        p = tf.paragraphs[0] if idx == 0 else tf.add_paragraph()
        p.alignment = PP_ALIGN.LEFT
        p.line_spacing = 1.0
        run = p.add_run()
        run.text = line if line else " "
        run.font.name = MONO
        run.font.size = Pt(size)
        run.font.color.rgb = TEXT
    return h


def render_table(slide, rows, x, y, w, base):
    size = table_font(base)
    widths = table_col_widths(rows, w, size)
    cols = len(widths)
    h = table_height(rows, w, size)
    gshape = slide.shapes.add_table(len(rows), cols, Inches(x), Inches(y), Inches(w), Inches(h))
    table = gshape.table
    tbl_pr = gshape._element.graphic.graphicData.tbl.tblPr
    style = tbl_pr.find(qn("a:tableStyleId"))
    if style is not None:
        style.text = "{5940675A-B579-460E-94D1-54222C63F5DA}"  # No Style, Table Grid
    for c, cw in enumerate(widths):
        table.columns[c].width = Inches(cw)
    for r, row in enumerate(rows):
        lines = max([text_lines(row[c], widths[c] - 0.14, size) for c in range(min(cols, len(row)))] or [1])
        table.rows[r].height = Inches(lines * size * 1.2 / 72 + 0.1)
        for c in range(cols):
            cell = table.cell(r, c)
            cell.margin_left = cell.margin_right = Inches(0.06)
            cell.margin_top = cell.margin_bottom = Inches(0.03)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            cell.fill.solid()
            if r == 0:
                cell.fill.fore_color.rgb = NAVY
            else:
                cell.fill.fore_color.rgb = ROW_ALT if r % 2 == 0 else WHITE
            tf = cell.text_frame
            tf.word_wrap = True
            p = tf.paragraphs[0]
            text = row[c] if c < len(row) else ""
            if r == 0:
                add_runs(p, text, size, color=WHITE, bold=True, link_color=WHITE)
            else:
                add_runs(p, text, size)
    return h


def render_image(slide, path, caption, x, y, w, h):
    with Image.open(path) as img:
        iw, ih = img.size
    cap_h = 0.3 if caption else 0.0
    avail_h = h - cap_h
    scale = min(w / iw, avail_h / ih)
    pw, ph = iw * scale, ih * scale
    px = x + (w - pw) / 2
    py = y + (avail_h - ph) / 2
    pic = slide.shapes.add_picture(path, Inches(px), Inches(py), Inches(pw), Inches(ph))
    pic.line.color.rgb = CODE_BORDER
    pic.line.width = Pt(0.75)
    if caption:
        _, tf = new_textbox(slide, x, py + ph + 0.04, w, 0.28)
        p = tf.paragraphs[0]
        p.alignment = PP_ALIGN.CENTER
        run = p.add_run()
        run.text = caption
        run.font.size = Pt(11)
        run.font.italic = True
        run.font.name = FONT
        run.font.color.rgb = MUTED


def render_blocks(slide, blocks, x, y, w, h, start=22.0):
    size, total = fit_font(blocks, w, h, start=start)
    if total > h + 0.05:
        print(f"  warning: content overflows by {total - h:.2f} in at {size:.1f} pt")
    cy = y
    for block in blocks:
        kind = block[0]
        if kind == "list":
            cy += render_list(slide, block[1], x, cy, w, size)
        elif kind == "para":
            cy += render_para(slide, block[1], x, cy, w, size)
        elif kind == "subhead":
            cy += render_para(slide, block[1], x, cy, w, size * 1.05, color=BLUE, bold=True)
        elif kind == "callout":
            cy += render_callout(slide, block[1], x, cy, w, size)
        elif kind == "code":
            cy += render_code(slide, block[1], x, cy, w, size)
        elif kind == "table":
            cy += render_table(slide, block[1], x, cy, w, size)
        cy += BLOCK_GAP
    return size


def fill_background(slide, color):
    bg = slide.background.fill
    bg.solid()
    bg.fore_color.rgb = color


def add_footer(slide, number):
    _, tf = new_textbox(slide, MARGIN, 7.05, 9.0, 0.3)
    run = tf.paragraphs[0].add_run()
    run.text = FOOTER
    run.font.size = Pt(10)
    run.font.name = FONT
    run.font.color.rgb = MUTED
    _, tf = new_textbox(slide, SLIDE_W - MARGIN - 1.0, 7.05, 1.0, 0.3)
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.RIGHT
    run = p.add_run()
    run.text = str(number)
    run.font.size = Pt(10)
    run.font.name = FONT
    run.font.color.rgb = MUTED


def add_title(slide, title):
    _, tf = new_textbox(slide, MARGIN, 0.35, SLIDE_W - 2 * MARGIN - 2.6, 0.8)
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    size = 30 if len(title) <= 48 else 26
    add_runs(p, title, size, color=NAVY, bold=True)
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(MARGIN), Inches(1.15), Inches(1.2), Inches(0.06))
    line.fill.solid()
    line.fill.fore_color.rgb = TEAL
    line.line.fill.background()


def add_handson_badge(slide, time):
    label = "HANDS-ON" + (f"  ·  {time}" if time else "")
    w = 1.5 if not time else 2.6
    badge = slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, Inches(SLIDE_W - MARGIN - w), Inches(0.45),
                                   Inches(w), Inches(0.45))
    badge.fill.solid()
    badge.fill.fore_color.rgb = GREEN
    badge.line.fill.background()
    tf = badge.text_frame
    tf.margin_left = tf.margin_right = Inches(0.05)
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    run = p.add_run()
    run.text = label
    run.font.size = Pt(13)
    run.font.bold = True
    run.font.name = FONT
    run.font.color.rgb = WHITE


# --------------------------------------------------------------------------------------
# Slide builders
# --------------------------------------------------------------------------------------

def build_title_slide(slide, s):
    fill_background(slide, NAVY)
    band = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(5.6), Inches(SLIDE_W), Inches(0.08))
    band.fill.solid()
    band.fill.fore_color.rgb = TEAL
    band.line.fill.background()
    _, tf = new_textbox(slide, 0.9, 1.8, SLIDE_W - 1.8, 1.3)
    add_runs(tf.paragraphs[0], s["title"], 48, color=WHITE, bold=True)
    if s["subtitle"]:
        _, tf = new_textbox(slide, 0.9, 3.1, SLIDE_W - 1.8, 1.0)
        add_runs(tf.paragraphs[0], s["subtitle"], 26, color=RGBColor(0xBF, 0xD9, 0xF2))
    paras = [b[1] for b in s["blocks"] if b[0] == "para"]
    if paras:
        _, tf = new_textbox(slide, 0.9, 4.5, SLIDE_W - 1.8, 0.8)
        add_runs(tf.paragraphs[0], paras[0], 18, color=RGBColor(0xD0, 0xD8, 0xE2))
    _, tf = new_textbox(slide, 0.9, 6.0, SLIDE_W - 1.8, 0.5)
    add_runs(tf.paragraphs[0], "Alif Semiconductor · Analog Devices", 16, color=WHITE, bold=True)


def build_section_slide(slide, s, number):
    fill_background(slide, BLUE)
    bar = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.9), Inches(2.55), Inches(0.12), Inches(2.0))
    bar.fill.solid()
    bar.fill.fore_color.rgb = TEAL
    bar.line.fill.background()
    _, tf = new_textbox(slide, 1.3, 2.4, SLIDE_W - 2.2, 1.2)
    add_runs(tf.paragraphs[0], s["title"], 44, color=WHITE, bold=True)
    if s["subtitle"]:
        _, tf = new_textbox(slide, 1.3, 3.6, SLIDE_W - 2.2, 0.9)
        add_runs(tf.paragraphs[0], s["subtitle"], 24, color=RGBColor(0xDC, 0xEB, 0xF7))
    _, tf = new_textbox(slide, SLIDE_W - MARGIN - 1.0, 7.05, 1.0, 0.3)
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.RIGHT
    run = p.add_run()
    run.text = str(number)
    run.font.size = Pt(10)
    run.font.color.rgb = WHITE


def build_content_slide(slide, s, number, base_dir):
    add_title(slide, s["title"])
    if s["layout"] == "handson":
        add_handson_badge(slide, s["time"])
    add_footer(slide, number)

    images = [b for b in s["blocks"] if b[0] == "image"]
    others = [b for b in s["blocks"] if b[0] != "image"]
    x, y = MARGIN, CONTENT_TOP
    w, h = SLIDE_W - 2 * MARGIN, CONTENT_BOTTOM - CONTENT_TOP

    def img_path(b):
        return os.path.normpath(os.path.join(base_dir, b[2]))

    if images and not others:
        if len(images) == 1:
            render_image(slide, img_path(images[0]), images[0][1], x, y, w, h)
        else:
            gap = 0.3
            iw = (w - gap * (len(images) - 1)) / len(images)
            for k, b in enumerate(images):
                render_image(slide, img_path(b), b[1], x + k * (iw + gap), y, iw, h)
    elif images:
        chars = sum(block_chars(b) for b in others)
        text_w = 3.6 if chars < 160 else 4.6 if chars < 300 else 5.8
        gap = 0.35
        render_blocks(slide, others, x, y, text_w, h, start=20.0)
        ix = x + text_w + gap
        iw = w - text_w - gap
        if len(images) == 1:
            render_image(slide, img_path(images[0]), images[0][1], ix, y, iw, h)
        else:
            ih = (h - 0.2) / len(images)
            for k, b in enumerate(images):
                render_image(slide, img_path(b), b[1], ix, y + k * (ih + 0.2), iw, ih)
    else:
        render_blocks(slide, others, x, y, w, h)

    if s["notes"]:
        slide.notes_slide.notes_text_frame.text = s["notes"]


def build(md_path, out_path):
    with open(md_path, encoding="utf-8") as f:
        md = f.read()
    md = re.sub(r"\A\s*<!--(?!\s*(layout|time):).*?-->", "", md, flags=re.S)
    slides = [parse_slide(s) for s in split_slides(md)]
    base_dir = os.path.dirname(os.path.abspath(md_path))

    prs = Presentation()
    prs.slide_width = Inches(SLIDE_W)
    prs.slide_height = Inches(SLIDE_H)
    blank = prs.slide_layouts[6]

    for number, s in enumerate(slides, start=1):
        print(f"[{number:2d}] {s['layout']:8s} {s['title']}")
        slide = prs.slides.add_slide(blank)
        if s["layout"] == "title":
            build_title_slide(slide, s)
            if s["notes"]:
                slide.notes_slide.notes_text_frame.text = s["notes"]
        elif s["layout"] == "section":
            build_section_slide(slide, s, number)
        else:
            build_content_slide(slide, s, number, base_dir)

    prs.save(out_path)
    print(f"Wrote {out_path} ({len(slides)} slides)")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("markdown", nargs="?", default=os.path.join(here, "alif_ml_training.md"))
    parser.add_argument("-o", "--output", default=None)
    args = parser.parse_args()
    out = args.output or os.path.splitext(args.markdown)[0] + ".pptx"
    build(args.markdown, out)


if __name__ == "__main__":
    sys.exit(main())
