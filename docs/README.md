# Documentation setup for libiso8583

## Prerequisites

```bash
# Doxygen (C++ XML extraction)
sudo apt install doxygen graphviz    # Linux
brew install doxygen graphviz        # macOS
choco install doxygen.install        # Windows

# Python tools (Sphinx + Breathe + theme)
pip install -r docs/requirements.txt
```

## Build locally

```bash
# 1. Run Doxygen — extracts C++ API into docs/_doxygen/xml/
doxygen docs/Doxyfile

# 2. Run Sphinx — renders HTML into docs/_build/html/
sphinx-build -b html docs docs/_build/html

# Open in browser
open docs/_build/html/index.html      # macOS
xdg-open docs/_build/html/index.html # Linux
start docs/_build/html/index.html     # Windows
```

## Structure

```
docs/
├── Doxyfile                  ← Doxygen config (reads include/, outputs XML)
├── conf.py                   ← Sphinx config (Breathe + Furo + MyST)
├── requirements.txt          ← pip dependencies
├── index.rst                 ← Table of contents
├── quickstart.md             ← Getting started guide
├── agents.md                 ← AI agent reference (wraps include/AGENTS.md)
├── changelog.md
├── api/
│   ├── isomessage.rst        ← ISOMessage, field types, ISOUtils
│   ├── isospec.rst           ← SpecDecoder::loadFromYaml
│   ├── isolog.rst            ← Logging API
│   ├── isoparser.rst         ← Custom parser base classes
│   ├── codec.rst             ← Encoding enums
│   └── interfaces.rst        ← Abstract base classes
└── internals/
    ├── yaml_format.md        ← YAML spec format reference
    └── encoding.md           ← Encoding system explanation
```

## Continuous deployment

The `.github/workflows/docs.yml` workflow:
- Builds on every push that touches `include/` or `docs/`
- Deploys to GitHub Pages automatically on push to `main`

**Enable GitHub Pages:**
1. Repo → Settings → Pages
2. Source → **GitHub Actions**
3. Next push to `main` deploys automatically

## Updating the docs

| What changed | What to do |
|---|---|
| C++ `///` doc comments in `include/` | Re-run Doxygen + Sphinx |
| Prose in `docs/*.md` / `docs/*.rst` | Re-run Sphinx only |
| `include/iso8583/AGENTS.md` | Re-run Sphinx (it is `{include}`d) |
