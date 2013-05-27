from distutils.core import setup, Extension

module1 = Extension('lru',
                    sources = ['lru.c'])

setup (name = 'lru',
       version = '1.0',
       description = 'An Dict like LRU container.',
       ext_modules = [module1])
