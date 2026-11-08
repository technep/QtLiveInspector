"""qli_version.py — single source of truth for the tool versions.

Must match the CMake `project(QtLiveInspector VERSION ...)` in the
top-level CMakeLists.txt; VersionConsistencyTest in tests/CMakeLists.txt
fails the build+test cycle when they drift apart.
"""

__version__ = "0.3.0"
