#include "foxhollow_compat.h"
#include "foxhollow_cheats.h"

#include <dolphin/types.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>

#include "game/objects/object.h"
#include "main/gamebit_ids.h"
#include "main/gamebits.h"
#include "main/gameloop.h"
#include "main/mapEventTypes.h"
#include "main/model_engine_ui.h"
#include "main/obj_list.h"
#include "main/dll/ARW/dll_029A_arwarwing.h"
#include "main/dll/cmenu_item_table.h"
#include "main/dll/player.h"
#include "main/dll/player_state.h"
#include "main/dll/player_status.h"
#include "main/dll/savegame_load.h"
#include "main/pad.h"
#include "main/objseq.h"
#include "sys/objects.h"
#include "sys/objects/lifecycle.h"

#include <stdio.h>

#define FH_CHEATS_GAME_STATE_RUNNING 1
#define FH_CHEATS_UI_DLL_GAMEPLAY 1
#define FH_CHEATS_UI_DLL_FRONTEND_FIRST 2
#define FH_CHEATS_UI_DLL_FRONTEND_LAST 7
#define FH_CHEATS_ARWING_BOMBS 3
#define FH_CHEATS_ARWING_RINGS 10
#define FH_CHEATS_MAX_SCARABS 200
#define FH_CHEATS_100_BAG_SCARABS 100
#define FH_CHEATS_50_BAG_SCARABS 50
#define FH_CHEATS_START_SCARABS 10
#define FH_CHEATS_ADD_SCARABS 10
#define FH_CHEATS_GLITCHED_ITEM_TEXT 0x404
#define FH_CHEATS_FAST_MOVEMENT_SCALE 2.0f

#define FH_CHEATS_JUMP_VELOCITY 2.5f
#define FH_CHEATS_JUMP_REQUEST_RETRACES 10
#define FH_CHEATS_PLAYER_MODE_IDLE 1
#define FH_CHEATS_PLAYER_MODE_MOVING 2
#define FH_CHEATS_PLAYER_ENTER_MOVING (FH_CHEATS_PLAYER_MODE_MOVING + 1)

#define DEBUG_SAULO_JUMP_LOG(...)                                                                                      \
  do {                                                                                                                 \
    fprintf(stderr, "[DEBUG_SAULO][Jump] " __VA_ARGS__);                                                               \
    fputc('\n', stderr);                                                                                               \
    fflush(stderr);                                                                                                    \
  } while (0)
#define DEBUG_SAULO_JUMP_GROUND_RETRACES 30
#define DEBUG_SAULO_JUMP_TRACK_RETRACES 600

typedef enum FhCheatsJumpSource {
  FH_CHEATS_JUMP_SOURCE_KEYBOARD,
  FH_CHEATS_JUMP_SOURCE_RIGHT_STICK,
} FhCheatsJumpSource;

static const char* const sJumpSourceNames[] = {"keyboard 0", "Right Stick"};

void playerStartWallTransition(GameObject* obj, PlayerState* inner, PlayerState* state);

typedef struct FhCheatsItem {
  int gameBit;
  int max;
} FhCheatsItem;

static const FhCheatsItem sItems[] = {
    {GAMEBIT_ITEM_TrickyFood_Count, 15}, {GAMEBIT_ITEM_BombSpore_Count, 7}, {GAMEBIT_ITEM_MoonSeed_Count, 7},
    {GAMEBIT_ITEM_Firefly_Count, 31},    {GAMEBIT_ITEM_FuelCell_Count, 42},
};

static const int sScarabBagBits[] = {
    GAMEBIT_ITEM_50ScarabBag_Got,
    GAMEBIT_ITEM_100ScarabBag_Got,
    GAMEBIT_ITEM_200ScarabBag_Got,
};

extern CMenuItemDef gCMenuCollectableItems[];
extern CMenuItemDef gCMenuStaffAbilities[];

static int sGodMode;
static int sInfiniteMagic;
static int sFastMovement;
static int sInfiniteItems;
static int sInfiniteTrickyEnergy;
static int sArwingGodMode;
static int sArwingInfiniteBombs;
static int sArwingRapidFire;
static int sArwingMaxRings;
static u32 sArwingBombLaunchRetrace;
static GameObject* sCloudRunner;
static int sCloudRunnerRapidFire;
static int sSessionActive;
static int sJump;
static int sJumpPending = -1;
static u32 sJumpPendingRetrace;
static int sJumpKeyWasDown;
static int sJumpStickWasDown;
static int sDebugSauloJumpActive;
static u32 sDebugSauloJumpStartRetrace;
static f32 sDebugSauloJumpStartY;
static f32 sDebugSauloJumpPeakY;
static int sDebugSauloJumpLeftGround;

