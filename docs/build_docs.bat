@echo off
setlocal
rem libiso8583-Dokumentation bauen
rem   1. Doxygen: C++-Header -> XML (wird von Breathe verwendet)
rem   2. Sphinx:  docs/ -> docs/_build/html
rem
rem Voraussetzungen:
rem   - doxygen im PATH
rem   - python-Pakete: pip install -r docs/requirements.txt
rem
rem Lauffeuerort-ueberpruefung: Das Skript arbeitet immer vom Repo-Root aus
rem (also z. B. auch, wenn man es aus docs/ heraus startete).
cd /d "%~dp0.."

echo [1/2] Doxygen: C++-Header -> docs/_doxygen/xml
doxygen docs/Doxyfile
if errorlevel 1 (
    echo Doxygen ist fehlgeschlagen - Sphinx-Schritt wird uebersprungen.
    exit /b 1
)

echo [2/2] Sphinx: docs/ -> docs/_build/html
sphinx-build -b html docs docs/_build/html
if errorlevel 1 (
    echo Sphinx-Build ist fehlgeschlagen.
    exit /b 1
)

echo.
echo Fertig. HTML unter docs\_build\html (index: docs\_build\html\index.html)
endlocal