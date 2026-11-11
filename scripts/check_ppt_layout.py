#!/usr/bin/env python3
"""Geometric validation of the generated deck."""
import sys
from pptx import Presentation
from pptx.util import Emu
from PIL import ImageFont
from pptx.enum.shapes import MSO_SHAPE_TYPE

TERM = 914400.0
DPI = 120
REG = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
BOLD = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
try:
    ImageFont.truetype(REG, 12, index=2); IDX = 2
except Exception:
    IDX = 0
_fc = {}
def fnt(pt, bold):
    k = (pt, bold)
    if k not in _fc:
        _fc[k] = ImageFont.truetype(BOLD if bold else REG, max(6, int(round(pt*DPI/72.0))), index=IDX)
    return _fc[k]

prs = Presentation("rmt_pipeline_2slides.pptx")
W, H = prs.slide_width/TERM, prs.slide_height/TERM
print(f"slide size: {W:.3f} x {H:.3f} in, slides={len(prs.slides)}")
problems = []

for si, slide in enumerate(prs.slides, 1):
    texts = []
    for sh in slide.shapes:
        try:
            x, y, w, h = sh.left/TERM, sh.top/TERM, sh.width/TERM, sh.height/TERM
        except Exception:
            continue
        if x < -0.01 or y < -0.01 or x+w > W+0.01 or y+h > H+0.01:
            problems.append(f"S{si}: shape out of slide: {x:.2f},{y:.2f},{w:.2f},{h:.2f} {sh.shape_type}")
        has_text = False
        lines = []
        if sh.has_text_frame:
            for p in sh.text_frame.paragraphs:
                t = "".join(r.text for r in p.runs)
                if t.strip():
                    has_text = True
                    sz = max([r.font.size.pt if r.font.size else 10 for r in p.runs] or [10])
                    lines.append((t, sz))
        if has_text:
            # width check
            avail = max(0.1, w - 0.10)
            for t, sz in lines:
                tw = sum(fnt(sz, False).getlength(ch)/DPI for ch in t)
                if tw > avail + 0.02:
                    problems.append(f"S{si}: text too wide ({tw:.2f}>{avail:.2f}): {t[:36]}")
            est_h = sum(sz*1.45/72.0 for _, sz in lines) + 0.03
            if est_h > h + 0.03:
                problems.append(f"S{si}: text too tall ({est_h:.2f}>{h:.2f}): {lines[0][0][:36]}")
            texts.append((x, y, w, h, " | ".join(t for t, _ in lines)))
    # pairwise text bbox overlap
    for i in range(len(texts)):
        for j in range(i+1, len(texts)):
            x1,y1,w1,h1,t1 = texts[i]; x2,y2,w2,h2,t2 = texts[j]
            ox = max(0, min(x1+w1, x2+w2) - max(x1, x2))
            oy = max(0, min(y1+h1, y2+h2) - max(y1, y2))
            if ox*oy > 0.012:
                problems.append(f"S{si}: text bbox overlap {ox:.2f}x{oy:.2f}in: '{t1[:28]}' <> '{t2[:28]}'")

print(f"problems: {len(problems)}")
for p in problems:
    print(" -", p)
sys.exit(1 if problems else 0)