int fhCheatsGameplayActive(void) {
  return getGameState() == FH_CHEATS_GAME_STATE_RUNNING && getCurUiDll() == FH_CHEATS_UI_DLL_GAMEPLAY &&
         getSaveGameLoadStatus() == 0 && (Obj_GetPlayerObject() != NULL || getArwing() != NULL);
}

static void reset_cheats(void) {
  sGodMode = 0;
  sInfiniteMagic = 0;
  sFastMovement = 0;
  sInfiniteItems = 0;
  sInfiniteTrickyEnergy = 0;
  sArwingGodMode = 0;
  sArwingInfiniteBombs = 0;
  sArwingRapidFire = 0;
  sArwingMaxRings = 0;
  sArwingBombLaunchRetrace = 0;
  sCloudRunner = NULL;
  sCloudRunnerRapidFire = 0;
  fhCheatsSetJump(0);
}

void fhCheatsUpdateSession(void) {
  int uiDll;

  if (fhCheatsGameplayActive()) {
    sSessionActive = 1;
    return;
  }
  uiDll = getCurUiDll();
  if (sSessionActive && (getGameState() != FH_CHEATS_GAME_STATE_RUNNING ||
                         (uiDll >= FH_CHEATS_UI_DLL_FRONTEND_FIRST && uiDll <= FH_CHEATS_UI_DLL_FRONTEND_LAST))) {
    sSessionActive = 0;
    reset_cheats();
  }
}

int fhCheatsGodModeEnabled(void) { return sGodMode; }

void fhCheatsSetGodMode(int enabled) {
  GameObject* player;
  int current;
  int missing;

  enabled = enabled != 0;
  if (enabled && !sGodMode && fhCheatsGameplayActive() && (player = Obj_GetPlayerObject()) != NULL) {
    current = playerGetCurHealth(player);
    missing = playerGetMaxHealth(player) - current;
    if (current > 0 && missing > 0) {
      playerAddHealth(player, missing);
    }
  }
  sGodMode = enabled;
}

int fhCheatsInfiniteMagicEnabled(void) { return sInfiniteMagic; }

void fhCheatsSetInfiniteMagic(int enabled) {
  GameObject* player;
  int missing;

  enabled = enabled != 0;
  if (enabled && !sInfiniteMagic && fhCheatsGameplayActive() && (player = Obj_GetPlayerObject()) != NULL) {
    missing = playerGetMaxMagic(player) - playerGetCurMagic(player);
    if (missing > 0) {
      playerAddRemoveMagic(player, missing);
    }
  }
  sInfiniteMagic = enabled;
}

int fhCheatsPlayerIsFox(void) {
  GameObject* player = Obj_GetPlayerObject();
  return player != NULL && objIsCurModelNotZero(player) != 0;
}

int fhCheatsTrickyPresent(void) { return getTrickyObject() != NULL; }

int fhCheatsPlayerDamage(int damage) { return sGodMode && damage > 0 ? 0 : damage; }

int fhCheatsMagicCost(int cost) { return sInfiniteMagic && cost > 0 ? 0 : cost; }

int fhCheatsFastRunEnabled(void) { return sFastMovement; }

void fhCheatsSetFastRun(int enabled) { sFastMovement = enabled != 0; }

float fhCheatsMoveScale(GameObject* obj) {
  if (!sFastMovement || obj != Obj_GetPlayerObject()) {
    return 1.0f;
  }
  return FH_CHEATS_FAST_MOVEMENT_SCALE;
}

int fhCheatsJumpEnabled(void) { return sJump; }

void fhCheatsSetJump(int enabled) {
  enabled = enabled != 0;
  if (enabled != sJump) {
    DEBUG_SAULO_JUMP_LOG("Jump %s", enabled ? "enabled" : "disabled");
  }
  sJump = enabled;
  sJumpPending = -1;
  sDebugSauloJumpActive = 0;
}

