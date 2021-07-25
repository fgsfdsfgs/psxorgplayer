#pragma once

#include "types.h"
#include "util.h"

#define ORG_START_CH 8
#define ORG_MAX_TRACKS 16

typedef struct org_note {
  s32 pos;
  u8 len;
  u8 key;
  u8 vol;
  u8 pan;
} org_note_t;

extern s32 org_freqshift;

void org_init(struct sfx_bank *drum_bank);
int org_load(const char *name);
void org_free(void);
void org_restart_from(const s32 pos);
void org_tick(void);

int org_get_wait(void);
int org_get_pos(void);
u16 org_get_mute_mask(void);
u16 org_set_mute_mask(const u16 mask);

org_note_t *org_get_track(const int tracknum, u32 *numnotes);
org_note_t *org_get_track_pos(const int tracknum);
