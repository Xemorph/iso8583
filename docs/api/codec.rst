Codec — Encoding-Enums und -Funktionen
=======================================

Der Namespace ``iso8583::codec`` enthält die Präfix- und
Länge-Encoder sowie die zugehörigen Enum- und Funktionsdeklarationen,
über die Feldformat-Strings wie ``LLCHAR`` oder ``LLLNUM``
interpretiert werden. Der veraltete Alias ``iso8583::ISOCodec``
(bevorzugt ist die ``codec::``-Schreibweise) bleibt aus
Kompatibilitätsgründen erhalten. Die Format-Strings selbst sind in
:doc:`../internals/yaml_format` dokumentiert.

Die drei Enums ``PrefixEncoder``, ``Length`` und ``Encoder`` wählen
Präfix-Kodierung bzw. Längenformat (L … LLLL) bzw. Inhalts-Encoder;
die Template-Funktionen ``parsed_length``, ``encode_length``,
``decode_length``, ``as``, ``to`` und ``required_sz_for_as``
wählen diese Parameter zur Compile-Zeit über ihre
Template-Argumente.

.. doxygennamespace:: iso8583::codec
   :project: libiso8583
   :members: