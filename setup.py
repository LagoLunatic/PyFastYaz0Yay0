from distutils.core import setup, Extension

extra_compile_args = []
extra_compile_args.append('-Werror=incompatible-pointer-types')
if False:
    extra_compile_args.append('-Werror')

module1 = Extension('pyfastyaz0yay0',
                    define_macros = [('MAJOR_VERSION', '2'),
                                     ('MINOR_VERSION', '0')],
                    include_dirs = [],
                    libraries = [],
                    library_dirs = [],
                    sources = ['pyfastyaz0yay0.c'],
                    extra_compile_args = extra_compile_args)

setup (name = 'PyFastYaz0Yay0',
       version = '2.0',
       description = 'Functions for Yaz0 and Yay0 compressing and decompressing files, written in C.',
       author = 'LagoLunatic',
       author_email = '',
       url = 'https://github.com/LagoLunatic',
       long_description = '''
Python module written in C for Yaz0 and Yay0 compressing and decompressing files with better performance than native Python code.
''',
       ext_modules = [module1])
