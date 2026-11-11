#!/usr/bin/env python3
"""Generate a 2-slide RMT pipeline overview deck (16:9) + PNG previews."""
import os, sys
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.enum.shapes import MSO_SHAPE, MSO_CONNECTOR
from pptx.enum.dml import MSO_LINE_DASH_STYLE
from pptx.oxml.ns import qn
from lxml import etree
from PIL import Image, ImageDraw, ImageFont

# ---------------- palette ----------------
NAVY   = RGBColor(0x1F, 0x38, 0x64)
BLUE   = RGBColor(0x2E, 0x75, 0xB6)
LBLUE  = RGBColor(0xDE, 0xEB, 0xF7)
GREEN  = RGBColor(0x70, 0xAD, 0x47)
LGREEN = RGBColor(0xE2, 0xEF, 0xDA)
YELLOW = RGBColor(0xBF, 0x90, 0x00)
LYELLOW= RGBColor(0xFF, 0xF2, 0xCC)
PURPLE = RGBColor(0x70, 0x30, 0xA0)
LPURPLE= RGBColor(0xED, 0xE7, 0xF6)
RED    = RGBColor(0xC0, 0x00, 0x00)
LRED   = RGBColor(0xFB, 0xE5, 0xD6)
GRAY   = RGBColor(0x59, 0x59, 0x59)
LGRAY  = RGBColor(0xF2, 0xF2, 0xF2)
MGRAY  = RGBColor(0xBF, 0xBF, 0xBF)
WHITE  = RGBColor(0xFF, 0xFF, 0xFF)
DARK   = RGBColor(0x1F, 0x1F, 0x1F)
FONT = "Noto Sans CJK SC"

SW, SH = 13.333, 7.5
DPI = 120
REG = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
BOLD = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
try:
    ImageFont.truetype(REG, 12, index=2)
    FACE_INDEX = 2
except Exception:
    FACE_INDEX = 0

_prs = Presentation()
_prs.core_properties.title = "RMT 软件流水线：架构与处理流程"
_prs.core_properties.subject = "Software RMT pipeline overview (2 slides)"
_prs.core_properties.author = "rmt-pipeline"
_prs.slide_width = Inches(SW)
_prs.slide_height = Inches(SH)
_slide = None
_PIL = None
_DRAW = None
_IMAGES = []
SCALE = DPI

def new_slide():
    global _slide, _PIL, _DRAW
    if _PIL is not None:
        _IMAGES.append(_PIL)
    _slide = _prs.slides.add_slide(_prs.slide_layouts[6])
    _PIL = Image.new("RGB", (int(SW*DPI), int(SH*DPI)), "white")
    _DRAW = ImageDraw.Draw(_PIL)

def _set_ea(run):
    rPr = run._r.get_or_add_rPr()
    for tag in ("a:ea", "a:cs"):
        el = rPr.find(qn(tag))
        if el is None:
            el = etree.SubElement(rPr, qn(tag))
        el.set("typeface", FONT)

def _rgb(c): return (c[0], c[1], c[2])

def _font(pt, bold=False):
    return ImageFont.truetype(BOLD if bold else REG, max(6, int(round(pt*DPI/72.0))), index=FACE_INDEX)

def _text_block(x, y, w, h, lines, align="left", anchor="middle", pad=0.05):
    """lines: list of (text, pt, bold, RGBColor)."""
    # PIL preview
    heights = [int(round(pt*DPI/72.0))*1.22 for (_, pt, _, _) in lines]
    total = sum(heights)
    if anchor == "middle":
        cy = y*DPI + (h*DPI - total)/2.0
    elif anchor == "bottom":
        cy = y*DPI + h*DPI - total - pad*DPI
    else:
        cy = y*DPI + pad*DPI
    maxw = (w - 2*pad)*DPI
    for (text, pt, bold, color), lh in zip(lines, heights):
        if not text:
            cy += lh; continue
        f = _font(pt, bold)
        tw = _DRAW.textlength(text, font=f)
        if tw > maxw + 1:
            print(f"WARN text overflow ({tw:.0f} > {maxw:.0f}px): {text[:40]}", file=sys.stderr)
        if align == "center":
            tx = x*DPI + (w*DPI - tw)/2.0
        elif align == "right":
            tx = x*DPI + w*DPI - tw - pad*DPI
        else:
            tx = x*DPI + pad*DPI
        _DRAW.text((tx, cy + lh*0.06), text, font=f, fill=_rgb(color))
        cy += lh

    # PPTX
    shape = _slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = shape.text_frame
    tf.word_wrap = False
    tf.margin_left = tf.margin_right = Inches(pad)
    tf.margin_top = tf.margin_bottom = Inches(0.01)
    tf.vertical_anchor = {"top": MSO_ANCHOR.TOP, "middle": MSO_ANCHOR.MIDDLE,
                          "bottom": MSO_ANCHOR.BOTTOM}[anchor]
    first = True
    for text, pt, bold, color in lines:
        p = tf.paragraphs[0] if first else tf.add_paragraph()
        first = False
        p.alignment = {"left": PP_ALIGN.LEFT, "center": PP_ALIGN.CENTER,
                       "right": PP_ALIGN.RIGHT}[align]
        p.space_before = Pt(0); p.space_after = Pt(0)
        p.line_spacing = 1.0
        r = p.add_run(); r.text = text
        r.font.size = Pt(pt); r.font.bold = bold; r.font.name = FONT
        r.font.color.rgb = color
        _set_ea(r)

