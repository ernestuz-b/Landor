# Landor — Implementation

Technical design, decisions and constraints. Player-facing material lives in
[`GAMEPLAY.md`](GAMEPLAY.md); build/usage summary in [`README.md`](README.md).

Status: **design phase**. The documented layout, files and commands below are the
contract the implementation must satisfy; `src/`, `assets/` and `tools/` are being
created to match.

---

## 1. Design principles

1. **Rules own the truth; rendering derives everything.** All rules live in
   `src/world` + `src/game`; rendering only reads state through a
   backend-agnostic `Frame`. Graphics work must never require a change to a rule, a
   save file, or a test.
2. **Stable ids are the interface.** Every tile carries a stable type id beside its
   glyph, so a sprite renderer can index an atlas by that id and reuse everything
   (§6).
3. **Determinism first.** No RNG and no wall-clock in logic. Reproducibility is what
   makes scripted headless play, snapshot tests and replay-based saves possible
   (§7). Container iteration order is part of determinism.
4. **Mutation has one front door.** Rules change only through `step(input)`; terrain
   changes only through a single edit operation (§3.1, §5).
5. **Limits are configuration, not code.** Engine-shape numbers are compile-time
   knobs with asserted relationships; gameplay numbers are data shipped with the
   world (§8).
6. **The object model is an engine's.** Actors/Pawns/Characters, NPC nature via D&D
   alignment plus behaviour trees (§3).
   
### Run modes (host only)

```
landor                       # interactive terminal play
landor --demo                # headless: replays a winning route, prints final frame
landor --headless --script route.txt
landor --save save.txt       # auto-saves when you quit
landor --load save.txt
landor --view 60x30
```

Run the binary from the project root (it loads `assets/worlds/starter.txt` by
relative path), or pass `--world /abs/path.txt`. `--view` sizes the fixed frame
buffer described in §6.1.

---

## 2. Data model

### 2.1 Tile data is a Flyweight

Intrinsic tile-type data lives once, in a shared descriptor table; the map stores
ids. This is what makes the multi-target memory budget work: a fat object per tile
(≈24–32 B) would put a 120×60 map at ~230 KB, which exceeds the RP2044's entire
RAM, while 1–2 B per tile plus one table fits every target.

* `TileDesc` (intrinsic): material, glyph-set entry, `tileset_id`, walkable, opaque,
  hardness + required tool, blend group, paint order, art-capability flags.
* Ids are **indices, never pointers**. An index survives serialisation, a pointer
  does not, and identity comparisons stay cheap.
* Intrinsic flags (`walkable`, `opaque`, `unbreakable`) are *never duplicated* per
  cell: duplicating them wastes bits and creates two sources of truth that can
  disagree. Per-cell bits hold only state that changes during play.
* Dynamic variants (`d`/`D`, chest full/open, rock/cracked/rubble) are separate
  descriptor entries selected by sparse state, not per-tile member variables.

### 2.2 The cell array (the backbone)

ASCII is a render target, **not** the data model. The backbone is a fixed cell array
of packed unsigned words — type id plus per-instance fields such as height, ground
overlay and stage — so terrain can carry information no single glyph could express:

```cpp
// File format, so: explicit masks and shifts. C++ bit-field allocation order is
// implementation-defined -- `struct { uint8 a:4; }` must never be used here.
using CellWord = std::uint16_t;   // cfg::cell_bits; 32-bit where RAM allows

// uint16 layout
//  [7:0]    type_id   key into TileDesc (material, glyph, walkable, opaque, hardness)
//  [10:8]   height    discrete elevation, 0..7
//  [13:11]  overlay   ground content: none / crop / rubble / debris / shallow water
//  [15:14]  stage     overlay growth stage or damage stage
```

* Every field has a `static_assert`; the layout constants are part of the save-file
  header together with a layout version number.
* Accessors are the whole API (`height_at(c)`, `overlay_at(c)`, …). No code outside
  `world` shifts bits, which is what lets `CellWord` be 16-bit on Pico and 32-bit on
  host without touching logic — and it is why the renderer never sees a cell.
* Elevation needs real rules, not just appearance: max climb Δh per actor kind,
  whether mountains block sight past their crest while hills only screen low tiles,
  and movement cost per Δh (open, §11).
* Glyph choice is derived from a cell, never stored in it: priority chain
  actor → tile-bound content → overlay → material, with elevation resolved by a
  shade ramp. The glyph table is generated from the same descriptor table as the
  sprite ids, so ASCII and graphics output cannot drift apart.

### 2.3 Storage: immutable base + patch overlay

RAM scales with *change*, not with world size, and the view costs a constant:

| Cost | Proportional to |
| --- | --- |
| `Frame` | screen size (+1 halo ring) — independent of map size |
| FOV and queries | view radius × chunks touched (vision radius is 4–7) |
| Storage | authored data, compressed, in flash + the player's edits |

So **world size is bounded by flash/asset size, not by RAM.**

```cpp
// 32x32: 2^k dimensions -> shifts, no divides
struct Chunk
{
    Span<const Run>   base_runs;     // RLE (value, length) over read-only authored data
    SmallVec<Patch, N> patches;      // {local_index, CellWord}, SORTED by index
    std::uint32_t     version;       // bumped on first patch -> invalidates caches
};
```

* The authored base lives in `rodata`/flash and is decoded on demand into a small
  resident working set of chunks.
* **The edit journal is the save file** (§7.2): `--save` writes the applied ops plus
  the rule-set version, not a grid dump. `--load` parses the base and re-applies ops.
  Compact enough for the tiny targets, diffable for analysis.
* Row-wise RLE is hostile to diagonal raycasts, so rows/chunks decode into a cached
  line buffer and `at()` stays the only public access.
* Patch iteration must be coordinate-ordered. Hash-container iteration order varies
  between standard libraries and runs, and would silently break the "no RNG"
  guarantee through the back door.
* Patch growth needs a policy, since ambient change plus endless digging drifts
  unbounded (and RP2044 flash has wear and erase-block limits). Recommended: compact
  into rewritten chunk blobs at save time, and keep mutable regions finite by design
  so the patch count has a natural ceiling.

**Storage maintenance safe point** *(design direction; the managed-heap defrag pass is
specified in §2.4)*:

* A safe point exists **immediately before the next frame begins** — after rules, terrain
  operations, vision and rendering, at the end of the frame — at which nothing holds a
  resolved address. Object relocation therefore happens only there; the heap tracks
  live scoped accesses and reports a blocked compaction if one is still open, so the
  rule is enforced by the component, not assumed.
* Each pass moves a **limited amount of bytes**, when fragmentation is noticeable. The
  maintained invariant is progress, not packedness: interior hole volume — free space
  strictly between live objects, excluding the top and bottom spans — shrinks by at
  least one byte on every pass that runs, or the pass is skipped; contiguity is never
  promised within one frame. This bounds worst-case frame cost, which is the property
  that matters on a slow core.
* Both the trigger and the choice of which objects move must be deterministic functions
  of game state, or replays diverge while nothing visibly happened.
* A deferred idea worth keeping: several object classes sharing **one backing store**
  (now the managed heap, §2.4) and each with its own typed index arithmetic over it (`sizeof(T)`-scaled indexing, distinct
  C++ index types so an actor slot cannot be passed where a patch slot is expected), with
  partial packing whose guarantee is only that free extents stay large enough for the
  largest live class — rather than promising full defragmentation. Cross-class starvation
  is then the hazard to manage, per-class extent floors being the cheap defence.

### 2.4 Managed heap: variable-length object store

All dynamic allocation in `world`/`game` flows through one managed region — no
`malloc`, no allocating containers. The region is the shared backing store
sketched in §2.3, and it is now a concrete in-repo component: the single
header [`managed-heap/managed_heap.hpp`](managed-heap/managed_heap.hpp) —
C++11 baseline, usable as-is from the C++20 build — with its own design
document [`managed-heap/SPEC.md`](managed-heap/SPEC.md), usage guide
[`managed-heap/USER_GUIDE.md`](managed-heap/USER_GUIDE.md), and examples
(`example.cpp` smoke test, `example2.cpp` minimal showcase). This section
states the landor-side contract; the component's internals follow its spec.
The header also supports several same-shaped heaps with distinct compile-time
domains if landor ever needs more than one arena.

**Status: externally supplied, frozen.** `managed-heap/` was developed in its
own layer before landor's implementation starts, and landor consumes it as-is.
Implementation work must not modify `managed_heap.hpp`, `SPEC.md`,
`USER_GUIDE.md`, or the examples. Landor uses the public API and configures the
component (region capacity, compaction policy, budget and threshold, safe-point
timing); gaps or defects found while using it are recorded for the component's
owner, not fixed in-tree.

