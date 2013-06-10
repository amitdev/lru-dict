LRU Dict
========

A fixed size dict like container which evicts Least Recently Used (LRU) items
once size limit is exceeded. There are many python implementations available
which does similar things. This is a fast and efficient C implementation which
is not thread safe. If you are looking for pure python version, look `else where <www.google.com/search?q=python+lru+dict>`.

Usage
=====

This can be used to build a LRU cache. Usage is almost like a dict.

.. code:: python

  >>> from lru import LRU
  >>> l = LRU(5)         # Create an LRU container that can hold 5 items
  >>> for i in range(0):
  >>>   l[i] = str(i)
  >>> l.items()          # Prints items in MRU order
  [(4, '4'), (3, '3'), (2, '2'), (1, '1'), (0, '0')]
  >>> l[5] = '5'         # Inserting one more item should evict the old item
  >>> l.items()
  [(5, '5'), (4, '4'), (3, '3'), (2, '2'), (1, '1')]  # Oldest item is gone now
  >>> l[3]               # Accessing an item would make it MRU
  '3'
  >>> l.items()
  [(3, '3'), (5, '5'), (4, '4'), (2, '2'), (1, '1')] # Now 3 is in front
  >>> l.keys()           # Can get keys alone in MRU order
  [3, 5, 4, 2, 1]
  >>> del l[4]           # Delete an item
  >>> l.items()
  [(3, '3'), (5, '5'), (2, '2'), (1, '1')]


When to use this
================

Like mentioned above there are many python implementations of an LRU. Use this
if you need a faster and memory efficient alternative.
