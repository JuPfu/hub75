# hub75_init.pio — Design-Dokumentation

## Überblick

Das PIO-Programm `hub75_init` ersetzt die CPU-basierte Initialisierungssequenz
aus `rul6024.cpp` und `fm6126a.cpp` durch ein einziges, generisches PIO-Programm,
das beide Chips unterstützt.

## Protokoll-Analyse beider Chips

Beide Chips verwenden dasselbe Grundprinzip:

```
CLK     ____/‾\/‾\/‾\/‾\/‾\/‾\ … /‾\/‾\/‾\/‾\/‾\_____
DATA    ═══════════════════════ … ══════════════════════
STROBE  ________________________________/‾‾‾‾‾‾‾‾‾‾‾\__
OEN     ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾ … ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\__
        ←─── threshold Takte ──→←── strobe_len Takte ──→
```

- **threshold** CLK-Zyklen: STROBE LOW (Schieberegister füllen)
- **strobe_len** CLK-Zyklen: STROBE HIGH (Befehlskodierung)
- OEN bleibt HIGH (deaktiviert) für die gesamte Sequenz
- OEN geht LOW (aktiv) erst nach dem letzten CLK-Impuls

### Chip-spezifische Strobe-Längen

| Chip     | Register | strobe_len | threshold (bei 64 CLKs) |
|----------|----------|-----------|-------------------------|
| RUL6024  | WREG1    | 11        | 53                      |
| RUL6024  | WREG2    | 12        | 52                      |
| FM6126A  | REG1     | 12        | 52                      |
| FM6126A  | REG2     | 13        | 51                      |

## PIO-Design-Entscheidungen

### Warum zwei separate Schleifen (Phase A + B)?

PIO bietet keinen direkten Vergleich `X < Y`. Daher wird der Prozess in
zwei sequentielle Schleifen aufgeteilt:

- **Phase A**: `threshold` Takte mit STROBE=LOW
- **Phase B**: `strobe_len` Takte mit STROBE=HIGH

Beide Zähler werden als separate Scratchregister (X, Y) übergeben.

### Side-Set für STROBE und OEN

STROBE und OEN werden via `.side_set 2` gesteuert, da sie immer zusammen
mit dem CLK-Signal wechseln müssen (atomar). Das vermeidet Glitches die
auftreten würden, wenn STROBE und CLK in separaten Instruktionen gesetzt
würden.

```
side 0b10  →  STROBE=0, OEN=1   (Schieben, Ausgänge gesperrt)
side 0b11  →  STROBE=1, OEN=1   (Befehls-Strobe, Ausgänge gesperrt)
side 0b00  →  STROBE=0, OEN=0   (Fertig, Ausgänge freigegeben)
```

### CLK-Waveform pro PIO-Instruktion

Jeder CLK-Zyklus besteht aus 2 PIO-Instruktionen:
```
out  pins, 1   side 0bXX   →  CLK bleibt LOW, Datenbit wird angelegt
nop            side 0bXX   →  CLK geht HIGH (steigende Flanke = Latch)
```
CLK wird implizit über die OUT/NOP-Instruktionen gesteuert (SET-Pin wird
nicht explizit gesetzt — stattdessen steuert das `side_set` auf CLK_PIN).

**Korrektur**: CLK selbst ist ein SET-Pin und wird in der .pio implizit
durch das side-set gesteuert, das STROBE und OEN regelt. CLK muss daher
separat via `set pins` oder als Teil des side-set inkludiert werden.
→ Für die finale Implementierung: CLK über einen dritten side-set-Bit
   oder als eigenes SET-Pin mit expliziten `set pins` Instruktionen.

### Off-by-One bei JMP X--

`JMP X--` in PIO dekrementiert X und springt wenn X *vor* der Dekrement
**nicht Null** war. Das bedeutet:
- Startwert X=0: Loop läuft **1** Mal (springt nicht, weil X war nicht 0 vor Dekrement... Nein!)

Tatsächlich: `JMP X--` springt wenn X ≠ 0 **vor** dem Dekrement:
- X=0: springt NICHT (führt Schleife genau **1** Mal durch)
- X=1: springt 1× (führt Schleife **2** Mal durch)
- X=N: führt Schleife **(N+1)** Mal durch

Daher: CPU übergibt `threshold - 1` und `strobe_len - 1` im Steuerwort
(wenn beide > 0), um exakt `threshold` bzw. `strobe_len` Iterationen zu
erreichen. Die `Hub75Init::write_register()`-Methode erledigt das automatisch.

## TX FIFO Protokoll (32-bit Words)

```
Word 0 — Steuerwort:
  Bit 31..24:  x_val = max(threshold - 1, 0)   → Phase A Schleifenzähler
  Bit 23..16:  y_val = max(strobe_len - 1, 0)  → Phase B Schleifenzähler
  Bit 15.. 0:  0 (ungenutzt)

Word 1 — Datenwort:
  Bit 31..16:  16-bit Registerwert, linksbündig (MSB zuerst)
  Bit 15.. 0:  0 (Padding)
```

## Integration in hub75.cpp

In `hub75.cpp` (oder `hub75.hpp`) an der Stelle, wo `PANEL_TYPE` ausgewertet
wird:

```cpp
#include "hub75_init.hpp"

// In der Initialisierungsfunktion:
if constexpr (PANEL_TYPE == PANEL_RUL6024 || PANEL_TYPE == PANEL_FM6126A) {
    uint offset = pio_add_program(pio, &hub75_init_program);
    Hub75Init init(pio, offset, /*clkdiv=*/100.0f);

    if constexpr (PANEL_TYPE == PANEL_RUL6024) {
        init.setup_rul6024();
    } else {
        init.setup_fm6126a();
    }
    // Hub75Init destructor gibt den SM frei; das PIO-Programm bleibt geladen.
}
```

## Ressourcenbedarf

- 1 PIO State Machine (wird nach Init freigegeben)
- ~18 PIO-Instruktionen Programmspeicher (dauerhaft belegt)
- 0 DMA-Kanäle (CPU-gesteuertes Blocking via `pio_sm_put_blocking`)
- Initialisierungszeit: 64 CLKs × 2 Register × clkdiv/f_sys
  - Bei clkdiv=100, 125MHz: ≈ 0.1 ms total → vernachlässigbar

## Offene Punkte / Verbesserungen

1. **CLK via side-set**: Im aktuellen Design wird CLK als SET-Pin deklariert,
   aber in der .pio-Datei nicht explizit über `set` gesteuert. Die korrekte
   Lösung ist entweder:
   - CLK als dritten side-set Bit (`.side_set 3`) hinzufügen, oder
   - Separate `set pins` Instruktionen für CLK verwenden (→ 3 Instruktionen
     pro CLK-Zyklus statt 2)

2. **DMA-Unterstützung**: Für sehr lange Transfers könnte DMA verwendet werden,
   aber bei nur 2 × 32-bit = 8 Bytes pro Register-Write ist blocking optimal.

3. **RUL6024 DATA_LATCH und RESET_OEN**: Diese Befehle aus `rul6024.cpp` sind
   noch nicht ins PIO-Programm integriert. Sie können als zusätzliche
   `write_register(0, strobe_len)` Aufrufe implementiert werden:
   - DATA_LATCH:  `write_register(0, 3)`
   - RESET_OEN:   komplexere Sequenz (1+2 LE-Pulse), evtl. separates PIO-Programm