def box(x, y, w, h, lines, fill=WHITE, line=BLUE, lw=1.0, dash=False,
        align="center", anchor="middle", shape="round", radius=0.10, pad=0.05):
    shp = {"round": MSO_SHAPE.ROUNDED_RECTANGLE,
           "rect": MSO_SHAPE.RECTANGLE,
           "diamond": MSO_SHAPE.DIAMOND}[shape]
    s = _slide.shapes.add_shape(shp, Inches(x), Inches(y), Inches(w), Inches(h))
    s.fill.solid(); s.fill.fore_color.rgb = fill
    s.line.color.rgb = line; s.line.width = Pt(lw)
    if dash:
        s.line.dash_style = MSO_LINE_DASH_STYLE.DASH
    if shape == "round":
        try: s.adjustments[0] = radius
        except Exception: pass
    s.shadow.inherit = False
    # PIL background/outline first, then the text on top
    if shape == "diamond":
        cx, cy = (x+w/2)*DPI, (y+h/2)*DPI
        pts = [(cx, y*DPI), ((x+w)*DPI, cy), (cx, (y+h)*DPI), (x*DPI, cy)]
        _DRAW.polygon(pts, fill=_rgb(fill), outline=_rgb(line), width=max(1, int(lw)))
    elif shape == "round":
        _DRAW.rounded_rectangle([x*DPI, y*DPI, (x+w)*DPI, (y+h)*DPI],
                                radius=radius*min(w, h)*DPI, fill=_rgb(fill),
                                outline=_rgb(line), width=max(1, int(lw)))
    else:
        _DRAW.rectangle([x*DPI, y*DPI, (x+w)*DPI, (y+h)*DPI],
                        fill=_rgb(fill), outline=_rgb(line), width=max(1, int(lw)))
    if lines:
        if shape == "diamond":
            _text_block(x + w*0.18, y + h*0.5 - 0.13, w*0.64, 0.26,
                        lines, align=align, anchor="middle", pad=0.01)
        else:
            _text_block(x, y, w, h, lines, align=align, anchor=anchor, pad=pad)

def arrow(x1, y1, x2, y2, color=GRAY, lw=1.2, dash=False):
    c = _slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT,
                                    Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    c.line.color.rgb = color; c.line.width = Pt(lw)
    if dash:
        c.line.dash_style = MSO_LINE_DASH_STYLE.DASH
    ln = c.line._get_or_add_ln()
    etree.SubElement(ln, qn("a:tailEnd"), {"type": "triangle", "w": "med", "len": "med"})
    # PIL
    _DRAW.line([x1*DPI, y1*DPI, x2*DPI, y2*DPI], fill=_rgb(color), width=max(1, int(lw)))
    import math
    ang = math.atan2(y2-y1, x2-x1)
    L = 8
    for da in (math.radians(150), math.radians(-150)):
        ex = x2*DPI + L*math.cos(ang+da); ey = y2*DPI + L*math.sin(ang+da)
        _DRAW.line([x2*DPI, y2*DPI, ex, ey], fill=_rgb(color), width=max(1, int(lw)))

def line(x1, y1, x2, y2, color=GRAY, lw=1.2, dash=False):
    c = _slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT,
                                    Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    c.line.color.rgb = color; c.line.width = Pt(lw)
    if dash:
        c.line.dash_style = MSO_LINE_DASH_STYLE.DASH

