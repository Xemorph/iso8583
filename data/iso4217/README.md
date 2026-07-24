# data/iso4217/

Gevendorte ISO-4217-Rohdaten - **bewusst nicht bei jedem Build neu heruntergeladen**
(siehe Begründung unten). `scripts/generate_currency_table.py` liest
`codes-all.csv` und erzeugt daraus `include/iso8583/detail/_currency_table.hh`.

## Quelle

`codes-all.csv` stammt von <https://github.com/datasets/currency-codes>
(Public Domain Dedication and License), das seinerseits die konsolidierte
Fassung der offiziellen SIX-Group-XML-Tabellen ist:

- [Table A.1 - Current currency & funds code list](https://www.six-group.com/dam/download/financial-information/data-center/iso-currrency/lists/list-one.xml)
- [Table A.3 - List of codes for historic denominations of currencies & funds](https://www.six-group.com/dam/download/financial-information/data-center/iso-currrency/lists/list-three.xml)

Stand des aktuellen Snapshots: siehe Git-Historie dieser Datei
(`git log -- data/iso4217/codes-all.csv`).

## Warum vendored statt beim Build heruntergeladen?

Ein `file(DOWNLOAD ...)` in `CMakeLists.txt`, das bei jedem Konfigurieren die
aktuelle Liste nachlädt, hätte für eine Bibliothek wie diese mehrere reale
Nachteile:

- **Keine reproduzierbaren Builds** - derselbe Git-Commit könnte an zwei
  Tagen zwei unterschiedliche Binärdateien erzeugen, wenn sich die Liste
  zwischenzeitlich ändert (neue Währung, Historisierung, Redenomination).
- **Air-gapped/Bank-Build-Umgebungen** - viele Build-Umgebungen im
  Finanzumfeld haben bewusst keinen Internetzugang oder blockieren
  unbekannte Hosts über einen Proxy. Ein Netzwerkfehler beim Laden der
  Währungsliste würde dann den GESAMTEN Build brechen, nicht nur die
  Währungsfunktion.
- **Keine stabile, versionierte Quelle** - es gibt keine offizielle
  ISO-4217-REST-API mit URL-Stabilitätsgarantie.
- **Kein Audit-Trail** - bei Neugenerierung pro Build sieht man im
  Git-Log nicht, wann/warum sich die Tabelle geändert hat.

Stattdessen: `codes-all.csv` wird bewusst und selten aktualisiert (von einem
Maintainer manuell ausgeführt, siehe `scripts/generate_currency_table.py`),
das Ergebnis ganz normal committed und per Pull Request reviewt - wie jede
andere Code-Änderung auch.

## Aktualisieren

```sh
curl -L -o data/iso4217/codes-all.csv \
    https://raw.githubusercontent.com/datasets/currency-codes/main/data/codes-all.csv
python3 scripts/generate_currency_table.py
git diff  # Änderungen prüfen, bevor committed wird
```
