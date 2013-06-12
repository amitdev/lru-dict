LRU Dict
========

A fixed size dict like container which evicts Least Recently Used (LRU) items
once size limit is exceeded. There are many python implementations available
which does similar things. This is a fast and efficient C implementation which
is not thread safe. If you are looking for pure python version, look `else where <http://www.google.com/search?q=python+lru+dict>`_.

Usage
=====

This can be used to build a LRU cache. Usage is almost like a dict.

.. code:: python

  from lru import LRU
  l = LRU(5)         # Create an LRU container that can hold 5 items
  for i in range(5):
     l[i] = str(i)
  print l.items()    # Prints items in MRU order
  # Would print [(4, '4'), (3, '3'), (2, '2'), (1, '1'), (0, '0')]

  l[5] = '5'         # Inserting one more item should evict the old item
  print l.items()
  # Would print [(5, '5'), (4, '4'), (3, '3'), (2, '2'), (1, '1')]

  l[3]               # Accessing an item would make it MRU
  l.items()
  # Would print [(3, '3'), (5, '5'), (4, '4'), (2, '2'), (1, '1')]
  # Now 3 is in front

  l.keys()           # Can get keys alone in MRU order
  # Would print [3, 5, 4, 2, 1]

  del l[4]           # Delete an item
  print l.items()
  # Would print [(3, '3'), (5, '5'), (2, '2'), (1, '1')]


Install
=======

``pip install lru-dict`` or ``easy_install lru_dict``.

When to use this
================

Like mentioned above there are many python implementations of an LRU. Use this
if you need a faster and memory efficient alternative. It is implemented with a
dict and associated linked list to keep track of LRU order. See code for a more
detailed explanation.