static int jump_request_expired(void) {
  return VIGetRetraceCount() - sJumpPendingRetrace > FH_CHEATS_JUMP_REQUEST_RETRACES;
}

void fhCheatsJumpPoll(int keyDown, int active) {
  int stickDown = (padGetExtButtons(0) & PAD_BUTTON_RIGHT_STICK) != 0;
  int source = -1;

  keyDown = keyDown != 0;
  if (sJumpPending >= 0 && jump_request_expired()) {
    DEBUG_SAULO_JUMP_LOG("jump rejected: invalid state (%s jump not reached by normal player control)",
                         sJumpSourceNames[sJumpPending]);
    sJumpPending = -1;
  }
  if (sJump && active) {
    if (keyDown && !sJumpKeyWasDown) {
      source = FH_CHEATS_JUMP_SOURCE_KEYBOARD;
    } else if (stickDown && !sJumpStickWasDown) {
      source = FH_CHEATS_JUMP_SOURCE_RIGHT_STICK;
    }
  }
  sJumpKeyWasDown = keyDown;
  sJumpStickWasDown = stickDown;
  if (source < 0) {
    return;
  }
  DEBUG_SAULO_JUMP_LOG("%s jump", sJumpSourceNames[source]);
  sJumpPending = source;
  sJumpPendingRetrace = VIGetRetraceCount();
}

static const char* jump_reject_reason(GameObject* player, PlayerState* st) {
  s16 mode = st->baddie.controlMode;

  if (!fhCheatsGameplayActive() || fhCheatsArwingActive() || fhCheatsCloudRunnerActive()) {
    return "invalid state (not on foot)";
  }
  if (getCurSeqNo() != 0) {
    return "invalid state (sequence running)";
  }
  if ((st->flags360 & PLAYER_FLAG_LOCKED) != 0 || st->characterId == -1) {
    return "invalid state (controls locked)";
  }
  if (st->playerStatus == NULL || st->playerStatus->health <= 0) {
    return "invalid state (dead)";
  }
  if (mode != FH_CHEATS_PLAYER_MODE_IDLE && mode != FH_CHEATS_PLAYER_MODE_MOVING) {
    return "invalid state (control mode)";
  }
  if (st->flags3F0.b20) {
    return "invalid state (swimming)";
  }
  if (st->flags3F0.b02 || st->flags3F0.b10 || st->flags3F0.b40 || st->flags3F0.b80) {
    return "invalid state (guard, attack or quick turn)";
  }
  if (st->heldObj != NULL) {
    return "invalid state (carrying object)";
  }
  if (st->baddie.targetObj != NULL || st->flags3F6.b40) {
    return "invalid state (target lock)";
  }
  if (st->verticalVel != 0.0f) {
    return "invalid state (climbing)";
  }
  if (st->curAnimId == 0x44 || st->curAnimId == 0x4e || st->curAnimId == 0x47 || st->curAnimId == 0x48) {
    return "invalid state (camera mode)";
  }
  if ((player->objectFlags & OBJECT_OBJFLAG_PARENT_SLACK) != 0) {
    return "invalid state (attached to object)";
  }
  if (st->flags3F0.b04 || st->flags3F0.b08 || !st->flags3F1.b01) {
    return "airborne";
  }
  return NULL;
}

static void debug_saulo_jump_begin(GameObject* player) {
  sDebugSauloJumpActive = 1;
  sDebugSauloJumpLeftGround = 0;
  sDebugSauloJumpStartRetrace = VIGetRetraceCount();
  sDebugSauloJumpStartY = player->anim.worldPosY;
  sDebugSauloJumpPeakY = player->anim.worldPosY;
}

