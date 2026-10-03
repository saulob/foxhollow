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
#include "main/dll/dll_0044_cameramodeviewfinder.h"
#include "main/dll/dll_004E_cameramodeworldmap.h"
#include "main/dll/player.h"
#include "main/dll/player_state.h"
#include "main/dll/player_status.h"
#include "main/dll/savegame_load.h"
#include "main/frame_timing.h"
#include "main/gameloop_internal.h"
#include "main/lightmap.h"
#include "main/pad.h"
#include "main/object_transform.h"
#include "main/objseq.h"
#include "main/shader.h"
#include "main/track_dolphin.h"
#include "sys/objects.h"
#include "sys/objects/lifecycle.h"

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
#define FH_CHEATS_PLAYER_MODE_ON_CLOUDRUNNER 0x1a

#define FH_CHEATS_FLY_VERTICAL_SPEED 1.25f
#define FH_CHEATS_FLY_SWIM_SURFACE_DEPTH 22.0f
#define FH_CHEATS_NO_FLOOR_Y -1e+05f
#define FH_CHEATS_PLAYER_TELEPORT_HOLD 0x4000

#define FH_CHEATS_NOCLIP_SYNC_LOCAL_POINTS 1
#define FH_CHEATS_NOCLIP_PUSH_EPSILON_SQ 1e-04f
#define FH_CHEATS_NOCLIP_FLOOR_QUERY_MASK 1

typedef struct FhCheatsSafePosition {
  int valid;
  Vec3f pos;
  Vec3s rot;
  int swimming;
  int mapId;
  int layer;
} FhCheatsSafePosition;

static const s16 sFlyCombatModes[] = {0x1f, 0x23, 0x24, 0x25, 0x26, 0x27, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d};

void playerStartWallTransition(GameObject* obj, PlayerState* inner, PlayerState* state);
void playerRefreshCollisionState(GameObject* obj, PlayerState* p2, int flags);

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
static int sJumpPending;
static u32 sJumpPendingRetrace;
static int sJumpKeyWasDown;
static int sJumpStickWasDown;
static int sFly;
static int sFlyInput;
static int sFlyUpArmed;
static int sFlyDownArmed;
static int sFlyToggleWasDown;
static int sFlyReturnWasDown;
static FhCheatsSafePosition sSafe;
static int sNoclip;
static int sNoclipToggleWasDown;
static int sNoclipCollision;
static f32 sNoclipPosX;
static f32 sNoclipPosZ;
static int sNoclipHold;
static f32 sNoclipHoldY;
static GameObject* sNoclipHoldPlayer;

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
  fhCheatsSetFly(0);
  fhCheatsSetNoclip(0);
  sSafe.valid = 0;
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
  sJump = enabled != 0;
  sJumpPending = 0;
}

static int jump_request_expired(void) {
  return VIGetRetraceCount() - sJumpPendingRetrace > FH_CHEATS_JUMP_REQUEST_RETRACES;
}

void fhCheatsJumpPoll(int keyDown, int active) {
  int stickDown = (padGetExtButtons(0) & PAD_BUTTON_RIGHT_STICK) != 0;
  int pressed;

  keyDown = keyDown != 0;
  if (sJumpPending && jump_request_expired()) {
    sJumpPending = 0;
  }
  pressed = sJump && active && ((keyDown && !sJumpKeyWasDown) || (stickDown && !sJumpStickWasDown));
  sJumpKeyWasDown = keyDown;
  sJumpStickWasDown = stickDown;
  if (pressed) {
    sJumpPending = 1;
    sJumpPendingRetrace = VIGetRetraceCount();
  }
}

static int player_controllable_on_foot(GameObject* player, PlayerState* st) {
  if (!fhCheatsGameplayActive() || fhCheatsArwingActive() ||
      st->baddie.controlMode == FH_CHEATS_PLAYER_MODE_ON_CLOUDRUNNER) {
    return 0;
  }
  if (getCurSeqNo() != 0) {
    return 0;
  }
  if ((st->flags360 & PLAYER_FLAG_LOCKED) != 0 || st->characterId == -1) {
    return 0;
  }
  if (st->playerStatus == NULL || st->playerStatus->health <= 0) {
    return 0;
  }
  if (st->heldObj != NULL || st->verticalVel != 0.0f) {
    return 0;
  }
  return (player->objectFlags & OBJECT_OBJFLAG_PARENT_SLACK) == 0;
}

