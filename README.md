# Bticino SCS Bus Sniffer

Our building complex has an intercom from Bticino (Legrand).
Even though we were provisioned in 2021, we're on a proprietary, 2-wire bus called [SCS](https://en.wikipedia.org/wiki/Bus_SCS).
We are interested in logging some basic events, such as "which unit opened the garage door".
This does *not* capture the audio or video streams, and that's by design because we do not really want to stomp on people's privacy.
When deploying, keep in mind that even call metadata, door opening, etc, are typically [PII](https://en.wikipedia.org/wiki/Personal_data).

## Software

Everything is in [`sw/`](sw/):

- [`scs_decoder.h`](sw/scs_decoder.h): header-only C++20 decoder that turns pulse timings (e.g., from the ESP32's RMT) into bytes and validated `A8 ... A3` frames.
  Frames are 7 or 11 bytes long depending on their first payload byte; payload bytes can take any value, including `A3`.
- [`scs_protocol.h`](sw/scs_protocol.h): what a frame means, as far as it has been reverse-engineered: lighting/automation commands, state broadcasts, status requests, scenarios, door-entry rings and door lock release.
  Everything else is reported as `unknown`, with the raw bytes.
- [`scs_decoder_test.cpp`](sw/scs_decoder_test.cpp), [`scs_protocol_test.cpp`](sw/scs_protocol_test.cpp): tests, written as `static_assert`s; `make -C sw test` compiles them, so a successful build means they all passed.
- [`scs_esphome.h`](sw/scs_esphome.h) and [`scs-tap.yaml`](sw/scs-tap.yaml): an ESPHome snippet for the M5Stack Unit PoE-P4 ([`U213`](https://docs.m5stack.com/en/unit/Unit_PoE-P4), ESP32-P4).
  The tap's Grove `T↓R` pin goes to GPIO54, and the `R↑T` pin goes to GPIO53, which is held high.
  Every frame is logged (e.g., `A8 91 03 60 08 FA A3: ring flat 03`) and handed to an example lambda, as raw hex and as text; malformed captures, stray bytes and glitches are logged with the raw capture.
- [`scs-tap-poe-p4.yaml`](sw/scs-tap-poe-p4.yaml): a minimal complete device config for the U213 that includes the snippet.

## Hardware

It looks like this is a job for F454, which has been EOL-ed by Legrand, so we have to build some simple bus sniffer.
I was lazy, so I decided to reuse some existing isolated RS-485 converter (even though SCS is very different from [RS-485](https://en.wikipedia.org/wiki/RS-485)).
This project adapts the M5Stack's RS485-ISO ([`U094`](https://docs.m5stack.com/en/unit/Unit_RS485-ISO)) for passive SCS sniffing.

SCS is a 27 V DC bus that carries power and data on the same two wires.
Data is UART-like (9600 Bd, 8N1), but a `0` bit is only a ~6 V dip lasting 34 µs at the start of the 104 µs bit cell; idle and `1` bits leave the bus alone.
The RS-485 receiver is just a differential comparator, so we feed its A/B inputs from two voltage dividers with slightly different ratios, one lightly filtered and one heavily filtered:

- `A` follows the bus quickly (33k/4.7k, τ ≈ 9 µs),
- `B` sits at a slightly lower ratio (36k/4.7k) behind a 10 µF capacitor, i.e., it tracks the average bus voltage over ~40 ms.

At idle, `A` is ~0.25 V above `B` and the receiver outputs high.
During a dip, `A` drops ~0.5 V below `B` and the output goes low.
The transceiver's isolation barrier and its isolated DC/DC converter keep the MCU galvanically separate from the bus, and the 33k/36k series resistors limit the load to ~1.5 mA, so the tap cannot disturb the bus even if the RS-485 driver gets enabled.
The MCU receives the raw 34 µs pulses on the Grove port and decodes them with the ESP32's RMT peripheral.
Polarity matters: SCS bus + goes to terminal A (linked to B), bus − to G.
A reversed connection damages nothing, but the tap receives nothing either.

The actual hardware modification is reasonably straightforward; the trickiest part is removing a few rather tiny 0402 resistors.
Also remove a few other, hand-soldering-friendly components, and place a bunch of 1206 SMD parts, which is a piece of cake:

- one 4k7 1% 1206 resistor (plus another one if R8 comes off by accident)
- one 33kΩ 1% MiniMELF resistor (SMM02040C3302FB300, or just a regular 1206 resistor if you do not care about surges)
- one 36kΩ 1% MiniMELF resistor (SMM02040C3602FB300, see above for alternatives)
- one 0Ω 1206 jumper
- one 10µF ≥16V X5R/X7R 1206 capacitor
- one 2.2nF C0G/X7R 1206 capacitor

[![Modified schematic](docs/schematic.svg)](docs/schematic.pdf)

Here are the SMD removals and replacements:

![Top side: parts to remove, replace and keep](docs/board-top.jpg)

...and here is one tiny wire at the bottom:

![Bottom side: terminal and Grove wiring](docs/board-bottom.jpg)

Then, connect the **SCS +** wire to terminal A or B, and the **SCS -** wire to terminal G.

## Credits

This project was born out of frustration when a five-year-old building is already half-obsolete in 2026.
HW ideas and the RS-485 repurposing are mine, and the actual implementation was tremendously accelerated via use of Claude.

The actual SCS bus reverse engineering was done by many people.
Notable mentions:

- Michael Stapelberg: [intro](https://michael.stapelberg.ch/posts/2021-03-13-nuki-scs-series-overview/), [SCS signal decoding](https://michael.stapelberg.ch/posts/2020-09-28-nuki-scs-bticino-decoding/), [processing SCS with a microcontroller](https://michael.stapelberg.ch/posts/2020-11-30-scs-processing-microcontroller/) (bus levels: 28 V idle, 22 V low), [smart intercom backpack](https://michael.stapelberg.ch/posts/2021-03-13-smart-intercom-backpack/) and [intercom-backpack](https://github.com/stapelberg/intercom-backpack) (ring frames)
- [Bus SCS on Italian Wikipedia](https://it.wikipedia.org/wiki/Bus_SCS) (bit timing and checksum; its framing description is inaccurate)
- Guido Pagani: [SCS bus log](https://guidopic.altervista.org/eibscout/logticino.html) and [EIB/SCS gateway](http://guidopic.altervista.org/alter/eibscsgt.html) (commands, states, acknowledgements)
- [flavio/scsgate](https://github.com/flavio/scsgate/blob/master/scsgate/messages.py) (message types)
- [r0bb10/ESPHome-SCS-Bridge](https://github.com/r0bb10/ESPHome-SCS-Bridge) (action codes, extended frames, door lock release)
- BTicino product data sheet [BT00646](https://assets.legrand.com/pim/NP-FT-GT/BT00646-c-IT.pdf) (18–27 V bus supply)
- TechOverflow on the KNX TP1 [pulse voltage](https://techoverflow.net/?p=11263) and [timing](https://techoverflow.net/2023/09/24/how-long-is-the-knx-tp1-active-pulse-equalization-pulse), a similar physical layer
