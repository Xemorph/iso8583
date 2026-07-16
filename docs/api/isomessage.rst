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

.. doxygenclass:: tng::ISOMessage
   :project: libiso8583
   :members:
   :undoc-members:

ISOUtils
--------

.. doxygennamespace:: tng::ISOUtils
   :project: libiso8583
   :members:

Header classes
--------------

.. doxygenclass:: tng::BaseHeader
   :project: libiso8583
   :members:

.. doxygenclass:: tng::BASE1Header
   :project: libiso8583
   :members:

.. doxygenclass:: tng::WLP_FOHeader
   :project: libiso8583
   :members:
