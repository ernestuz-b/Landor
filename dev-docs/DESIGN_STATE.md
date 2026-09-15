# Landor — Design State

**This file is authoritative for design decisions.** Where it disagrees with
[`IMPLEMENTATION.md`](IMPLEMENTATION.md), [`../README.md`](../README.md),
[`GAMEPLAY.md`](../docs/GAMEPLAY.md) or the prose currently sitting in `src/**`, **this file wins**:
it records what was decided in the design conversation of 2026-09-14, which overwrites earlier text.
The older documents are still useful as background and as a source of open questions; they have not
yet been rewritten to match. Rewriting them is deliberately deferred, like all tidying.

**Provenance warning.** README, GAMEPLAY and the `Rules (v0.1)` section of IMPLEMENTATION were written
by an AI summarising a verbal description, based on games the owner named — Legend of Zelda, Rogue,
Stardew Valley. They are therefore *derived* text, not primary source. Their content (three shards, a
chest, a gated beacon, the version roadmap table) is treated as an **unratified fixture**: a plausible
test scenario, not the goal of the project. Where any document states what the game *is*, §13 wins.

Nothing here has been implemented. The repository is in a deliberate prose phase: headers hold
thoughts, nothing is expected to compile. Housekeeping (build settings, include paths, doc link
rot, staged renames) is out of scope for this file on purpose.

Status labels used throughout:

| Label | Meaning |
| --- | --- |
| **decided** | settled in the conversation, safe to build on |
| **leaning** | proposed and not objected to, but never explicitly confirmed — confirm before depending on it |
| **open** | recognised, unanswered |

---

## 1. Targets and ports

* **D-01 · decided · Multiple targets, not one tiny board.** Landor targets Linux hosts and small
  hardware, and a real device needs more than a bare MCU: display generation, input, and an SD card
  for storage. Designing against "a Pico alone" is wrong.
* **D-02 · decided · Ports may be different games.** Coming from the old-microcomputer era, the goal
  is *the same engine, the same spirit, different ambition per platform* — the way a ZX port and a
  C64 port were different games. Behavioural equality across targets is explicitly **not** a goal
  (§7). What must be identical everywhere is *format* (§3, §8).
* **D-03 · decided · A framebuffer exists on every target; its extent is the per-target knob.**
  *(Amended 2026-09-15 by the owner; first recorded as "No framebuffer, ever", which overstated it.)*
  Host and Pi Zero-class builds have a whole-screen framebuffer a renderer may fill directly.
  Pico-class hardware has a framebuffer too, but only a **line buffer**: two lines in flight, one
  being presented/DMAed while the other is rendered from tiles. The surviving prohibition is about
  extent, not existence: nothing in the design may assume a *whole-screen* pixel buffer exists on
  every target — all display work must be expressible row-at-a-time on the weakest tier.
* **D-04 · decided · Raster time is working time, with one prohibition.** Spare time during the
  raster is used for other work — compaction, input collection, simulation. But during scanout we
  **must not touch actor positions or the map**, because those are what the image is being built
  from (§4.5, §9).

## 2. Object model

Litmus test for the boundary, worth putting in header comments on both sides — "one responsibility"
is arguable, this is not: **does this class need to know what a sector is?**

```
host     argv · terminal / SD / framerate · input sampling at step boundaries
game     RuleSet (tables, versioned) · Progress (flags, objectives) · Game (phase order)
world    World: open maps · SectorCache · pins · actor registry · effect layers
geo      GeoMap descriptor · StorageManager · BlockSource   ← only side that knows bytes exist
render   build_frame() → Frame copy; backends read nothing else
```

* **D-05 · decided · `World` is the live container.** It holds the maps of the active level, the
  actors, the effect layers, and the tile cache that the renderer consumes. Formerly a bare
  `MapProvider` member in `world.hpp`; that instinct turned out to be level-shaped, and level is
  world.
* **D-06 · decided · v0.1 has interiors.** Several maps per World, joined by doors rather than shared
  borders, and there are moments of **complete map-set swap** (teleport).
* **D-07 · decided · `GeoMap` is a cheap descriptor, not a memory owner.** Geometry, coordinate→sector
  mapping, `at(coord)`, `apply(op)`. Several maps may be alive at once for the cost of tens of bytes
  each, because per-map RAM reservation is gone.
* **D-08 · leaning · Namespaces and names.** Suggested: `landor::geo` for the storage side, so the
  subsystem directory `src/world`, the namespace and the class `World` stop colliding in name and
  meaning. Unsettled; see §11.
* **D-09 · decided · `RuleSet` / `Progress` / `Game` are three things, not one `Game`.** Immutable
  tables (hashed into the fingerprint); sparse non-spatial `flag → value` progress state; thin
  coordinator owning the phase order and the win check. Guard against the God object: **`Game` must
  stay unit-testable without a `BlockSource`.**

## 3. Cells, planes and the map file

* **D-10 · decided · A cell's two chars are two planes, and the planes are separate files.** The
  *type* plane stays a picture: one glyph per tile, so `cat valley.types` shows the valley. Weaving
  type and state byte-per-byte destroys that, and hand-editable maps matter.
