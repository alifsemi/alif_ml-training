"""Build the training PowerPoint from the Markdown slide source.

Usage:
    python build_slides.py [alif_ml_training.md] [-o alif_ml_training.pptx] [--template adi_template.pptx]

The Markdown file is the single source of truth, so the .md and the .pptx stay in sync.
The masters, layouts and embedded Barlow fonts come from the Analog Devices template
(adi_template.pptx); any slides already in the template are dropped.
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
import copy
import math
import os
import re
import sys

from PIL import Image
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE, PP_PLACEHOLDER
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

# Theme: the Analog Devices palette from the template's color scheme
NAVY = RGBColor(0x00, 0x32, 0x5C)
BLUE = RGBColor(0x00, 0x67, 0xB9)
SKY = RGBColor(0x1B, 0x9C, 0xD0)
GREEN = RGBColor(0x17, 0x99, 0x63)
TEXT = RGBColor(0x00, 0x00, 0x00)
MUTED = RGBColor(0x3C, 0x41, 0x57)
CODE_BG = RGBColor(0xF4, 0xF5, 0xF7)
CODE_BORDER = RGBColor(0xD0, 0xD5, 0xDD)
CALLOUT_BG = RGBColor(0xE4, 0xF3, 0xFA)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)

FONT = "Barlow"
TITLE_FONT = "Barlow Medium"
MONO = "Consolas"
TABLE_STYLE = "{21E4AEA4-8DFA-4A89-87EB-49C32662AFE0}"  # Medium Style 2 - Accent 2

LAYOUT_COVER = "Cover with Pattern"
LAYOUT_SECTION = "Section Slide with Pattern"
LAYOUT_CONTENT = "Title Only – Blank"

SLIDE_W = 13.333
SLIDE_H = 7.5
MARGIN = 0.46
CONTENT_TOP = 1.45
CONTENT_BOTTOM = 7.0
BLOCK_GAP = 0.14
CHAR_W = 0.47  # average character width relative to the font size
FONT_SCALE = 1.06  # Barlow against the Calibri-based estimates in char_em
TITLE_SCALE = 1.04  # Barlow Medium against Barlow


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
    """Approximate glyph advance in em: Barlow for text, Consolas for code."""
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
    return em * FONT_SCALE * (1.05 if style == "bold" else 1.0)


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
    indent = Emu(Inches(0.28 if number is None else 0.34))
    pPr.set("marL", str(int(Inches(0.3 + 0.35 * level + (0 if number is None else 0.06)))))
    pPr.set("indent", str(-int(indent)))
    for tag in ("a:buClr", "a:buSzPct", "a:buFont", "a:buNone", "a:buChar", "a:buAutoNum"):
        for el in pPr.findall(qn(tag)):
            pPr.remove(el)
    if number is not None:
        pPr.append(pPr.makeelement(qn("a:buFont"), {"typeface": FONT}))
        pPr.append(pPr.makeelement(qn("a:buAutoNum"), {"type": "arabicPeriod", "startAt": str(number)}))
        return
    clr = pPr.makeelement(qn("a:buClr"), {})
    clr.append(clr.makeelement(qn("a:srgbClr"), {"val": str(BLUE if level == 0 else TEXT)}))
    pPr.append(clr)
    if level == 0:
        pPr.append(pPr.makeelement(qn("a:buSzPct"), {"val": "70000"}))
        pPr.append(pPr.makeelement(qn("a:buFont"), {"typeface": "Wingdings", "pitchFamily": "2", "charset": "2"}))
        pPr.append(pPr.makeelement(qn("a:buChar"), {"char": "§"}))
    else:
        pPr.append(pPr.makeelement(qn("a:buSzPct"), {"val": "90000"}))
        pPr.append(pPr.makeelement(qn("a:buFont"), {"typeface": "Arial"}))
        pPr.append(pPr.makeelement(qn("a:buChar"), {"char": "–"}))


def add_runs(paragraph, text, size, color=TEXT, bold=False, link_color=BLUE, font=FONT):
    for value, style, url in inline_runs(text):
        run = paragraph.add_run()
        run.text = value
        f = run.font
        f.size = Pt(size)
        f.name = font
        f.bold = bold or style == "bold"
        f.italic = style == "italic"
        if color is not None:
            f.color.rgb = color
        if style == "code":
            f.name = MONO
            f.size = Pt(size * 0.92)
            if color is not None:
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
    tbl_pr.set("firstRow", "1")
    tbl_pr.set("bandRow", "1")
    style = tbl_pr.find(qn("a:tableStyleId"))
    if style is None:
        style = tbl_pr.makeelement(qn("a:tableStyleId"), {})
        tbl_pr.append(style)
    style.text = TABLE_STYLE
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
            cy += render_para(slide, block[1], x, cy, w, size * 1.05, color=NAVY, bold=True)
        elif kind == "callout":
            cy += render_callout(slide, block[1], x, cy, w, size)
        elif kind == "code":
            cy += render_code(slide, block[1], x, cy, w, size)
        elif kind == "table":
            cy += render_table(slide, block[1], x, cy, w, size)
        cy += BLOCK_GAP
    return size


def placeholder(slide, idx):
    for ph in slide.placeholders:
        if ph.placeholder_format.idx == idx:
            return ph
    raise KeyError(f"layout '{slide.slide_layout.name}' has no placeholder idx={idx}")


def pin_geometry(shape, width=None):
    """Give a placeholder its own position, so it can be resized without moving to the origin."""
    left, top, height = shape.left, shape.top, shape.height
    shape.left, shape.top, shape.height = left, top, height
    shape.width = Inches(width) if width is not None else shape.width


def remove_shape(shape):
    shape._element.getparent().remove(shape._element)


def fit_size(text, width_in, start, minimum, scale=TITLE_SCALE):
    size = start
    while size > minimum and text_em(text) * scale * size / 72 > width_in:
        size -= 1
    return size


def add_footer(slide, number):
    """Copy the legal statement and slide number from the layout; add_slide does not clone them."""
    for ph in slide.slide_layout.placeholders:
        if ph.placeholder_format.type not in (PP_PLACEHOLDER.FOOTER, PP_PLACEHOLDER.SLIDE_NUMBER):
            continue
        el = copy.deepcopy(ph._element)
        el.nvSpPr.cNvPr.id = slide.shapes._next_shape_id
        for field in el.iter(qn("a:fld")):
            field.find(qn("a:t")).text = str(number)
        slide.shapes._spTree.insert_element_before(el, "p:extLst")


def add_title(slide, title, width):
    ph = slide.shapes.title
    if width < ph.width / 914400:
        pin_geometry(ph, width)
    size = fit_size(title, width, 32, 22)
    add_runs(ph.text_frame.paragraphs[0], title, size, color=BLUE, font=TITLE_FONT)


def add_handson_badge(slide, time):
    label = "HANDS-ON" + (f"  ·  {time}" if time else "")
    w = 1.5 if not time else 2.6
    badge = slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, Inches(SLIDE_W - MARGIN - w), Inches(0.86),
                                   Inches(w), Inches(0.36))
    badge.fill.solid()
    badge.fill.fore_color.rgb = GREEN
    badge.line.fill.background()
    badge.shadow.inherit = False
    tf = badge.text_frame
    tf.margin_left = tf.margin_right = Inches(0.05)
    tf.margin_top = tf.margin_bottom = Inches(0)
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    run = p.add_run()
    run.text = label
    run.font.size = Pt(12)
    run.font.name = TITLE_FONT
    run.font.color.rgb = WHITE
    return w


# --------------------------------------------------------------------------------------
# Slide builders
# --------------------------------------------------------------------------------------

def build_title_slide(slide, s):
    """Cover with Pattern: title and subtitle in the title block, the detail line split over
    the author (light blue) and job title (white) lines at the first ' · '."""
    tf = placeholder(slide, 10).text_frame
    add_runs(tf.paragraphs[0], s["title"], 44, color=WHITE, font=TITLE_FONT)
    if s["subtitle"]:
        p = tf.add_paragraph()
        p.space_before = Pt(6)
        add_runs(p, s["subtitle"], fit_size(s["subtitle"], 2 * 4.8, 28, 18), color=WHITE, font=TITLE_FONT)
    paras = [b[1] for b in s["blocks"] if b[0] == "para"]
    detail = paras[0].split(" · ", 1) if paras else []
    for k, (idx, start, bold) in enumerate(((14, 20, True), (15, 12, False))):
        ph = placeholder(slide, idx)
        if k >= len(detail):
            remove_shape(ph)
            continue
        pin_geometry(ph, 5.51)
        size = fit_size(detail[k], 5.4, start, 12)
        add_runs(ph.text_frame.paragraphs[0], detail[k], size, color=None, bold=bold, font=TITLE_FONT)


def build_section_slide(slide, s):
    tf = placeholder(slide, 10).text_frame
    add_runs(tf.paragraphs[0], s["title"], 44, color=WHITE, font=TITLE_FONT)
    if s["subtitle"]:
        p = tf.add_paragraph()
        p.space_before = Pt(10)
        add_runs(p, s["subtitle"], 24, color=WHITE, font=TITLE_FONT)


def build_content_slide(slide, s, number, base_dir):
    add_footer(slide, number)
    title_w = SLIDE_W - 2 * MARGIN - 1.4
    if s["layout"] == "handson":
        badge_w = add_handson_badge(slide, s["time"])
        title_w = min(title_w, SLIDE_W - 2 * MARGIN - badge_w - 0.25)
    add_title(slide, s["title"], title_w)

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


def open_template(path):
    """Open the template and drop its slides, keeping the masters, layouts and fonts."""
    prs = Presentation(path)
    sld_ids = prs.slides._sldIdLst
    for sld_id in list(sld_ids):
        prs.part.drop_rel(sld_id.rId)
        sld_ids.remove(sld_id)
    ext_lst = prs.part._element.find(qn("p:extLst"))
    if ext_lst is not None:
        for ext in list(ext_lst):
            if any(child.tag.endswith("}sectionLst") for child in ext):
                ext_lst.remove(ext)
    return prs


def find_layout(prs, name):
    for layout in prs.slide_layouts:
        if layout.name == name:
            return layout
    names = ", ".join(repr(l.name) for l in prs.slide_layouts)
    raise SystemExit(f"Template has no layout named {name!r}; available: {names}")


def build(md_path, out_path, template_path):
    with open(md_path, encoding="utf-8") as f:
        md = f.read()
    md = re.sub(r"\A\s*<!--(?!\s*(layout|time):).*?-->", "", md, flags=re.S)
    slides = [parse_slide(s) for s in split_slides(md)]
    base_dir = os.path.dirname(os.path.abspath(md_path))

    prs = open_template(template_path)
    prs.slide_width = Inches(SLIDE_W)
    prs.slide_height = Inches(SLIDE_H)
    cover = find_layout(prs, LAYOUT_COVER)
    section = find_layout(prs, LAYOUT_SECTION)
    content = find_layout(prs, LAYOUT_CONTENT)

    for number, s in enumerate(slides, start=1):
        print(f"[{number:2d}] {s['layout']:8s} {s['title']}")
        if s["layout"] == "title":
            slide = prs.slides.add_slide(cover)
            build_title_slide(slide, s)
            if s["notes"]:
                slide.notes_slide.notes_text_frame.text = s["notes"]
        elif s["layout"] == "section":
            slide = prs.slides.add_slide(section)
            build_section_slide(slide, s)
        else:
            slide = prs.slides.add_slide(content)
            build_content_slide(slide, s, number, base_dir)

    prs.save(out_path)
    print(f"Wrote {out_path} ({len(slides)} slides)")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("markdown", nargs="?", default=os.path.join(here, "alif_ml_training.md"))
    parser.add_argument("-o", "--output", default=None)
    parser.add_argument("--template", default=os.path.join(here, "adi_template.pptx"))
    args = parser.parse_args()
    out = args.output or os.path.splitext(args.markdown)[0] + ".pptx"
    build(args.markdown, out, args.template)


if __name__ == "__main__":
    sys.exit(main())
