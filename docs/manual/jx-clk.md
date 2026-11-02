# JX-CLK — Clock

12 HP · JXMP Audio · [all modules](README.md)

Master clock of a patch. It follows CLOCK IN if something is patched there, else the transport (the standalone's own, or the DAW's in a plugin), else its own TEMPO with RUN. Seven divisions with swing, RESET and RUN outputs.

<img src="img/jx-clk-front.png" alt="JX-CLK front" height="380"> <img src="img/jx-clk-back.png" alt="JX-CLK back (sockets)" height="380">

## Front panel

| Control | What it does |
|---|---|
| BPM window | The tempo in use, and STOPPED while the clock stands. |
| SOURCE LEDs | Which source runs the clock: CLOCK IN, TRANSPORT or OWN TEMPO. |
| TEMPO | Own tempo, 30–300 BPM, used when nothing else runs the clock. |
| RUN / STOP | Starts and stops the own tempo (the transport and CLOCK IN run it by themselves). |
| BEAT | LED, blinks on every beat. |
| SWING | Delays every second x4 and x2 tick: 50 % straight, 66 % triplet feel, 75 % hard shuffle. |
| PPQN  24 / 4 / 1 | How many CLOCK IN pulses make one beat (Host MIDI's clock gives 24). |

## Sockets (back, `Tab` turns the rack)

| Socket | What it does |
|---|---|
| CLOCK IN | Clock pulses, e.g. the clock output of Host MIDI. |
| x4, x2 | Sixteenths and eighths (swing applies), 10 V gates. |
| BEAT | Every beat. |
| /2, /4, /8, /16 | Every 2, 4 (a bar in 4/4), 8 and 16 beats. |
| RESET | A trigger when the clock starts: patch it to the RESET of sequencers. Patching this jack restarts the clock. |
| RUN | 10 V while the clock runs. |

## How to use it

- One JX-CLK drives everything: x4 → JX-EUC CLOCK, x2 → JX-SEQ CLOCK, RESET → both RESETs.
- In a DAW it follows the host's play and tempo with nothing patched.
- Tempo-synced LFO: BEAT or /4 into JX-LFO CLOCK.
