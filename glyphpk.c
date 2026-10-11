/* glyph packer
 * Build: gcc glyphpk.c -o glyphpk
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GPK_GENERATOR 1
#define GPK_IMPLEMENTATION
#include "glyphpk.h"

#define QLI_DECODE
#define QLI_PIXEL_FORMAT QLI_PF_RGB444
#define QLI_ENDIAN QLI_BIG_ENDIAN
#define QLI_POSTFIX _q4
#define QLI_IMPLEMENTATION
#include "qli/qli.h"

#define QLI_PIXEL_FORMAT QLI_PF_RGB565
#define QLI_ENDIAN QLI_LITTLE_ENDIAN
#define QLI_POSTFIX _q5
#include "qli/qli.h"

#define QLI_PIXEL_FORMAT QLI_PF_RGB888
#define QLI_ENDIAN QLI_BIG_ENDIAN
#define QLI_POSTFIX _q8
#include "qli/qli.h"

#define MAX_GLYPHS (256)

struct input_glyph
{
  uint8_t *data;
  uint16_t length;
  uint8_t width;
};
static struct input_glyph inputs[MAX_GLYPHS];

static void fail(const char *message, const char *path)
{
  fprintf(stderr, "glyphpk: %s%s%s\n", message, path ? ": " : "", path ? path : "");
  exit(EXIT_FAILURE);
}

static int hexval(int c)
{
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return(-1);
}

static void dimensions(uint8_t *header, uint8_t *width, uint8_t *height, const char *path)
{
  qli_image_t_q4 q4;
  qli_image_t_q5 q5;
  qli_image_t_q8 q8;
  unsigned w=0, h=0;
  int st;

  /* Byte 9 contains index-size bits; no glyph extensions are accepted. */
  if(header[9] & ~3u) fail("expected plain qli1 without glyphs or extensions", path);
  st=qli_init_header_q4(&q4, header, 0);
  if(0==st) { w=q4.width; h=q4.height; }
  st=qli_init_header_q5(&q5, header, 0);
  if(0==st) { w=q5.width; h=q5.height; }
  st=qli_init_header_q8(&q8, header, 0);
  if(0==st) { w=q8.width; h=q8.height; }
  if(w==0||h==0) fail("invalid qli header", path);
  if(w>255||h>255) fail("glyph dimensions must be within 1..255", path);
  *width = (uint8_t)w;
  *height = (uint8_t)h;
}

int main(int argc, char **argv)
{
  DIR *dir;
  struct dirent *ent;
  unsigned count = 0, id, index = 0;
  uint8_t height = 0, *header;
  struct gpk_write_glyph *glyphs;
  FILE *out;
  int headerlen;

  if(argc<=1||argc>3)
  {
    printf("Usage: %s DIRECTORY [OUTPUT|-]\n", argv[0]);
    return(argc==1?0:1);
  }
  if(NULL!=(dir=opendir(argv[1])))
  {
    for(;;)
    {
      int a, b;
      size_t pathlen;
      char *path;
      FILE *in;
      struct stat st;
      uint8_t w, h;

      errno = 0;
      ent = readdir(dir);
      if(!ent)
      {
        if(errno) fail("cannot read directory", argv[1]);
        break;
      }
      if(strlen(ent->d_name) != 12 || strncmp(ent->d_name, "glyph-", 6) || strcmp(ent->d_name + 8, ".qli")) continue;
      a = hexval(ent->d_name[6]);
      b = hexval(ent->d_name[7]);
      if(a<0 || b<0) continue;
      id = (unsigned)(a * 16 + b);
      if(inputs[id].data) fail("duplicate glyph ID", ent->d_name);
      if(count>=(MAX_GLYPHS-1)) fail("too many glyphs in font", NULL);
      pathlen = strlen(argv[1]) + 1 + strlen(ent->d_name) + 1;
      path = malloc(pathlen);
      snprintf(path, pathlen, "%s/%s", argv[1], ent->d_name);
      if(NULL!=(in=fopen(path, "rb")))
      {
        if(fstat(fileno(in), &st) || !S_ISREG(st.st_mode)) fail("glyph must be a regular file", path);
        if(st.st_size <= QLI_HEADER_LEN || st.st_size > UINT16_MAX)fail("QLI file size must be within 11..65535 bytes", path);
        inputs[id].length = (uint16_t)st.st_size;
        inputs[id].data = malloc(inputs[id].length);
        if(fread(inputs[id].data, 1, inputs[id].length, in)!=inputs[id].length) fail("cannot read complete glyph", path);
        fclose(in);
      }
      else fail("cannot open glyph", path);
      dimensions(inputs[id].data, &w, &h, path);
      if(height && height!=h) fail("all glyphs must have the same height", path);
      height=h;
      inputs[id].width=w;
      count++;
      free(path);
    }
    closedir(dir);
  }
  else fail("cannot open directory", argv[1]);
  if(count == 0) fail("no glyph-XX.qli files found", argv[1]);

  glyphs = malloc(count * sizeof(*glyphs));
  for(id=0; id<MAX_GLYPHS; id++)
  {
    if(inputs[id].data)
    {
      glyphs[index].id = (uint8_t)id;
      glyphs[index].width = inputs[id].width;
      glyphs[index++].length = inputs[id].length;
    }
  }
  header = malloc(GPK_METADATA_LEN(count));
  headerlen = gpk_generate_header(header, GPK_METADATA_LEN(count), height, glyphs, (uint8_t)count);
  if(headerlen<0) fail("cannot generate font header", NULL);
  free(glyphs);

  /* Load every input before opening output, which may name an input file. */
  out = ( argc == 2 || !strcmp(argv[2], "-") ? stdout : fopen(argv[2], "wb") );
  if(!out) fail("cannot open output", argv[2]);
  if(fwrite(header, 1, (size_t)headerlen, out) != (size_t)headerlen)fail("cannot write font header", NULL);
  free(header);
  for(id=0; id<MAX_GLYPHS; id++)
  {
    if(inputs[id].data)
    {
      if(fwrite(inputs[id].data, 1, inputs[id].length, out) != inputs[id].length)fail("cannot write glyph", NULL);
      free(inputs[id].data);
    }
  }
  if(fflush(out)==EOF) fail("cannot flush output", NULL);
  if(out!=stdout && fclose(out)==EOF) fail("cannot close output", NULL);
  return(0);
}
