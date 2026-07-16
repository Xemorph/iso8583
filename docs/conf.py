"""
Sphinx configuration for libiso8583 documentation.

Build:
    pip install sphinx breathe furo myst-parser
    doxygen docs/Doxyfile
    sphinx-build -b html docs docs/_build/html
"""

import subprocess
import sys
from pathlib import Path

# ── Project info ──────────────────────────────────────────────────────────────
project   = "libiso8583"
author    = "TNG"
release   = "0.1.1-alpha"
copyright = f"2024, {author}"

# ── Extensions ────────────────────────────────────────────────────────────────
extensions = [
    "breathe",          # bridges Doxygen XML → Sphinx RST/MD
    "myst_parser",      # allows .md files as Sphinx pages (AGENTS.md, CLAUDE.md)
    "sphinx.ext.autosectionlabel",
    "sphinx.ext.viewcode",
    "sphinx.ext.todo",
]

# Prefix section labels with the document name to avoid duplicate label warnings
# when the same heading appears in multiple files (e.g. "Field types" in
# api/isomessage.rst and agents.md).
autosectionlabel_prefix_document = True

# ── Source ────────────────────────────────────────────────────────────────────
source_suffix = {
    ".rst": "restructuredtext",
    ".md":  "markdown",
}
master_doc = "index"
exclude_patterns = ["_build", "_doxygen", "Thumbs.db", ".DS_Store", "README.md"]

# ── Theme: Furo (clean, modern, responsive) ───────────────────────────────────
html_theme = "furo"
html_static_path = []   # set to ["_static"] once you add custom CSS/JS

html_theme_options = {
    "sidebar_hide_name":        False,
    "navigation_with_keys":     True,
    "source_repository":        "https://github.com/YOUR-ORG/libiso8583/",
    "source_branch":            "main",
    "source_directory":         "include/",
    "footer_icons": [
        {
            "name":  "GitHub",
            "url":   "https://github.com/YOUR-ORG/libiso8583",
            "html":  """<svg …/>""",
            "class": "fa-brands fa-github",
        },
    ],
}

# ── Breathe: point at Doxygen XML output ─────────────────────────────────────
breathe_projects        = {"libiso8583": "./_doxygen/xml"}
breathe_default_project = "libiso8583"

# ── MyST: allow Markdown features ────────────────────────────────────────────
myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "tasklist",
    "smartquotes",
]
myst_heading_anchors = 3

# ── Auto-run Doxygen before Sphinx builds (optional) ─────────────────────────
# Uncomment if you want `sphinx-build` to also run doxygen automatically.
#
# def run_doxygen(_):
#     repo_root = Path(__file__).parent.parent
#     subprocess.run(["doxygen", "docs/Doxyfile"], cwd=repo_root, check=True)
#
# def setup(app):
#     app.connect("builder-inited", run_doxygen)
