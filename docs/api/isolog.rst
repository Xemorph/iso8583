ISOLog — Logging API
====================

.. doxygenenum:: tng::log::Level
   :project: libiso8583

.. doxygenclass:: tng::log::ISOLogger
   :project: libiso8583
   :members:

.. note::

   ``tng::log::QuillBridge`` is a header-only class activated only when
   ``<quill/LogMacros.h>`` is included before ``ISOLog.hh`` (i.e. when
   ``QUILL_VERSION`` is defined).  It is not extracted by Doxygen in a
   standard build — see :ref:`quickstart:logging` for usage.

.. doxygenfunction:: tng::log::setLevel
   :project: libiso8583

.. doxygenfunction:: tng::log::getLevel
   :project: libiso8583

.. doxygenfunction:: tng::log::setLogger
   :project: libiso8583

.. doxygenfunction:: tng::log::setQuillLogger
   :project: libiso8583
