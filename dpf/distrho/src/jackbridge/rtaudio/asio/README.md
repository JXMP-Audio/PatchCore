# ASIO for the PatchCore standalone (Windows)

PatchCore offers ASIO next to WASAPI on Windows. ASIO is compiled in only when these files are here
(`dpf/Makefile.plugins.mk` checks for `asio.cpp`):

- from Steinberg's ASIO SDK (GPLv3 release, 2025 or newer): `asio.cpp`, `asio.h`, `asiodrivers.cpp`,
  `asiodrivers.h`, `asiodrvr.h`, `asiolist.cpp`, `asiolist.h`, `asiosys.h`, `ginclude.h`, `iasiodrv.h`,
  and the SDK's licence file
- from RtAudio 5.1.0 (`include/`): `iasiothiscallresolver.cpp`, `iasiothiscallresolver.h` (MIT)

Without them the Windows build has WASAPI only. They are not added yet: they are fetched and checked
together with the first Windows build (on request).
