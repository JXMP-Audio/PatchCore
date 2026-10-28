# JXMP modules

Built-in modules by JXMP Audio for PatchCore. Their sockets are on the back of the rack
(press `Tab`), except where a module puts them on the front.

| Module | What it does |
|---|---|
| Patchbay 4 / 8 / 16 (`JX-PB4/8/16`) | Passes channels between the front and the back. Plug something into front jack *n* and it comes out of back jack *n*, or the other way round. The first cable decides the direction; the LEDs show it (green → back, yellow → front). |

Tests for pure logic: `plugins/JXMP/tests/run.sh`.
