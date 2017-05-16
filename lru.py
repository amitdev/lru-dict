""" LRU(size, callback=None) -> new LRU dict that can store up to size elements
    An LRU dict behaves like a standard dict, except that it stores only fixed
    set of elements. Once the size overflows, it evicts least recently used
    items.  If a callback is set it will call the callback with the evicted key
     and item.

    Eg:
    >>> l = LRU(3)
    >>> for i in range(5):
    >>>   l[i] = str(i)
    >>> l.keys()
    [2,3,4]

    Note: An LRU(n) can be thought of as a dict that will have the most
    recently accessed n items.
"""

class Node(object):
    '''Linked List Node'''
    _prev = None
    _next = None
    key = None
    value = None

    def __repr__(self):
        return repr(self.value)

def _lru_remove_node(self, node):
    if self.first is node:
        self.first = node.next
    if self.last is node:
        self.last = node.prev
    if node.prev:
        node.prev.next = node.next
    if node.next:
        node.next.prev = node.prev
    node.next = node.prev = None

def _lru_add_node_at_head(self, node):
    node.prev = None
    if not self.first:
        self.first = self.last = node
        node.next = None
    else:
        node.next = self.first
        if node.next:
            node.next.prev = node
        self.first = node

def _lru_delete_last(self):
    n = self.last
    if not self.last:
        return
    if self.callback:
        self.callback(n.key, n.value)
    _lru_remove_node(self, n)
    del self.dict[n.key]

def _lru_length(self):
    return len(self.dict)

def collect(self, getterfunc):
    v = [None] * len(self.dict)
    curr = self.first
    i = 0
    while curr:
        v[i] = getterfunc(curr)
        i += 1
        curr = curr.next
    assert i == len(self.dict)
    return v

class LRU(object):
    """
    LRU(size, callback=None) -> new LRU dict that can store up to size elements
    An LRU dict behaves like a standard dict, except that it stores only fixed
    set of elements. Once the size overflows, it evicts least recently used
    items.  If a callback is set it will call the callback with the evicted key
     and item.

    Eg:
    >>> l = LRU(3)
    >>> for i in range(5):
    >>>   l[i] = str(i)
    >>> l.keys()
    [2,3,4]

    Note: An LRU(n) can be thought of as a dict that will have the most
    recently accessed n items.
    """
    misses = 0
    hits = 0
    first = None
    last = None
    size = 0

    def __init__(self, size, callback=None):
        self.size = size
        if callback and not callable(callback):
            raise TypeError('parameter must be callable')
        if size <=0:
            raise ValueError('Size should be a positive number')
        self.dict = {}
        self.callback = callback

    def __del__(self):
        self.clear()

    def __repr__(self):
        return repr(self.dict)

    def __len__(self):
        return len(self.dict)

    def __contains__(self, key):
        '''Check if key is there in L'''
        return key in self.dict

    def __getitem__(self, key):
        try:
            node = self.dict[key]
        except KeyError:
            self.misses += 1
            raise
        assert isinstance(node, Node)
        # We don't need to move the node when it's already self.first
        if node is not self.first:
            _lru_remove_node(self, node)
            _lru_add_node_at_head(self, node)

        self.hits += 1
        return node.value

    def __setitem__(self, key, value):
        node = self.dict.get(key, None)
        if value:
            if node:
                node.value = value
                _lru_remove_node(self, node)
                _lru_add_node_at_head(self, node)
            else:
                node = Node()
                node.key = key
                node.value = value
                self.dict[key] = node
                if len(self.dict) > self.size:
                    _lru_delete_last(self)
                _lru_add_node_at_head(self, node)
        else:
            del self.dict[key]
            _lru_remove_node(self, node)
        return 0

    def __delitem__(self, key):
        node = self.dict.get(key, None)
        del self.dict[key]
        _lru_remove_node(self, node)
        return 0

    def keys(self):
        '''list of L's keys in MRU order'''
        return collect(self, lambda x: x.key)

    def values(self):
        '''list of L's values in MRU order'''
        return collect(self, lambda x: x.value)

    def items(self):
        '''list of L's items (key, value) in MRU order'''
        return collect(self, lambda x: (x.key, x.value))

    def has_key(self, key):
        '''Check if key is there in L'''
        return key in self.dict

    def get(self, key, instead=None):
        '''If L has key return its value, otherwise instead'''
        result = self.dict.get(key, None)
        if result:
            self.hits += 1
            return result.value
        self.misses += 1
        return instead

    def set_size(self, newSize):
        '''set size of LRU'''
        if newSize < 0:
            raise ValueError("size should be a positive number")
        while len(self) > newSize:
            _lru_delete_last(self)
        self.size = newSize

    def get_size(self):
        '''get size of LRU'''
        return self.size

    def clear(self):
        '''clear LRU'''
        c = self.first
        while c:
            n = c
            c = n.next
            _lru_remove_node(self, n)
        self.dict = {}
        self.hits = 0
        self.misses = 0

    def get_stats(self):
        '''returns a tuple with cache hits and misses'''
        return self.hits, self.misses

    def peek_first_item(self):
        '''returns the MRU item (key,value) without changing key order'''
        if self.first:
            return (self.first.key, self.first.value)

    def peek_last_item(self):
        '''returns the MRU item (key,value) without changing key order'''
        if self.last:
            return (self.last.key, self.last.value)

    def update(self, *args, **kwargs):
        '''update value for key in LRU'''
        if args:
            if isinstance(args[0], dict):
                for key, value in args[0].items():
                    self[key] = value
        for key, value in kwargs.items():
            self[key] = value

    def set_callback(self, callback):
        '''set a callback to call when an item is evicted.'''
        if callback is None:
            self.callback = None
        elif not callable(callback):
            raise TypeError('parameter must be callable')
        else:
            self.callback = callback
