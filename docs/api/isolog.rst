ISOLog — Logging-API
====================

Die Bibliothek protokolliert intern über den abstrakten Logger
``ISOLogger``. Standardmäßig wird die Konsole mit
``Level::WARN`` verwendet; per ``setLevel`` lässt sich der
Level ändern oder das Logging vollständig stilllegen.

.. doxygenenum:: iso8583::log::Level
   :project: libiso8583

.. doxygenclass:: iso8583::log::ISOLogger
   :project: libiso8583
   :members:

.. doxygenfunction:: iso8583::log::setLevel
   :project: libiso8583

.. doxygenfunction:: iso8583::log::getLevel
   :project: libiso8583

.. doxygenfunction:: iso8583::log::setLogger
   :project: libiso8583

.. doxygenfunction:: iso8583::log::setQuillLogger
   :project: libiso8583

.. note::

   ``iso8583::log::QuillBridge`` ist eine header-only-Klasse, die
   nur aktiv ist, wenn ``<quill/LogMacros.h>`` vor ``ISOLog.hh``
   eingebunden wird (d. h. ``QUILL_VERSION`` definiert ist). In
   einem Standard-Build extrahiert Doxygen sie nicht – die
   Verwendung ist in der Logging-Sektion von :doc:`../quickstart`
   beschrieben.