```text

high addr┌───────────────────────────────────────────────┐
         │ object region                  grows downwards│
         │                                               │
         │             free span                         │
         │                                               │
         │ descriptor-table stack          grows upwards│
low addr └───────────────────────────────────────────────┘
```

**Metadata.** Descriptors live in a dense stack of equal-sized tables (16
entries per table by default) growing upward from the base, added at runtime
within capacity as demand requires — there is no fixed object cap and no
pointer links between tables; the *n*-th table is found by arithmetic. Each
table carries a stable logical id, and a handle is that id plus an entry
index, packed into one compact integer, so whole tables may relocate or be
popped without invalidating a single handle. An empty interior table is
covered by moving the physical top table into its place (moved tables keep
their logical id), which keeps every handle into that table's entries valid.

**Offset widths.** Offsets and sizes are stored in the smallest integer that
can name the configured capacity (8/16/32-bit, or a native pointer where a
compact integer buys nothing — on a 32-bit target a 32-bit offset is no
cheaper than a pointer). A region up to 0x10000 bytes therefore gets 16-bit
offsets and sizes automatically, and bigger host capacities widen them
without any per-target code.

**The object chain.** Descriptors are singly linked, **sorted by object
position**: `next` points at the descriptor whose object lies next in memory
(downward). Each descriptor holds `{offset from the heap base, size}`; the
reserved extent is the size rounded up to the heap alignment. Everything else
is *derived* by walking the chain, using each object's base, `offset`, and
top, `offset + reserved`:

* hole between two links, A above B = A.next below it:
  `A.offset − (B.offset + B.reserved)`;
* top gap, above the top-most object: `heap_top − (first.offset + first.reserved)`;
* bottom span, above the metadata stack: `last.offset − metadata_top`;
* used and free totals without a walk: free = capacity − metadata bytes −
  reserved object bytes, kept as a running counter, so total free space is O(1).

The derivations hold only while the chain invariant holds (position-sorted,
`next` pointing down); a debug build asserts it on every walk, and
`assert_invariants()` runs a full check on demand.

Holes are computed, never stored: adjacent frees merge automatically and
coalescing costs nothing. The heap's entire global state is base, capacity,
root link, the descriptor tables, and the reserved-bytes counter.

**Allocation — best fit.** One walk from the root computes every candidate
region (top gap, interior holes, bottom span); the tightest region that still
holds the request after alignment is chosen, ties broken by highest address
first (the walk order). Offsets are byte-granular, so the heap guarantees
each object base aligned to the configured heap alignment (a compile-time
knob, `alignof(std::max_align_t)` by default): candidate start offsets are
rounded up and the alignment slack counts against the region. (With an empty
heap the first object simply starts at the top.) When the winning region is a
span, the new object is anchored against the existing objects — at the bottom
of the top gap, at the top of the bottom span — so the remainder stays one
contiguous span; an existing object is never extended in place, a new
descriptor is always inserted. A request that fits no region fails
deterministically (null handle), and a failed `make` never relocates anything
on its own — relocation is explicit, at the safe point (§2.3). End-only
(bump) allocation is rejected on purpose: it monotonically shrinks the free
span toward the descriptor tables, so the heap can report exhaustion while
interior holes are plentiful. Best fit keeps the span alive.

**Free.** The refcount reaching 0 destroys the object and unlinks the
descriptor — one walk from the root to find its predecessor, O(objects),
acceptable at this scale — and returns its entry to its table; no further
bookkeeping, the hole simply appears in the derived computation.

**Handles and safety.** The game is one synchronous loop, which is exactly the
heap's single-owner requirement: no other thread or ISR allocates, frees or
accesses it. Handles come in two tiers:

* *Function-scoped*: `unwrap()` resolves the object once, caches the address,
  and returns a non-copyable access object that owns one temporary reference;
  repeated `->` inside the scope is plain pointer-speed access, the object
  stays alive even if the persistent handle is reset in the meantime, and the
  retained reference is released at scope exit. Compaction cannot occur
  mid-scope — the heap tracks active scoped accesses and reports a blocked
  result while one is open — so the pointer is trivially valid for the whole
  call.
