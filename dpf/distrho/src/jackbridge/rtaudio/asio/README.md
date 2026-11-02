# ASIO for the PatchCore standalone (Windows)

PatchCore offers ASIO next to WASAPI on Windows. ASIO is compiled in only when these files are here
(`dpf/Makefile.plugins.mk` checks for `asio.cpp`).

- From Steinberg's **ASIO SDK 2.3.4 (2025-10-15)**, downloaded from https://www.steinberg.net/asiosdk
  (`ASIO-SDK_2.3.4_2025-10-15.zip`): `asio.cpp`, `asio.h`, `asiodrvr.h`, `asiosys.h`, `iasiodrv.h` (common/),
  `asiodrivers.cpp`, `asiodrivers.h`, `ginclude.h` (host/), `asiolist.cpp`, `asiolist.h` (host/pc/). Unmodified.
  The SDK is dual licensed, the Steinberg ASIO License or the **GNU GPL version 3**
  (`LICENSE-ASIO-SDK.txt`, the SDK's `LICENSE.txt`; `changes.txt` of 2.3.4: "new DUAL licensing: Proprietary and
  GPLv3"). PatchCore (GPL-3.0-or-later) uses it under the GPLv3.
- From RtAudio 5.1.0 (`include/`): `iasiothiscallresolver.cpp`, `iasiothiscallresolver.h` by Ross Bencina,
  public domain ("Public Domain & Open Source distribute freely"). Only active on 32-bit Windows.

"ASIO" is a trademark of Steinberg Media Technologies GmbH; PatchCore names the driver type only and does not use
the ASIO logo.