def rect(x, y, w, h, fill, line=None, lw=0):
    s = _slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    s.fill.solid(); s.fill.fore_color.rgb = fill
    if line is None:
        s.line.fill.background()
    else:
        s.line.color.rgb = line; s.line.width = Pt(lw)
    s.shadow.inherit = False
    _DRAW.rectangle([x*DPI, y*DPI, (x+w)*DPI, (y+h)*DPI], fill=_rgb(fill),
                    outline=_rgb(line) if line else None, width=max(1, int(lw)))

# =====================================================================
# Slide 1: architecture + features
# =====================================================================
new_slide()
rect(0, 0, SW, 0.12, NAVY)
_text_block(0.45, 0.14, 8.9, 0.62, [("RMT 软件流水线：架构与核心功能", 28, True, NAVY)], anchor="middle")
_text_block(0.47, 0.78, 9.2, 0.24, [("纯 C11 · 零依赖 · 可编程数据面（RMT）参考实现", 11.5, False, GRAY)], anchor="middle")
_text_block(9.4, 0.30, 3.55, 0.4, [("逐指令可调试 · 可嵌入 C 程序", 10, False, BLUE)], align="right")

# outer panel
box(0.35, 1.08, 8.45, 5.48, [], fill=RGBColor(0xFA,0xFC,0xFF), line=RGBColor(0xC9,0xD6,0xEA), lw=1.0, radius=0.03)

# config strip
box(0.50, 1.16, 3.35, 0.34, [("P4 编译器 p4c.c：解析图 / 表 / ALU 微码 / extern", 8, False, NAVY)],
    fill=RGBColor(0xE8,0xEE,0xF8), line=RGBColor(0xB4,0xC7,0xE7), lw=0.8, radius=0.15, pad=0.04)
box(3.98, 1.16, 3.42, 0.34, [("控制面 runtime.c：table_add 表项 / 动作参数", 8, False, NAVY)],
    fill=RGBColor(0xE8,0xEE,0xF8), line=RGBColor(0xB4,0xC7,0xE7), lw=0.8, radius=0.15, pad=0.04)
_text_block(7.55, 1.16, 1.15, 0.34, [("GCC 7+ · Linux", 8, False, GRAY)], align="center")
arrow(1.75, 1.50, 1.75, 1.60, color=PURPLE, lw=1.0, dash=True)
arrow(5.55, 1.50, 5.55, 1.60, color=PURPLE, lw=1.0, dash=True)

# data path
box(0.50, 1.76, 0.72, 0.84, [("报文", 8.5, True, DARK), ("in", 8, False, GRAY)], fill=LGRAY, line=MGRAY, lw=0.8)
arrow(1.24, 2.18, 1.52, 2.18)
box(1.55, 1.60, 1.05, 1.14,
    [("可编程", 9.5, True, NAVY), ("Parser", 9.5, True, NAVY),
     ("FSM · bit 提取", 7.2, False, DARK), ("头栈 · select", 7.2, False, DARK)],
    fill=LBLUE, line=BLUE, lw=1.0)
arrow(2.62, 2.18, 2.84, 2.18)
box(2.87, 1.54, 3.45, 1.28, [], fill=RGBColor(0xF2,0xF8,0xEE), line=GREEN, lw=1.0)
_text_block(2.87, 1.56, 3.45, 0.24, [("Match-Action 流水线（≤ 8 级）", 8.5, True, RGBColor(0x37,0x6E,0x2B))], align="center")
for i in range(4):
    bx = 2.95 + i*0.84
    box(bx, 1.82, 0.78, 0.90,
        [(f"S{i}", 8.5, True, DARK), ("Match", 7.0, False, DARK),
         ("→", 7.5, True, GRAY), ("Action", 7.0, False, DARK)],
        fill=LGREEN, line=GREEN, lw=0.8, pad=0.02)
arrow(6.32, 2.18, 6.52, 2.18)
box(6.55, 1.60, 1.05, 1.14,
    [("逆解析", 9.5, True, NAVY), ("Deparser", 9.5, True, NAVY),
     ("valid 头重串", 7.2, False, DARK), ("+ payload", 7.2, False, DARK)],
    fill=LBLUE, line=BLUE, lw=1.0)
arrow(7.62, 2.18, 7.84, 2.18)
box(7.87, 1.76, 0.68, 0.84, [("报文", 8.5, True, DARK), ("out", 8, False, GRAY)], fill=LGRAY, line=MGRAY, lw=0.8)

