from setuptools import setup, Extension

extensions = [
    Extension("lru", ["src/lru.c"]),
]

args = {
    "include_package_data": True,
    "exclude_package_data": {"": ["*.c"]},
}

setup(ext_modules=extensions, **args)