static void debug_saulo_jump_track(GameObject* player, PlayerState* st) {
  unsigned elapsed;

  if (!sDebugSauloJumpActive) {
    return;
  }
  elapsed = (unsigned)(VIGetRetraceCount() - sDebugSauloJumpStartRetrace);
  if (player->anim.worldPosY > sDebugSauloJumpPeakY) {
    sDebugSauloJumpPeakY = player->anim.worldPosY;
  }
  if (!st->flags3F1.b01) {
    sDebugSauloJumpLeftGround = 1;
  }
  if (st->baddie.controlMode != FH_CHEATS_PLAYER_MODE_IDLE && st->baddie.controlMode != FH_CHEATS_PLAYER_MODE_MOVING) {
    DEBUG_SAULO_JUMP_LOG("jump ended in control mode %d after %u retraces, peak +%.1f", st->baddie.controlMode,
                         elapsed, sDebugSauloJumpPeakY - sDebugSauloJumpStartY);
  } else if (sDebugSauloJumpLeftGround && st->flags3F1.b01 && !st->flags3F0.b08) {
    DEBUG_SAULO_JUMP_LOG("jump landed after %u retraces, peak +%.1f, landing height %+.1f, fall mode %d", elapsed,
                         sDebugSauloJumpPeakY - sDebugSauloJumpStartY, player->anim.worldPosY - sDebugSauloJumpStartY,
                         st->flags3F0.b04);
  } else if (!sDebugSauloJumpLeftGround && elapsed > DEBUG_SAULO_JUMP_GROUND_RETRACES) {
    DEBUG_SAULO_JUMP_LOG("jump never left the ground");
  } else if (elapsed > DEBUG_SAULO_JUMP_TRACK_RETRACES) {
    DEBUG_SAULO_JUMP_LOG("jump still airborne after %u retraces, peak +%.1f", elapsed,
                         sDebugSauloJumpPeakY - sDebugSauloJumpStartY);
  } else {
    return;
  }
  sDebugSauloJumpActive = 0;
}

static int jump_start(GameObject* player, PlayerState* st, int source) {
  s16 mode = st->baddie.controlMode;
  f32 nativeVelocity;

  playerStartWallTransition(player, st, st);
  nativeVelocity = player->anim.velocityY;
  player->anim.velocityY = FH_CHEATS_JUMP_VELOCITY;
  DEBUG_SAULO_JUMP_LOG("jump started from %s: velocityY=%.2f (native %.2f), move=0x%X, mode=%d%s",
                       sJumpSourceNames[source], player->anim.velocityY, nativeVelocity,
                       (unsigned)(u16)player->anim.currentMove, mode, mode == FH_CHEATS_PLAYER_MODE_IDLE ? "->2" : "");
  debug_saulo_jump_begin(player);
  return mode == FH_CHEATS_PLAYER_MODE_IDLE ? FH_CHEATS_PLAYER_ENTER_MOVING : 0;
}

int fhCheatsJumpUpdate(GameObject* player) {
  PlayerState* st;
  const char* reason;
  int source;

  if (!sJump || player == NULL || player != Obj_GetPlayerObject()) {
    return 0;
  }
  st = player->extra;
  debug_saulo_jump_track(player, st);
  source = sJumpPending;
  if (source < 0) {
    return 0;
  }
  sJumpPending = -1;
  if (jump_request_expired()) {
    DEBUG_SAULO_JUMP_LOG("jump rejected: invalid state (%s jump request expired)", sJumpSourceNames[source]);
    return 0;
  }
  reason = jump_reject_reason(player, st);
  if (reason != NULL) {
    DEBUG_SAULO_JUMP_LOG("jump rejected: %s (%s, mode=%d)", reason, sJumpSourceNames[source], st->baddie.controlMode);
    return 0;
  }
  return jump_start(player, st, source);
}

int fhCheatsArwingActive(void) { return fhCheatsGameplayActive() && getArwing() != NULL; }

int fhCheatsArwingGodModeEnabled(void) { return sArwingGodMode; }

void fhCheatsSetArwingGodMode(int enabled) { sArwingGodMode = enabled != 0; }

int fhCheatsArwingInfiniteBombsEnabled(void) { return sArwingInfiniteBombs; }

void fhCheatsSetArwingInfiniteBombs(int enabled) { sArwingInfiniteBombs = enabled != 0; }

int fhCheatsArwingRapidFireEnabled(void) { return sArwingRapidFire; }

void fhCheatsSetArwingRapidFire(int enabled) { sArwingRapidFire = enabled != 0; }

int fhCheatsArwingMaxRingsEnabled(void) { return sArwingMaxRings; }

void fhCheatsSetArwingMaxRings(int enabled) { sArwingMaxRings = enabled != 0; }

void fhCheatsArwingRefillHealth(GameObject* arwing) {
  int current;
  int max;

  if (!sArwingGodMode || arwing == NULL) {
    return;
  }
  current = arwarwing_getHealth(arwing);
  max = arwarwing_getMaxHealth(arwing);
  if (current < max) {
    arwarwing_addHealth(arwing, max - current);
  }
}

