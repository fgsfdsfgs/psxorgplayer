#pragma once

#include <stdint.h>

#define NUM_INST 100
#define INST_LEN 256
#define MAX_SFX  160

#define SPURAM_ALIGN 8
#define SPURAM_START 0x1100
#define SPURAM_SIZE  (512 * 1024)
#define SPURAM_AVAIL (SPURAM_SIZE - SPURAM_START)

#define ALIGN(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#pragma pack(push, 1)

struct bank_hdr {
  uint32_t data_size;   // size of raw SPU data at the end
  uint32_t num_sfx;     // number of samples in bank, including #0 (dummy) and all the unused samples
  uint32_t sfx_addr[1]; // address in SPU RAM of each sample, first one is always 0, others may be 0 (means it's unused)
  // after the last sfx_addr, raw SPU data follows
};

struct sfx {
  int16_t *data;
  uint32_t len; // in samples
  uint32_t freq;
  uint32_t addr;
};

#pragma pack(pop)
