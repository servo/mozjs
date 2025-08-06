=================================
Using third-party Python packages
=================================

Mach and its associated commands have a variety of 3rd-party Python dependencies. Many of these
are vendored in ``third_party/python``, while others are installed at runtime via ``pip``.

The dependencies of Mach itself can be found at ``python/sites/mach.txt``. Mach commands
may have additional dependencies which are specified at ``python/sites/<site>.txt``.

For example, the following Mach command would have its 3rd-party dependencies declared at
``python/sites/foo.txt``.

.. code:: python

    @Command(
        "foo-it",
        virtualenv_name="foo",
    )
    # ...
    def foo_it_command():
        import specific_dependency

The format of ``<site>.txt`` files are documented further in the
:py:class:`~mach.requirements.MachEnvRequirements` class.

Adding a Python package
=======================

There's two ways of using 3rd-party Python dependencies:

* :ref:`pip install the packages <python-pip-install>`. Python dependencies with native code must
  be installed using ``pip``. This is the recommended technique for adding new Python dependencies.
* :ref:`Vendor the source of the Python package in-tree <python-vendor>`. Dependencies of the Mach
  core logic or of building Firefox itself must be vendored.

.. note::

    For dependencies that meet both restrictions (dependency of Mach/build, *and* has
    native code), see the :ref:`mach-and-build-native-dependencies` section below.

.. _python-pip-install:

``pip install`` the package
~~~~~~~~~~~~~~~~~~~~~~~~~~~

To add a ``pip install``-d package dependency, add it to your site's
``python/sites/<site>.txt`` manifest file:

.. code:: text

    ...
    pypi:new-package==<version>
    ...

.. note::

    Some tasks are not permitted to use external resources, and for those we can
    publish packages to an internal PyPI mirror.
    See `how to upload to internal PyPI <https://wiki.mozilla.org/ReleaseEngineering/How_To/Upload_to_internal_Pypi>`_
    for more details.

.. _python-vendor:

Vendoring Python packages
~~~~~~~~~~~~~~~~~~~~~~~~~

To vendor a Python package run ``./mach vendor python --add
<package>~=<major>.<minor>``. This will add your dependency to
``third_party/python/pyproject.toml`` then begin the re-vendoring process
for all dependencies. The `pyproject.toml` is used by ``uv`` to create a
lockfile (``uv.lock``) that ensures all the dependencies are compatible.
This lockfile is then used to generate a ``third_party/python/requirements.txt``
which is then used by ``pip`` to download all dependencies into the
``third_party/python`` directory.

.. note::
    The dependency you are attempting to add may not be compatible with what's
    already vendored. In this case, the lockfile generation/dependency
    resolution will fail with an error message along the lines of ``No
    solution found when resolving dependencies:``. You may be able to get
    around this by pinning your dependency to a newer or older version. If
    that doesn't work you can try modifying the pin(s) of the already vendored
    dependency(ies) that are causing the conflict(s).

    Beware that this is a rather painful process. Changing the version of an
    already vendored dependency may break functionality somewhere in the codebase.
    This means that even if you get ``uv`` to make a compatible lockfile, you
    may have caused a breakage somewhere else that ``uv`` cannot foresee. It is
    your responsibility to fix anything you break, otherwise your changes will be
    rejected or backed out if the issue isn't discovered until after landing.

    If you change pins for packages to workaround issues, please add comments in the
    ``third_party/python/pyproject.toml`` for each necessary pin indicating why it's
    needed and which dependency(ies) need it. Doing so will make it much easier for
    the next person that comes along trying to do the same thing.

