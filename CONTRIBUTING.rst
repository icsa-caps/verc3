Guidelines
==========

Following `C++ Core Guidelines <https://github.com/isocpp/CppCoreGuidelines>`_
where possible.

Naming and formatting is according to the `Google C++ Style Guide
<https://google.github.io/styleguide/cppguide.html>`_. Use ``scons format``.

New standards-compliant language features in C++14 are encouraged if they make
the code safer, more maintainable and/or easier to understand/use. The only
limiting factor is the oldest to be supported compiler versions.

Use ``scons {tidy,lint}``; they can report many issues, but not all.

Organization
============

+ ``doc``: Documentation.

  + ``api``: API documentation, auto generated with ``scons doc``.

+ ``include``: Public interface headers.

+ ``src``: Main binary and library sources. The modules (``.cc``, etc.)
  corresponding to public API headers go in here, as well as non-public headers
  and modules.

+ ``test``: Unit tests.

+ ``third_party``: Third-party dependencies.

