# Crate12: SP-1200 feature checklist

Working name **Crate12** (placeholder, see "Decisions" at the bottom). Sources: E-mu SP-1200 Owner's Manual (Anderton, Rev. E) and the Rossum 40th Anniversary spec page. Items marked *(verify)* still need checking against a full copy of the manual.

Legend: ✅ done in this build · 🟡 partial · ⬜ not started · ➖ hardware-only, replaced by a software equivalent

## Sound engine

| Hardware feature | Status | Where / notes |
|---|---|---|
| 12-bit linear samples | ✅ | Stored as int16 in -2048..2047 (`Types.h`) |
| 26.04 kHz DAC/ADC clock | ✅ | Engine steps a native 26,040 Hz clock and zero-order-holds it to the host rate, so you get the original imaging |
| Drop-sample pitch shifting (aliasing) | ✅ | No interpolation, as on the hardware (`Engine::nativeStep`) |
| 32 sound locations (4 banks x 8 pads) | ✅ | |
| 10 s total memory, 2.5 s max per sound | ✅ | Enforced when "Authentic limits" is on (default). Turn it off for unlimited memory |
| 8 output channels, one voice each, retrigger chokes | ✅ | |
| Per-sound tune, decay, level | ✅ | Tune range -8..+7 semitones *(verify exact range)* |
| Velocity-sensitive pads | ✅ | Click position on the pad sets velocity; MIDI velocity is always used |
| Ch 1-2 SSM2044-style filter that follows the decay envelope | ✅ | Ladder model; cutoff/resonance mimic the Rossum rear trims |
| Ch 3-4 / 5-6 fixed low-pass, ch 7-8 unfiltered | ✅ | Cutoffs 7.5 kHz / 11 kHz are estimates and adjustable *(verify from schematic)* |
| "Filter Envs Off" power-on option | ✅ | Set-up > Analog stage |
| Analog output stage character | 🟡 | tanh drive + DC block. Needs measuring against a real unit |
| Mix out + 8 individual outs | ✅ | Stereo mix bus plus 8 optional mono aux outputs in the DAW |
| Panning | ✅ | Extension (the hardware mix out is mono). Default is centre |

## SAMPLE module

| Function | Status | Notes |
|---|---|---|
| 1 VU mode | ✅ | LCD shows an input meter while armed |
| 2 Assign sound location | ✅ | Tap a pad to select, then Sample menu |
| 3 Input level 0/+20/+40 dB | ✅ | |
| 4 Threshold | ✅ | |
| 5 Sample length 0.1-2.5 s | ✅ | |
| 6 Re-sample | ✅ | Arm again on the same pad |
| 7 Arm (threshold-triggered) | ✅ | Input comes from the plugin's audio input (sidechain in a DAW, audio interface in the standalone) |
| 9 Force sampling | ✅ | |
| Input anti-alias filter before the ADC | ✅ | |
| Load audio files (drag and drop onto pads) | ✅ | Extension; files go through the same 26.04 kHz / 12-bit path |
| Export a sound as WAV | ✅ | Extension |
| Import a folder of audio files into consecutive pads | ✅ | Disk menu. Works with WAV packs people made from their own factory disks |
| Original factory-style kit (24 sounds, banks A-C) | ✅ | Synthesized from scratch in `FactoryKit.cpp`, so it can ship. Uses 8.4 of the 10 s; Special > Load Factory Kit restores it |
| Import original SP-1200 disk images (.hfe / raw .img) | ⬜ | The disk format isn't publicly documented. Needs a real disk image to reverse-engineer |

## SET-UP module

| # | Function | Status |
|---|---|---|
| 11 | Multi-Pitch | ✅ |
| 12 | Multi-Level | ✅ |
| 13 | Exit Multi Mode | ✅ |
| — | Multi-Decay (cymbals) | ✅ |
| 14 | Dynamic Buttons on/off | ✅ |
| 15 | Define Mix (1-8) | ✅ |
| 16 | Select Mix (1-8) | ✅ |
| 17 | Channel Assign | ✅ |
| 18 | Tune/Decay slider select | ✅ |
| 19 | Truncate and loop (+ make truncation permanent) | ✅ |
| 20 | Delete Sound | ✅ |
| 21 | First song step | ✅ |
| 22 | MIDI channel, Omni/Poly, pitch keys | ✅ |
| 23 | Special menu | ✅ |

## SPECIAL functions

| # | Function | Status |
|---|---|---|
| 12 | Clear all memory | ✅ |
| 13 | Check remaining memory | ✅ |
| 14 | Write SMPTE to tape | ✅ LTC written to the main outs at the chosen rate and start time |
| 15 | Clear all sounds | ✅ |
| 16 | Clear all sequences | ✅ |
| 17 | Copy sound | ✅ |
| 18 | Swap sound | ✅ |
| 19 | Default decay for all sounds | ✅ |
| 22 | Dynamic allocation per channel pair | ✅ |
| 21 | Name sound | ✅ |
| 23 | Transmit sound (MIDI Sample Dump) | ✅ |
| 24 | Receive sound (MIDI Sample Dump) | ✅ |
| 99 | Lamp test | ✅ |
| — | Reverse sound | ✅ extension |