* **D-11 · decided · Type plane is dense and repetitive; state plane is sparse exceptions.** Most of
  the world's state follows from defaults on the descriptor (including a default height per material),
  so the state plane records only cells that disagree with their default. Same record shape as the old
  patch idea — when two roles converge on one shape, that is a signal.
* **D-12 · leaning · Row-equals-sector.** With 2-byte cells, width 256 gives exactly 512 B per row:
  `byte_offset = y << 9`, no division in the hot path, and the raster's row orientation coincides with
  the storage sector. If wider maps are wanted, a row spans several sectors and the coincidence is lost.
* **D-13 · decided · Terrain maps are not compressed.** Compression was buying a discount on a
  resource nobody is short of (256×256 @ 2 B = 128 KB) and paying for it in write amplification:
  variable-length lines shift every subsequent byte, so RLE does not reduce write amplification, it
  *causes* it. Deleting it also deletes the row-offset index, the device encoder, canonical-encoding
  discipline, decode state, the boot decompress pass, and "two compilers pack differently".
  Originals *could* be compressed; we won't bother. Best code is code never written.
* **D-14 · decided · `GeoMapCodec` is retired.** The name is reused by `SectorCache` (§4). What
  survives of its old job: a file header, constexpr offset maths, and validation.
* **D-15 · decided · Header and validation replace the structure compression used to give us.** A raw
  cell file never fails to parse: an out-of-range id silently becomes whatever sits at that table
  index and you get a garbled valley instead of an error. So: magic, width, height, layout version,
  config fingerprint, declared little-endian, plus an id-range check (`id < num_materials`) and a
  checksum. Ten lines, and corrupt data stays an error instead of becoming art.
* **D-16 · decided (important consequence) · The in-memory layout is now the on-disk ABI.** There is
  no encoding layer left to hide a change. Therefore §2.2's bit layout
  `[type_id | height | overlay | stage]` must be **frozen and versioned before the first valley
  exists**; its cost grows faster than any other decision here.
* **D-17 · decided · Authored text is authoring, not runtime.** Glyph-grid text → binary cell ids is a
  build-time translation in `tools/`, next to the map generator. Device code loads binary only; the
  hand-decoded UTF-8 reader of §6.5 stays out of the game.

## 4. Storage, cache, and when work may happen

* **D-18 · decided · The primary abstraction is a cache, not a block tree.** What GeoMap provides to
  the engine is a set of live uncompressed cells whose sole purpose is answering `at(x,y)`, maintained
  as a **write-back cache with hysteresis**; cells are written back when space is needed. Cache
  behaviour is well understood, which is exactly why it was preferred over a bespoke chunk object.
* **D-19 · decided · Cache entry = whole storage sector.** Equality of units removes read-modify-write
  entirely: every block written back is complete, so there are no extra reads, no shadow buffers, no
  alignment apology. (Entry = one line was the earlier conclusion; the SD card overrode it.)
* **D-20 · decided · No overlap between cached regions.** The HP50-era overlapping-snapshot idea — with
  band copies on transition — was replaced by non-overlapping residency. Consequence: uniqueness is
  *structural*, so there is no writer conflict to resolve, no stale-copy resurrection bug, and cache
  contents can influence speed but never an answer. This is what makes platform-variable caching legal.
* **D-21 · decided · The "chunk around an actor" survives as a pin.** The big block object did two
  jobs: I/O granularity (now the sector) and non-evictability during simulation (now a refcounted pin
  held by an actor or event). An actor's populated region is its pin set, not a stored object. Pins
  decay by age and are released at frame end unless re-acquired; `cfg::max_live_regions` is a hard
  ceiling, and overflow **defers** a newly woken sim rather than evicting a pinned one.
* **D-22 · decided · `SectorCache` lives inside `World` (composition), private.** The engine talks only
  to World / GeoMap, so nothing can reach past the facade. Cache keys are `(map_slot, sector)`
  flattened through a small `[slot → source]` table in a **fixed order** (authored index or load step,
  never insertion order) so no nondeterminism enters victim selection.
* **D-23 · leaning · `BlockSource` is injected, not constructed**, so `GeoMap` is not welded to the SD
  card and tests can run against a RAM array. Injection by reference beats a template here: one
  indirect call per 512-byte load is free, diagnostics stay readable. Revisit only if §8's
  `WorldT<Storage, ClockPolicy>` seam turns out to be needed.
* **D-24 · decided · `StorageManager` abstracts storage and owns:** `(map_slot, sector) ↔ bytes`,
  master/working identity, lazy materialization of working copies, volatile-map flag, space and
  typed I/O failures, sector geometry. It does **not** cache. It is the only object that knows a
  filesystem exists.
* **D-25 · leaning · New game / reset = copy master → working; runtime consults one flat address
  space,** so no base-plus-overlay lookup on every read and no "forgot the patch table" bug class.
  Materialize each map on first visit rather than all at once, and record which exist. Reset is a copy.