* *Persistent* (actor registry, behaviour-tree blackboards, journal):
  `managed_ptr<T>` is a refcounted owning handle holding only the logical
  descriptor identity — copy retains, destruction releases, and it carries no
  address. It is re-resolved through the descriptor at the point of use, which
  is always correct: the descriptor holds the current offset (compaction
  rewrites it) and the refcount keeps the object alive while the reference
  exists.

No generations, no epoch counters: safety is the double indirection itself —
clients never hold addresses, objects move only at the safe point, and only
descriptors are rewritten. A refcount of 0 is a dead handle: the null handle
is the component's dead form for registry entries that lose their object, and
resolving a dead handle is a checked, deterministic error. A raw pointer is
reachable only from a scoped access, via `unsafe_ptr()`, for immediate
synchronous interop (serialisation, C-style APIs); it must never outlive the
scope, survive a compaction point, or be stored, queued or logged — the
deliberate friction is what keeps the relocation model intact. Managed types
must be nothrow-constructible, nonthrow-destructible, and semantically safe
under byte-wise `memmove` relocation (no member that points into the object's
own storage); defining `MANAGED_HEAP_STRICT_TRIVIALLY_COPYABLE` adds a
conservative static check on toolchains without relocatability inspection.

**Compaction.** Runs only at the end-of-frame safe point (§2.3), never during
ordinary allocation. The strategy is implemented in the component rather than
undecided: *evacuate* moves the lowest objects into higher holes to grow the
bottom span next to the metadata (needed when another descriptor table must
be pushed), *shift* closes interior holes by moving lower runs, and *hybrid*
alternates both in bounded rounds. Each pass moves at most a configured byte
budget, with per-strategy attempt limits, and a fragmentation threshold lets a
caller consult `should_compact()` (derived interior hole volume above the
threshold). The invariant is progress, not packedness: a pass that runs must
strictly improve geometry — reduced interior hole volume or enlarged bottom
span — and compaction conserves *total* free volume (capacity minus used
never changes under relocation), so what improves is its distribution.
Contiguity is never promised within one frame. Which policy, budget and
threshold landor configures, and when the first post-render pass switches on,
remain open (§11) — the decision awaits observed transient-object statistics
(lifetimes, sizes, churn) from headless runs.

