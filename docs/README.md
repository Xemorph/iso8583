# Doku-Aufbau für libiso8583

## Voraussetzungen

```bash
# Doxygen (C++-XML-Extraktion)
sudo apt install doxygen graphviz    # Linux
brew install doxygen graphviz        # macOS
choco install doxygen.install        # Windows

# Python-Tools (Sphinx + Breathe + Theme)
pip install -r docs/requirements.txt
```

## Lokaler Build

```bash
# 1. Doxygen ausführen – extrahiert die C++-API nach docs/_doxygen/xml/
doxygen docs/Doxyfile

# 2. Sphinx ausführen – rendert das HTML nach docs/_build/html/
sphinx-build -b html docs docs/_build/html

# Im Browser öffnen
open docs/_build/html/index.html      # macOS
xdg-open docs/_build/html/index.html # Linux
start docs/_build/html/index.html     # Windows
```

## Struktur

```
docs/
├── Doxyfile                  ← Doxygen-Konfiguration (liest include/, schreibt XML)
├── conf.py                   ← Sphinx-Konfiguration (Breathe + Furo + MyST)
├── requirements.txt          ← pip-Abhängigkeiten
├── index.rst                 ← Inhaltsverzeichnis
├── quickstart.md             ← Einstieg (Installation & Grundgebrauch)
├── agents.md                 ← KI-Agenten-Referenz (bindet include/iso8583/AGENTS.md ein)
├── changelog.md              ← Änderungsprotokoll (spiegelt CHANGELOG.md im Repo-Root)
├── api/
│   ├── isomessage.rst        ← ISOMessage, Feldtypen, ISOUtils
│   ├── isospec.rst           ← YAML-Spec-Loader (SpecDecoder)
│   ├── isolog.rst            ← Logging-API
│   ├── isoparser.rst         ← Basisklassen für eigene Parser
│   ├── codec.rst             ← Encoding-Enums
│   └── interfaces.rst        ← Abstrakte Basisklassen
└── internals/
    ├── yaml_format.md        ← Referenz für das YAML-Spezifikationsformat
    └── encoding.md           ← Erläuterung des Encodings-Systems
```

## Kontinuierliche Veröffentlichung

Der Workflow `.github/workflows/docs.yml`:
- Baut bei jedem Push, der `include/` oder `docs/` betrifft
- Veröffentlicht automatisch auf GitHub Pages bei Push auf `main`

**GitHub Pages aktivieren:**
1. Repo → Settings → Pages
2. Source → **GitHub Actions**
3. Der nächste Push auf `main` veröffentlicht automatisch

## Doku aktualisieren

| Was sich geändert hat | Was zu tun ist |
|---|---|
| C++-`///`-Dokukommentare in `include/` | Doxygen + Sphinx neu ausführen |
| Fließtext in `docs/*.md` / `docs/*.rst` | Nur Sphinx neu ausführen |
| `include/iso8583/AGENTS.md` | Sphinx neu ausführen (wird per `{include}` eingebunden) |