* **D-26 · decided · Maintenance runs only in the safe point / blanking window, in bounded batches.**
  Hysteresis: hi-water occupancy triggers reclaim down to lo-water, capped at K sectors per frame,
  victims by counter (FIFO or clock-aging over fixed arrays — never pointer ordering, never
  `std::list` bookkeeping). On slow targets the blanking window is the *only* legal place for eviction,
  recompression-free writeback and heap compaction.
* **D-27 · decided · Compaction never implies flush.** Relocating bytes must never cause a write to the
  world, or heap policy leaks into saves and digests.
* **D-28 · decided · The cache is quiesced during scanout.** Frame build resolves every fault — halo
  rows included — writes the `Frame` copy, and then raster touches storage exactly zero times.
  A renderer may not query the cache directly. This gives the storage manager a batch window that
  cannot race anybody.
* **D-29 · derived · The cache has a floor, not just a budget.** Scanline output needs the current row
  plus its neighbour; a framed view with halo needs `view_h + 2·halo` rows. So
  `static_assert(cfg::cache_sectors >= view_h + 2*cfg::halo + slack)` — impossible profiles fail the
  build, per §8.
* **D-30 · closed (because interiors join by doors) · Cross-map halo is a non-problem.** Spatially
  tiling maps would have made neighbour reads span two maps; doors are cuts, not borders, so neighbour
  reads stay intra-map and reduce to three rows of one map.

## 5. Mutation: one channel, dumb terrain

* **D-31 · decided · Terrain does not know what absence means.** There is no `vacate`/`carve` taxonomy.
  An operation carries its own resulting cells: **the explosion actor injects the hole, the pickup
  action injects the filling tile.** Policy belongs to the agent, geometry stays dumb.
