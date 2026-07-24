#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_currency_table.py
===========================================================================
Erzeugt include/iso8583/detail/_currency_table.hh aus data/iso4217/codes-all.csv.

WICHTIG: Dieses Skript läuft NICHT automatisch bei jedem Build (siehe
data/iso4217/README.md für die Begründung). Ein Maintainer führt es bewusst
und selten manuell aus, wenn die Währungsliste aktualisiert werden soll:

    curl -L -o data/iso4217/codes-all.csv \
        https://raw.githubusercontent.com/datasets/currency-codes/main/data/codes-all.csv
    python3 scripts/generate_currency_table.py
    git diff   # Änderungen vor dem Commit prüfen

Die Eingabedatei (data/iso4217/codes-all.csv) ist die konsolidierte
ISO-4217-Liste von https://github.com/datasets/currency-codes, die
ihrerseits direkt aus den offiziellen SIX-Group-XML-Tabellen (Table A.1 -
aktuelle Währungen, Table A.3 - historische Bezeichnungen) abgeleitet ist.

Verarbeitungsschritte:
  1. Nur AKTIVE Einträge (keine WithdrawalDate) mit vorhandenem Alpha- UND
     Numeric-Code übernehmen - "No universal currency"-Platzhalterzeilen
     (z.B. Antarktis) und historische Währungen (z.B. AFA, FIM) fallen raus.
  2. Nach (AlphabeticCode, NumericCode) deduplizieren - dieselbe Währung
     kommt in der Quelle einmal PRO LAND vor (EUR z.B. 38x), wir wollen aber
     genau einen Tabelleneintrag pro Währung.
  3. MinorUnit "-" (Edelmetalle XAU/XAG/XPD/XPT, SDR, Testcodes, XXX) wird
     als 0 Dezimalstellen behandelt - ISO 4217 definiert dafür keine
     Nachkommastellen, 0 ist der sinnvollste Default für ein numerisches Feld.
  4. Alphabetisch nach AlphabeticCode sortiert, für ein deterministisches,
     diff-freundliches Ergebnis.
===========================================================================
"""
import csv
import datetime
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
INPUT_CSV = REPO_ROOT / "data" / "iso4217" / "codes-all.csv"
OUTPUT_HH = REPO_ROOT / "include" / "iso8583" / "detail" / "_currency_table.hh"


def load_currencies(csv_path: pathlib.Path):
    with open(csv_path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    # Nur aktive Einträge mit vorhandenem Alpha-/Numeric-Code
    active = [
        r for r in rows
        if r["WithdrawalDate"].strip() == ""
        and r["AlphabeticCode"].strip()
        and r["NumericCode"].strip()
    ]

    # Nach (Alpha, Numeric) deduplizieren - eine Zeile pro Land, wir wollen
    # eine Zeile pro Währung.
    seen: dict[tuple[str, str], dict] = {}
    for r in active:
        key = (r["AlphabeticCode"].strip(), r["NumericCode"].strip())
        seen.setdefault(key, r)

    currencies = []
    for (alpha, numeric), r in seen.items():
        minor = r["MinorUnit"].strip()
        decimals = 0 if minor in ("", "-", "N.A.") else int(minor)
        currencies.append({
            "alpha": alpha,
            "numeric": int(numeric),
            "decimals": decimals,
            "name": r["Currency"].strip(),
        })

    currencies.sort(key=lambda c: c["alpha"])
    return currencies


def render_header(currencies: list[dict]) -> str:
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("// =============================================================================")
    lines.append("// _currency_table.hh - AUTOMATISCH GENERIERT, NICHT VON HAND BEARBEITEN.")
    lines.append("// =============================================================================")
    lines.append("//")
    lines.append("// Erzeugt von scripts/generate_currency_table.py aus data/iso4217/codes-all.csv")
    lines.append(f"// ({len(currencies)} eindeutige, aktive Währungscodes)")
    lines.append(f"// Generiert am: {datetime.date.today().isoformat()}")
    lines.append("//")
    lines.append("// Quelle: https://github.com/datasets/currency-codes (Public Domain), abgeleitet")
    lines.append("// aus den offiziellen SIX-Group-ISO-4217-XML-Tabellen. Siehe data/iso4217/README.md")
    lines.append("// für Provenienz und den (bewusst manuellen, nicht build-automatischen)")
    lines.append("// Aktualisierungsprozess.")
    lines.append("//")
    lines.append("// Zum Aktualisieren: scripts/generate_currency_table.py erneut ausführen,")
    lines.append("// NICHT diese Datei von Hand anpassen.")
    lines.append("//")
    lines.append("// WICHTIG: Reines Fragment, kein eigenständig inkludierbarer Header - wird")
    lines.append("// ausschließlich von include/iso8583/Currency.hh eingebunden (analog zu")
    lines.append("// detail/_codec_impl.hh, das ebenfalls nur von _codec.hh aus benutzt wird).")
    lines.append("// Setzt voraus, dass `class Currency` bereits sichtbar ist - kein eigenes")
    lines.append("// #include <iso8583/Currency.hh> hier, um einen zirkulären Include zu vermeiden.")
    lines.append("// =============================================================================")
    lines.append("")
    lines.append("// [stdc++]")
    lines.append("#include <array>")
    lines.append("")
    lines.append("namespace TNG_NAMESPACE {")
    lines.append("    namespace currency {")
    lines.append("        namespace detail {")
    lines.append("")
    lines.append(f"            inline constexpr std::size_t kCurrencyTableSize = {len(currencies)};")
    lines.append("")
    lines.append("            inline constexpr std::array<Currency, kCurrencyTableSize> kCurrencyTable = {{")
    for c in currencies:
        # Name als Kommentar - rein informativ, kein Teil des geparsten Werts
        name_comment = c["name"].replace("*/", "* /")  # Kommentar-Ende nicht versehentlich schließen
        lines.append(
            f'                Currency("{c["alpha"]}", {c["numeric"]}, {c["decimals"]}), '
            f'// {name_comment}'
        )
    lines.append("            }};")
    lines.append("")
    lines.append("        } // namespace detail")
    lines.append("    } // namespace currency")
    lines.append("} // namespace TNG_NAMESPACE")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    if not INPUT_CSV.exists():
        print(f"FEHLER: {INPUT_CSV} nicht gefunden.", file=sys.stderr)
        print("Siehe data/iso4217/README.md zum Beschaffen der Quelldatei.", file=sys.stderr)
        return 1

    currencies = load_currencies(INPUT_CSV)
    if not currencies:
        print("FEHLER: Keine Währungen aus der CSV extrahiert - Format geändert?", file=sys.stderr)
        return 1

    OUTPUT_HH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_HH.write_text(render_header(currencies), encoding="utf-8")

    print(f"{len(currencies)} Währungen -> {OUTPUT_HH.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
