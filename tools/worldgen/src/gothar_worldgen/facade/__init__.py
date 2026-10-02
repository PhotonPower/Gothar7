"""Facade reference tool (W4): 360° images -> rectified facade views -> per-building overrides.

The core modules are UI-independent: ``equirect`` (projection), ``rectify`` (facade views),
``poses`` (camera positions from GPS tracks) and ``overrides`` (annotation JSON). The annotation
web UI builds on them.
"""