static int jump_allowed(GameObject* player, PlayerState* st) {
  s16 mode = st->baddie.controlMode;

  if (!player_controllable_on_foot(player, st)) {
    return 0;
  }
  if (mode != FH_CHEATS_PLAYER_MODE_IDLE && mode != FH_CHEATS_PLAYER_MODE_MOVING) {
    return 0;
  }
  if (st->flags3F0.b20 || st->flags3F0.b02 || st->flags3F0.b10 || st->flags3F0.b40 || st->flags3F0.b80) {
    return 0;
  }
  if (st->baddie.targetObj != NULL || st->flags3F6.b40) {
    return 0;
  }
  if (st->curAnimId == 0x44 || st->curAnimId == 0x4e || st->curAnimId == 0x47 || st->curAnimId == 0x48) {
    return 0;
  }
  return !st->flags3F0.b04 && !st->flags3F0.b08 && st->flags3F1.b01;
}

int fhCheatsJumpUpdate(GameObject* player) {
  PlayerState* st;
  s16 mode;

  if (!sJump || player == NULL || player != Obj_GetPlayerObject() || !sJumpPending) {
    return 0;
  }
  sJumpPending = 0;
  st = player->extra;
  if (jump_request_expired() || !jump_allowed(player, st)) {
    return 0;
  }
  mode = st->baddie.controlMode;
  playerStartWallTransition(player, st, st);
  player->anim.velocityY = FH_CHEATS_JUMP_VELOCITY;
  return mode == FH_CHEATS_PLAYER_MODE_IDLE ? FH_CHEATS_PLAYER_ENTER_MOVING : 0;
}

int fhCheatsFlyEnabled(void) { return sFly; }

void fhCheatsSetFly(int enabled) {
  sFly = enabled != 0;
  sFlyInput = 0;
  sFlyUpArmed = 0;
  sFlyDownArmed = 0;
}

static int safe_position_warp_active(void) { return gArrivedWarpIndex != -1 || gPendingWarpIndex != -1; }

static void safe_position_check_map(void) {
  if (sSafe.valid && (safe_position_warp_active() || sSafe.mapId != gGameLoopPendingMapId ||
                      sSafe.layer != getCurMapLayer())) {
    sSafe.valid = 0;
  }
}

static int safe_position_allowed(GameObject* player, PlayerState* st) {
  if (safe_position_warp_active()) {
    return 0;
  }
  if (player->anim.parent != NULL || st->focusObject != NULL) {
    return 0;
  }
  if ((st->flags360 & FH_CHEATS_PLAYER_TELEPORT_HOLD) != 0) {
    return 0;
  }
  if (isInBounds(player->anim.localPosX, player->anim.localPosZ) != 1) {
    return 0;
  }
  return !(st->baddie.curvesCollision.resultFloorY <= FH_CHEATS_NO_FLOOR_Y);
}

static void safe_position_update(GameObject* player, PlayerState* st) {
  safe_position_check_map();
  if (!safe_position_allowed(player, st)) {
    return;
  }
  sSafe.valid = 1;
  sSafe.pos.x = player->anim.localPosX;
  sSafe.pos.y = player->anim.localPosY;
  sSafe.pos.z = player->anim.localPosZ;
  sSafe.rot.x = player->anim.rotX;
  sSafe.rot.y = player->anim.rotY;
  sSafe.rot.z = player->anim.rotZ;
  sSafe.swimming = st->flags3F0.b20;
  sSafe.mapId = gGameLoopPendingMapId;
  sSafe.layer = getCurMapLayer();
}

void fhCheatsReturnToSafePosition(void) {
  GameObject* player;
  PlayerState* st;

  safe_position_check_map();
  if (sSafe.valid && isInBounds(sSafe.pos.x, sSafe.pos.z) != 1) {
    sSafe.valid = 0;
  }
  if (!sSafe.valid || !fhCheatsGameplayActive() || fhCheatsArwingActive() ||
      (player = Obj_GetPlayerObject()) == NULL) {
    return;
  }
  st = player->extra;
  if (!player_controllable_on_foot(player, st)) {
    return;
  }
  if (player->anim.parent != NULL) {
    Obj_SetParent(player, NULL, 1);
  }
  player->anim.velocityX = 0.0f;
  player->anim.velocityY = 0.0f;
  player->anim.velocityZ = 0.0f;
  st->baddie.animSpeedA = 0.0f;
  st->baddie.animSpeedB = 0.0f;
  st->baddie.animSpeedC = 0.0f;
  st->flags3F0.b04 = 0;
  st->flags3F0.b08 = 0;
  st->staffHoldFrames = 0;
  if (!sSafe.swimming) {
    st->flags3F0.b20 = 0;
  }
  playerTeleport(player, &sSafe.pos, &sSafe.rot, 0);
  objSetPos(player, sSafe.pos.x, sSafe.pos.y, sSafe.pos.z);
  playerTeleport(player, NULL, NULL, 0);
  sNoclipHold = 0;
}

