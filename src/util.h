#pragma once

#include "types.h"

#define ALIGN(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ASSERT(x) do_assert((int)(x), #x, __FILE__, __LINE__)

void panic(const char *fmt, ...) __attribute__((noreturn));
void do_assert(const int, const char *, const char *, const int);

struct sfx_bank {
  u32 data_len;
  u32 num_sfx;
  u32 sfx_addr[]; // [num_sfx];
};

struct sfx_bank *load_sfx_bank(const char *fname);
int free_sfx_bank(struct sfx_bank *bank);
