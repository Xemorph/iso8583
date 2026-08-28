ISOMessage
==========

Das zentrale Objekt von libiso8583. Jede dekodiert oder gebaute
ISO-8583-Nachricht wird als ``ISOMessage`` repräsentiert
(publicer Alias: ``iso8583::Message``).

Feldtypen
---------

Die Feldtypen sind Aliase für ``ISOComponent`` mit passendem
Wertetyp – sie bestimmen, wie ein DE gelesen oder geschrieben
wird:

.. doxygentypedef:: iso8583::OpaqueField
   :project: libiso8583

.. doxygentypedef:: iso8583::BinaryField
   :project: libiso8583

.. doxygentypedef:: iso8583::FastBinaryField
   :project: libiso8583

.. doxygentypedef:: iso8583::Bitmap
   :project: libiso8583

.. doxygentypedef:: iso8583::CodeField
   :project: libiso8583

.. doxygentypedef:: iso8583::Message
   :project: libiso8583

Die Klasse selbst
-----------------

.. doxygenclass:: iso8583::ISOMessage
   :project: libiso8583
   :members:
   :undoc-members:

Hilfsfunktionen
---------------

Flache Abfragen über verschachtelte Tags (Punkt-Notation) und
weitere Helferfunktionen finden sich im Namespace ``iso8583::utils``.
Der veraltete Alias ``iso8583::ISOUtils`` (bevorzugt ist die
``utils::``-Schreibweise) bleibt aus Kompatibilitätsgründen erhalten.

.. doxygennamespace:: iso8583::utils
   :project: libiso8583
   :members:

Header-Klassen
--------------

Header-Klassen beschreiben, wie Prä- und Post-Header (z. B.
BASE1/Visa) an die Nachricht angehängt werden:

.. doxygenclass:: iso8583::BaseHeader
   :project: libiso8583
   :members:

.. doxygenclass:: iso8583::BASE1Header
   :project: libiso8583
   :members:

.. doxygenclass:: iso8583::WLP_FOHeader
   :project: libiso8583
   :members: