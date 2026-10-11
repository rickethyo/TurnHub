# S3 prototype purchasing BOM

**Planning only, 2026-10-11; USD, excluding tax, shipping and assembly.** Two Atlas
hubs plus eight or ten Sigils. Ten Sigils are inventory for multiple hubs/spares;
the current radio/device-count limit is not increased. Electrical details and
acceptance holds: [hardware plan](ESP32_S3_REFRESH.md).

Prices are either observed retailer prices or clearly identified owner preliminary
quotes. Blank prices mean **not priced**, not free. No order has been placed.

## Confirmed selections

Selected parts, not a claim of S3 electrical acceptance.

| Part / purchasing source | Unit USD | 8-Sigil qty / subtotal | 10-Sigil qty / subtotal | Notes |
|---|---:|---:|---:|---|
| [SparkFun Qwiic Navigation Switch PRT-27576](https://www.sparkfun.com/sparkfun-qwiic-navigation-switch.html) | 8.75 observed | 8 / 70.00 | 10 / 87.50 | Ordinary unit listing; shipping/stock at checkout; PCA9554, 3.3 V |
| [Adafruit NeoPixel Jewel 7 RGBW](https://www.adafruit.com/product/2856) | TBD | 8 / TBD | 10 / TBD | Match existing RGBW white variant, not RGB-only; retailer page denied automated access, price unverified |

## Provisional selections

| Part / source | Preliminary unit USD | 8-Sigil qty / subtotal | 10-Sigil qty / subtotal | Notes |
|---|---:|---:|---:|---|
| LCDWIKI ES3C40P [manufacturer](https://www.lcdwiki.com/4.0inch_ESP32-S3_Display) | 20.27 owner quote | 2 / 40.54 | 2 / 40.54 | Complete capacitive board, package contents and quote validity not independently verified |
| LCDWIKI ES3C28P [manufacturer](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display) | 1.80 owner quote | 8 / 14.40 | 10 / 18.00 | **Do not use as a verified procurement price** |
| Protected 1S LiPo packs, qualified battery distributor | TBD | 10 / TBD | 12 / TBD | Atlas 2–4 Ah, Sigil 1.5–2.5 Ah fit candidates; ratings/size/charge compatibility still open |

### $1.80 investigation

Manufacturer page distinguishes complete ES3C28P capacitive hardware from ES3N28P
without touch and documents the complete board features, but publishes **no
purchase price, normal-quantity quote or MOQ**. No reliable ordinary-quantity
complete-board listing supporting $1.80 was found. A web-search request returned
an interstitial, not supplier evidence. It could be a variant/accessory price,
first-customer promotion, deposit or quantity-tier quote; none of those hypotheses
is confirmed. Obtain an itemized quote for 8 and 10 **ES3C28P N16R8 complete boards**,
including touch, taxes, freight, speaker and harness contents. $20.27 is similarly
an owner-supplied preliminary quote, not a verified retailer price.

## Pending compatibility verification

| Part / sourcing path | Qty (8 / 10 Sigils) | Unit / totals | Engineering selection criteria |
|---|---:|---|---|
| 74AHCT1G125 buffers, authorized TI/Nexperia distributor (DigiKey/Mouser) | 8 / 10 | TBD | 5 V TTL input, correct SOT-23-5 footprint, partial-power behavior; one per LED data output |
| 330 Ω series resistors + 100 nF ceramic decouplers | 8 each / 10 each | TBD | Per buffer; include supply decoupling and IRQ pull-up if needed |
| 470 µF ≥10 V LED bulk capacitors | 8 / 10 | TBD | One per Jewel adapter; polarity and enclosure height |
| Protected-cell to board battery harness, connector supplier | 10 / 12 | TBD | Board 1.25 mm 2P, exact family/polarity pending; common JST-PH battery plugs are not direct mates |
| LCD 1.25 mm 4P to JST-SH 1.0 mm Qwiic adapter harness | 8 / 10 | TBD | Cross-map supply/GND/SCL/SDA, not straight-through; verify included LCD cables first |
| GPIO expansion harness + separate ground/INT wiring | 8 / 10 | TBD | P3 has only four signals; cannot supply ring power |
| 1S-to-5 V booster/load switch or qualified power mux | 8 / 10 | TBD | Required for reliable battery Jewel operation; ≥1 A ring transient capacity, efficiency and no back-feed |
| Interface PCB / small assembly service | 8 / 10 | TBD | Combine harness adaptation, buffer, decoupling and power routing to reduce loose wires |
| FAT32 microSD cards, reputable authorized retail source | 2 / 2 baseline | TBD | Atlas assets/logs; reuse known cards where available; Sigil cards not required for initial simulator |
| Wire, heatshrink, keyed housings, strain relief, solder supplies | 1 batch / 1 batch | TBD | Meter-tested harnesses; no screw assortment |

No separate Pair buttons. No separate speakers budgeted until package contents
and onboard audio usefulness are verified; current evidence proves an amplifier
and speaker connector, **not an integrated loudspeaker**. If none is included,
record that exception and price a compatible speaker before ordering, not silently
assume sound output. No fasteners assortments included.

## Optional alternatives

- Bare five-way switch plus PCA9554 on interface PCB: fewer connectors/boards,
  potentially lower cost after assembly volume quote; unknown prototype savings.
- Solder Qwiic PTH pads directly: removes one JST-SH cable, sacrifices easy replacement.
- Existing Jewel adapter reuse after pad-fit check; revise power routing for S3.
- Optional second RGBW strip and its buffer/booster allowance: price/count deferred;
  not in base quantities. Optional Sigil SD cards: +8/+10 if local artwork caching
  is chosen later; no second profile store.
- USB-only first prototypes: defer booster purchase and disable Jewel on battery;
  this is a functional restriction, not equivalent to final battery operation.

## Cost calculations

| Basis | Two Atlas + 8 Sigils | Two Atlas + 10 Sigils |
|---|---:|---:|
| Independently observed price subtotal (navigation only) | **70.00** | **87.50** |
| Owner-quoted LCD board subtotal | 54.94 | 58.54 |
| Combined known/quoted partial subtotal | **124.94** | **146.04** |
| Complete build total | **124.94 + unpriced lines** | **146.04 + unpriced lines** |

These are **partial estimates, not complete kit prices**. If actual ES3C28P price
is `P`, replace the provisional line: eight-unit partial subtotal =
`110.54 + 8P`; ten-unit = `128.04 + 10P`. Then add Jewels, 10/12 batteries,
8/10 buffer/passive/harness/power/PCB sets, two cards, supplies and freight/tax.
Do not round an unverified $1.80 listing into a final budget. Request a consolidated
board/harness quote and prototype one complete interface before ordering all kits.
