#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "types.h"
#include "util.h"
#include "spu.h"
#include "org.h"
#include "cd.h"

#define ORG_MAGIC "Org-0"
#define ORG_MAGICLEN 5

#define MAX_TRACKS ORG_MAX_TRACKS
#define MAX_MELODY_TRACKS 8
#define MAX_DRUM_TRACKS 8

#define NUM_OCTS 8
#define NUM_ALTS 2

#define DRUM_BANK_BASE 150

#define PANDUMMY 0xFF
#define VOLDUMMY 0xFF
#define KEYDUMMY 0xFF

#define ALLOCNOTE 4096

#define DEFVOLUME 200
#define DEFPAN    6

#pragma pack(push, 1)

typedef struct {
  u16 freq;     // frequency modifier (default = 1000)
  u8 wave_no;   // waveform index in the wavetable
  u8 pipi;      // loop flag?
  u16 note_num; // number of notes in track
} org_trackhdr_t;

typedef struct {
  u16 wait;
  u8 line;
  u8 dot;
  s32 repeat_x;
  s32 end_x;
  org_trackhdr_t tdata[MAX_TRACKS];
} org_hdr_t;

#pragma pack(pop)

typedef struct {
  org_note_t *notes;
  org_note_t *cur_note;
  s32 vol;
  u32 sustain;
  s8 mute;
  u8 old_key;
} org_trackstate_t;

typedef struct {
  org_hdr_t info;
  org_trackstate_t tracks[MAX_TRACKS];
  s32 vol;
  s32 pos;
  u8 fadeout;
  s8 track;
  u8 def_pan;
  u8 def_vol;
} org_state_t;

static u32 key_on_mask; // all the keys that got keyed on this tick
static u32 key_off_mask; // all the keys that got keyed off this tick
static org_state_t org;
static struct sfx_bank *inst_bank;
static struct sfx_bank *drum_bank;

s32 org_freqshift = 0;

static const struct {
  s16 wave_size;
  s16 oct_par;
  s16 oct_size;
} oct_wave[NUM_OCTS] = {
  { 256,   1,  4 }, // 0 Oct
  { 256,   2,  8 }, // 1 Oct
  { 128,   4, 12 }, // 2 Oct
  { 128,   8, 16 }, // 3 Oct
  {  64,  16, 20 }, // 4 Oct
  {  32,  32, 24 }, // 5 Oct
  {  16,  64, 28 }, // 6 Oct
  {   8, 128, 32 }, // 7 Oct
};

static const s16 freq_tbl[12] = { 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494 };
static const s16 pan_tbl[13] = { 0, 43, 86, 129, 172, 215, 256, 297, 340, 383, 426, 469, 512 };

static void org_read_track(cd_file_t *f, const int track) {
  const org_trackhdr_t *hdr = &org.info.tdata[track];
  org_trackstate_t *dst = &org.tracks[track];
  // hope there's enough stack space
  u8 notedata[sizeof(s32) * hdr->note_num];
  // read positions ("x coordinate")
  cd_freadordie(notedata, sizeof(s32) * hdr->note_num, 1, f);
  for (u16 i = 0; i < hdr->note_num; ++i) dst->notes[i].pos = ((s32 *)notedata)[i];
  // read keys ("y coordinate")
  cd_freadordie(notedata, hdr->note_num, 1, f);
  for (u16 i = 0; i < hdr->note_num; ++i) dst->notes[i].key = notedata[i];
  // read lengths
  cd_freadordie(notedata, hdr->note_num, 1, f);
  for (u16 i = 0; i < hdr->note_num; ++i) dst->notes[i].len = notedata[i];
  // read volumes
  cd_freadordie(notedata, hdr->note_num, 1, f);
  for (u16 i = 0; i < hdr->note_num; ++i) dst->notes[i].vol = notedata[i];
  // read pans
  cd_freadordie(notedata, hdr->note_num, 1, f);
  for (u16 i = 0; i < hdr->note_num; ++i) dst->notes[i].pan = notedata[i];
}

void org_init(struct sfx_bank *sample_bank) {
  org.info.dot = 4;
  org.info.line = 4;
  org.info.wait = 128;
  org.info.repeat_x = 0;
  org.info.end_x = org.info.line * 255;
  org.def_pan = DEFPAN;
  org.def_vol = DEFVOLUME;
  for (int i = 0; i < MAX_TRACKS; ++i) {
    org.info.tdata[i].freq = 1000;
    org.info.tdata[i].wave_no = 0;
    org.info.tdata[i].pipi = 0;
  }
  drum_bank = sample_bank;
}

int org_load(const char *name) {
  char tmp[256];
  cd_file_t *f = NULL;

  snprintf(tmp, sizeof(tmp), "\\BNK\\%s.BNK;1", name);
  inst_bank = load_sfx_bank(tmp);
  if (!inst_bank) goto _error;
  if (inst_bank->num_sfx != MAX_MELODY_TRACKS * NUM_OCTS) {
    printf("org_load(%s): expected %d instruments in bank, got %d\n",
      name, MAX_MELODY_TRACKS * NUM_OCTS, inst_bank->num_sfx);
    goto _error;
  }

  snprintf(tmp, sizeof(tmp), "\\ORG\\%s.ORG;1", name);
  f = cd_fopen(tmp, 0);
  if (!f) goto _error;

  char magic[ORG_MAGICLEN + 1] = { 0 }; // +1 for version
  cd_freadordie(magic, ORG_MAGICLEN + 1, 1, f);

  if (memcmp(magic, ORG_MAGIC, ORG_MAGICLEN)) {
    printf("org_load(%s): invalid Org magic\n", name);
    goto _error;
  }

  const int ver = magic[ORG_MAGICLEN] - '0';
  if (ver != 1 && ver != 2) {
    printf("org_load(%s): expected version 1 or 2, got %d\n", name, ver);
    goto _error;
  }

  cd_freadordie(&org.info, sizeof(org.info), 1, f);

  for (int i = 0; i < MAX_TRACKS; ++i) {
    if (ver == 1)
      org.info.tdata[i].pipi = 0;
    if (org.info.tdata[i].note_num) {
      org.tracks[i].notes = malloc(org.info.tdata[i].note_num * sizeof(org_note_t));
      ASSERT(org.tracks[i].notes);
      org_read_track(f, i);
    } else {
      org.tracks[i].notes = NULL;
    }
  }

  cd_fclose(f);

  // dump eet
  /*
  for (int i = 0; i < MAX_MELODY_TRACKS; ++i) {
    printf("\nTRACK %02d\n", i);
    for (u32 j = 0; j < org.info.tdata[i].note_num; ++j) {
      const org_note_t *n = org.tracks[i].notes + j;
      printf("%04d | %02x %02x %02x %02x\n", j, n->key, n->len, n->vol, n->pan);
    }
  }
  */

  org.vol = 100;

  org_restart_from(0);

  return 1;

_error:
  if (f) cd_fclose(f);
  org_free();
  return 0;
}

void org_free(void) {
  if (inst_bank) {
    free_sfx_bank(inst_bank);
    inst_bank = NULL;
  }
  for (int i = 0; i < MAX_TRACKS; ++i) {
    if (org.tracks[i].notes) {
      free(org.tracks[i].notes);
      org.tracks[i].notes = NULL;
    }
  }
}

void org_restart_from(const s32 pos) {
  org.pos = pos;
  for (int i = 0; i < MAX_TRACKS; ++i) {
    org.tracks[i].cur_note = NULL;
    for (int j = 0; j < org.info.tdata[i].note_num; ++j) {
      if (org.tracks[i].notes[j].pos >= pos) {
        org.tracks[i].cur_note = &org.tracks[i].notes[j];
        break;
      }
    }
  }
}

static inline void org_play_melodic(const int trk, int key, int freq, int mode) {
  const u32 ch = ORG_START_CH + trk;
  const int oct = key / 12;
  const int inst = trk * NUM_OCTS + oct;
  switch (mode) {
    case 0: // also stop?
    case 2: // stop
      if (org.tracks[trk].old_key != KEYDUMMY) {
        key_off_mask |= SPU_VOICECH(ch);
        org.tracks[trk].old_key = KEYDUMMY;
      }
      break;
    case -1: // key on?
      org.tracks[trk].old_key = key;
      freq = ((oct_wave[oct].wave_size * freq_tbl[key % 12]) * oct_wave[oct].oct_par) / 8 + (freq - 1000);
      spu_set_voice_addr(ch, inst_bank->sfx_addr[inst]);
      spu_set_voice_freq(ch, freq + org_freqshift);
      key_on_mask |= SPU_VOICECH(ch);
      break;
    default:
      break;
  }
}