## Sequencer (segments)

| Feature | Status | Notes |
|---|---|---|
| 100 segments | ✅ | |
| 5000 note capacity | ✅ | Enforced with authentic limits |
| Time signature n/4, 8, 16, 32 | ✅ | |
| Length up to 99 bars + 99 beats, standard and truncate | ✅ | |
| Real-time record with overdub | ✅ | |
| Auto-correct 1/4 to 1/32T + HiRes | ✅ | 96 PPQ |
| Swing 50/54/58/63/67/71 % | ✅ | Applied at record time on the 1/8 and 1/16 grids |
| Tempo 40-240 BPM in 0.1 steps, tap tempo | ✅ | |
| Metronome divisions + 24 ppq click | ✅ | |
| TAP/REPEAT note repeat | ✅ | |
| Real-time erase (ERASE + pad) | ✅ | |
| Erase segment / erase sound from segment | ✅ | |
| Copy segment, append mode | ✅ | |
| Step program | ✅ | Keypad step program (cursor on the auto-correct grid, pad inserts, ERASE + pad deletes) plus a clickable step-edit grid window |

## Song mode

| Feature | Status |
|---|---|
| 100 songs | ✅ |
| Insert / delete steps | ✅ (append and delete-at in the menu; insert-at is in the engine) |
| Repeat begin/end with count | ✅ |
| Mix change | ✅ |
| Tempo change (accelerando/ritardando over N beats) | ✅ |
| Sub-song with return | ✅ |
| Trigger out step | ✅ sends a MIDI note (set-up menu picks it) and a click pulse |
| Per-song tempo | ✅ |
| Song editor window (insert, delete, move, clear) | ✅ |
| END: stop / repeat / link to song | ✅ |
| Start at a given step | ✅ |

## SYNC and MIDI

| Feature | Status | Notes |
|---|---|---|
| Sync 1 Internal | ✅ | |
| Sync 2 MIDI clock, start/stop/continue, song position pointer, ready mode | ✅ | |
| Sync 3 SMPTE (24/25/30/30df) | ✅ | Reads LTC from the audio input and chases it; start time set as HH:MM:SS:FF |
| Sync 4 Click (24 ppq audio in) | ✅ | Reads a click from the audio input, threshold adjustable |
| Host sync (DAW transport) | ✅ | Extension |
| MIDI in notes -> pads, Omni/Poly, velocity | ✅ | Per-sound note map |
| Pitch keys (G#3-B4 play one sound at 16 pitches) | ✅ | |
| MIDI out notes | ✅ | |
| MIDI out clock, start/stop, SPP | ✅ | |
| MIDI Sample Dump Standard in/out | ✅ | Send, receive with handshakes, answers dump requests, keeps loop points |
| DAW automation | ✅ | Master, 8 faders, cutoff, resonance, drive, tempo, swing |

## DISK module

| # | Function | Status |
|---|---|---|
| 0 | Load sequences and sounds | ✅ |
| 1 | Save sequences | ✅ |
| 2 | Save sounds | ✅ |
| 3 | Load sequences | ✅ |
| 4 | Load segment # | ✅ |
| 5 | Load sounds | ✅ |
| 6 | Load sound # | ✅ |
| 7/8 | Catalog sequences / sounds | ✅ browser window |
| 9 | Format / copy software | ➖ |
| Plugin state saved with the DAW project | ✅ | |

## Still to do

1. Import original SP-1200 floppy images (.hfe / .img). Needs a real disk image to work out the format.
2. Tune the ch 3-6 filter cutoffs and the output stage against recordings of a real unit. Current values are estimates.
3. Test inside real DAWs and on macOS 11 (Intel). So far it has only been built and run on Linux.
4. Installers for macOS (Intel and Apple Silicon, macOS 11+) and Windows, at the end alongside the other plugins.

## Decisions for Adam

- **Name.** "SP-1200" and "E-mu" are trademarks (E-mu is owned by Creative; Rossum sells the current SP-1200). The product can't use them in its name or branding. It can say "inspired by classic 12-bit samplers". Crate12 is a placeholder.
- **Look.** The UI deliberately does not copy the SP-1200 panel trade dress.
- **Sounds.** Decided: ship our own synthesized kit, and let users import factory sounds from disks they own. The original E-mu factory sounds are never bundled.

## UI and themes

| Feature | Status | Notes |
|---|---|---|
| 20 built-in colour themes | ✅ | THEME button. Choice is remembered per computer |
| Custom colour schemes | ✅ | Theme editor with 16 colour slots, live preview, saved to the user's Crate12/Themes folder |
| Share themes | ✅ | Export/import `.c12theme` files (small JSON) |
| Hardware-style controls | ✅ | Recessed fader slots with ridged caps, knurled knobs with skirts, bevelled buttons with LEDs, rubber pads, dot-matrix LCD, brushed panel and screws. All drawn in code, so every theme gets the same look |
