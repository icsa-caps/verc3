==================================
VerC3: Verification Toolkit for C3
==================================

Contains a library for explicit-state model checking in C++. Example models can
be found in ``src/models``.

Current features:

* Symmetry reduction.

* Several search strategies: memory-usage friendly hashing of states only; and
  precise but more demand on memory usage.

* Synthesis.

Build Instructions
------------------

Dependencies:

* TCMalloc (optional, but improves performance noticeably)

.. code:: sh

    $ ./third_party/update.sh
    $ scons -j4 # add --release for optimized build

Running
-------

.. code:: sh

    $ ./build/bin/verc3 runmodel list
    $ ./build/bin/verc3 runmodel <model-name>

Citation
--------

If you use this library or any parts of the code in your work, we would
appreciate if you cite:

.. code-block::

    @inproceedings{ElverBJN2018,
      author    = {Marco Elver and Christopher J. Banks and Paul Jackson and
                   Vijay Nagarajan},
      title     = {{VerC3}: {A} {L}ibrary for {E}xplicit {S}tate {S}ynthesis of
                   {C}oncurrent {S}ystems},
      booktitle = {Design, Automation {\&} Test in Europe (DATE)},
      publisher = {{IEEE}},
      month     = mar,
      year      = {2018},
      venue     = {Dresden, Germany}
    }