# PHV band
box(1.55, 3.00, 6.15, 0.36,
    [("PHV 字段总线：160 × 128-bit · hvalid / dirty / structural · meta.drop / egress_port", 8.2, False, RGBColor(0x7F,0x60,0x00))],
    fill=LYELLOW, line=YELLOW, lw=0.9, radius=0.15, pad=0.03)

# externs
_text_block(1.60, 3.44, 4.9, 0.22, [("有状态 Extern（声明式，数据面指令直接读写）", 8.5, True, RGBColor(0x54,0x82,0x35))], align="left")
ext = [("cache 引擎", "CS / PIT", "util 老化"),
       ("DNN 引擎", "Q16.16 MLP", "argmax 分类"),
       ("Sketch 引擎", "CMS 计数", "freq 估计")]
for i, (t, l1, l2) in enumerate(ext):
    ex = 1.60 + i*1.75
    box(ex, 3.70, 1.55, 0.98,
        [(t, 9, True, NAVY), (l1, 7.5, False, DARK), (l2, 7.5, False, GRAY)],
        fill=LGREEN, line=GREEN, lw=0.8)
    line(ex+0.775, 3.70, ex+0.775, 3.38, color=GREEN, lw=1.0)

# bottom row
box(0.50, 5.10, 3.35, 1.02,
    [("多队列引擎 mq.c", 9.5, True, PURPLE),
     ("RSS 5 元组哈希 → SPSC 无锁环", 7.8, False, DARK),
     ("→ 绑核 worker → burst 批处理", 7.8, False, DARK)],
    fill=LPURPLE, line=PURPLE, lw=0.9)
arrow(1.05, 5.10, 1.05, 2.62, color=PURPLE, lw=1.4)

box(4.05, 5.20, 4.35, 0.82,
    [("嵌入 API（include/rmt.h）", 8.5, True, DARK),
     ("rmt_create / p4c_compile_file / rmt_load_entries", 7.2, False, GRAY),
     ("rmt_process / rmt_process_batch / rmt_process_burst", 7.2, False, GRAY)],
    fill=LGRAY, line=MGRAY, lw=0.8, radius=0.06)
_text_block(0.50, 6.22, 8.2, 0.26,
            [("三种处理入口（单包 / batch / burst）；流水线配置只读，可多线程共享", 8, False, GRAY)], align="left")

# right feature column
_text_block(8.78, 1.02, 4.2, 0.34, [("核心功能", 15, True, NAVY)], align="left")
rect(8.80, 1.40, 4.10, 0.03, BLUE)
features = [
    ("可编程解析 / 逆解析", ["表驱动 FSM + bit 级提取；extract_array 展开头栈",
                        "按头有效位重串头部，再追加 payload"]),
    ("Match 匹配", ["exact / LPM；路径压缩 radix trie",
                  "字典序最长前缀优先 + 跨字段回溯；表门控"]),
    ("Action 执行", ["RISC ALU；每级 16 ALU 的 VLIW 执行",
                   "两相提交（读旧写新）；编译期 RAW/WAW 调度"]),
    ("有状态 extern", ["cache（CS/PIT/epoch 老化）· DNN（Q16.16 MLP）",
                    "Sketch（CMS 频率估计，d×w 原子计数）"]),
    ("流水线编排", ["parse → ≤8 级 Match-Action → deparse",
                 "单包 / batch(64) / burst；verbose 打印逐级轨迹"]),
    ("多队列并行", ["RSS 5 元组哈希 → SPSC 无锁环 → 绑核 worker",
                 "同流保序；可横向扩展"]),
    ("工程与嵌入", ["P4 子集单遍编译 + table_add 控制面",
                 "纯 C11、零第三方依赖、可嵌入 C API"]),
]
y = 1.56
for title, desc in features:
    d1, d2 = desc
    rect(8.82, y+0.06, 0.11, 0.11, BLUE)
    _text_block(9.02, y, 3.95, 0.26, [(title, 10.5, True, NAVY)], align="left", anchor="top")
    _text_block(9.02, y+0.26, 3.95, 0.40,
                [(d1, 8.3, False, DARK), (d2, 8.3, False, GRAY)], align="left", anchor="top")
    y += 0.70