**Out of scope for the memory design** (lifetime/persistence questions, not
layout questions): which references are strong vs weak (journal `cause`,
saves, NPC memory); richer dead-handle semantics for weak records (persistent
identity beyond the component's null-handle form); which non-actor
payloads share the arena; save-file encoding of handles.

---

## 3. Engine concept and actors

The game is developed in C++ (C++20, no dependencies) with strong modern OO design,
and has the concept of actors like other game engines:

```
 Actor (can be placed in the world, has components)
    └── Pawn (an Actor that can be "possessed" by an AI or Player Controller)
        └── Character (a Pawn with built-in movement, physics, and collision)
            ├── BP_NPC_Base      (custom class for all AI characters)
            └── BP_Player_Base   (custom class for human-controlled characters)
```

Actors are placed on maps. The player only sees a limited top view of a much bigger
map and of the actors in that area.

### 3.1 Objects and the step function

Three families of things, keyed differently:

| Family | Keyed by | Examples | Storage |
| --- | --- | --- | --- |
| Terrain | coordinate | rock, grass, road, dug hole | dense packed cell array |
| Tile-bound | coordinate, sparse | **crops**, rubble, dropped items, chest/gate state | `{coord → stage/state}` table |
| Actors | `ActorId` slot handle | player, NPCs, **animals** | actor registry over the managed heap (§2.4) |

* Crops are **not** actors: no slot, no per-frame pass over hundreds of wheat tiles,
  and they serialise as coordinates. Animals **are** actors: driven by AI, movable,
  and assignable as the `cause` of a `TerrainOp`.
* Growth stage stays on the Flyweight side: a small sparse `{coord → planted_step}`
  table plus `stage = f(world_clock - planted_step)` resolving to a
  `wheat_1..wheat_4` display entry — no per-tile counters.
* One resolution order decides what a cell presents, in a single accessor used by
  both rendering and queries: `terrain id → tile-bound overlay → display id`. ASCII
  and sprite backends therefore cannot disagree about what is under the player.
* Phase order inside `step(input)`: `input → rules → (ecology) → terrain ops →
  vision`. The ecology phase is empty today, so filling it in later costs nothing.

### 3.2 NPCs: alignment and behaviour trees

NPCs can be human, monsters or animals, and have complex behaviour determined by
their nature. We follow the D&D alignment schema:

> **Lawful good** — acts with compassion, honour and duty; regrets actions that
> violate their code even when those actions are good. Gold dragons, righteous
> knights, paladins, most dwarves.
>
> **Neutral good** — altruistic without regard for lawful precepts; cooperates with
> officials but feels beholden to nobody; bends rules without inner conflict.
> Celestials, some cloud giants, most gnomes.
>
> **Chaotic good** — does whatever brings change for the better, disdains
> bureaucracy, values personal freedom highly; methods often out of sync with
> society. Copper dragons, many elves, unicorns.
>
> **Lawful neutral** — believes strongly in order, law, tradition or a personal code
> and adheres strictly to it, for themselves or for the world. A soldier who always
> follows orders, a merciless enforcer of the letter of the law, a disciplined monk,
> some wizards.
>
> **True neutral** — neutral on both axes, or actively seeks balance. Druids
> frequently follow this dedication to balance. Lizardfolk, most druids, many humans.
>
> **Chaotic neutral** — individualist who follows their own heart and shirks rules
> and traditions; their own freedom comes first, good and evil second. Many
> barbarians and rogues, some bards.
>
> **Lawful evil** — sees a well-ordered system as the way to fulfil personal wants
> and uses it to further their power. Tyrants, devils, corrupt officials, mercenaries
> with a strict code, blue dragons, hobgoblins.
>
> **Neutral evil** — selfish, turns on allies-of-the-moment, harms others without
> compunction but needlessly only for direct benefit; or holds evil up as an ideal.
> An assassin with little regard for law who does not needlessly kill, a plotting
> henchman, a mercenary who switches sides for a better offer; a masked killer
> striking for fear. Many drow, some cloud giants, yugoloths.
>
> **Chaotic evil** — no respect for rules, other people's lives, or anything but
> their own selfish and cruel desires; resents orders, works poorly in groups. Higher
> undead such as liches, killers who strike for pleasure, demons, red dragons, orcs.
>
> **Unaligned** — not sapient enough for moral decisions; operates purely on
> instinct. Sharks are savage predators, but they have no alignment.

NPCs are driven by behaviour trees. For instance, an NPC wants to go through a door:
it tries to open it, discovers it is locked, then — depending on its nature —
searches for a key, or decides to break it down, which may mean finding an axe first
because the door is wood. It may ask another NPC carrying an axe for help, or decide
to kill that NPC and take it, depending on alignment.

NPCs can interact with the player, produce dialog and receive responses.

Because terrain is editable (§5), alignment has teeth beyond dialogue: an unlawful
actor can destroy a wall instead of opening a door, and the world edit carries the
acting agent as its cause, so the law (and the victim) can react.

The compiled behaviour tree is itself a Flyweight: one shared, stateless tree per NPC
archetype, with the per-NPC blackboard supplying the extrinsic state. Alignment
parameterises which tree variant is assigned. This is how a small target hosts many
NPCs without one tree each.

---

## 4. Rules (v0.1)

* Starter world: three shards hidden in the valley; a chest holding a key; a gated
  chamber containing the beacon, whose gate is its only entrance and consumes one
  key. Reaching the beacon while carrying all 3 shards wins.
* Day advances every 120 steps; night vision radius 4 vs day 7. Vision radius and
  day length are rule *data*, not constants (§8).
* Fog of war: line-of-sight tiles become visible; seen tiles are remembered (dimmed).
  HUD reports `Explored N%`.
* Stepping onto pickups collects them; stepping into chest/gate/door/sign interacts
  in place; solid tiles cannot be entered.
* No combat in 0.1 (see §10).

---

## 5. Terrain mutation

Terrain is editable: make holes, destroy mountains, create and remove roads. Because
an edit is often many tiles (clear rock, leave rubble, drop an item, invalidate
sight), it is expressed as an operation, never as `set_tile()`:

```cpp
struct TerrainOp
{
    OpKind    kind;      // Excavate, Breach, PaveRoad, Fill, Build, ...
    TileCoord origin;
    Shape     shape;     // point / radius / brush id
    Tool      by_tool;   // resolves against hardness in TileDesc
    ActorId   cause;     // attribution: law, grievance, analysis
};

[[nodiscard]] GameError apply(const TerrainOp&, StepNo at) noexcept;
[[nodiscard]] StepNo version() const noexcept;        // per-chunk versions bump
[[nodiscard]] DirtyRegion take_dirty() noexcept;      // caches recompute just this
```

* **Partial destruction uses damage stages as descriptor variants**
  (`rock → rock_cracked → rubble`), so no per-tile hit-point counters. Arbitrary HP
  would need a sparse overlay and a second save section; deferred.
* Hardness and required tool live in `TileDesc`, which is what makes "wood door
  yields to an axe, mountain needs a pick and three stages" implementable rather than
  scripted.
* `unbreakable` bedrock protects winnability: digging can otherwise seal the beacon
  away or trap the player. Reachability invariants that `gen_starter_map.py` checks at
  authoring time are void at play time, and re-checks stay local because only patched
  chunks can have changed connectivity.
* Derived caches (walkability, sight maps) key off the version/dirty region, or NPC
  pathfinding re-derives height forever. NPC movement should use **shared flow
  fields** toward goals, recomputed over dirty regions: one computation serves every
  actor heading the same way, and new holes are handled naturally.

---

## 6. Rendering

### 6.1 Frame contract

`build_frame()` produces a backend-agnostic `Frame`; `TerminalRenderer`,
`HeadlessRenderer` and a future `SpriteRenderer` all consume it. `RenderCell` is a
pure copy with no pointer back into the world:

```cpp
struct RenderCell
{
    char32_t      codepoint;    // projected glyph for this cell (own tile, always)
    std::uint16_t tileset_id;   // stable logical type
    std::uint8_t  fg, bg;       // palette indices
    Mark          mark;         // None / Remembered / Dim
};
```

`Frame` is allocated as the view **+1 clipped halo ring**: the extra ring carries
ids/glyphs, is excluded from output, and any backend may read it. Without it every
tile on the screen border gets a hard, wrong edge (~20 % of a 60×30 view).

**Everything decorative is derived inside the backend at draw time** — symmetry,
rotation, transitions, influence. None of it is `Frame` state or world state, because
a neighbour mask is a pure local function of cells already present, and because
digging creates new adjacencies that would invalidate any baked-in join. The ASCII
backends simply never call those helpers. This is also why graphics work can be
rewritten wholesale without touching a tested rule.

### 6.2 Auto-tiling by composition, not by variant explosion

Under D4 (4 rotations × optional mirror), one source asset yields 8 appearances, so a
material needs centre + 1 edge + 1 corner ≈ 3 assets instead of the classic 47 blob
variants; T-junctions emerge from overlapping edges and corners. Masks are computed
over view+halo and cached per chunk against the chunk `version`, so destroying a
mountain costs work proportional to the blast, not the map.

Rotation applies to geometry only. Anything carrying directional meaning must not
rotate — baked shadows, sun direction, road ruts, arrows, signs, doors, damage cracks
— hence art-capability flags in `TileDesc` (`rot_symmetric`, `rotatable`, `flipable`,
`asymmetric`) asserted by the sprite backend rather than silently
mis-rendered. Rotations/flips bake once at load (DMA2D blends but does not rotate;
software rotate-per-frame is waste) and nearest-neighbour only, so binary alpha stays
crisp and identical across backends. 1:1 pixel aspect is required.

### 6.3 Composition and palettes

**Composition uses binary transparency**: colour-key skip (`if (px != KEY)`), no
blending math, identical cost on RP2044 and on DMA2D. Palette-indexed art with
reserved index 0 as transparent makes false-positive keys impossible, costs 1 B per
pixel, and turns day/night and torchlight into a **palette swap** — `game` exposes a
semantic light level, the renderer picks the palette. Day-night never touches pixels
or glyphs.

With pure colour key there is no blending to hide ambiguity, so **`paint_order` is a
strict total order with no ties** — two materials sharing a priority would let the
terminal and sprite renderers legitimately disagree. Checked at startup over the
tileset table. Three worked examples belong in the renderer contract: straight join,
T-junction, three-way corner.

### 6.4 Transitions: self-shape vs influence

Two distinct mechanisms, which must not be conflated:

* **Self-shape** (symmetric): same-type adjacency blob edges — roads, water, fields.
* **Influence** (directed): material A paints a decal onto B's cell when adjacent —
  mountain→grass, mountain→tree, hill→grass, tree→path canopy, sand→water wet rim.
  Influence is data: an `influences[a][b]` bit matrix over ≤16 materials in the
  descriptor table.

Constraints that keep backends in agreement:

1. One dominant influencer per cell (cap two, corners only), chosen by strict
   influence priority with material-index tie-break.
2. The influenced cell always keeps its own identity — logic-wise *and* glyph-wise.
   For ASCII that means own glyph plus shifted `fg` from the influencer's palette row.
3. Directional decals take direction from context (including sun direction implied by
   the day-night clock), never from rotating the source sprite.

### 6.5 Text target character set

Map files and the text target are **UTF-8**, not restricted 7-bit ASCII. This is safe
because we produce every map, tile table and renderer ourselves — no untrusted input path
— so the guarantee comes from our own tools rather than from sanitising someone else's
file.

* `RenderCell` carries a `char32_t` codepoint; UTF-8 encoding happens in the terminal
  backend at output time, so logic, digests and comparisons work on integers and encoding
  never leaks upward.
* Cell storage is unaffected: cells hold type ids, glyphs were always a projection (§2.1).
* Each descriptor keeps an ASCII fallback alongside its preferred codepoint, used when the
  locale or font cannot render it and for machine-readable logging. Snapshot tests run in
  fallback mode so digests stay identical across machines.
* Block and shade elements (`▀ ▄`, eighth blocks, `░ ▒ ▓`) give genuine two-source
  composition inside one text cell, which is the transition idea of §6.2 with a consumer
  today instead of only once sprites exist. Elevation uses a shade ramp (§2.2).
* Wide (East-Asian-Wide), combining and variation-selector characters stay out of the
  tables: they occupy two cells or shift the following glyph, breaking the one-codepoint-
  per-cell contract. The generator checks the tables for this, and the map parser decodes
  UTF-8 by hand (no dependency, columns counted in codepoints) rejecting BOM, CRLF and
  malformed sequences as errors.

---

## 7. Determinism, tests, save/load

### 7.1 Determinism

No RNG and no wall-clock in logic. Snapshot tests compare per-chunk digests ("same
inputs ⇒ identical map") and final frames; `--headless --script` replays inputs
against the ops journal. Reachability/climb invariants run in
`tools/gen_starter_map.py`. Iteration order of world state must be coordinate-ordered
everywhere it can affect output, because that is a determinism dependency just as
much as RNG would be.

### 7.2 Save/load

A save contains: rule-set version, config fingerprint (§8), map identity, actor
registry state, sparse tile-bound state, and the terrain edit journal. The grid is no
longer reconstructible from the `.txt` alone once terrain mutates, so the journal is
mandatory; a full cell dump exists only as a debug/`compress_base=false` option.
Loading checks fingerprint and rule-set version and fails cleanly rather than
misreading bits ("you replayed a host save against a Pico build").

---

## 8. Configuration: engine knobs vs gameplay numbers

All tunables live in one generated header, `landor_config.hpp`, produced per build
profile (`host`, `pi0`, `pico`, `f429`) and exposed through a single CMake INTERFACE
target — no hand-editing, no per-directory overrides. Magic numbers elsewhere in
`src/**` are lint errors.

Knobs follow the project constant convention: lowercase `snake_case` constants such as
`cfg::cell_bits` and `cfg::chunk_shift`, with `ALL_CAPS` reserved for the preprocessor
(`coding_style.md` §6). That reservation matters most here, since this header is produced
by `configure_file`, which is exactly where a define-versus-constant confusion would hide.

**Category A — engine shape (compile-time).** These change memory layout: cell width,
type-field width (which caps material count), chunk shift, view size, halo, resident
chunk count, managed-heap capacity, max patches per chunk, base compression. Their relationships
are asserted at configure time, so an impossible profile fails the build instead of
failing on a board nobody has plugged in:

```cpp
static_assert((1u << cfg::type_bits) - 2u >= num_materials, "material enum out of field");
static_assert(cfg::resident_chunks >= ((cfg::view_w >> cfg::chunk_shift) + 2*cfg::halo)
              * ((cfg::view_h >> cfg::chunk_shift) + 2*cfg::halo),
              "view+halo does not fit the decoded working set -- FOV would thrash");
static_assert(cfg::resident_chunks * (1u << (2*cfg::chunk_shift)) * sizeof(CellWord)
              <= LANDOR_RAM_BUDGET);
```

**Category B — gameplay numbers (data).** Vision radius 7/4, the 120-step day length,
climb limits, hardness/tool matrix, harvest amounts: these ship as a rules table with
in-repo defaults plus an optional `[rules]` section in the world file. Keeping them
out of `constexpr` means targets cannot silently diverge while claiming to run the
same version, snapshot tests mean cross-target equality, retuning the day cycle does
not invalidate saves or replays, and each save records the rule-set it used.

**Templating stays at the seams.** `template <class Storage, class ClockPolicy> class
WorldT;` with the config choosing the typedef (`using World = WorldT<ChunkedStorage,
StepClock>;`) so call sites stay `world::World&`; templates spreading through every
header would double code size on embedded targets and destroy diagnostics. Runtime
`IRenderer` polymorphism costs one indirect call against a blit, so the vtable stays;
CRTP devirtualisation is available behind a config switch if a target ever needs it.

A `config_fingerprint` string (hash of the knobs) is stamped into the build and into
save files, checked on load: mismatched constants across translation units would
otherwise mean silent ODR violations with different struct sizes.

---

## 9. Source layout

```
src/world    cells, chunks, tile descriptors, terrain ops     (no colours, no glyphs required)
src/game     step(), inventory, vision, day/night, win state  (no tilesets)
src/render   frame builder, autotile/influence, backends      (reads state only)
src/actor    Actor / Pawn / Character, controllers
src/npc      alignment, behaviour trees, dialog
src/host     argv, interactive terminal, script replay, save/load
managed-heap frozen single-header managed object store (SPEC.md, USER_GUIDE.md, examples)
assets/      worlds/*.txt, tilesets/*
tools/       gen_starter_map.py, checks/
```

World files are plain text, one glyph per tile (`P` marks the spawn). The authored
base is compressed at build time for embedded profiles (§2.3).

---

## 10. Deferred

* **Ecology** — an extension of animals (actors) and crops (tile-bound growth). Its
  `step()` phase slot already exists (§3.1); deferred partly because ambient change
  would make the patch overlay grow without bound.
* **Tiled graphics renderer** — a third `Frame` consumer, once tile ids and the
  tileset manifest are stable.
* **Damage/combat** — recommended absent from 0.1; keep 0.1 pure exploration.
* **Multi-character glyph composition** — building one cell's appearance from several
  characters (or several draws into one cell) for richer text representation than a single
  codepoint allows. Noted as viable; unspecified.
* Multi-Z/cave layers, arbitrary hit points, fluid simulation, larger NPC populations
  with dialog trees.

## 11. Open questions

1. Max world size per target, and therefore chunking parameters (§2.3).
2. Elevation semantics: Δh=1 free and Δh≥2 blocked, or cost-based climbing? Do
   mountains block sight past their crest? Is falling a thing?
3. Patch-growth policy beyond save-time compaction (§2.3).
4. Bedrock as the anti-soft-lock rule — accepted, or reject edits that break
   reachability instead?
5. Fluids/flooding explicitly out of scope?
6. v0.1 NPC count (one scripted NPC with a small behaviour tree would prove alignment
   and BT cheaply), and whether NPCs may perform `TerrainOp`s in 0.1.
7. Naming: Unreal-flavoured `BP_NPC_Base` as literal C++ type names, or `NpcBase` with
   `BP_` reserved for data-driven assets?
8. ~~Actor handles with generation counters~~ — resolved (§2.4): no generations
   needed for live objects; refcount plus the descriptor-held absolute offset
   make re-resolution always correct. Save/load identity encoding stays open.
9. ~~Storage topology behind the pool interface~~ — resolved (§2.4): the shared
   managed-heap component is in the repo (single header) with best-fit
   allocation, derived free geometry and byte-budgeted compaction. Remaining:
   the policy/budget/threshold landor configures and the first post-render
   pass timing, awaiting transient-object statistics.
10. Exact std surface allowed in `world`/`game` per target, beyond the exclusions already
    listed in [`coding_rules.md`](coding_rules.md) §13.