unsigned short fhCheatsArwingFireInput(unsigned short pressed, unsigned short held) {
  if (sArwingRapidFire && (held & PAD_BUTTON_A) != 0) {
    return (unsigned short)(pressed | PAD_BUTTON_A);
  }
  return pressed;
}

void fhCheatsArwingUpdate(GameObject* arwing) {
  ArwingState* state = arwing->extra;

  if (sArwingMaxRings) {
    state->collectedRings = FH_CHEATS_ARWING_RINGS;
  }
  if (sArwingInfiniteBombs && state->bombCount < FH_CHEATS_ARWING_BOMBS) {
    state->bombCount = FH_CHEATS_ARWING_BOMBS;
  }
  fhCheatsArwingRefillHealth(arwing);
}

void fhCheatsArwingBombLaunched(void) { sArwingBombLaunchRetrace = VIGetRetraceCount(); }

int fhCheatsArwingBombIgnoreButton(void) { return VIGetRetraceCount() == sArwingBombLaunchRetrace; }

int fhCheatsArwingBombIgnoreHit(GameObject* bomb, GameObject* hitObj) {
  GameObject** objects;
  int start = 0;
  int count = 0;
  int i;

  if (hitObj == NULL) {
    return 0;
  }
  objects = ObjList_GetObjects(&start, &count);
  for (i = start; i < count; i++) {
    if (objects[i] == hitObj) {
      return hitObj->anim.romDefNo == bomb->anim.romDefNo;
    }
  }
  return 0;
}

void fhCheatsSetCloudRunner(GameObject* cloudRunner, int alive) {
  if (alive) {
    sCloudRunner = cloudRunner;
  } else if (cloudRunner == sCloudRunner) {
    sCloudRunner = NULL;
  }
}

int fhCheatsCloudRunnerActive(void) { return sCloudRunner != NULL && fhCheatsGameplayActive(); }

int fhCheatsCloudRunnerRapidFireEnabled(void) { return sCloudRunnerRapidFire; }

void fhCheatsSetCloudRunnerRapidFire(int enabled) { sCloudRunnerRapidFire = enabled != 0; }

int fhCheatsCloudRunnerRapidFire(int held) { return sCloudRunnerRapidFire && held; }

int fhCheatsInfiniteItemsEnabled(void) { return sInfiniteItems; }

void fhCheatsSetInfiniteItems(int enabled) { sInfiniteItems = enabled != 0; }

int fhCheatsInfiniteTrickyEnergyEnabled(void) { return sInfiniteTrickyEnergy; }

void fhCheatsSetInfiniteTrickyEnergy(int enabled) { sInfiniteTrickyEnergy = enabled != 0; }

static void fill_scarabs(GameObject* player) {
  int money;

  if (mainGetBit(GAMEBIT_ITEM_200ScarabBag_Got) == 0) {
    mainSetBits(GAMEBIT_ITEM_200ScarabBag_Got, 1);
  }
  money = playerGetMoney(player);
  if (money < FH_CHEATS_MAX_SCARABS) {
    playerAddMoney(player, FH_CHEATS_MAX_SCARABS - money);
  }
}

static void refill_items(GameObject* player) {
  int i;

  for (i = 0; i < (int)(sizeof(sItems) / sizeof(sItems[0])); i++) {
    if ((int)mainGetBit(sItems[i].gameBit) < sItems[i].max) {
      mainSetBits(sItems[i].gameBit, sItems[i].max);
    }
  }
  fill_scarabs(player);
}

void fhCheatsUpdate(void) {
  GameObject* player;
  TrickyStats* stats;

  if (!fhCheatsGameplayActive()) {
    return;
  }
  player = Obj_GetPlayerObject();
  if (sInfiniteItems && player != NULL) {
    refill_items(player);
  }
  if (sInfiniteTrickyEnergy && getTrickyObject() != NULL) {
    stats = (*gMapEventInterface)->getTrickyStats();
    if (stats != NULL && stats->energy < stats->maxEnergy) {
      stats->energy = stats->maxEnergy;
    }
  }
}

