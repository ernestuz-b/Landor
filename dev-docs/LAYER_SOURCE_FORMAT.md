# Landor — Layer Source File Format

This document defines the current authored on-disk format for one Patch layer source.

The first format is intentionally dense and simple. Sparse coordinate-record variants may be considered later, but are **not** part of version 1.0.

## File naming

One authored layer source belongs to one Patch and one Layer.

The current filename convention is:

```text
<PatchName>.<LayerName>.layer
```

For example:

```text
GoodMagePalace.terrain.layer
GoodMagePalace.height.layer
GoodMagePalace.fire.layer
```

The filename is a platform/editor-facing convention. Runtime geography still refers to the backing source through `storage::SourceId`; filesystem paths do not leak into `Patch` or `Map`.

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

### `P` — natural position

`P` gives the authored Patch natural position.

Example:

```text
P:120 80
```

or:

```text
P:0x78 0x50
```

Integer metadata may use ordinary decimal notation or `0x`-prefixed hexadecimal notation.

The natural position does not participate in the physical byte offset within the layer file. Data coordinates are Patch-local.

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
0x20  ' '   no authored contribution at this coordinate
0x0A  '\n'  row terminator; never a cell value
```

A space does **not** mean runtime value zero. It means that this authored Patch contributes nothing for this layer at that coordinate, allowing normal Map resolution to continue to another authored contribution, working state, or procedural/default fallback.

Any other non-LF byte is an authored layer value. Its meaning belongs to that Layer, not to the generic file format.

Data bytes are literal. In particular, leading and trailing spaces in a row are cells and must never be trimmed.

## Direct addressing

Let `data_offset` be the byte immediately after the metadata-terminating `0x0A 0x0A`.

For a layer of width `width`:

```text
row_stride = width + 1
```

because every row has `width` cell bytes followed by one LF.

For Patch-local coordinate `(x, y)`:

```text
offset = data_offset + y * row_stride + x
```

with:

```text
0 <= x < width
0 <= y < height
```

This is the physical `storage::Offset` of that cell in the backing source.

The natural position `P` is not added here. Placement/orientation logic maps world coordinates to Patch-local coordinates before this file-offset calculation is used.

## Size and structural validation

Once metadata has been parsed, a version 1.0 dense source has expected data size:

```text
data_size = height * (width + 1)
```

and therefore expected total size:

```text
expected_size = data_offset + data_size
```

A strict version 1.0 reader can validate that:

- the first empty line exists;
- `V`, `D`, and `P` are valid;
- dimensions are representable and non-zero;
- exactly `height` data rows exist;
- every row contains exactly `width` cell bytes;
- every row terminates with exactly one `0x0A`;
- the final row also terminates with `0x0A`;
- no extra bytes follow the final row.

## Relationship to Patch and Storage

`LayerBinding::source` remains an opaque `storage::SourceId`.

The generic Storage interface only supplies bytes. It does not parse layer metadata and does not know Patch geometry.

A mapping/source reader above Storage is responsible for:

1. locating the metadata terminator;
2. parsing the metadata it needs;
3. validating dimensions/structure;
4. translating a Patch-local coordinate into the direct byte offset;
5. reading authored cell bytes into the layer-oriented Cache/Chunk path.

The same format can therefore be backed by a host filesystem, SD card, flash, ROM, or another Storage implementation without changing Map/Patch semantics.

## Deliberately deferred

Version 1.0 does not define:

- sparse coordinate-prefixed data records;
- compression;
- variable-width cell values;
- multi-byte layer values;
- layer-specific metadata records;
- unknown-record compatibility policy;
- cache replacement or write-back representation.

Those can be added only when there is a concrete need. The dense format above is the current authored layer source contract.
