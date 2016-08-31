==================================
verc3: Verification Toolkit for C3
==================================

Contains a library for explicit-state model checking in C++. Example models can
be found in ``src/models``.

Current features:

* Symmetry reduction.

* Several search strategies: memory-usage friendly hashing of states only; and
  precise but more demand on memory usage.

Build Instructions
------------------

Dependencies:

* TCMalloc (optional, but improves performance noticeably)

.. code:: sh

    $ ./third_party/update.sh
    $ scons -j4 # add --release for optimized build
