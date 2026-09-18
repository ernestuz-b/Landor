# Landor — Layer Source File Format

This document defines the current on-disk format for one layer source.

The same format is used by both immutable authored layer files and writable runtime/materialized layer files. Their different ownership, resolution, and write-back roles are defined in `LAYER_STORAGE_MODEL.md`.

The first format is intentionally dense and simple. Sparse coordinate-record variants may be considered later, but are **not** part of version 1.0.

## File naming

For authored data, one layer source belongs to one Patch and one Layer.

The current authored filename convention is:

```text
<PatchName>.<LayerName>.layer
```

For example:

```text
GoodMagePalace.terrain.layer
GoodMagePalace.height.layer
GoodMagePalace.fire.layer
```

Runtime files use the same `.layer` byte format, but their naming/identity convention is deliberately not pinned yet. Runtime state belongs to a live world occurrence/area, and Patch name alone is not sufficient when the same Patch has several Placements.

The filename is a platform/editor-facing convention. Runtime geography still refers to backing sources through `storage::SourceId`; filesystem paths do not leak into `Patch` or `Map`.

## Source roles

The byte format does not distinguish authored from runtime data. The role comes from how the source is registered and used.

Authored sources are immutable and may live on read-only storage. Runtime sources are writable persistence for live world changes and may live on a completely separate filesystem, device, root, or Storage backend.

Runtime write-back never modifies an authored source.

See `LAYER_STORAGE_MODEL.md` for the lifecycle and resolution rules.

## Overall layout

A layer source contains two sections:

1. line-oriented metadata;
2. a dense two-dimensional byte grid.

They are separated by the first empty line.

At byte level, the metadata terminator is exactly:

```text
0x0A 0x0A
```

The byte immediately after the second `0x0A` is the first cell of the dense data section.

Example:

```text
V:1.0
D:32 32
P:0 0

                                
      ####                      
     ######                     
      ####                      
                                
...
```

All line endings in the format are Unix/Linux LF only:

```text
'\n' == 0x0A
```

`CRLF` (`0x0D 0x0A`) is not part of the format.

Files should be treated as byte streams / opened in binary mode so host text-mode newline translation cannot change physical offsets.

## Metadata

Metadata is deliberately extensible.

Each metadata record occupies one line. The current notation is:

```text
<Record>:<payload>\n
```

The first empty line terminates metadata. Therefore metadata itself cannot contain an empty line before the end of the metadata section.

Version 1.0 defines these core records:

```text
V:1.0
D:<width> <height>
P:<x> <y>
```

### `V` — format version

Example:

```text
V:1.0
```

### `D` — dimensions

`D` gives the logical layer width and height in cells.

Example:

```text
D:32 32
```

For version 1.0, the dense data section contains exactly `height` rows and exactly `width` cell bytes per row.

For an authored Patch source, source-local coordinates are Patch-local coordinates. The represented v1 authored rectangle is anchored at Patch-local `(0, 0)` and spans:

```text
0 <= x < width
0 <= y < height
```

Consequently, the Patch's authored local extent and the source `D` dimensions describe the same rectangle; v1 does not carry a separate local-origin offset.

### `P` — natural position

`P` gives the natural position associated with the represented layer extent.

For an authored Patch source, this is the Map/world coordinate where Patch-local/source-local `(0, 0)` belongs when the Patch is at its natural placement. It therefore agrees with `Patch::natural_position()`.

Example:

```text
P:120 80
```

or:

```text
P:0x78 0x50
```

Integer metadata may use ordinary decimal notation or `0x`-prefixed hexadecimal notation.

The natural position does not participate in the physical byte offset within the layer file. Data coordinates within the file are local to the represented layer extent.

The exact association between a runtime source and its live Placement/world area is intentionally defined outside this byte-format document.

### Future metadata

More metadata records may be added later, including layer-specific metadata.

Version 1.0 intentionally does not define such records yet. Readers and writers must not assume that `V`, `D`, and `P` are the only metadata records the format can ever contain.

The policy for unknown future records should be defined together with the version/parser implementation that needs it; this document does not invent that policy in advance.

## Dense data section

After the metadata terminator, the layer data is a dense row-major byte grid.

For dimensions:

```text
D:<width> <height>
```

the data section consists of exactly:

```text
<width cell bytes> 0x0A
<width cell bytes> 0x0A
...
```

for exactly `height` rows.

The final row also ends in one LF byte.

There is no `CR`, no `CRLF`, no padding between rows, and no sparse coordinate prefix in version 1.0.

