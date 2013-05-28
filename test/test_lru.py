import unittest
from lru import LRU

SIZES = [1, 2, 10, 1000]

class TestLRU(unittest.TestCase):

    def setUp(self):
        pass


    def test_invalid_size(self):
        self.assertRaises(ValueError, LRU, -1)
        self.assertRaises(ValueError, LRU, 0)

    def test_empty(self):
        l = LRU(1)
        self.assertEquals([], l.keys())

    def test_add_within_size(self):
        for size in SIZES:
	    l = LRU(size)
            for i in xrange(size):
                l[i] = str(i)
            self.assertEquals(range(size-1,-1,-1), l.keys())

    def test_delete_multiple_within_size(self):
        for size in SIZES:
            l = LRU(size)
            for i in xrange(size):
                l[i] = str(i)
            for i in xrange(0,size,2):
                del l[i]
            self.assertEquals(range(size-1,0,-2), l.keys())
            for i in range(0,size,2):
       	        with self.assertRaises(KeyError):
                    l[i]

    def test_add_multiple(self):
        for size in SIZES:
            l = LRU(size)
            for i in xrange(size):
                l[i] = str(i)
            l[size] = size
            self.assertEquals(range(size,0,-1), l.keys())

    def test_access_within_size(self):
        for size in SIZES:
            l = LRU(size)
            for i in xrange(size):
                l[i] = str(i)
            for i in xrange(size):
                self.assertEquals(l[i], str(i))

    def test_access(self):
        for size in SIZES:
            l = LRU(size)
            n = size * 2
            for i in xrange(n):
                l[i] = str(i)
            self.assertEquals(range(n-1,size-1,-1), l.keys())
            for i in xrange(size, n):
                self.assertEquals(l[i], str(i))

if __name__ == '__main__':
    unittest.main()
