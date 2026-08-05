#!/usr/bin/env python3
"""Convert markdown paper to PDF via markdown + weasyprint with CJK support."""

import sys
import html
import base64
import os
import re
from io import BytesIO
import markdown
from weasyprint import HTML, CSS


def render_math(md_content):
    """Render the manuscript's LaTeX-delimited math to embedded SVG images."""
    os.environ.setdefault('MPLCONFIGDIR', '/tmp/codex-matplotlib')
    from matplotlib.font_manager import FontProperties
    from matplotlib.mathtext import math_to_image

    font = FontProperties(family='serif', size=9.2)

    def render(expr, display):
        # Matplotlib mathtext supports \mathrm but not LaTeX's \text command.
        normalized = re.sub(r'\\text\{([^{}]*)\}', r'\\mathrm{\1}', expr.strip())
        output = BytesIO()
        try:
            math_to_image(f'${normalized}$', output, prop=font, format='svg')
        except ValueError:
            return html.escape(expr)
        encoded = base64.b64encode(output.getvalue()).decode('ascii')
        alt = html.escape(expr, quote=True)
        if display:
            return (
                '<div class="math-display">'
                f'<img src="data:image/svg+xml;base64,{encoded}" alt="{alt}">'
                '</div>'
            )
        return (
            '<span class="math-inline">'
            f'<img src="data:image/svg+xml;base64,{encoded}" alt="{alt}">'
            '</span>'
        )

    md_content = re.sub(
        r'\\\[(.+?)\\\]',
        lambda match: render(match.group(1), True),
        md_content,
        flags=re.DOTALL,
    )
    return re.sub(
        r'\\\((.+?)\\\)',
        lambda match: render(match.group(1), False),
        md_content,
        flags=re.DOTALL,
    )

def convert_md_to_pdf(md_path, pdf_path):
    # Read markdown
    with open(md_path, 'r', encoding='utf-8') as f:
        md_content = f.read()

    title = next(
        (line[2:].strip() for line in md_content.splitlines()
         if line.startswith('# ')),
        'Technical Paper',
    )
    md_content = render_math(md_content)

    # Convert to HTML with extensions
    html_body = markdown.markdown(
        md_content,
        extensions=[
            'tables',
            'fenced_code',
            'codehilite',
            'toc',
            'nl2br',
            'sane_lists',
        ]
    )

    # Full HTML with CJK-compatible CSS
    html_full = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>
@page {{
    size: Letter;
    margin: 0.68in 0.65in 0.62in;
    @bottom-center {{
        content: counter(page);
        font-family: "Times New Roman", "Liberation Serif", serif;
        font-size: 8pt;
    }}
}}
body {{
    font-family: "Times New Roman", "Liberation Serif", "Noto Serif CJK SC", serif;
    font-size: 9.2pt;
    line-height: 1.18;
    color: #000;
    text-align: justify;
    hyphens: auto;
    column-count: 2;
    column-gap: 0.24in;
}}
h1 {{
    font-family: "Times New Roman", "Liberation Serif", serif;
    font-size: 18pt;
    font-weight: normal;
    line-height: 1.12;
    margin: 0 0 0.65em;
    color: #000;
    text-align: center;
    column-span: all;
    page-break-after: avoid;
}}
h2 {{
    font-family: "Times New Roman", "Liberation Serif", serif;
    font-size: 10pt;
    font-weight: bold;
    text-transform: uppercase;
    text-align: center;
    margin: 0.9em 0 0.35em;
    color: #000;
    page-break-after: avoid;
}}
h3 {{
    font-family: "Times New Roman", "Liberation Serif", serif;
    font-size: 9.4pt;
    font-weight: bold;
    font-style: italic;
    margin: 0.65em 0 0.2em;
    color: #000;
    page-break-after: avoid;
}}
h4 {{
    font-family: "Times New Roman", "Liberation Serif", serif;
    font-size: 9.2pt;
    font-weight: bold;
    font-style: italic;
    margin: 0.5em 0 0.15em;
    color: #000;
    page-break-after: avoid;
}}
p {{
    margin: 0.22em 0;
    text-indent: 0;
}}
table {{
    border-collapse: collapse;
    width: 100%;
    margin: 0.55em 0;
    font-size: 7.6pt;
    line-height: 1.12;
    page-break-inside: avoid;
    column-span: all;
}}
th, td {{
    border-top: 0.5pt solid #000;
    border-bottom: 0.5pt solid #000;
    border-left: 0;
    border-right: 0;
    padding: 2.5px 4px;
    text-align: left;
}}
th {{
    font-weight: bold;
    background: transparent;
}}
code {{
    font-family: "DejaVu Sans Mono", "Courier New", monospace;
    font-size: 7.5pt;
    background: transparent;
    padding: 0;
}}
.math-inline {{
    display: inline-block;
    white-space: nowrap;
    vertical-align: -0.18em;
    line-height: 1;
}}
.math-inline img {{
    height: 1.05em;
    width: auto;
}}
.math-display {{
    margin: 0.45em auto;
    text-align: center;
    break-inside: avoid;
}}
.math-display img {{
    max-width: 98%;
    height: auto;
}}
pre {{
    background: #fff;
    padding: 5px;
    border: 0.5pt solid #777;
    overflow-x: auto;
    font-size: 6.8pt;
    line-height: 1.15;
    page-break-inside: avoid;
    white-space: pre-wrap;
    column-span: all;
}}
pre code {{
    background: transparent;
    padding: 0;
}}
strong {{
    color: #000;
}}
em {{
    font-style: italic;
}}
ul, ol {{
    margin: 0.25em 0;
    padding-left: 1.25em;
}}
li {{
    margin: 0.12em 0;
}}
a {{
    color: #000;
    text-decoration: none;
}}
hr {{
    border: none;
    border-top: 0.5pt solid #000;
    margin: 0.55em 0;
    column-span: all;
}}
blockquote {{
    border-left: 1pt solid #000;
    padding-left: 0.6em;
    margin: 0.4em 0;
    color: #000;
    font-style: italic;
}}
/* Transactions-style full-width front matter. */
h2:first-of-type,
h2:first-of-type + p,
h2:first-of-type + p + p,
h2:first-of-type + p + p + p {{
    column-span: all;
}}
h2:first-of-type {{
    text-transform: none;
    text-align: left;
    font-size: 9.2pt;
    margin-top: 0;
}}
h2:first-of-type + p {{
    font-size: 8.8pt;
    line-height: 1.14;
}}
h2:first-of-type + p + p {{
    font-size: 8.6pt;
    margin-bottom: 0.55em;
}}
h2:first-of-type + p + p + p {{
    font-size: 8.6pt;
    margin-bottom: 0.55em;
}}
</style>
</head>
<body>
{html_body}
</body>
</html>"""

    # Generate PDF
    HTML(string=html_full).write_pdf(
        pdf_path,
        stylesheets=[CSS(string='')]
    )
    print(f"PDF generated: {pdf_path}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 md2pdf.py input.md output.pdf")
        sys.exit(1)
    convert_md_to_pdf(sys.argv[1], sys.argv[2])