static inline void org_play_drum(const int trk, int key, int mode) {
  const u32 ch = ORG_START_CH + trk;
  const int inst = trk - MAX_MELODY_TRACKS + DRUM_BANK_BASE;
  switch (mode) {
    case 0: // stop
      key_off_mask |= SPU_VOICECH(ch);
      break;
    case 1: // play
      spu_set_voice_addr(ch, drum_bank->sfx_addr[inst]);
      spu_set_voice_freq(ch, key * 800 + 100);
      key_on_mask |= SPU_VOICECH(ch);
      break;
    default:
      break;
  }
}

static inline void org_set_vol(const int trk, int vol) {
  spu_set_voice_volume(ORG_START_CH + trk, vol << 5);
}

static inline void org_set_pan(const int trk, int pan) {
  spu_set_voice_pan(ORG_START_CH + trk, pan_tbl[pan] - 256);
}

void org_tick(void) {
  if (org.fadeout && org.vol)
    org.vol -= 2;
  if (org.vol < 0)
    org.vol = 0;

  key_off_mask = 0;
  key_on_mask = 0;

  // waves
  for (int i = 0; i < MAX_MELODY_TRACKS; ++i) {
    const org_note_t *note = org.tracks[i].cur_note;
    if (note && org.pos == note->pos) {
      if (!org.tracks[i].mute && note->key != KEYDUMMY) {
        org_play_melodic(i, note->key, org.info.tdata[i].freq, -1);
        org.tracks[i].sustain = note->len;
      }
      if (note->pan != PANDUMMY)
        org_set_pan(i, note->pan);
      if (note->vol != VOLDUMMY)
        org.tracks[i].vol = note->vol;
      ++org.tracks[i].cur_note;
      if (org.tracks[i].cur_note >= org.tracks[i].notes + org.info.tdata[i].note_num)
        org.tracks[i].cur_note = NULL;
    }

    if (org.tracks[i].sustain == 0)
      org_play_melodic(i, 0, org.info.tdata[i].freq, 2);
    else
      --org.tracks[i].sustain;

    if (org.tracks[i].cur_note)
      org_set_vol(i, org.tracks[i].vol * org.vol / 0x7F);
  }

  // drums
  for (int i = MAX_MELODY_TRACKS; i < MAX_TRACKS; ++i) {
    const org_note_t *note = org.tracks[i].cur_note;
    if (note && org.pos == note->pos) {
      if (!org.tracks[i].mute && note->key != KEYDUMMY)
        org_play_drum(i, note->key, 1);
      if (note->pan != PANDUMMY)
        org_set_pan(i, note->pan);
      if (note->vol != VOLDUMMY)
        org.tracks[i].vol = note->vol;
      ++org.tracks[i].cur_note;
      if (org.tracks[i].cur_note >= org.tracks[i].notes + org.info.tdata[i].note_num)
        org.tracks[i].cur_note = NULL;
    }

    if (org.tracks[i].cur_note)
      org_set_vol(i, org.tracks[i].vol * org.vol / 0x7F);
  }

  spu_flush_voices();
  spu_key_off(key_off_mask);
  spu_key_on(key_on_mask);

  ++org.pos;
  if (org.pos >= org.info.end_x)
    org_restart_from(org.info.repeat_x);
}

int org_get_wait(void) {
  return org.info.wait;
}

int org_get_pos(void) {
  return org.pos;
}

u16 org_get_mute_mask(void) {
  register u16 mask = 0;
  for (u16 i = 0; i < MAX_TRACKS; ++i) {
    if (org.tracks[i].mute)
      mask |= (1 << i);
  }
  return mask;
}

u16 org_set_mute_mask(const u16 mask) {
  register u16 oldmask = 0;
  for (u16 i = 0; i < MAX_TRACKS; ++i) {
    if (org.tracks[i].mute)
      oldmask |= (1 << i);
    org.tracks[i].mute = !!(mask & (1 << i));
  }
  spu_key_off((u32)mask << ORG_START_CH);
  return oldmask;
}

org_note_t *org_get_track(const int tracknum, u32 *numnotes) {
  if (numnotes)
    *numnotes = org.info.tdata[tracknum].note_num;
  return org.tracks[tracknum].notes;
}

org_note_t *org_get_track_pos(const int tracknum) {
  return org.tracks[tracknum].cur_note;
}
