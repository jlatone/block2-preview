
.. figure:: _static/block2-logo.png
   :width: 200
   :alt: block2 logo

.. only:: latex

   block2
   ######

**block2** is an efficient and highly scalable implementation of the Density Matrix Renormalization Group (DMRG)
for quantum chemistry, based on Matrix Product Operator (MPO) formalism.
The code is highly optimized for production level calculation of realistic systems.
It also provides plenty of options for tuning performance and new algorithm development.

The block2 code is developed as an improved version of `StackBlock <https://sanshar.github.io/Block/>`_,
where the low-level structure of the code has been completely rewritten.
The block2 code is developed and maintained in Garnet Chan group at Caltech.

Documentation: https://block2.readthedocs.io/en/latest/
Source code: https://github.com/block-hczhai/block2-preview

Contributors
""""""""""""

* Huanchen Zhai `@hczhai <https://github.com/hczhai>`_: DMRG and parallelization
* Henrik R. Larsson `@h-larsson <https://github.com/h-larsson>`_: DMRG-MRCI/MRPT and big site
* Seunghoon Lee `@seunghoonlee89 <https://github.com/seunghoonlee89>`_: Stochastic perturbative DMRG
* Zhi-Hao Cui `@zhcui <https://github.com/zhcui>`_: user interface

Features
""""""""

* State symmetry
    * U(1) particle number symmetry
    * SU(2) or U(1) spin symmetry
    * Abelian point group symmetry
    * Translational / Lz symmetry
* Sweep algorithms (1-site / 2-site / 2-site to 1-site transition)
    * Ground-State DMRG
        * Decomposition types: density matrix / SVD
        * Noise types: wavefunction / density matrix / perturbative
    * Multi-Target Excited-State DMRG
        * State-averaged / state-specific
    * MPS compression / addition
    * Expectation
    * Imaginary / real time evolution
        * Hermitian / non-Hermitian Hamiltonian
        * Time-step targeting method
        * Time dependent variational principle method
    * Green's function
* Finite-Temperature DMRG (ancilla approach)
* Low-Temperature DMRG (partition function approach)
* Particle Density Matrix (1-site / 2-site)
    * 1PDM / 2PDM
    * Transition 1PDM
    * Spin / charge correlation
* Quantum Chemistry MPO
    * Normal-Complementary (NC) partition
    * Complementary-Normal (CN) partition
    * Conventional scheme (switch between NC and CN near the middle site)
* Symbolic MPO simplification
* MPS initialization using occupation number
* Supported matrix representation of site operators
    * Block-sparse (outer) / dense (inner)
    * Block-sparse (outer) / elementwise-sparse (CSR, inner)
* Fermionic MPS algebra (non-spin-adapted only)
* Determinant overlap (non-spin-adapted only)
* Determinant/CSF overlap sampling
* Multi-level parallel DMRG
    * Parallelism over sites (2-site only)
    * Parallelism over sum of MPOs (non-spin-adapted only)
    * Parallelism over operators (distributed/shared memory)
    * Parallelism over symmetry sectors (shared memory)
    * Parallelism within dense matrix multiplications (MKL)
* Stochastic perturbative DMRG
* Uncontracted dynamic correlation
    * DMRG Multi-Reference Configuration Interaction (MRCI) of arbitrary order
    * DMRG Multi-Reference Averaged Quadratic Coupled Cluster (AQCC)/ Coupled Pair Functional (ACPF)
    * DMRG NEVPT2/3/..., REPT2/3/..., MR-LCC, ...
* Orbital Reordering
    * Fiedler
    * Genetic algorithm
* MPS Transformation
    * SU2 to SZ mapping
    * Point group mapping
    * Orbital basis rotation

.. raw:: latex

   \chapter{User Guide}

.. toctree::
   :maxdepth: 2
   :caption: User Guide

   user/installation
   user/basic
   user/advanced
   user/keywords
   user/mps-io
   user/references

.. raw:: latex

   \chapter{Developer Guide}

.. toctree::
   :maxdepth: 2
   :caption: Developer Guide

   developer/dmrg
   developer/orbital-rotation
   developer/pg-mapping
   developer/mpo-reloading
   developer/hints
   developer/notes

.. raw:: latex

   \chapter{API Reference}

.. toctree::
   :maxdepth: 2
   :caption: API Reference

   api/global
   api/sparse_matrix
   api/tensor_functions
   api/tools