After the ``./mach vendor python`` completes successfully, you'll need to add that package
and any new transitive dependencies (you'll see them added in ``third_party/python/requirements.txt``)
to the associated site's dependency manifest in ``python/sites/<site>.txt``:

.. code:: text

    ...
    vendored:third_party/python/new-package
    vendored:third_party/python/new-package-dependency-foo
    vendored:third_party/python/new-package-dependency-bar
    ...

To remove a vendored package run ``./mach vendor python --remove <package>``. This re-creates the lockfile
with that dependency removed (along with any transitive dependencies that aren't shared) and re-vendor
everything.

.. note::
    - You can add or remove multiple packages at the same time: ``./mach vendor python --add <package_one> --add <package_two>``
    - If desired, you can add/remove dependencies manually in the ``third_party/python/pyproject.toml``. Once you've made your changes, just run ``./mach vendor python`` without the ``--add`` and/or ``--remove`` arguments.

After the ``./mach vendor python`` completes successfully you'll need to remove the package and transitive
dependencies from all the site manifest files (``python/sites/<site>.txt``) that used the removed package(s).

.. note::

    The following policy applies to **ALL** vendored packages:

    * Vendored PyPI libraries **MUST NOT** be modified
    * Vendored libraries **SHOULD** be released copies of libraries available on
      PyPI.

      * When considering manually vendoring a package, discuss the situation with
        the ``#build`` team to ensure that other, more maintainable options are exhausted.

.. note::

    We require that it is possible to build Firefox using only a checkout of the source,
    without depending on a package index. This ensures that building Firefox is
    deterministic and dependable, avoids packages from changing out from under us,
    and means we’re not affected when 3rd party services are offline. We don't want a
    DoS against PyPI or a random package maintainer removing an old tarball to delay
    a Firefox chemspill. Therefore, packages required by Mach core logic or for building
    Firefox itself must be vendored.

If the vendored dependencies in the ``third_party/python/pyproject.toml`` are not pinned with
``==``, they can be automatically upgraded. You can upgrade either a single package,
or all packages.

To upgrade an individual unpinned package just run ``./mach vendor python --upgrade-package <package>``. You can also update
multiple specific packages at the same time: ``./mach vendor python --upgrade-package <package_one> --upgrade-package <package_two>``

To upgrade all unpinned packages just run ``./mach vendor python --upgrade``.

For both cases the process is essentially the same. ``uv`` is invoked and it will determine if there is/are
newer versions available. If there aren't any compatible upgrades available then nothing will be vendored. If
there are, then everything will be re-vendored.

.. note::
    If an upgrade adds new transitive dependencies, you will need to add them to the site(s) manifest files
    (the same as you need to when adding a new package).

By default ``./mach vendor python`` only fully runs if changes are detected in the ``uv.lock`` file. If you
want to force the full vendor to run, just add ``--force``.


If the package contains optional native dependencies, they won't be compiled as
part of the vendored package. It is however possible to prefer the pypi version
which may contain the native bits, while allowing to fallback to the vendored
version:

.. code:: text

    ...
    vendored-fallback:pypi-package-name:third_party/python/new-package:explanation
    ...


.. _mach-and-build-native-dependencies:

Mach/Build Native 3rd-party Dependencies
========================================

There are cases where Firefox is built without being able to ``pip install``, but where
native 3rd party Python dependencies enable optional functionality. This can't be solved
by vendoring the platform-specific libraries, as then each one would have to be stored
multiple times in-tree according to how many platforms we wish to support.

Instead, this is solved by pre-installing such native packages onto the host system
in advance, then having Mach attempt to use such packages directly from the system.
This feature is only viable in very specific environments, as the system Python packages
have to be compatible with Mach's vendored packages.

.. note:

    All of these native build-specific dependencies **MUST** be optional requirements
    as to support the "no strings attached" builds that only use vendored packages.

To control this behaviour, the ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE`` environment
variable can be used:

.. list-table:: ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE``
    :header-rows: 1

    * - ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE``
      - Behaviour
    * - ``"pip"``
      - Mach will ``pip install`` all needed dependencies from PyPI at runtime into a Python
        virtual environment that's reused in future Mach invocations.
    * - ``"none"``
      - Mach will perform the build using only vendored packages. No Python virtual environment
        will be created for Mach.
    * - ``"system"``
      - Mach will use the host system's Python packages as part of doing the build. This option
        allows the usage of native Python packages without leaning on a ``pip install`` at
        build-time. This is generally slower because the system Python packages have to
        be asserted to be compatible with Mach. Additionally, dependency lockfiles are ignored,
        so there's higher risk of breakage. Finally, as with ``"none"``, no Python virtualenv
        environment is created for Mach.
    * - ``<unset>``
      - Same behaviour as ``"pip"`` if ``MOZ_AUTOMATION`` isn't set. Otherwise, uses
        the same behaviour as ``"none"``.

There's a couple restrictions here:

* ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE`` only applies to the top-level ``"mach"`` site,
   the ``"common"`` site and the ``"build"`` site. All other sites will use ``pip install`` at
   run-time as needed.

* ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE="system"`` is not allowed when using any site other
  than ``"mach"``, ``"common"`` or ``"build"``, because:

  * As described in :ref:`package-compatibility` below, packages used by Mach are still
    in scope when commands are run, and
  * The host system is practically guaranteed to be incompatible with commands' dependency
    lockfiles.

The ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE`` environment variable fits into the following use
cases:

Mozilla CI Builds
~~~~~~~~~~~~~~~~~

We need access to the native packages of ``zstandard`` and ``psutil`` to extract archives and
get OS information respectively. Use ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE="system"``.

Mozilla CI non-Build Tasks
~~~~~~~~~~~~~~~~~~~~~~~~~~

We generally don't want to create a Mach virtual environment to avoid redundant processing,
but it's ok to ``pip install`` for specific command sites as needed, so leave
``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE`` unset (``MOZ_AUTOMATION`` implies the default
behaviour of ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE="none"``).

In cases where native packages *are* needed by Mach, use
``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE="pip"``.

Downstream CI Builds
~~~~~~~~~~~~~~~~~~~~

Sometimes these builds happen in sandboxed, network-less environments, and usually these builds
don't need any of the behaviour enabled by installing native Python dependencies.
Use ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE="none"``.

Gentoo Builds
~~~~~~~~~~~~~

When installing Firefox via the package manager, Gentoo generally builds it from source rather than
distributing a compiled binary artifact. Accordingly, users doing a build of Firefox in this
context don't want stray files created in ``~/.mozbuild`` or unnecessary ``pip install`` calls.
Use ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE="none"``.

Firefox Developers
~~~~~~~~~~~~~~~~~~

Leave ``MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE`` unset so that all Mach commands can be run,
Python dependency lockfiles are respected, and optional behaviour is enabled by installing
native packages.

.. _package-compatibility:

Package compatibility
=====================

Mach requires that all commands' package requirements be compatible with those of Mach itself.
(This is because functions and state created by Mach are still usable from within the commands, and
they may still need access to their associated 3rd-party modules).

However, it is OK for Mach commands to have package requirements which are incompatible with each
other. This allows the flexibility for some Mach commands to depend on modern dependencies while
other, more mature commands may still only be compatible with a much older version.

.. note::

    Only one version of a package may be vendored at any given time. If two Mach commands need to
    have conflicting packages, then at least one of them must ``pip install`` the package instead
    of vendoring.

    If a Mach command's dependency conflicts with a vendored package, and that vendored package
    isn't needed by Mach itself, then that vendored dependency should be moved from
    ``python/sites/mach.txt`` to its associated environment.
