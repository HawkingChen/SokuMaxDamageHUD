# Damage attribution audit

The optional headless audit uses the publicly served engine behind
[cleista practice](https://soku.cleista.cc/practice) and its
[framedata](https://soku.cleista.cc/framedata). It sends observed action,
creation, helper-command and damage events through the production
`NativeAttribution` and `RecordTracker` code.

This is an integration test of attribution, not proof that the ported engine
matches every native game behavior. Native instruction fixtures separately
check the supported executable signatures, register/FPU preservation, and
execution of displaced instructions.

## Inputs

Keep downloaded engine files and extracted game data outside this repository.
The audit does not distribute game archives, images, or third-party engine code.

- Download `https://soku.cleista.cc/framedata-data/CHARACTER/moves.json` for
  the twenty character keys listed in `tools/audit_move_inventory.py`.
- Cache `/player/chunks/chunk-OBY5JK72.js` and
  `/player/chunks/chunk-3FKW4SWO.js` from that site. The adapter checks the
  engine SHA-256 before rewriting an external copy; a new website build
  requires review of the adapter.
- Use the tester's own 1.10a archives. Extract only simulation patterns and
  spell tables with the small tool below. Pass archives in load order so
  later patches override earlier files.

```sh
c++ -std=c++20 -Isrc/core tools/audit_extract_patterns.cpp -o /tmp/extract-patterns
/tmp/extract-patterns /tmp/patterns /path/to/th123a.dat /path/to/th123b.dat /path/to/th123c.dat
python3 tools/audit_move_inventory.py /tmp/moves /tmp/inventory.json
node tools/audit_engine_paths.mjs /tmp/engine /tmp/patterns /tmp/inventory.json /tmp/audit --variants
c++ -std=c++20 -Isrc/core tests/test_simulator_trace.cpp src/core/RecordTracker.cpp -o /tmp/check-attribution
/tmp/check-attribution /tmp/audit/simulator-native.tsv
```

The source adapter uses Node 22's bundled Acorn parser and refuses to run if
it is unavailable. It instruments HP assignments without changing their values.
It preserves the native collector's object-update context for player spawn
services, action-sequence callbacks, and Youmu's explicit helper mailbox.

## What the result means

`cases.jsonl` keeps observed HP writes, engine diagnostics and per-case status.
`summary.json` states the scenario limits. The checker reports unidentified
hits, a source action different from the initiating action, and incomplete
per-cast totals. Self-damage and opponent damage are counted separately.

The default sweep uses each distinct normal/skill/spell action, two distances,
900 updates, one seed and maximum skill level. `--variants` also runs every
listed notation row and holds a charged move's button for 60 updates before
releasing it. A scenario stops when a separate attack begins; follow-ups
are exercised in their own rows rather than counted as the first move. Rejected
action entries are explicitly recorded as unexercised. Actions are entered directly: this does not validate command
inputs, every release timing, every level, weather, guard, or all opponent
states. The update loop exercises character scripts and collisions, not the
complete practice UI and round lifecycle.

Some website rows have synthetic IDs at or above 10,000 for chained inputs
or activation demonstrations. These are not native game action IDs. The
adapter enumerates them as requiring an input scenario and does not pass
them to `setAction`. `--geometry` expands a selected inventory to three
distances and ground/air target positions for targeted follow-up of misses.

An entry with no observed damage is **not a pass**: it can be movement, a buff,
a miss, a prerequisite-dependent move, or an unavailable action. Engine
diagnostics also remain review items. Inventory coverage, simulated execution,
production attribution checks and native game verification must be reported
separately.

## Frozen regression input

`tests/fixtures/simulator-attribution.tsv` contains only simulated event
numbers, pruned to hit ancestry and command boundaries. Its adjacent JSON
records the source hash, scenario limits and per-character counts. CTest runs
it through the production code as `whm_simulator_tests`; no network or game
installation is required for that regression.

The recorded sweep covers 1,958 listed normal/skill/spell rows in 3,916
scenarios. Damage was observed in 2,471 scenarios, yielding 15,417 damage
events. Attribution, source labels and per-cast totals match for those
observed events. The other 1,445 scenarios did not produce damage; they are
not evidence of supported hit attribution. Twelve scenarios also carry
ported-engine diagnostics. The sweep does not establish exhaustive native
coverage.

A second fixture, `simulator-geometry.tsv`, adds 1,506 targeted scenarios
for 170 rows whose source listed positive damage but whose baseline missed.
It contributes 968 observed damage events and exercises 110 additional rows.
Across both sweeps, 1,665 of the 1,958 rows produced damage. The remaining
293 rows need scenario review; 60 of those have positive source damage.
They must not be described as verified. Both fixtures are checked in CI.

## Prediction and rollback

Native damage callbacks can run again after a prediction is discarded. A raw
hit log is therefore not a committed timeline. Giuroll 0.6.18's public
[`addRollbackCb` interface](https://github.com/Hagb/giuroll-hagb/blob/53535bd/src/lib.rs)
saves and restores this MOD's record tracker, attribution graph, actor/cast
state, helper commands, pending event buffer and consumed cursor. No changes
are made to Giuroll or game combat values.

Pending events are included because checkpoints may occur inside an outer
battle-process call. The processed core is shared between snapshots within
that call. Published attribution nodes are immutable between batches, so
future replayed births cannot mutate an older checkpoint. Restoring outside
the capture scope also resets HP corroboration to the restored game's HP.
UI preferences and graphics resources are not rolled back.

Regressions cover discarded Cirno 5C events (2601 inflated versus 1413 on
the final event sequence), Reisen's repeated 368-point event (1900 versus
1532), and Remilia's repeated 965-point J2A (1930 versus 965). Identical
legitimate hits and practice auto-heal still accumulate: there is no global
blacklist of hit signatures or static damage ceiling. Windows fixtures also
exercise the four-function cdecl ABI and pending/consumed event restoration.
