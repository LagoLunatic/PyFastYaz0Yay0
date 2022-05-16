from distutils.core import setup, Extension

module1 = Extension('pyfastyaz0',
                    define_macros = [('MAJOR_VERSION', '1'),
                                     ('MINOR_VERSION', '0')],
                    include_dirs = [],
                    libraries = [],
                    library_dirs = [],
                    sources = ['pyfastyaz0.c'])

setup (name = 'PyFastYaz0',
       version = '1.0',
       description = 'Functions for Yaz0 compressing files written in C.',
       author = 'LagoLunatic',
       author_email = '',
       url = 'https://github.com/LagoLunatic',
       long_description = '''
Python module written in C for Yaz0 compressing files with better performance than native Python code.
''',
       ext_modules = [module1])
