/* glyphpk - small glyph container
 *
 * BSD 3-Clause License
 *
 * glyphpk stores a fixed-height set of glyphs. Each glyph payload is a blob
 * glyphpk only provides glyph metadata and random-access offsets.
 *
 * Define GPK_IMPLEMENTATION in exactly one translation unit to include the
 * implementation.
 */
#ifndef GPK_H
#define GPK_H

#include <stddef.h>
#include <stdint.h>

#define GPK_MAGIC0 ('g')
#define GPK_MAGIC1 ('p')
#define GPK_MAGIC2 ('k')
#define GPK_MAGIC3 ('1')

#define GPK_HEADER_LEN 8u
#define GPK_ENTRY_LEN  8u
#define GPK_METADATA_LEN(count) (GPK_HEADER_LEN + ((size_t)(count) * GPK_ENTRY_LEN))

#ifndef GPK_CACHE_SIZE
#define GPK_CACHE_SIZE 8
#endif

#ifndef GPK_LOOKUP_CHUNK_SIZE
#define GPK_LOOKUP_CHUNK_SIZE 64u
#endif

#define GPK_ERROR     (-1)
#define GPK_MORE      (1)
#define GPK_FOUND     (2)
#define GPK_NOT_FOUND (3)

struct gpk_glyph
{
  uint8_t id;
  uint8_t width;
  uint16_t length;
  uint32_t offset; /* relative to the beginning of the GPK blob */
};

#if GPK_CACHE_SIZE > 0
struct gpk_cache_entry
{
  uint8_t valid;
  struct gpk_glyph glyph;
};
#endif

struct gpk_font
{
  uint8_t count;
  uint8_t height;
#if GPK_CACHE_SIZE > 0
  uint8_t cache_next;
  struct gpk_cache_entry cache[GPK_CACHE_SIZE];
#endif
};

struct gpk_lookup
{
  uint8_t glyph_id;
  uint8_t entries_left;
  uint8_t rem_len;
  uint8_t started;
  uint8_t rem[GPK_ENTRY_LEN];

  /* Valid when gpk_lookup_next() returns GPK_FOUND. */
  uint8_t width;
  uint16_t length;
  uint32_t offset;

  /* Caller-facing streaming hints. */
  uint16_t want;
  uint32_t read_offset; /* relative to the beginning of the GPK blob */
};

/* Writer-side description. The writer should append glyph blobs in exactly
 * this order immediately after the generated metadata.
 */
struct gpk_write_glyph
{
  uint8_t id;
  uint8_t width;
  uint16_t length;
};

/* Decode the fixed GPK header. buf must contain at least GPK_HEADER_LEN bytes. */
int gpk_init(struct gpk_font *font, const uint8_t *buf, size_t size);

/* Reset one streaming glyph lookup. No I/O occurs here. */
int gpk_lookup_init(const struct gpk_font *font, uint8_t glyph_id, struct gpk_lookup *lookup);

/* Continue a glyph lookup.
 *
 * The first call may be gpk_lookup_next(font, lookup, NULL, 0). That checks
 * the metadata cache first. On a miss it returns GPK_MORE and sets:
 *
 *   lookup->read_offset  offset at which the caller should read
 *   lookup->want         suggested number of bytes to read
 *
 * Subsequent calls may supply any number of bytes. Entry boundaries do not
 * need to match buffer boundaries.
 */
int gpk_lookup_next(struct gpk_font *font, struct gpk_lookup *lookup, const uint8_t *buf, size_t size);

/* Generate the complete fixed header + glyph metadata table.
 *
 * Offsets are generated assuming that glyph blobs are appended immediately
 * after the metadata, in glyphs[] order. Returns the number of metadata bytes
 * generated, or -1 on error.
 */
int gpk_generate_header(uint8_t *buf, size_t bufsize, uint8_t height, const struct gpk_write_glyph *glyphs, uint8_t count);

#endif /* GPK_H */

#ifdef GPK_IMPLEMENTATION

#include <string.h>

static uint16_t gpk_get_be16(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t gpk_get_be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8)  |
          (uint32_t)p[3];
}

static void gpk_put_be16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xffu);
}

static void gpk_put_be32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)((v >> 16) & 0xffu);
  p[2] = (uint8_t)((v >> 8) & 0xffu);
  p[3] = (uint8_t)(v & 0xffu);
}

static void gpk_parse_entry(struct gpk_glyph *g, const uint8_t *p)
{
  g->id     = p[0];
  g->width  = p[1];
  g->length = gpk_get_be16(&p[2]);
  g->offset = gpk_get_be32(&p[4]);
}

#if GPK_CACHE_SIZE > 0
static int gpk_cache_find(const struct gpk_font *font, uint8_t id,
                          struct gpk_glyph *glyph)
{
  int i;
  for(i = 0; i < GPK_CACHE_SIZE; i++)
  {
    if(font->cache[i].valid && font->cache[i].glyph.id == id)
    {
      if(glyph != NULL) *glyph = font->cache[i].glyph;
      return 1;
    }
  }
  return 0;
}

static void gpk_cache_put(struct gpk_font *font, const struct gpk_glyph *glyph)
{
  int i;

  /* Update an existing entry instead of creating duplicates. */
  for(i = 0; i < GPK_CACHE_SIZE; i++)
  {
    if(font->cache[i].valid && font->cache[i].glyph.id == glyph->id)
    {
      font->cache[i].glyph = *glyph;
      return;
    }
  }

  font->cache[font->cache_next].valid = 1;
  font->cache[font->cache_next].glyph = *glyph;
  font->cache_next++;
  if(font->cache_next >= GPK_CACHE_SIZE) font->cache_next = 0;
}
#endif

