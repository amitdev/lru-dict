from setuptools import setup, Extension
from platform import python_implementation

kwdict = {'name': 'lru-dict',
       'version': '1.1.6',
       'description': 'An Dict like LRU container.',
       'long_description': open('README.rst').read(),
       'author': 'Amit Dev',
       'url': 'https://github.com/amitdev/lru-dict',
       'license': 'MIT',
       'keywords': 'lru, dict',
       'classifiers': [
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Operating System :: POSIX',
        'Programming Language :: C',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: Implementation :: CPython',
        'Programming Language :: Python :: Implementation :: PyPy',
        'Topic :: Software Development :: Libraries :: Python Modules',
        ]
    }

if python_implementation() == 'PyPy':
    kwdict['py_modules'] = ['lru', ]
else:
    kwdict['ext_modules'] = [Extension('lru', sources = ['lru.c']), ]

setup (**kwdict)
