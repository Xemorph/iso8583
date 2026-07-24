ISOLog — Logging API
====================

.. doxygenenum:: iso8583::log::Level
   :project: libiso8583

.. doxygenclass:: iso8583::log::ISOLogger
   :project: libiso8583
   :members:

.. note::

   ``tng::log::QuillBridge`` is a header-only class activated only when
   ``<quill/LogMacros.h>`` is included before ``ISOLog.hh`` (i.e. when
   ``QUILL_VERSION`` is defined).  It is not extracted by Doxygen in a
   standard build — see :ref:`quickstart:logging` for usage.

.. doxygenfunction:: iso8583::log::setLevel
   :project: libiso8583

.. doxygenfunction:: iso8583::log::getLevel
   :project: libiso8583

.. doxygenfunction:: iso8583::log::setLogger
   :project: libiso8583