static uint16_t gpk_lookup_want(const struct gpk_lookup *lookup)
{
  size_t remaining;
  size_t want;

  if(lookup->entries_left == 0) return 0;

  remaining = ((size_t)lookup->entries_left * GPK_ENTRY_LEN) - lookup->rem_len;
  want = remaining;
  if(want > GPK_LOOKUP_CHUNK_SIZE) want = GPK_LOOKUP_CHUNK_SIZE;
  if(want > UINT16_MAX) want = UINT16_MAX;
  return (uint16_t)want;
}

int gpk_init(struct gpk_font *font, const uint8_t *buf, size_t size)
{
  if(font == NULL || buf == NULL || size < GPK_HEADER_LEN) return -1;

  if(buf[0] != GPK_MAGIC0 || buf[1] != GPK_MAGIC1 ||
     buf[2] != GPK_MAGIC2 || buf[3] != GPK_MAGIC3) return -1;

  if(buf[4] == 0 || buf[5] == 0) return -1;
  if(buf[6] != 0 || buf[7] != 0) return -1; /* reserved in gpk1 */

  memset(font, 0, sizeof(*font));
  font->count  = buf[4];
  font->height = buf[5];
  return 0;
}

int gpk_lookup_init(const struct gpk_font *font, uint8_t glyph_id,
                    struct gpk_lookup *lookup)
{
  if(font == NULL || lookup == NULL || font->count == 0 || font->height == 0)
    return -1;

  memset(lookup, 0, sizeof(*lookup));
  lookup->glyph_id = glyph_id;
  lookup->entries_left = font->count;
  lookup->read_offset = GPK_HEADER_LEN;
  return 0;
}

int gpk_lookup_next(struct gpk_font *font, struct gpk_lookup *lookup,
                    const uint8_t *buf, size_t size)
{
  size_t pos = 0;
  struct gpk_glyph glyph;

  if(font == NULL || lookup == NULL) return GPK_ERROR;
  if(size != 0 && buf == NULL) return GPK_ERROR;

  if(!lookup->started)
  {
#if GPK_CACHE_SIZE > 0
    if(gpk_cache_find(font, lookup->glyph_id, &glyph))
    {
      lookup->width  = glyph.width;
      lookup->length = glyph.length;
      lookup->offset = glyph.offset;
      lookup->want = 0;
      return GPK_FOUND;
    }
#endif
    lookup->started = 1;
    if(lookup->entries_left == 0) return GPK_NOT_FOUND;

    if(size == 0)
    {
      lookup->want = gpk_lookup_want(lookup);
      return GPK_MORE;
    }
  }

  while(pos < size && lookup->entries_left > 0)
  {
    size_t need = GPK_ENTRY_LEN - lookup->rem_len;
    size_t take = size - pos;
    if(take > need) take = need;

    memcpy(&lookup->rem[lookup->rem_len], &buf[pos], take);
    lookup->rem_len = (uint8_t)(lookup->rem_len + take);
    lookup->read_offset += (uint32_t)take;
    pos += take;

    if(lookup->rem_len == GPK_ENTRY_LEN)
    {
      gpk_parse_entry(&glyph, lookup->rem);
      lookup->rem_len = 0;
      lookup->entries_left--;

      if(glyph.width == 0 || glyph.length == 0 || glyph.offset < GPK_HEADER_LEN)
        return GPK_ERROR;

#if GPK_CACHE_SIZE > 0
      gpk_cache_put(font, &glyph);
#endif

      if(glyph.id == lookup->glyph_id)
      {
        lookup->width  = glyph.width;
        lookup->length = glyph.length;
        lookup->offset = glyph.offset;
        lookup->want = 0;
        return GPK_FOUND;
      }
    }
  }

  if(lookup->entries_left == 0)
  {
    lookup->want = 0;
    return GPK_NOT_FOUND;
  }

  lookup->want = gpk_lookup_want(lookup);
  return GPK_MORE;
}

int gpk_generate_header(uint8_t *buf, size_t bufsize, uint8_t height, const struct gpk_write_glyph *glyphs, uint8_t count)
{
  size_t metadata_len;
  uint32_t offset;
  size_t pos;
  int i, j;

  if(buf == NULL || glyphs == NULL || count == 0 || height == 0) return -1;

  metadata_len = GPK_METADATA_LEN(count);
  if(bufsize < metadata_len || metadata_len > UINT32_MAX) return -1;

  /* Keep IDs unique: lookup semantics require at most one result per ID. */
  for(i = 0; i < count; i++)
  {
    if(glyphs[i].width == 0 || glyphs[i].length == 0) return -1;
    for(j = i + 1; j < count; j++)
      if(glyphs[i].id == glyphs[j].id) return -1;
  }

  buf[0] = GPK_MAGIC0;
  buf[1] = GPK_MAGIC1;
  buf[2] = GPK_MAGIC2;
  buf[3] = GPK_MAGIC3;
  buf[4] = count;
  buf[5] = height;
  buf[6] = 0;
  buf[7] = 0;

  offset = (uint32_t)metadata_len;
  pos = GPK_HEADER_LEN;

  for(i = 0; i < count; i++)
  {
    if(UINT32_MAX - offset < glyphs[i].length) return -1;

    buf[pos++] = glyphs[i].id;
    buf[pos++] = glyphs[i].width;
    gpk_put_be16(&buf[pos], glyphs[i].length);
    pos += 2;
    gpk_put_be32(&buf[pos], offset);
    pos += 4;

    offset += glyphs[i].length;
  }

  return (int)metadata_len;
}

#endif