void fhCheatsGiveAllItems(void) {
  const CMenuItemDef* item;
  GameObject* player;

  if (!fhCheatsGameplayActive()) {
    return;
  }
  for (item = gCMenuCollectableItems; item->ownedGameBit != -1; item++) {
    if (item->nameTextId == FH_CHEATS_GLITCHED_ITEM_TEXT || item->ownedGameBit == GAMEBIT_ITEM_FireGem_Got ||
        item->ownedGameBit == GAMEBIT_ITEM_GiveScarabs_Count) {
      continue;
    }
    if (item->usedGameBit != -1 && mainGetBit(item->usedGameBit) != 0) {
      continue;
    }
    if (mainGetBit(item->ownedGameBit) == 0) {
      mainSetBits(item->ownedGameBit, 1);
    }
  }
  player = Obj_GetPlayerObject();
  if (player != NULL) {
    refill_items(player);
  }
}

void fhCheatsGiveAllStaffAbilities(void) {
  const CMenuItemDef* ability;
  GameObject* player;
  int missing;

  if (!fhCheatsGameplayActive()) {
    return;
  }
  for (ability = gCMenuStaffAbilities; ability->ownedGameBit != -1; ability++) {
    if (mainGetBit(ability->ownedGameBit) == 0) {
      mainSetBits(ability->ownedGameBit, 1);
    }
    if (ability->activeGameBit != -1 && mainGetBit(ability->activeGameBit) != 0) {
      mainSetBits(ability->activeGameBit, 0);
    }
  }
  if (mainGetBit(GAMEBIT_ITEM_Magic_Got) == 0) {
    mainSetBits(GAMEBIT_ITEM_Magic_Got, 1);
  }
  player = Obj_GetPlayerObject();
  if (player != NULL) {
    missing = playerGetMaxMagic(player) - playerGetCurMagic(player);
    if (missing > 0) {
      playerAddRemoveMagic(player, missing);
    }
  }
}

static GameObject* scarab_player(void) {
  if (!fhCheatsGameplayActive() || fhCheatsArwingActive() || !fhCheatsPlayerIsFox()) {
    return NULL;
  }
  return Obj_GetPlayerObject();
}

static void clear_scarab_bag_bits(void) {
  int i;

  for (i = 0; i < (int)(sizeof(sScarabBagBits) / sizeof(sScarabBagBits[0])); i++) {
    if (mainGetBit(sScarabBagBits[i]) != 0) {
      mainSetBits(sScarabBagBits[i], 0);
    }
  }
}

int fhCheatsScarabCount(void) {
  GameObject* player = scarab_player();
  return player != NULL ? playerGetMoney(player) : 0;
}

int fhCheatsScarabCapacity(void) {
  if (mainGetBit(GAMEBIT_ITEM_200ScarabBag_Got) != 0) {
    return FH_CHEATS_MAX_SCARABS;
  }
  if (mainGetBit(GAMEBIT_ITEM_100ScarabBag_Got) != 0) {
    return FH_CHEATS_100_BAG_SCARABS;
  }
  if (mainGetBit(GAMEBIT_ITEM_50ScarabBag_Got) != 0) {
    return FH_CHEATS_50_BAG_SCARABS;
  }
  return FH_CHEATS_START_SCARABS;
}

void fhCheatsResetScarabBag(void) {
  GameObject* player = scarab_player();

  if (player == NULL) {
    return;
  }
  clear_scarab_bag_bits();
  playerAddMoney(player, FH_CHEATS_START_SCARABS - playerGetMoney(player));
}

void fhCheatsAddScarabs(void) {
  GameObject* player = scarab_player();
  int capacity;

  if (player == NULL) {
    return;
  }
  capacity = fhCheatsScarabCapacity();
  if (playerGetMoney(player) + FH_CHEATS_ADD_SCARABS > capacity) {
    if (capacity == FH_CHEATS_START_SCARABS) {
      mainSetBits(GAMEBIT_ITEM_50ScarabBag_Got, 1);
    } else if (capacity == FH_CHEATS_50_BAG_SCARABS) {
      mainSetBits(GAMEBIT_ITEM_100ScarabBag_Got, 1);
    } else if (capacity == FH_CHEATS_100_BAG_SCARABS) {
      mainSetBits(GAMEBIT_ITEM_200ScarabBag_Got, 1);
    }
  }
  playerAddMoney(player, FH_CHEATS_ADD_SCARABS);
}