## Cell bytes

Each cell occupies exactly one byte on disk in version 1.0.

The generic format reserves:

```text
0x20  ' '   no contribution from this source at this coordinate
0x0A  '\n'  row terminator; never a cell value
```

A space does **not** mean runtime value zero. It means that this source contributes nothing at that coordinate and resolution may continue to another source.

For an authored source this means "no authored contribution". For a runtime source it means "no runtime override".

If a Layer needs an explicit semantic value meaning empty, zero, absent, burned, dry, and so on, that value must have its own non-space Layer encoding. It must not overload the source-level no-contribution marker.

Any other non-LF byte is a layer value. Its meaning belongs to that Layer, not to the generic file format.

Data bytes are literal. In particular, leading and trailing spaces in a row are cells and must never be trimmed.

## Direct addressing

Let `data_offset` be the byte immediately after the metadata-terminating `0x0A 0x0A`.

For a layer of width `width`:

```text
row_stride = width + 1
```

because every row has `width` cell bytes followed by one LF.

For source-local coordinate `(x, y)`:

```text
offset = data_offset + y * row_stride + x
```

with:

```text
0 <= x < width
0 <= y < height
```

This is the physical `storage::Offset` of that cell in the backing source.

The natural position `P` is not added here. Higher-level placement/runtime association maps world coordinates to source-local coordinates before this file-offset calculation is used.

## Incremental access model

A `.layer` file is a random-access backing source. **Normal gameplay must not require loading the complete file into RAM.**

The intended access pattern is:

```text
Storage source
    -> read metadata incrementally until the first empty line
    -> retain parsed V / D / P, data_offset and row_stride
    -> use direct offsets for later Cache fills
```

After the header has been parsed, a Cache miss should read only the row fragments or ranges required to populate the requested layer Chunk/plane. The fixed-width dense layout exists specifically so those reads can be calculated directly.

A source reader may keep small parsed source metadata, but it must not require an in-memory copy of the complete layer grid.

The same rule applies in reverse to runtime persistence: dirty resident data is written back to the corresponding ranges of the writable runtime source. Runtime write-back does not require rewriting or holding the whole layer merely because the on-disk format is dense.

## Size and structural validation

Once metadata has been parsed, a version 1.0 dense source has expected data size:

```text
data_size = height * (width + 1)
```

and therefore expected total size:

```text
expected_size = data_offset + data_size
```

Normal gameplay opening can validate the metadata and compare the Storage-reported source size with `expected_size` without scanning the complete grid.

When row data is actually read, the reader can also verify the encountered row terminator(s) as part of that bounded read.

A separate strict/offline validator may scan the whole source and verify that:

- the first empty line exists;
- `V`, `D`, and `P` are valid;
- dimensions are representable and non-zero;
- exactly `height` data rows exist;
- every row contains exactly `width` cell bytes;
- every row terminates with exactly one `0x0A`;
- the final row also terminates with `0x0A`;
- no extra bytes follow the final row.

Whole-file structural scanning is **not** a prerequisite for normal gameplay access.

## Relationship to Map and Storage

The generic Storage interface only supplies bytes. It does not parse layer metadata and does not know Patch, Placement, runtime, or simulation semantics.

A mapping/source reader above Storage is responsible for:

1. locating and parsing the metadata incrementally;
2. retaining the source dimensions, natural position, `data_offset`, and row stride needed for direct addressing;
3. translating a source-local coordinate/range into physical byte offsets;
4. reading only the source ranges required to populate the layer-oriented Cache/Chunk path;
5. validating structural bytes encountered by those reads as appropriate.

A corresponding runtime writer uses the same layout when persisting mutable layer state. It targets runtime storage only; authored files are never modified by write-back.

Authored and runtime storage are allowed to be physically independent. A read-only authored filesystem plus a writable save/runtime filesystem is a normal supported architecture.

The same byte format can therefore be backed by a host filesystem, SD card, flash, ROM, or another Storage implementation without changing Map/layer semantics.

## Deliberately deferred

Version 1.0 does not define:

- sparse coordinate-prefixed data records;
- compression;
- variable-width cell values;
- multi-byte layer values;
- layer-specific metadata records;
- unknown-record compatibility policy;
- runtime source naming/identity;
- runtime source creation/registration API;
- dirty tracking granularity;
- write-back scheduling or crash-consistency policy;
- cache replacement representation.

Those can be added only when there is a concrete need. The dense format above is the current layer source contract for both authored and runtime persistence.
