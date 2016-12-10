from setuptools import setup, Extension

module1 = Extension('lru',
                    sources = ['lru.c'])

setup (name = 'lru-dict',
       version = '1.1.5',
       description = 'An Dict like LRU container.',
       long_description = open('README.rst').read(),
       author='Amit Dev',
       url='https://github.com/amitdev/lru-dict',
       license='MIT',
       keywords='lru, dict',
       ext_modules = [module1],
       classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Operating System :: POSIX',
        'Programming Language :: C',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: Implementation :: CPython',
        'Topic :: Software Development :: Libraries :: Python Modules',
        ],
)
