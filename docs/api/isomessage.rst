ISOMessage
==========

The central object of libiso8583.  Every decoded or constructed ISO 8583
message is represented as an ``ISOMessage``.

Field types
-----------

.. doxygentypedef:: ISOOpaqueField
   :project: libiso8583

.. doxygentypedef:: ISOBinaryField
   :project: libiso8583

.. doxygentypedef:: ISOBitmap
   :project: libiso8583

.. doxygentypedef:: ISOCodeField
   :project: libiso8583

ISOMessage class
----------------

.. doxygenclass:: iso8583::ISOMessage
   :project: libiso8583
   :members:
   :undoc-members:

ISOUtils
--------

.. doxygennamespace:: iso8583::ISOUtils
   :project: libiso8583
   :members:

Header classes
--------------

.. doxygenclass:: iso8583::BaseHeader
   :project: libiso8583
   :members:

.. doxygenclass:: iso8583::BASE1Header
   :project: libiso8583
   :members:
