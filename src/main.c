#include <psxetc.h>
#include <psxapi.h>
#include <psxspu.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "spu.h"
#include "cd.h"
#include "util.h"
#include "org.h"

#define MAX_MENU_FILES 128
#define MENU_DISP_FILES 20

static char padbuf[2][34];
static DISPENV disp[2];
static DRAWENV draw[2];
static int db;

static struct sfx_bank *bnk_sfx;

static u16 pad_btn = 0xFFFF;
static u16 pad_btn_old = 0xFFFF;

static void init(void) {
  ResetGraph(0);

  SetDefDispEnv(&disp[0], 0, 0, 320, 240);
  SetDefDispEnv(&disp[1], 0, 240, 320, 240);
  SetDefDrawEnv(&draw[0], 0, 240, 320, 240);
  SetDefDrawEnv(&draw[1], 0, 0, 320, 240);

  setRGB0(&draw[0], 0, 0, 32);
  setRGB0(&draw[1], 0, 0, 32);
  draw[0].isbg = 1;
  draw[1].isbg = 1;

  db = 0;

  PutDispEnv(&disp[db]);
  PutDrawEnv(&draw[db]);

  cd_init();
  spu_init();

  InitPAD(&padbuf[0][0], 34, &padbuf[1][0], 34);
  StartPAD();
  ChangeClearPAD(0);

  FntLoad(960, 0);
  FntOpen(0, 8, 320, 224, 0, 1024);
}

static void display(void) {
  db = !db;
  DrawSync(0);
  VSync(0);
  PutDispEnv(&disp[db]);
  PutDrawEnv(&draw[db]);
  SetDispMask(1);
}

static inline int btn_pressed(const u32 m) {
  return !(pad_btn & m) && (pad_btn_old & m);
}

static inline int btn_released(const u32 m) {
  return (pad_btn & m) && !(pad_btn_old & m);
}

static inline void btn_scan(void) {
  pad_btn_old = pad_btn;
  pad_btn = ((PADTYPE *)padbuf[0])->btn;
}

static void draw_tracks(void) {
  FntPrint(-1, " TRACKS\n\n");
  FntPrint(-1, " 000 001 002 003 004 005 006 007 008");

  org_note_t dummy = { 0 };
  org_note_t *n[ORG_MAX_TRACKS];
  for (int i = 0; i <= 8; ++i) {
    n[i] = org_get_track_pos(i);
    if (!n[i]) n[i] = &dummy;
  }

  FntPrint(-1, "\n\n ");
  for (int i = 0; i <= 8; ++i)
    FntPrint(-1, "%03x ", n[i]->pos);
  FntPrint(-1, "\n\n ");
  for (int i = 0; i <= 8; ++i)
    FntPrint(-1, "K%02x ", n[i]->key);
  FntPrint(-1, "\n\n ");
  for (int i = 0; i <= 8; ++i)
    FntPrint(-1, "L%02x ", n[i]->len);
  FntPrint(-1, "\n\n ");
  for (int i = 0; i <= 8; ++i)
    FntPrint(-1, "V%02x ", n[i]->vol);
  FntPrint(-1, "\n\n ");
  for (int i = 0; i <= 8; ++i)
    FntPrint(-1, "P%02x ", n[i]->pan);
}

static volatile u32 play_org = 0;

static void mus_callback(void) {
  if (play_org)
    org_tick();
}

static void timer_start(const u32 rate) {
  EnterCriticalSection();
  const u32 tick = 15625 * rate / 1000;
  SetRCnt(RCntCNT1, tick, RCntMdINTR);
  InterruptCallback(5, mus_callback); // IRQ5 is RCNT1
  StartRCnt(RCntCNT1);
  ChangeClearRCnt(1, 0);
  ExitCriticalSection();
}

static void timer_stop(void) {
  EnterCriticalSection();
  StopRCnt(RCntCNT1);
  ExitCriticalSection();
}

