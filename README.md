# glyphpk

A small, single-header C library for storing and looking up glyphs in a compact binary container, designed for memory-constrained embedded systems.

Each glyph has an 8-bit ID (typically an ASCII character code), a width, and an arbitrary binary payload. All glyphs share a common height.

The library handles glyph metadata and blob offsets only. It does not interpret or decode the glyph payloads.

## Features

- Up to 255 glyphs per font, identified by 8-bit IDs.
- Compact binary format (`gpk1`) with fixed-size metadata entries.
- Arbitrary glyph payloads, independent of image encoding.
- Streaming lookup API for reading metadata directly from flash or other storage.
- No heap allocation or internal I/O.
- Optional fixed-size glyph metadata cache with round-robin replacement.
- Asymmetric design: lightweight decoder for embedded targets, separate generator for desktop tools.
- Single-header implementation with no external dependencies beyond the C standard library.

## Integration

Include `glyphpk.h` in your project and define `GPK_IMPLEMENTATION` in exactly one translation unit:

```c
#define GPK_IMPLEMENTATION
#include "glyphpk.h"
```

Configuration macros (define before including the header):

| Macro | Default | Description |
|---|---|---|
| `GPK_CACHE_SIZE` | `8` | Number of cached glyph entries; `0` disables caching |
| `GPK_LOOKUP_CHUNK_SIZE` | `64` | Maximum suggested metadata read size |
| `GPK_GENERATOR` | `0` | Enable the generator API when nonzero |

## Decoder API

### `gpk_init()`

```c
int gpk_init(struct gpk_font *font,
             const uint8_t *buf, size_t size);
```

Initializes a font from the first 8 bytes of a GPK file.

Validates the magic, glyph count, height, and reserved bytes. Initializes the optional cache.

Returns `0` on success, `-1` on error.

The initialized `font` structure exposes `count` and `height`.

### `gpk_lookup_init()`

```c
int gpk_lookup_init(const struct gpk_font *font,
                    uint8_t glyph_id,
                    struct gpk_lookup *lookup);
```

Initializes a streaming lookup for the specified glyph ID. Does not perform I/O.

Returns `0` on success, `-1` on error.

### `gpk_lookup_next()`

```c
int gpk_lookup_next(struct gpk_font *font,
                    struct gpk_lookup *lookup,
                    const uint8_t *buf, size_t size);
```

Continues a glyph lookup using the supplied metadata bytes.

The first call can use `NULL, 0` to check the cache and obtain the initial read request.

Return values:

| Value | Meaning |
|---|---|
| `GPK_MORE` | More metadata required |
| `GPK_FOUND` | Glyph found |
| `GPK_NOT_FOUND` | Glyph not present |
| `GPK_ERROR` | Invalid metadata or arguments |

On `GPK_MORE`, the lookup structure provides:

- `read_offset` — file-relative offset of the next metadata read.
- `want` — suggested number of bytes to read.

On `GPK_FOUND`, it provides:

- `width` — glyph width in pixels.
- `length` — glyph payload length in bytes.
- `offset` — file-relative offset of the glyph payload.

The caller is responsible for reading and interpreting the payload.

Example lookup:

```c
struct gpk_lookup lookup;
uint8_t buf[64];
int ret;

gpk_lookup_init(&font, 'A', &lookup);

ret = gpk_lookup_next(&font, &lookup, NULL, 0);

while(ret == GPK_MORE)
{
    /* read_file() is supplied by the application */
    read_file(lookup.read_offset, buf, lookup.want);
    ret = gpk_lookup_next(&font, &lookup, buf, lookup.want);
}

if(ret == GPK_FOUND)
{
    /* Glyph payload: lookup.offset, lookup.length */
}
```

The example assumes successful storage reads. Production code should handle I/O errors.

## Generator API

Enable with `GPK_GENERATOR=1`.

```c
int gpk_generate_header(
    uint8_t *buf, size_t bufsize,
    uint8_t height,
    const struct gpk_write_glyph *glyphs,
    uint8_t count);
```

Generates the complete GPK header and glyph metadata table.

Each `gpk_write_glyph` specifies an ID, width, and payload length. Payload offsets are calculated automatically.

Returns the metadata size in bytes, or `-1` on error.

The caller must append the glyph payloads immediately after the generated metadata, in the same order as the supplied array.

## File format

All multibyte integers use big-endian byte order.

| Section | Size | Contents |
|---|---|---|
| Header | 8 bytes | `gpk1` magic, count, height, 2 reserved bytes |
| Glyph table | 8 bytes × count | Glyph metadata entries |
| Payloads | Variable | Consecutive glyph blobs |

Each metadata entry contains:

| Field | Size |
|---|---|
| Glyph ID | 1 byte |
| Width | 1 byte |
| Payload length | 2 bytes |
| Payload offset | 4 bytes |

Offsets are relative to the beginning of the GPK container. Glyph entries need not be sorted, although ASCII ordering is recommended for font generation.

## Included utility

The repository includes `glyphpk.c`, a desktop utility for packaging QLI glyph images into GPK files.

It uses the [qli](https://github.com/gega/qli) submodule and expects glyph files named `glyph-XX.qli`, where `XX` is the two-digit hexadecimal glyph ID.

The resulting GPK file contains the glyph metadata followed by the complete QLI image blobs in ascending glyph-ID order.

The utility is an example implementation of the generator. The library itself is independent of QLI and can store any binary glyph representation.

## License

BSD 3-Clause License.
