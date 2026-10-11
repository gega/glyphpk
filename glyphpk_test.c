#include <stdio.h>
#include <string.h>

#define GPK_CACHE_SIZE 2
#define GPK_LOOKUP_CHUNK_SIZE 5
#define GPK_GENERATOR 1
#define GPK_IMPLEMENTATION
#include "glyphpk.h"

static int fails;
#define CHECK(x) do { if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); fails++; } } while(0)

int main(void)
{
  static const struct gpk_write_glyph g[] = {
    { '0', 9, 11 },
    { '1', 7, 13 },
    { ':', 3, 5  },
  };
  uint8_t metadata[GPK_METADATA_LEN(3)];
  struct gpk_font font;
  struct gpk_lookup l;
  int n, s;
  size_t p;

  n = gpk_generate_header(metadata, sizeof(metadata), 16, g, 3);
  CHECK(n == (int)sizeof(metadata));
  CHECK(0 == memcmp(metadata, "gpk1", 4));
  CHECK(metadata[4] == 3);
  CHECK(metadata[5] == 16);

  CHECK(gpk_init(&font, metadata, GPK_HEADER_LEN) == 0);
  CHECK(font.count == 3);
  CHECK(font.height == 16);

  CHECK(gpk_lookup_init(&font, ':', &l) == 0);
  s = gpk_lookup_next(&font, &l, NULL, 0);
  CHECK(s == GPK_MORE);
  CHECK(l.read_offset == GPK_HEADER_LEN);

  p = GPK_HEADER_LEN;
  while(s == GPK_MORE)
  {
    size_t r = l.want;
    if(r > sizeof(metadata) - p) r = sizeof(metadata) - p;
    CHECK(r > 0);
    s = gpk_lookup_next(&font, &l, &metadata[p], r);
    p += r;
  }

  CHECK(s == GPK_FOUND);
  CHECK(l.width == 3);
  CHECK(l.length == 5);
  CHECK(l.offset == GPK_METADATA_LEN(3) + 11 + 13);

  /* ':' should now be a cache hit and require no table I/O. */
  CHECK(gpk_lookup_init(&font, ':', &l) == 0);
  CHECK(gpk_lookup_next(&font, &l, NULL, 0) == GPK_FOUND);
  CHECK(l.width == 3 && l.length == 5);

  CHECK(gpk_lookup_init(&font, 'X', &l) == 0);
  s = gpk_lookup_next(&font, &l, NULL, 0);
  p = GPK_HEADER_LEN;
  while(s == GPK_MORE)
  {
    size_t r = l.want;
    if(r > sizeof(metadata) - p) r = sizeof(metadata) - p;
    CHECK(r > 0);
    s = gpk_lookup_next(&font, &l, &metadata[p], r);
    p += r;
  }
  CHECK(s == GPK_NOT_FOUND);

  if(fails) {
    fprintf(stderr, "gpk_test: %d failure(s)\n", fails);
    return 1;
  }
  puts("gpk_test: PASS");
  return 0;
}