static void run_player(const char *orgname) {
  org_load(orgname);

  timer_start(org_get_wait());

  u32 sfx = 1;
  u16 mute_cur = 0;
  u16 mute_mask = 0;
  char mute_chans[17] = "................";

  spu_set_voice_volume(0, SPU_MAX_VOLUME);

  while (1) {
    btn_scan();

    if (btn_pressed(PAD_LEFT)) {
      if (sfx == 1) sfx = bnk_sfx->num_sfx - 1;
      else --sfx;
    } else if (btn_pressed(PAD_RIGHT)) {
      if (sfx == bnk_sfx->num_sfx - 1) sfx = 1;
      else ++sfx;
    }

    if (btn_pressed(PAD_UP)) {
      if (mute_cur == 0) mute_cur = 15;
      else --mute_cur;
    } else if (btn_pressed(PAD_DOWN)) {
      if (mute_cur == 15) mute_cur = 0;
      else ++mute_cur;
    }

    if (btn_pressed(PAD_CROSS) && bnk_sfx->sfx_addr[sfx])
      spu_play_sample(0, bnk_sfx->sfx_addr[sfx], 22050);
    else if (btn_released(PAD_CROSS))
      spu_key_off(SPU_VOICECH(0));

    if (btn_pressed(PAD_CIRCLE)) {
      play_org = !play_org;
      if (!play_org)
        spu_key_off(0xFFFFFFFF);
    }

    if (btn_pressed(PAD_TRIANGLE)) {
      mute_mask ^= (1 << mute_cur);
      mute_chans[mute_cur] = (mute_chans[mute_cur] == 'm') ? '.' : 'm';
      org_set_mute_mask(mute_mask);
    }

    if (btn_pressed(PAD_START))
      break;

    // HACK
    const char old = mute_chans[mute_cur];
    mute_chans[mute_cur] = (old == 'm') ? 'X' : ',';

    FntPrint(-1, "\n X, O: PLAY\n DPAD: CHANGE\n START: BACK\n\n");
    FntPrint(-1, " SFX: %03d / %03d\n\n", sfx, bnk_sfx->num_sfx - 1);
    FntPrint(-1, " ORG: %4d\n", org_get_pos());
    FntPrint(-1, " CHN: %s\n\n", mute_chans);
    draw_tracks();
    FntPrint(-1, "\n\n\n %s.ORG", orgname);
    FntFlush(-1);
    display();

    mute_chans[mute_cur] = old;
  }

  play_org = 0;
  timer_stop();
  spu_clear_all_voices();
  org_free();
}

static const char *run_menu(void) {
  static char files[MAX_MENU_FILES][CD_MAX_FILENAME];
  int numfiles = 0;

  // scan root CD directory if needed
  numfiles = cd_scandir("\\ORG", files, ".ORG");
  if (numfiles < 0)
    panic("could not scan ORG directory");

  int filepos = 0;

  while (1) {
    btn_scan();

    if (btn_pressed(PAD_DOWN)) {
      if (filepos == numfiles - 1) filepos = 0;
      else ++filepos;
    } else if (btn_pressed(PAD_UP)) {
      if (filepos == 0) filepos = numfiles - 1;
      else --filepos;
    }

    if (btn_pressed(PAD_CROSS) && numfiles) {
      // nuke everything after the extension
      char *dot = strrchr(files[filepos], '.');
      if (dot) *dot = '\0';
      return files[filepos];
    }

    if (btn_pressed(PAD_START))
      break;

    FntPrint(-1, "\n SELECT FILE AND PRESS X\n");
    FntPrint(-1, " OR SWAP CD AND PRESS START\n\n");

    if (!numfiles) {
      FntPrint(-1, "   SORRY NOTHING\n");
    } else {
      const int startfile = filepos / MENU_DISP_FILES * MENU_DISP_FILES;
      int endfile = startfile + MENU_DISP_FILES;
      if (endfile > numfiles) endfile = numfiles;
      for (int i = startfile; i < endfile; ++i)
        FntPrint(-1, " %c %s\n", (i == filepos) ? '*' : ' ', files[i]);
    }    

    FntFlush(-1);
    display();
  }

  return NULL;
}

int main(int argc, char **argv) {
  init();

  bnk_sfx = load_sfx_bank("\\BNK\\SFX.BNK;1");
  org_init(bnk_sfx);

  while (1) {
    const char *org = NULL;
    while (!org)
      org = run_menu();
    run_player(org);
  }

  return 0;
}