_text_block(8.82, 6.48, 4.15, 0.24,
            [("容量：40 头 / 8 栈 / 32 状态 / 16 表 / 8 级 / 8 key", 7.5, False, GRAY)], align="left")
_text_block(12.25, 7.02, 0.9, 0.22, [("1 / 2", 8, False, GRAY)], align="right")
_slide.notes_slide.notes_text_frame.text = (
    "架构分五层：可编程解析/逆解析、PHV 字段总线、≤8 级 Match-Action"
    "（每级 16 ALU 的 VLIW，两相提交）、三类有状态 extern（cache / DNN / Sketch）、"
    "以及配置面（p4c 编译 + runtime 表项下发）与多队列数据面引擎。"
    "右侧为核心能力与容量：40 头 / 8 栈 / 32 状态 / 16 表 / 8 级 / 8 key。")

# =====================================================================
# Slide 2: processing flow
# =====================================================================
new_slide()
rect(0, 0, SW, 0.12, NAVY)
_text_block(0.45, 0.14, 10.3, 0.60, [("报文处理流程（rmt_process / rmt_process_burst）", 26, True, NAVY)], anchor="middle")
_text_block(0.47, 0.78, 10.8, 0.24, [("单包主路径 · 丢弃 / 异常分支 · MQ 引擎按流并行调用同一流程", 10.5, False, GRAY)], anchor="middle")

# 1 entry
box(0.45, 1.12, 6.70, 0.60,
    [("① 入口：rmt_process(p, in, out) ｜ rmt_process_burst(p, in[], out, n)", 9.5, True, NAVY),
     ("初始化 out（len / dropped）与 PHV（字段模板 + meta 有效位 + dirty/structural）；写 ingress_port、timestamp", 8.0, False, DARK)],
    fill=LBLUE, line=BLUE, lw=1.0, pad=0.10)
arrow(3.80, 1.72, 3.80, 1.86)

# 2 parse
box(0.45, 1.86, 6.70, 0.72,
    [("② 解析 rmt_parse：表驱动 FSM", 9.5, True, NAVY),
     ("extract（bit 级）→ extract_array（运行时 count 展开头栈）→ select 跳转；记录 hdr_bytes；失败 → -1", 8.0, False, DARK)],
    fill=LBLUE, line=BLUE, lw=1.0, pad=0.10)
arrow(3.80, 2.58, 3.80, 2.72)

# 3 stage loop container
box(0.45, 2.72, 6.70, 1.62, [], fill=RGBColor(0xF2,0xF8,0xEE), line=GREEN, lw=1.0)
_text_block(0.55, 2.75, 6.5, 0.24, [("③ 逐级 Match-Action：s = 0 … N−1（≤ 8 级）", 9.5, True, RGBColor(0x37,0x6E,0x2B))], align="left")
rows = [("门控", "pred_hdr / valid(h) 不满足 → 跳过该表"),
        ("Lookup", "key 字段 → 级联 radix trie（exact/LPM 候选）→ 最长前缀优先 + 跨字段回溯 → entry / default_action"),
        ("Action", "每 word 16 ALU 并行执行（读旧值）→ commit 统一写回；extern：cache / DNN / sketch"),
        ("循环", "s ← s + 1；每级后检查 meta.drop：否 → 下一级，是 → 丢弃")]
ys = [3.02, 3.28, 3.72, 4.10]
for (chip, txt), ry in zip(rows, ys):
    box(0.55, ry, 0.62, 0.22, [(chip, 7.6, True, WHITE)], fill=GREEN, line=GREEN, lw=0.5, radius=0.2, pad=0.01)
    _text_block(1.24, ry, 5.80, 0.22, [(txt, 8.1, False, DARK)], align="left", anchor="middle")

arrow(3.80, 4.34, 3.80, 4.48)

# 4 drop decision
box(2.85, 4.48, 1.90, 0.62, [("meta.drop ?", 8.8, True, RGBColor(0x7F,0x60,0x00))], fill=LYELLOW, line=YELLOW, lw=1.0, shape="diamond", pad=0.02)
arrow(4.76, 4.79, 5.12, 4.79, color=RED, lw=1.2)
_text_block(4.72, 4.54, 0.30, 0.20, [("是", 8, True, RED)], align="left")
box(5.12, 4.46, 2.03, 0.66,
    [("④ 丢弃：out.dropped = 1", 8.5, True, RED), ("out.len = 0，跳过逆解析", 7.8, False, RGBColor(0x8B,0x00,0x00))],
    fill=LRED, line=RED, lw=1.0, pad=0.06)
