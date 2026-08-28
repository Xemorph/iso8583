"""
Sphinx-Konfiguration für die libiso8583-Dokumentation.

Build:
    pip install sphinx breathe furo myst-parser
    doxygen docs/Doxyfile
    sphinx-build -b html docs docs/_build/html
"""

import subprocess
import sys
from pathlib import Path

# ── Projektinformationen ──────────────────────────────────────────────────────
project   = "libiso8583"
author    = "iso8583-Kontributoren"
release   = "0.2.0"
copyright = f"2024, {author}"

# ── Erweiterungen ─────────────────────────────────────────────────────────────
extensions = [
    "breathe",          # verbindet Doxygen-XML mit Sphinx RST/MD
    "myst_parser",      # erlaubt .md-Dateien als Sphinx-Seiten (AGENTS.md, …)
    "sphinx.ext.autosectionlabel",
    "sphinx.ext.viewcode",
    "sphinx.ext.todo",
]

# Abschnitts-Labels um den Dokumentnamen ergänzen, um Duplikat-Warnungen zu
# vermeiden, wenn dieselbe Überschrift in mehreren Dateien vorkommt
# (z. B. "Feldtypen" in api/isomessage.rst und agents.md).
autosectionlabel_prefix_document = True

# Harmlose, wiederkäuende Warnungen unterdrücken:
# - misc.highlighting_failure: Pygments hat keinen "cmake"-Lexer,
#   wechselt aber automatisch in relaxed Mode (AGENTS.md-CMake-Blöcke).
suppress_warnings = [
    "misc.highlighting_failure",
]

# ── Quelldateien ──────────────────────────────────────────────────────────────
source_suffix = {
    ".rst": "restructuredtext",
    ".md":  "markdown",
}
master_doc = "index"
exclude_patterns = ["_build", "_doxygen", "Thumbs.db", ".DS_Store", "README.md"]

# ── Theme: Furo (sauber, modern, responsiv) ───────────────────────────────────
html_theme = "furo"
html_static_path = []   # auf ["_static"] setzen, sobald eigenes CSS/JS dazu kommt

html_theme_options = {
    "sidebar_hide_name":        False,
    "navigation_with_keys":     True,
    "source_repository":        "https://github.com/Xemorph/iso8583/",
    "source_branch":            "main",
    "source_directory":         "include/",
    "footer_icons": [
        {
            "name":  "GitHub",
            "url":   "https://github.com/Xemorph/iso8583",
            "html":  """<svg …/>""",
            "class": "fa-brands fa-github",
        },
    ],
}

# ── Breathe: auf die Doxygen-XML-Ausgabe zeigen ───────────────────────────────
breathe_projects        = {"libiso8583": "./_doxygen/xml"}
breathe_default_project = "libiso8583"

# ── MyST: Markdown-Erweiterungen erlauben ─────────────────────────────────────
myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "tasklist",
    "smartquotes",
]
myst_heading_anchors = 3

# ── Doxygen automatisch vor Sphinx-Builds ausführen (optional) ────────────────
# Kommentar entfernen, wenn `sphinx-build` Doxygen automatisch ausführen soll.
#
# def run_doxygen(_):
#     repo_root = Path(__file__).parent.parent
#     subprocess.run(["doxygen", "docs/Doxyfile"], cwd=repo_root, check=True)
#
# def setup(app):
#     app.connect("builder-inited", run_doxygen)