static int fly_key_held(int* armed, int keyDown, int active) {
  if (!active) {
    *armed = 0;
    return 0;
  }
  if (!keyDown) {
    *armed = 1;
    return 0;
  }
  return *armed;
}

void fhCheatsFlyPoll(int toggleKeyDown, int upKeyDown, int downKeyDown, int returnKeyDown, int active) {
  int up;
  int down;

  safe_position_check_map();
  toggleKeyDown = toggleKeyDown != 0;
  returnKeyDown = returnKeyDown != 0;
  if (active && toggleKeyDown && !sFlyToggleWasDown && !fhCheatsArwingActive()) {
    fhCheatsSetFly(!sFly);
  }
  sFlyToggleWasDown = toggleKeyDown;
  if (active && returnKeyDown && !sFlyReturnWasDown) {
    fhCheatsReturnToSafePosition();
  }
  sFlyReturnWasDown = returnKeyDown;
  active = sFly && active;
  up = fly_key_held(&sFlyUpArmed, upKeyDown != 0, active);
  down = fly_key_held(&sFlyDownArmed, downKeyDown != 0, active);
  sFlyInput = up - down;
}

static int fly_mode_allowed(s16 mode) {
  int i;

  if (mode == FH_CHEATS_PLAYER_MODE_IDLE || mode == FH_CHEATS_PLAYER_MODE_MOVING) {
    return 1;
  }
  for (i = 0; i < (int)(sizeof(sFlyCombatModes) / sizeof(sFlyCombatModes[0])); i++) {
    if (sFlyCombatModes[i] == mode) {
      return 1;
    }
  }
  return 0;
}

static int fly_allowed(GameObject* player, PlayerState* st) {
  if (!player_controllable_on_foot(player, st) || !fly_mode_allowed(st->baddie.controlMode)) {
    return 0;
  }
  return !(st->flags3F0.b04 && st->flags3F1.b01);
}

static int fly_input(PlayerState* st) {
  if (st->curAnimId == CAMERA_MODE_VIEWFINDER_RESOURCE_ID || st->curAnimId == CAMERA_MODE_WORLD_MAP_RESOURCE_ID) {
    return 0;
  }
  return sFlyInput;
}

static int fly_at_swim_surface(PlayerState* st, int input) {
  return st->flags3F0.b20 && input == 0 && st->waterDepth <= FH_CHEATS_FLY_SWIM_SURFACE_DEPTH;
}

static int fly_crossed_water_surface(PlayerState* st, int input) {
  s16 mode = st->baddie.controlMode;

  return st->flags3F0.b20 && input > 0 && st->waterDepth < 0.0f &&
         (mode == FH_CHEATS_PLAYER_MODE_IDLE || mode == FH_CHEATS_PLAYER_MODE_MOVING);
}

static void fly_keep_inside_map(GameObject* player, PlayerState* st) {
  f32 step;

  if (player->anim.parent != NULL || st->focusObject != NULL) {
    return;
  }
  step = timeDelta * fhCheatsMoveScale(player);
  if (isInBounds(player->anim.localPosX + player->anim.velocityX * step,
                 player->anim.localPosZ + player->anim.velocityZ * step) == 0) {
    player->anim.velocityX = 0.0f;
    player->anim.velocityZ = 0.0f;
  }
}

void fhCheatsFlyUpdate(GameObject* player) {
  PlayerState* st;
  int input;

  if (!sFly || player == NULL || player != Obj_GetPlayerObject()) {
    return;
  }
  st = player->extra;
  if (!fly_allowed(player, st)) {
    return;
  }
  st->flags3F0.b08 = 0;
  st->flags3F0.b04 = 0;
  st->staffHoldFrames = 0;
  input = fly_input(st);
  if (!fly_at_swim_surface(st, input)) {
    player->anim.velocityY = (f32)input * FH_CHEATS_FLY_VERTICAL_SPEED;
  }
  if (fly_crossed_water_surface(st, input)) {
    st->flags3F0.b20 = 0;
  }
  fly_keep_inside_map(player, st);
  safe_position_update(player, st);
}

int fhCheatsNoclipEnabled(void) { return sNoclip; }