arrow(3.80, 5.10, 3.80, 5.22)

# 5 deparse
box(0.45, 5.22, 6.70, 0.95,
    [("⑤ 逆解析：rmt_deparse / in-place 快路径", 9.5, True, NAVY),
     ("structural / has_inject / 解析乱序 → 全量：按 valid 重串 + payload（或注入内容）", 8.0, False, DARK),
     ("否则 in-place：复制原包，仅回写 dirty 字段（省去全量序列化）", 8.0, False, DARK)],
    fill=LBLUE, line=BLUE, lw=1.0, pad=0.08)
arrow(3.80, 6.17, 3.80, 6.31)

# 6 output
box(0.45, 6.31, 6.70, 0.48,
    [("⑥ 输出：out.data / out.len / out.out_port / out.dropped → batch 数组或 MQ 回调返回", 8.5, True, RGBColor(0x37,0x6E,0x2B))],
    fill=LGREEN, line=GREEN, lw=1.0, pad=0.06)
_text_block(0.45, 6.94, 11.5, 0.24,
            [("异常路径：解析失败 → 返回 −1（batch 置 dropped）；drop 命中 → 立即跳过剩余 stage 与逆解析", 8, False, GRAY)], align="left")

# right notes
_text_block(7.55, 1.10, 5.3, 0.34, [("关键语义与并行路径", 14, True, NAVY)], align="left")
rect(7.57, 1.46, 5.25, 0.03, BLUE)
notes = [
    ("PHV 字段总线", ["160 × 128-bit 字段 + hvalid 位图；dirty / structural", "记录动作写入与结构变化，决定逆解析走哪条路径"]),
    ("Match 查找", ["路径压缩 radix trie：代价只与分支 / 前缀终点数相关，", "与表项总数无关；按字段序最长前缀 + 回溯"]),
    ("VLIW 执行", ["编译期 RAW/WAW 调度；同 word 读旧写新（WAR 安全）", "每级 ≤8 word × 16 ALU，commit 统一写回"]),
    ("Extern 引擎", ["cache：FNV-1a + 线性探测(8) + 最老淘汰 + 老化", "DNN：Q16.16 整数前向；Sketch：d×w 原子计数"]),
    ("并行入口", ["mq：RSS 5 元组 → SPSC 无锁环 → 绑核 worker", "→ rmt_process_burst；同流保序"]),
    ("快路径条件", ["无头增删、无 cache 注入、解析顺序 = 声明顺序", "→ in-place；否则全量 deparse"]),
]
yc = 1.60
for title, desc in notes:
    d1, d2 = desc
    _text_block(7.57, yc, 5.2, 0.26, [(title, 10, True, BLUE)], align="left", anchor="top")
    _text_block(7.57, yc+0.26, 5.2, 0.40, [(d1, 8.3, False, DARK), (d2, 8.3, False, GRAY)], align="left", anchor="top")
    yc += 0.80

_text_block(7.57, 6.48, 5.25, 0.45,
            [("入口 API：rmt_process / rmt_process_batch / rmt_process_burst", 7.8, False, GRAY),
             ("队列：rmt_mq_create / start / submit / flush / stop", 7.8, False, GRAY)], align="left", anchor="top")
_text_block(12.25, 7.02, 0.9, 0.22, [("2 / 2", 8, False, GRAY)], align="right")
_slide.notes_slide.notes_text_frame.text = (
    "按 rmt_process 走一遍：① 入口初始化 PHV；② 解析 FSM（extract / extract_array / select）；"
    "③ 逐级 Match-Action（门控 → radix trie 查找 → VLIW 动作 + extern）；"
    "④ drop 检查；⑤ 逆解析（结构变化/注入走全量 deparse，否则 in-place 只回写 dirty 字段）；"
    "⑥ 输出。MQ 路径由 RSS 把同一流程按流分发到绑核 worker。"
    "异常：解析失败返回 -1（batch 置 dropped），drop 命中立即短路。")

_IMAGES.append(_PIL)
out = "rmt_pipeline_2slides.pptx"
_prs.save(out)
print("saved", os.path.abspath(out))

if "--preview" in sys.argv:
    os.makedirs("figures", exist_ok=True)
    for i, im in enumerate(_IMAGES, 1):
        p = f"figures/_ppt_preview_{i}.png"
        im.save(p)
        print("preview", p)
