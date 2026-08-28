ISOParser — Eigene Parser
=========================

Das öffentliche Interface für die Implementierung eigener
Parser. **Die meisten Benutzer brauchen diesen Header nicht** –
``ISOSpec.hh`` genügt zum Laden von YAML-Spezifikationen und
zum Dekodieren von Nachrichten.

Ein eigener Parser leitet von
:ref:`ISOParserPtrBase <interfaces>` ab und implementiert die
drei rein virtuellen Methoden (``emit_bitmap``, ``unparse``,
``parse``). Ein vollständiges Beispiel steht in der
Kopfdokumentation des Headers ``iso8583/ISOParser.hh``.

Die konkreten Implementierungen (``ISOBaseParser``,
``ISOFieldParser<>`` und die ``IFE_*``/``IFA_*``-Alias-Typen)
liegen unter ``src/`` und sind bewusst nicht Teil der
öffentlichen API.

.. doxygenfile:: ISOParser.hh
   :project: libiso8583