void fhCheatsSetNoclip(int enabled) {
  sNoclip = enabled != 0;
  sNoclipHold = 0;
}

void fhCheatsNoclipPoll(int toggleKeyDown, int active) {
  toggleKeyDown = toggleKeyDown != 0;
  if (active && toggleKeyDown && !sNoclipToggleWasDown && !fhCheatsArwingActive()) {
    fhCheatsSetNoclip(!sNoclip);
  }
  sNoclipToggleWasDown = toggleKeyDown;
}

static int noclip_allowed(GameObject* player, PlayerState* st) {
  return player_controllable_on_foot(player, st) && st->focusObject == NULL &&
         fly_mode_allowed(st->baddie.controlMode);
}

int fhCheatsNoclipActive(GameObject* player) {
  return sNoclip && player != NULL && player == Obj_GetPlayerObject() && noclip_allowed(player, player->extra);
}

static int noclip_floor_below(GameObject* player, PlayerState* st) {
  CurvesCollisionState* collision = &st->baddie.curvesCollision;
  f32 depth;

  if (!(collision->resultFloorY <= FH_CHEATS_NO_FLOOR_Y)) {
    return 1;
  }
  if (collision->resultFloorGap <= 0.0f) {
    return -1;
  }
  return trackGetHeightAboveGround(player, player->anim.localPosX, player->anim.localPosY, player->anim.localPosZ,
                                   &depth, FH_CHEATS_NOCLIP_FLOOR_QUERY_MASK) != 0;
}

void fhCheatsNoclipUpdate(GameObject* player) {
  PlayerState* st;
  int floor;

  if (!fhCheatsNoclipActive(player)) {
    sNoclipHold = 0;
    return;
  }
  st = player->extra;
  fly_keep_inside_map(player, st);
  if (sFly || st->flags3F0.b20 || player->anim.parent != NULL || safe_position_warp_active()) {
    sNoclipHold = 0;
    return;
  }
  floor = noclip_floor_below(player, st);
  if (floor > 0 || (floor < 0 && !sNoclipHold)) {
    sNoclipHold = 0;
    return;
  }
  if (!sNoclipHold || sNoclipHoldPlayer != player) {
    sNoclipHold = 1;
    sNoclipHoldPlayer = player;
    sNoclipHoldY = player->anim.localPosY;
  }
  player->anim.localPosY = sNoclipHoldY;
  player->anim.velocityY = 0.0f;
  st->flags3F0.b08 = 0;
  st->flags3F0.b04 = 0;
  st->staffHoldFrames = 0;
}

void fhCheatsNoclipBeginCollision(GameObject* player) {
  sNoclipCollision = fhCheatsNoclipActive(player);
  if (sNoclipCollision) {
    sNoclipPosX = player->anim.localPosX;
    sNoclipPosZ = player->anim.localPosZ;
  }
}

static void noclip_reanchor_traces(GameObject* player, PlayerState* st) {
  CurvesCollisionState* collision = &st->baddie.curvesCollision;
  int count = collision->pointCounts >> CURVES_POINT_COUNT_SEGMENT_SHIFT;
  int i;

  for (i = 0; i < count; i++) {
    collision->traceStart[i][0] = player->anim.worldPosX;
    collision->traceStart[i][2] = player->anim.worldPosZ;
  }
  playerRefreshCollisionState(player, st, FH_CHEATS_NOCLIP_SYNC_LOCAL_POINTS);
}

void fhCheatsNoclipEndCollision(GameObject* player) {
  f32 dx;
  f32 dz;

  if (!sNoclipCollision) {
    return;
  }
  sNoclipCollision = 0;
  dx = player->anim.localPosX - sNoclipPosX;
  dz = player->anim.localPosZ - sNoclipPosZ;
  player->anim.localPosX = sNoclipPosX;
  player->anim.localPosZ = sNoclipPosZ;
  if (player->anim.parent != NULL) {
    Obj_TransformLocalPointToWorld(player->anim.localPosX, player->anim.localPosY, player->anim.localPosZ,
                                   &player->anim.worldPosX, &player->anim.worldPosY, &player->anim.worldPosZ,
                                   player->anim.parent);
  } else {
    player->anim.worldPosX = player->anim.localPosX;
    player->anim.worldPosZ = player->anim.localPosZ;
  }
  if (dx * dx + dz * dz > FH_CHEATS_NOCLIP_PUSH_EPSILON_SQ) {
    noclip_reanchor_traces(player, player->extra);
  }
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