* **D-32 · decided · Majority-vote fill is available as one recipe** ("count the neighbours, most common
  type wins"), which removes the need to author what lies under stamped features — half the map format
  disappears. Constraints agreed with it:
  * computed from a **pre-op snapshot**, so a multi-cell blast cannot depend on iteration order;
  * ties broken by an explicit descriptor `priority`, **never by type id** — id order is table order,
    and inserting a material later would change geology in old saves and replays;
  * vote on type only; height clamped to what climb rules allow (a majority winner can otherwise
    leave an unreachable shelf); overlay does not inherit the neighbour's crop;
  * candidate filter flag so lone water cannot vote itself into a meadow unless single-cell water is a
    thing we intend to support;
  * descriptors may override with `leaves = recipe` — `NeighbourVote | Literal(tile) | Keep | Rubble`
    — for the crypt under the chapel. One field, a dozen uses, keeps the vote from becoming a law of
    physics that eats authored content.
* **D-33 · decided · Ops are the universal mutation currency.** Pressing `E`, an orc grabbing a rock, a
  blast and a corpse settling are all "an agent emits an operation during its tick, applied in a fixed
  phase order". Headless replay, digests and demo mode work without special machinery because nothing
  can happen outside the channel. It also keeps the heap's relocate-at-safe-point model sound: mutation
  never occurs while someone holds a resolved address.
* **D-34 · decided · Plan, then apply.** Compute affected cells against the pre-op state, allocate,
  then write once. Ordering rule that matters: **allocate before patching.** If the heap is exhausted
  after the patch, the rock exists nowhere and there is a hole in the world. Fail atomically with a
  visible `GameError`.
* **D-35 · decided · Guardrails move into `apply()`.** Since anything can inject anything, validation is
  the write path's job: bedrock/`unbreakable`, overlay-legality, local reachability. Otherwise "NPC
  breaks a door" and "NPC rewrites the throne room into a pond" are literally the same call.
* **D-36 · decided · Unbreakable rock is a different identity from breakable rock** — separate descriptor
  entries, not a flag checked at the call site.
* **D-37 · decided · No-op writes are skipped** when injected value equals current value, or a 30-cell
  blast costs 30 dirty sectors for changing nothing.
* **D-38 · leaning · Ops may carry cell writes and entity spawns.** Otherwise agents need a second
  channel for "blast also scatters rubble-items and starts a fire", and the one-channel property (D-33)
  is lost. Unsettled.

## 6. Actors, attention, layers

* **D-39 · decided · Actor-ness is by attention needed, not by kind.** The §3.1 table that sorted by kind
  (crops tile-bound, animals actors) is replaced by three tiers:
  | tier | needs | lives | cost |
  | --- | --- | --- | --- |
  | **ticked** | next state depends on what others just did | actor on the managed heap | stepped |
  | **scheduled** | next state is a known function of the clock | timer/wakeup entry | nothing until due |
  | **inert** | nothing, forever | a word in the ground | zero |
  Attention tiers and pin lifetimes are the same decision seen from two sides (§4, D-21).
* **D-40 · decided · Actors can settle into the terrain.** A corpse degrades as an actor; its last act is
  one operation writing a mark into a cell, after which it stops existing as a thing and is purely
  cosmetic — cheap forever. Same shape covers burnt ground, a fallen tree leaving a clearing.
* **D-41 · decided · Ground features can be promoted to actors on demand.** Pressing `E` beside a rock,
  or an enemy picking one up to throw it, lifts a map feature into an actor. Bidirectional promotion is
  the same mechanism as settling, so `lifts_to` / `leaves` belong on the descriptor as data.
  Consequences agreed: loose stuff that is not individually interesting stays tile state with a
  stage/count (a gravel pile cannot be 40 actors); each lift spawns one transient actor and decrements
  it.
* **D-42 · open · Identity across settle/lift.** After Bill throws the rock and it settles in the mud, is
  it still *that* rock? Persistence costs a sparse named-things record or bits in the cell; forgetting
  is free and usually invisible — until an NPC recognises the weapon. Ties to handle-on-disk (§7, D-50).
* **D-43 · decided · Seasons are a descriptor-table selection, not per-cell mutation.** Winter costs zero
  bytes and zero patches, and because `TileDesc` also carries walkable/opaque/hardness, a season swap
  can freeze a lake, open winter sightlines through leafless trees, soften mud — maze topology and
  vision change with the calendar, for free.
* **D-44 · decided (invariant) · Cells, patches and saves store the authored id, never the season-resolved
  id.** Resolution is `cell.type_id → variant(season) → overlay → actor`, one accessor used by rules and
  renderers alike. Otherwise a save taken in March replays as frozen lake in August, and chunk digests
  depend on when the test ran.
* **D-45 · decided · Behavioural layers over the map** — fire, wind moving grass, clouds, and much else.
  The saving rule is principled rather than ad-hoc: **a layer is saved iff it holds state no function of
  (authored data, saved state, step number) can reproduce.** Wind, clouds, sway, shimmer, torch flicker:
  save nothing. Fire spread: save it. Note "unsaved" and "does not affect rules" are independent
  properties — a wind field derived purely from the clock can legitimately drive fire spread while
  storing nothing, since every port recomputes identical wind. No RNG needed: an integer hash of
  `(coord, step)` gives spatially varying phases that look like noise and keep §7.1 intact.
* **D-46 · decided · Two kinds of layer.** *Presentation* may read cells and clock, may never write
  state, and may differ freely between ports. *Simulation* writes through the front door, appears in
  saves, and its absence changes the game — so it must be declared (§8) and winnability plus
  no-softlock verified on the weakest enabled set.
* **D-47 · leaning · Presentation effects are not actors, not heap objects.** Sparks, puffs and embers in
  fixed-capacity POD arrays inside the layer: no handles, no refcounts, no relocation, ring-buffer
  lifetime. Anything worth a handle should be worth its cost; a spark is not, and `managed_ptr`
  refcount traffic per particle per frame would be murder on the slow target while handing compaction
  Swiss cheese to tidy. The persistent half — `burning`, `scorched` — lives in the sparse tile-bound
  table, where saves and grieving NPCs can both see it.
* **D-48 · leaning · Layer enablement goes into `config_fingerprint`.** Presentation toggles are harmless
  there, simulation layers are not: a demo route recorded with the fire layer on would misbehave
  silently against a build without it, and the terrain ops would take the blame.
* **D-49 · open · Fire as the test case.** Flames/flicker/glow are presentation; "this tile is alight,
  this one caught at step 1204" is authoritative. The example splits cleanly across the boundary, which
  is evidence the boundary is placed right rather than conveniently. Which simulation layers exist in
  v0.1 (if any) is unanswered.

## 7. Determinism, saves

* **D-50 · decided (restatement) · Formats identical everywhere; determinism within a target.** D-02
  kills cross-target behavioural equality, so the invariant becomes: cell layout version, authored-id
  rule, glyph/tileset ids, the `TerrainOp` vocabulary are byte-identical across all ports, while
  behaviour may differ. A Pico save is **legible** to the host build but not equivalent to it.
* **D-51 · decided · Freeze is logical, flush is physical.** Whether a far-away sim keeps running depends
  on step count and geometry only ("inactive for N steps beyond radius R"), never on cache occupancy —
  otherwise whether an NPC finishes a thought depends on how full RAM was, and host and Pico diverge for
  a reason that has nothing to do with rules. Cache pressure decides only when bytes land on the card.
  Invariant: **a frozen sim leaves zero dirty bytes in the cache**, because a frozen thing is exactly
  what must not resurrect stale rows later. Far sims run hot, then cool (running unpinned, faulting
  ground transiently), then freeze.
* **D-52 · decided test · Policy must not leak into semantics.** Same script, `cache_sectors = 1` vs
  `= N`: digests equal. Any divergence is a leak, caught in milliseconds instead of in a Pico build
  three months out. Per-port digest baselines, and CI runs the **floor tier** headless — winnable under
  its own ceilings — not only the host (§8).
* **D-53 · decided · Input is sampled at step boundaries, never per frame.** If the sim ticks 20 Hz on the
  handheld and 60 Hz on the host while input arrives per frame, replay divergence appears from pure
  presentation timing and days go into hunting a nonexistent rules bug. Framerate is decoration; a step
  records an explicitly defined edge/level.
* **D-54 · leaning · Save layout is a directory.** Cell files must stay sector-aligned multiples, so
  non-cell records cannot be appended into them: one directory per save containing raw map files
  (only materialized maps), actors, inventory, progress, plus a **manifest** carrying fingerprint,
  rule-set version, sim tier, layer mask and the list of materialized maps.
* **D-55 · leaning · Upgrades beat refusals.** A small-port save holds fewer deltas against an identical
  immutable base, so a bigger port can replay those onto the same valley and add the life that wasn't
  there. Fingerprint mismatch still fails cleanly for genuinely incompatible layouts (§7.2 spirit kept).
* **D-56 · open · Handles on disk.** Pilot it in inventory, since that is where it bites first, using what
  `coding_rules.md` §3 already prescribes: a stable logical id distinct from the descriptor index,
  assigned at spawn, recorded in a per-level id table. Bonus: item identity is what makes "Bill's axe,
  taken by whoever blew up his wall" expressible.

## 8. Ports as tiers

* **D-57 · decided · Reduction is declared capability, not dialled-down numbers.** A ZX-to-C64 port never
  just shrank constants; features came and went. So a per-tier table lists ceilings
  (`cfg::cache_sectors`, `cfg::max_live_regions`, view size) **and switched-off features**, with
  compile-time checks so the weak build does not contain the code.
* **D-58 · decided · Budget the weakest machine before sizing containers.** The weakest tier's
  framebuffer is only a couple of scanlines (§D-03), so display cost is those line buffers plus
  tile-row lookups, but peak matters more than steady state: during
  frame build we hold scanline buffers + `Frame` + cache + managed heap + filesystem buffers
  simultaneously, and put reclaim after the blit so display memory is not alive alongside scratch space.
  One table — RAM available after display, input, filesystem, code — answers several §11 questions by
  arithmetic instead of argument. **Not yet drawn.**
* **D-59 · decided · Instrumentation first, policy later.** Occupancy, miss rate, pin-set sizes,
  dirty-sector lifetime and heap churn from headless runs are the same statistics that three deferred
  policies are waiting on. Measure once, then set budgets.

## 9. Invariants (enforced, not merely chosen)

1. Terrain state changes only through `apply(op)`; one front door.
2. Ops carry their results; terrain holds no policy about absence.
3. Allocate, then patch — never the reverse. Failure leaves the world unchanged.
4. Cells store authored ids; season/variant resolution happens at read time.
5. Reads of a cell have exactly one authority, structurally guaranteed by non-overlapping residency.
6. Cache entry equals storage sector; writeback always writes whole sectors.
7. Cache state is a pure function of input history (no pointers in victim choice).
8. Freeze from step count and geometry; flush from pressure and the blanking window. Frozen ⇒ zero dirty bytes.
9. No eviction, flush or compaction during active scanout; cache quiesced for the raster.
10. Compaction moves bytes and never writes to the world.
11. Presentation layers never write state. Ambient time alone never mutates terrain; agent decisions do.
12. Iteration and tie-break orders are explicit and stable — coordinate order, priority fields, fixed slot tables.
13. `managed-heap/` is frozen: consume it, record gaps for its owner, never patch it in-tree.

## 10. What this overwrites

| Older claim | Where | Now |
| --- | --- | --- |
| Immutable base + sorted patch overlay; "the edit journal is the save file" | IMPL §2.3, §7.2 | Materialized uncompressed working copy behind a sector write-back cache (D-13, D-18, D-25). Patch machinery retired; §11.3 (patch growth policy) dissolves |
| Line-by-line RLE, cell = 2 chars, `GeoMapCodec` compresses rectangular sections | `src/world/geomapcodec.hpp` header prose | Compression dropped entirely (D-13); class retired (D-14); planes split into files (D-10) |
| `GeoMapView`: mutable rectangular section, recompressed when cells leave sight | `src/world/geomap.hpp` header prose | Replaced by cache + pins (D-18…D-22). The name is retired: `string_view` is read-only and owes nothing; this thing owns memory and must flush |
| Identical digests across targets | IMPL §7.1 | Formats identical, determinism per target (D-50, D-52) |
| Families keyed by kind | IMPL §3.1 | Attention tiers (D-39) |
| Save contains a terrain edit journal | IMPL §7.2 | Save directory of raw map files + manifest (D-54) |
| "graphics later reuses everything"; framebuffer assumed implicitly | IMPL §6 | Framebuffer exists on every target but its extent is tier-dependent — line buffer on small targets, hence scanline output; maintenance in blanking only (D-03, D-04, D-26, D-28) |
| Frame may be read back into world queries | IMPL §6.1 | `Frame` is a pure copy built in the logic window; backends read nothing else (D-28) |

Surviving intact, unchanged: flyweight `TileDesc` with stable ids (§2.1), derived-everything-in-the-backend
principle (§6.1), palette-indexed composition and colour-key transparency (§6.3), the auto-tiling and
influence model (§6.2, §6.4), the managed heap contract including scoped access and safe-point compaction
(§2.4), config as generated knobs with compile-time asserts (§8), wall-clock stays out of the rules (§13) —
but "no RNG" is **superseded**, see D-64,
and the Actor → Pawn → Character concept plus alignment-driven behaviour trees as *design intent* (§3).

## 11. Open questions

1. **The weakest machine's memory map** (D-58) — blocks container sizing everywhere. Do this next.
2 Cell bit layout frozen and versioned before any map file exists (D-16). Fastest-growing cost here.
3 Names/namespaces: `landor::geo` vs `world::World` stutter; is `GeoMapProvider` renamed to
   `StorageManager` or kept as the thing that resolves maps? Are Unreal-flavoured `BP_NPC_Base` names
   literal C++ types (IMPL §11.7)?
4 Does an op carry entity spawns as well as cell writes (D-38)?
5 Identity of settled/lifted things, and handle-on-disk encoding (D-42, D-56).
6 Which simulation layers, if any, run in v0.1 (D-49); are volatile (unsaved) maps a feature?
7 Cool-down shape: hot → running-unpinned → frozen, or hot straight to frozen (D-51)?
8 Inventory shape: stackables as `{item_id, count}` records versus unique actors with identity (lean
   recorded, undecided); capacity knob; loot/gifting flow through ops.
9 Row-vs-sector width decision (D-12): accept width 256 and its shift-indexing, or wider maps with rows
   spanning sectors?
10 Do we ever need single-cell water, wading, or flooding — D-32's water vote filter depends on it.
11 Whether v0.1 objectives (shards, key, chest) are `Progress` flags or stealable real actors.
12 Elevation rules, still unanswered from IMPL §11.2 (climb cost, crest occlusion, falling); note D-32's
    height clamp and D-43's season sightlines both depend on it.
13 Max world size per target (IMPL §11.1), now expressible as a budget calculation once item 1 exists.
14 NPC count and whether NPCs perform `TerrainOp`s in v0.1 (IMPL §11.6) — off-view agents are what make
    D-51's tiers necessary, so this interacts with everything in §6.
15 **World time model (§13, the big fork):** does time advance only when the player acts (roguelike), or
    continuously while the player idles (Stardew-like)? Everything about schedules, replays and digests
    hangs off this; see Q-A in §13.
16 Ownership ledger (§13): item→owner, plot→owner, shop stock as owned inventory. Not yet designed.
17 Economy shape (§13): prices per shop, supply/demand, arbitrage while hauling goods between towns.
18 Minigame timing unit (§13): reaction windows must be measured in **steps**, not milliseconds, or
    §13's no-wall-clock rule and D-53 both break.
19 Clock granularity: is a day divided into enough slots for work/chores/sleep (current IMPL number is
    one day per 120 steps, which is too coarse for a shift-based routine)?
20 Which PRNG, its state width, and golden vectors per target (D-64); owner to supply the xorshift variant.
    Is `std::mt19937` offered as a host-profile alternative, or one generator everywhere?
21 Craft failure semantics; stake-free practice mode; is the world seed player-visible/selectable?
22 Does adversary difficulty adapt to measured performance, or stay fixed by table (D-64 band proposal)?
23 Full inventory of minigame kinds wanted (melee, ranged, casting, forging, …?) — the envelope is one
    object, but the payload list is still unknown.
24 How many RNG streams, and what identifies them (loot? worldgen? combat? per-entity?), if the
    counter-based proposal is taken up (D-64).
25 Whether one stream survives per-run determinism testing better than splittable ones — worth a measurement
    before committing, since streams change what "same seed" means.

---

## 12. Vocabulary

| Term | Status | Meaning now |
| --- | --- | --- |
| `World` | current | live container: open maps, SectorCache, pins, actor registry, effect layers |
| `GeoMap` | current | cheap descriptor: geometry, coord→sector, `at()`, `apply()` |
| `SectorCache` | current | private member of World; resident uncompressed cells, dirty bits, pins, hysteresis writeback |
| `StorageManager` | proposed | sector ↔ bytes, master/working identity, materialization, typed I/O failure |
| `BlockSource` | proposed | the tiny storage seam: host file · SD card · `rodata` array |
| `TerrainOp` | current | universal mutation record: kind, origin, shape, tool, cause (+ maybe spawns) |
| `pin` | current | refcounted keep-alive on resident regions, held by an actor or event |
| `Layer` | current | behavioural stratum over maps; presentation or simulation |
| settle / lift | current | actor→terrain demotion / terrain→actor promotion |
| `GeoMapCodec` | **retired** | was line RLE + import; no compression (D-13, D-14) |
| `GeoMapView` | **retired** | mutable window idea became the cache; name misleading (D-18) |
| `Chunk` | **retired** | block object replaced by sector + pin (D-18…D-21) |
| Level | **merged into World** | the level-shaped owner of residency is World itself (D-05) |
| patch / journal overlay | **retired** | see §10 |

---

## 13. The game — primary source

Owner's narration, 2026-09-14. Highest authority in this repository: what is written here overrides any
description elsewhere, including anything above that assumed a smaller scope. Lineage: **Legend of
Zelda** (items enabling new solution paths), **Rogue** (procedural world, stories falling out of rules),
**Stardew Valley** (schedules, relationships, ownership, farming). The owner's stated interest is
**the simulations**; the game content is the carrier.

### What it is

* A **discovery game**. The main storyline is driven by **missions**, mostly of the "go and get
  something" kind.
* Open life options alongside the storyline: own a **farm**, keep **animals**, go **hunting** in the
  forest, **buy and sell** in shops, **carry goods from one shop to another** (trade), own a **house**
  — or not.
* **People live in the world**: chores, work, sleep, interacting with the player. They are not scenery.
* **Travel between towns and cities.** Several maps alive at once, with complete map-set swaps — which
  is what D-06 was reaching for. Interiors are houses, shops and the school.
* Some content assembled procedurally from **mix-and-match parts**. Explicitly deferred; do not design
  around it yet.

### The protagonist

* Starts as a **boy or a girl**, taking missions that help people.
* Goes to **school**, where they learn **arithmetic**.
* **Develops along whatever direction the player actually uses** — practice-driven growth, not class
  selection. Doing archery makes you better at archery.

### The interaction layer (and why school exists)

Player skill is expressed through minigames, and *arithmetic is the interface to magic*:

| Situation | What the player does | Example |
| --- | --- | --- |
| Melee | reproduce a presented sequence on keys/joystick | — |
| Ranged attack | press letters/numbers very fast | like fishing minigames |
| Magic | solve arithmetic | `2+3=?`, `4*5+2=?` |
| Harder magic | inverse problems, multiple choice | `3*? = 12`, pick from options |

School learning therefore maps directly onto the ability to play the interaction layer, and spell
difficulty scales with character development. Integer-only domain suits the integer-only rule constraint
(`coding_rules.md` §5), and generated problems need only a deterministic generator plus distractor
generation for the multiple-choice form.

### Subsystems this requires that no earlier document mentions

| System | Why the game needs it | Existing hooks |
| --- | --- | --- |
| **Ownership ledger** | you can own a house, a farm, animals, stock — and take things from people who own them | `TerrainOp::cause`, grievance/law (IMPL §3.2), item identity (D-42, D-56) |
| **Economy** | shops, prices, buying/selling, hauling goods between towns | none yet |
| **Capability / skill via practice** | activity-driven development; scales school difficulty and mission gating | `Progress` is deliberately non-spatial, so skills belong on the Character record (D-09) |
| **Schedules** | chores, work shifts, sleep, opening hours | tier-2 *scheduled* attention (D-39) is exactly this: next state known from the clock, free while nothing is due |
| **Input abstraction** | melee sequences and quick-key bursts must survive different devices across ports (D-02) | host layer only; results enter `step()` as one outcome (D-53) |

### The chain mechanic — owner's statement, primary source

The shape of an exchange, using an abstract token sequence such as `E W X C A`:

* A token is shown; the player enters it; **on a correct press the next token is revealed**. Reactive
  chain, not memorisation — the whole string is never held in view for recall.
* Two numbers define the challenge: **sequence length** and **time allowed for the whole sequence**.
  Both are derived from the character's stats **and the enemy's stats**.
* What is displayed and which physical key it maps to are **port-specific**. The chain is over abstract
  tokens; the device supplies letters, joystick directions or whatever it has.

Design consequences recorded as proposals, not yet ruled:

* **D-60 · leaning · Challenge / result split.** Rules emit an `InteractionChallenge { kind, payload,
  difficulty }`; the world **pauses** while the host runs it; the host returns an
  `InteractionResult { progress, success, aborted }` at one step boundary and rules resolve the outcome.
  Because no steps advance during the exchange, wall-clock cannot enter the logic at all — this replaces
  my earlier "express reaction windows in steps" suggestion, which was the weaker answer. A replay
  records `{challenge, presses}`, so determinism (D-50, D-53) survives untouched.
* **D-61 · leaning · Difficulty is dimensionless in the rules, milliseconds on the device.** With the
  world paused, "time for the full sequence" cannot be sim time: it is a scalar the host converts to real
  time per port. Token-alphabet size is likewise a port parameter (a d-pad offers 4 near-equivalent
  inputs, a keyboard more), so each port needs a calibration constant — otherwise the same stats mean a
  different difficulty on two boards and cross-port comparisons become meaningless.
* **D-62 · leaning · One abstraction for all three minigames.** Melee chains, ranged quick-keys and magic
  arithmetic-plus-options are the same object: a challenge with a payload and a difficulty, returning a
  result. One host seam, one replay record type, one place where port mapping lives.
* **D-63 · leaning · NPC-versus-NPC uses the same maths without a player.** The difficulty scalar must
  therefore be readable two ways: as a time budget for a human, as an implicit expectation for a simulated
  exchange, keeping player and agent symmetric. Variability comes from a deterministic function of
  `(step, actor ids)` — no RNG (§7.1).
* **open ·** Global budget versus a per-token window beneath it; what partial completion means (partial
  damage, chain break plus an enemy counterbeat, a chance to flee); and how the character's and the
  adversary's stats combine into sequence length and allowed time. Token content comes from the game RNG
  inside a logic phase (D-64), not from presentation-time hashing.

### D-64 · decided · There is an RNG

This supersedes "no RNG" wherever it appears (IMPL §7.1, coding_rules, and several statements in this
file). Determinism is therefore re-founded the roguelike way: **reproducible given (seed, input
stream)**, not guaranteed by absence of randomness. Four things follow, all cheap:

* **Specified generator.** One named algorithm, explicit widths (`uint32_t` arithmetic, defined
  wraparound), no reliance on the standard library's implementation-defined behaviour, small enough for
  the slowest target. Committed golden test vectors run on every platform, so host and board draw the
  same numbers or the build fails.
* **Generator state is world state.** Saves store it (state vector plus position/draw count) next to the
  fingerprint and rule-set version; a load that restores cells but not draws diverges silently, which is
  the worst possible failure because nothing looks wrong.
* **Draws happen only in logic phases, never in presentation layers.** This is the one place where the
  port-variable cache and view size could leak into the world: if clouds or grass sway consumed draws,
  draw counts would depend on what was resident on screen. Presentation keeps deriving its noise from
  `hash(coord, step)` — deliberately *not* the game RNG, for exactly this reason.
* Digest and replay tests carry the seed explicitly (D-52 unchanged in spirit).

Implementation shape agreed in outline:

* **Entropy source ≠ generator.** A timer reading or input-timing jitter is *entropy*, used once at boot /
  new game to fill the seed — never as a draw, and **never a reseed mid-run**, which would end replay and
  digest testing. On device, RP2040-class parts have a ring-oscillator noise source and STM32F4-class parts
  a hardware RNG peripheral; both are optional, with timer/input jitter as fallback.
* **The seed becomes part of the run record**: stamped into the save/manifest, ideally shown to the player,
  and settable (`--seed`) for demos and CI. Reproducibility means "given this seed and this input stream",
  so `--headless --script` must imply a fixed seed unless overridden.
* **Small explicit generator for logic** (owner's tiny xorshift), because it is a dozen bytes of state and a
  handful of instructions, and pulls nothing in. Caveats attached: use the 64-bit variants,
  derive range mapping from the **high** bits (`(draw * n) >> 32` style, bias behaviour stated), not `& mask`
  on low bits, and never treat it as cryptographic.
* **Standard C++ is allowed, with one limit.** `<random>` engines such as `std::mt19937` are specified
  normatively, so they are portable given a seed; `<random>` **distributions** are not constrained tightly
  enough — libstdc++ and libc++ consume engine output differently and would diverge across toolchains, which
  is exactly the failure we are guarding. So: no distribution objects in logic paths; map raw outputs with an
  explicitly defined bound function. Also `std::random_device` only as entropy, never persisted into logic;
  and serialise generator state as raw integers, not `operator<<` — core has no stream I/O (§13). Note host
  `mt19937` costs ~2.5 KB of state plus init cost against a few words for xorshift.
* **leaning · Counter-based streams instead of one global sequential stream.** With a single shared stream,
  any port that changes the *number* of draws — fewer NPCs, smaller valley, a tier with the fire layer off —
  shifts every later draw and the whole world drifts beyond the intended tier differences. Splitting by
  purpose, drawing from `f(seed, stream_id, counter)` where the counter is derived from stable inputs
  (step, entity ids, coordinates), decouples them: adding a neighbour stops reshuffling loot tables.
  Structurally similar to presentation-time hashing, but authoritative — its inputs are recorded logic facts,
  and replay reduces to keeping the seed.

### Adversary difficulty *is* minigame difficulty, and the family is bigger than combat

* **decided ·** An adversary's difficulty **is** the minigame's difficulty — one dial, not a fight system
  with a separate puzzle system bolted on.
* **decided ·** The interaction layer covers far more than fighting: **casting a magic item**, and
  **making a sword**, among others. Explicit goal: each is **simple enough that a child can practise a
  genuinely useful skill** through it. The game is a wrapper around practice.
* **leaning ·** Two parents pull on difficulty, and they are not the same force: the **adversary** sets
  what the encounter demands, while the **learning zone** sets what keeps a beginner in flow instead of
  frustrated. Reconciled as a band: adversary gives the ceiling, adaptation picks within it per skill.
  The practice-driven development counters (§13) double as the teaching model's history — one set of
  numbers feeding growth, gating and adaptation.
* **leaning ·** Crafting performance writes into the product: `InteractionResult{progress, accuracy,
  time-left}` becomes item quality at creation. Crafted things are therefore **unique items with
  generated attributes**, which puts them on the actor/record side rather than in a `{item_id, count}`
  stack, and revives the identity question (D-42, D-56) with a concrete consumer attached.
* **open ·** What a poor craft yields — junk item, ruined materials, or nothing at all; whether a stake-free
  practice mode exists (cheap, since rules only ever see the returned result); whether the player chooses
  or sees the world seed.

Two observations worth keeping:

* **Schedules are the payoff of D-39.** A shopkeeper's routine is a function of the clock, so an
  unobserved NPC costs nothing until an action is due, and wakes at shift start rather than being
  stepped. The attention tiers were not merely convenient — they are the natural shape of life in this
  world.
* **The minigame layer couples player skill to simulated capability**, which is why it cannot be
  decorative: it is how a growing child becomes a capable adult inside a world whose other inhabitants
  are simulated by rules rather than scripted.
