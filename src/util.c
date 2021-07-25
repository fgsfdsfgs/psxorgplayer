#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <psxgpu.h>
#include <malloc.h>
#include <string.h>

#include "cd.h"
#include "spu.h"
#include "util.h"

static char errmsg[512];

static void __attribute__((noreturn)) fatal(void) {
  DISPENV disp;
  DRAWENV draw;
  SetDefDispEnv(&disp, 0, 0, 320, 240);
  SetDefDrawEnv(&draw, 0, 0, 320, 240);
  setRGB0(&draw, 0x40, 0x00, 0x00);
  draw.isbg = 1;
  PutDispEnv(&disp);
  PutDrawEnv(&draw);
  while (1) {
    FntPrint(-1, "FATAL ERROR:\n%s", errmsg);
    FntFlush(-1);
    DrawSync(0);
    VSync(0);
  }
}

// while our libc has a declaration for assert(), it does not actually provide it

void do_assert(const int expr, const char *strexpr, const char *file, const int line) {
  if (!expr) {
    snprintf(errmsg, sizeof(errmsg), "ASSERTION FAILED:\n`%s` at %s:%d", strexpr, file, line);
    printf("%s\n", errmsg);
    fatal();
  }
}

void panic(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(errmsg, sizeof(errmsg), fmt, args);
  va_end(args);
  printf("FATAL ERROR: %s\n", errmsg);
  fatal();
}

struct sfx_bank *load_sfx_bank(const char *fname) {
  cd_file_t *f = cd_fopen(fname, 0);
  if (!f) panic("could not open bank file '%s'", fname);

  printf("loading bank '%s' at addr %u\n", fname, spuram_ptr);

  const u32 buflen = cd_fread_u32le(f);
  const u32 num_sfx = cd_fread_u32le(f);

  struct sfx_bank *bank = malloc(sizeof(*bank) + sizeof(u32) * num_sfx);
  ASSERT(bank);
  bank->data_len = buflen;
  bank->num_sfx = num_sfx;
  cd_freadordie(&bank->sfx_addr[0], sizeof(u32) * num_sfx, 1, f);

  u8 *buf = malloc(buflen);
  ASSERT(buf);
  cd_freadordie(buf, buflen, 1, f);

  cd_fclose(f);

  ASSERT(spuram_ptr == bank->sfx_addr[0] || spuram_ptr == bank->sfx_addr[1]);

  SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
  spu_set_transfer_addr(spuram_ptr);
  SpuWrite((void *)buf, buflen);
  spu_wait_for_transfer();

  spuram_ptr += buflen;

  printf("bank '%s': read %u bytes of sample data (%u samples), spuram_ptr=%u\n", fname, buflen, num_sfx, spuram_ptr);
  printf("bank ident: %02x %02x %02x %02x\n", buf[0], buf[1], buf[2], buf[3]);
  /*
  for (u32 i = 0; i < bank->num_sfx; ++i)
    printf("* (%03d) 0x%06x\n", i, bank->sfx_addr[i]);
  */

  free(buf);

  return bank;
}

int free_sfx_bank(struct sfx_bank *bank) {
  const u32 prevaddr = spuram_ptr - bank->data_len;
  if (prevaddr == bank->sfx_addr[0] || prevaddr == bank->sfx_addr[1])
    spuram_ptr = prevaddr; // free SPU RAM if this is the last loaded bank
  free(bank);
}
