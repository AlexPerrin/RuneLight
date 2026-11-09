#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Read-only views of game state, resolved per game build (see game.cpp).
namespace game {

// Resolves offsets for the loaded game library; false if the build is unknown,
// in which case every accessor below returns its "unavailable" value.
bool init();
bool available();
const char* build_name();

int cycle();       // client cycle, +1 every 20 ms; -1 if unavailable
int game_state();  // RuneLite GameState values (10 login screen, 30 logged in, ...); -1 if unavailable
uintptr_t client_vtable();  // address of the Client vtable in the loaded library; 0 if unavailable
uintptr_t do_cycle();       // Client::doCycle, the expected value of vtable slot 5

int server_tick();  // number of SERVER_TICK_END packets received (one per 600 ms game tick)

int varp_count();
int varp(int id);         // client-side varp value (what scripts see); 0 if out of range
int server_varp(int id);  // last value the server sent
// Calls the game's varbit lookup (definition from the cache + bit extraction). Game thread only:
// the definition cache isn't thread-safe.
int varbit(int id);
// Minimap dot style per category 0-6 (item, NPC, player, friend, team, friends chat, clan):
// set (rebuilds the dot sprite) and get {styled, dot rgb, outline rgb, thickness}. Game thread only.
bool minimap_dot_style(int cat, int dot_rgb, int outline_rgb, int thickness);
bool minimap_dot_get(int cat, int out[4]);
// Client.setGeSearchResultIds/Count/Index: the GE item search results OC_FINDNEXT reads. Game thread only.
bool set_ge_search_ids(const int16_t* ids, int n);
bool set_ge_search_int(bool index, int value);
// {count, index, ids...} as the setters left them. Game thread only.
bool ge_search(std::vector<int>& out);
// Client.queueChangedSkill: records `skill` in the changed-skills ring as UPDATE_STAT does, so the
// skill tab's onStatTransmit scripts redraw it. Game thread only.
bool queue_changed_skill(int skill);
// The client records each varp id the server changes in a 32-entry ring; `changed_varp_count`
// is the running total, entry n lives at slot n % 32.
uint32_t changed_varp_count();
int changed_varp(uint32_t n);

struct Point { int x, y; };

// Friends / ignore lists: FriendSystem = *(Client +0x46c0); list state +0x150 (0 none, 1 loaded
// (FRIENDLIST_LOADED), 2 after UPDATE_FRIENDLIST); friends container +0x08, ignores +0xb8
// ({count +0xc, Element** +0x20}); element: name string +0x08, previous name +0x38, friend world
// +0x68 (0 offline), rank +0x88. From static RE of the CS2 friend ops (0x4ebdb0) and parsers
// 0x56e448/0x56ee54 (2026-10-16).
struct FriendEntry { std::string name, prev; int world = 0, rank = -1; };
int friend_list_state();
std::vector<uint8_t> target_state_bytes(int len);
bool target_mode(int out[3]);  // {active, selected component, index}
bool end_target_mode();  // probe: widget target mode state
// setGameState(LOADING): the game's own settings-changed reload (0x429900(client): when logged in,
// game state 30 -> 25 and the scene is rebuilt). Client thread only; false when not logged in.
bool reload_scene();
// Tile.setGroundObject(null): the game's own ground-decoration remover 0x6dca24(scene, plane, x, y)
// (LOC_DEL layer 3 calls it from 0x473a10): clears tile +0x118, queues (tag, 3) on the scene's
// removal list when the renderer flag 0x1d7ddc0 is set, and frees the object. Top-level scene,
// game thread only; false when there is no tile or no ground decoration there.
bool remove_ground_decoration(int plane, int x, int y);
// Scene terrain (docs/offsets.md "Scene terrain"), top-level scene, game thread only; false when
// unreadable. what: 0 scene fields {draw distance, min level, tint (hue | sat << 8 | lum << 16 |
// amount << 24)}; 1 tile paint {ptr lo, ptr hi, sw, se, ne, nw, texture, rgb, flat};
// 2 tile model {ptr lo, ptr hi, shape, rotation, underlay rgb, overlay rgb, texture, overlay mask,
// vertices, faces, (x, height, y) per vertex, (a, b, c) per face, (colour a, b, c) per face};
// 3/4/5 map underlay ids / overlay ids / shapes {planes, size x, size y, [plane][x][y] values};
// 6 the skybox model packed as pack_mesh does.
bool scene_terrain(int what, int plane, int x, int y, std::vector<int32_t>& out);
// what 0: scene field `field` (0 draw distance, 1 min level) = value; 1: paint field `field`
// (0 sw, 1 se, 2 ne, 3 nw, 4 texture, 5 rgb) of the tile's paint, when it is still `ptr`.
bool scene_set(int what, int plane, int x, int y, uint64_t ptr, int field, int value);
// Actor overhead text (+0x2d8, assigned with the game's string assign 0x3f1498) and its timer
// (+0x2f4, ticks left). `text` null keeps the string. Game thread only.
bool actor_set_overhead(uintptr_t actor, const std::string* text, int cycle);
// Actor state writes (game thread; Native.actorSet): sequences through the game's setSequence.
enum { kActorAnimation = 0, kActorAnimationFrame = 1, kActorPoseAnimation = 2, kActorPoseFrame = 3, kActorSkull = 4,
       kActorPose = 8 /* ..15: idle, idle left, idle right, walk, walk 180, walk left, walk right, run */ };
bool actor_set(uintptr_t actor, bool player, int what, int value);
// Spot anims: 6 ints per entry {slot, id, height, start cycle, frame, frame cycle}; field 0..4 = id..frame cycle.
bool actor_spot_anims(uintptr_t actor, std::vector<int32_t>& out);
bool actor_spot_anim_set(uintptr_t actor, int slot, int field, int value);
bool actor_create_spot_anim(uintptr_t actor, int slot, int id, int height, int delay);  // id -1 removes the slot
// Pending friend login/logout notices (game.cpp pending_logins), uncancelled ones in queue order. Game thread.
struct PendingLogin { std::string name; int world; int time; };
bool pending_logins(std::vector<PendingLogin>& out);
// A player's per-slot obj customisations (game.cpp appearance_overrides). Game thread.
struct ObjOverride { bool present = false; std::vector<int16_t> recolour, retexture; };
bool appearance_overrides(uintptr_t player, ObjOverride out[12]);
bool friend_list(bool ignores, std::vector<FriendEntry>& out);
Point scene_base();        // world tile of scene tile (0, 0) (top-level WorldView)
Point player_world();       // world tile (x, y) = scene base + scene tile; {-1, -1} if unavailable
Point walk_destination();
int player_plane();         // 0-3; -1 if unavailable
int scene_plane();          // WorldView plane (what RuneLite's WorldView.getPlane returns)   // tile the player was told to walk to; {-1, -1} when none
Point player_local_tile();  // tile within the loaded scene
Point player_fine();        // scene position in 1/128 tile units (RuneLite LocalPoint)

// Actor fields shared by Npc and Player (Actor ctor 0x474a2c; offsets from static RE of NPC_INFO
// 0x469778 / face tick 0x435240, docs/offsets.md "Actor").
struct ActorState {
    int animation = -1, animation_frame = -1;  // +0x4d8 sequence id, +0x4de frame (s16)
    int orientation = 0, current_orientation = 0;  // +0x3e0 target, +0x270 current (0..2047)
    int interacting = -1;  // RuneLite encoding: NPC index, or player index + 32768; -1 none
    int overhead_cycle = 0;  // +0x2f4 ticks left for the overhead text
    std::string overhead_text;  // +0x2d8
    // Pose animations copied from NpcType +0x58..+0x74 by NPC_INFO (players: from the appearance):
    // idle, idle rotate left/right, walk, walk 180/left/right, run.
    int pose[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // +0x280..+0x29c
    int pose_animation = -1, pose_frame = -1;      // +0x4b8 current pose sequence, +0x4be frame (s16)
    // First head bar (+0x388 list): last update's health and the bar type's width; -1 when none.
    int health_ratio = -1, health_scale = -1;
    // Hitmark slots (+0x330 vector of {type, amount, end cycle}, -1 = empty), up to 4.
    int hitsplats[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

struct Npc {
    int index = -1;  // server NPC index
    int id = -1;     // NpcType id
    std::string name;
    int fine_x = 0, fine_y = 0;    // scene position, 1/128 tile
    int world_x = 0, world_y = 0;  // world tile
    uintptr_t address = 0;         // Npc object; changes when the index is reused
    ActorState actor;
};
// RuneLite conventions: x/y horizontal (local units), z height (negative is up).
struct Camera {
    bool valid = false;
    int x = 0, y = 0, z = 0;
    int pitch = 0, yaw = 0;  // 0..16383
    int pitch_target = 0, yaw_target = 0;  // +0x8949c, +0x894a0: the camera eases towards these
    int focal_x = 0, focal_z = 0, focal_y = 0;  // +0x89500: smoothed player position (camera frame)
    int render_base_x = 0, render_base_y = 0, render_wv_same = 0;  // debug: WorldView at Client+0xc940
};
Camera camera();
// Writes the camera pitch (which 0) or yaw (which 1) target, as RuneLite's setCameraPitchTarget/
// setCameraYawTarget; the game eases the camera towards it. Yaw is masked to 14 bits. Game thread only.
bool set_camera_target(int which, int value);
// Writes the camera focal point (Client camera block +0x78/+0x7c/+0x80: x, height, y) on axis 0 x, 1 y,
// 2 height (RuneLite's setCameraFocalPointX/Y/Z). The game moves it towards the player each frame. Game thread only.
bool set_camera_focal(int axis, int value);
// The 3D viewport the scene renderer projects with (drawRenderable 0x72db3c): screen offset =
// r * zoom / depth, centre, clip rect, far plane. Reached as renderer = holder->vtbl[3](holder),
// holder = *(Client +0x90) + 0x1b0 (scene draw caller 0x457128), viewport = *(renderer +0x630c60).
// Game thread only. Unverified.
struct Viewport {
    bool valid = false;
    int zoom = 0, centre_x = 0, centre_y = 0, clip[4] = {}, far = 0;
    // The renderer's camera from the top-level Scene (WorldView +0x10): position +0x288/+0x28c/
    // +0x290 and sin/cos pitch, sin/cos yaw floats +0x418..+0x424 (x 65536).
    int scene_cam[3] = {}, trig[4] = {};
};
Viewport viewport();
// Queue a synth sound effect as SYNTH_SOUND (0x4ae508(Client +0x2610, id, loops, delay)). Game thread only.
bool play_sound_effect(int id, int loops, int delay);

int root_interface();
// Widget object for a component id (group << 16 | child), or 0 if its group isn't loaded.
uintptr_t widget(int component);
bool widget_hidden(uintptr_t widget);
struct WidgetGeometry { int x = 0, y = 0, width = 0, height = 0, parent = -1, group = -1; };  // x/y relative to parent
WidgetGeometry widget_geometry(uintptr_t widget);
int widget_child_count(int group);  // components in a loaded group (0 if not loaded)
std::string widget_text(uintptr_t widget);
// {name (CC_SETOPBASE, +0x2c0), target verb (CC_SETTARGETVERB, +0x330), ops... (CC_SETOP: vector of
// strings, size +0x2e0, data +0x2e8)} (handlers in 0x4e06dc; setOp 0x5dfae8).
std::vector<std::string> widget_strings(uintptr_t widget);
// Children created by CC_CREATE: Widget +0xb50 count, +0xb58 array of shared_ptr {ctrl, Widget*}.
int widget_dynamic_count(uintptr_t widget);
uintptr_t widget_dynamic(uintptr_t widget, int index);  // 0 if empty or out of range
struct WidgetItem { int id = -1, quantity = 0; };
WidgetItem widget_item(uintptr_t widget);
// scrollX, scrollY, scrollWidth, scrollHeight, modelZoom, rotationX, rotationY, rotationZ,
// opacity, modelType, modelId, animationId.
constexpr int kWidgetProps = 35;
// Writes a whitelisted int/bool widget field (CC_SET* offsets) and marks the widget dirty;
// index >= 0 addresses a CC_CREATE child. Game thread only.
bool widget_set_field(int component, int index, uint32_t offset, int value);
int widget_get_field(int component, int index, uint32_t offset);  // INT32_MIN if not readable
// Drag parent (Widget +0x318, CC_SETDRAGGABLE): component/index of the container; set: game thread only, parent < 0 clears.
bool widget_drag_parent(int component, int index, int* parent, int* parent_index);
bool widget_set_drag_parent(int component, int index, int parent, int parent_index);
// Re-runs the game's layout for a widget after position/size changes (what CC_SETPOSITION does).
bool widget_revalidate(int component, int index);
int widget_create_child(int component, int index, int type);
bool widget_delete_children(int component);
// Drops CC_CREATE children from index `count` on (Widget.setChildren truncation). Game thread.
bool widget_truncate_children(int component, int count);
void widget_props(uintptr_t widget, int32_t out[kWidgetProps]);  // item set by IF_SETOBJECT / CC_SETOBJECT
int widget_color(uintptr_t widget);        // 0xRRGGBB (IF_SETCOLOUR), text colour for text widgets
void widget_set_color(uintptr_t widget, int rgb);  // game thread only; marks the widget's area dirty
void widget_set_hidden(uintptr_t widget, bool hidden);  // game thread only, same
bool widget_set_text(uintptr_t widget, const std::string& text);  // game thread only, same (latin-1)
// which: -1 name (opbase), -2 target verb, 0..31 op (0-based; RuneLite setAction index). Game thread.
bool widget_set_string(uintptr_t widget, int which, const std::string& text);
int widget_flags(uintptr_t widget);  // +0x2b0: bit i+1 = op i shown (when no IF_SETEVENTS range covers it)
// The IF_SETEVENTS range covering a widget (component +0x04, CC_CREATE index +0x08): out = {flags, op mask};
// false when none covers it (the game then uses +0x2b0). Game thread.
bool widget_events(uintptr_t widget, int out[2]);
// All IF_SETEVENTS ranges, 5 ints each {component, start, end, flags, op mask}.
bool widget_event_ranges(std::vector<int32_t>& out);
void widget_set_flags(uintptr_t widget, int flags);  // game thread only
struct SubInterface { int component, group, type; };  // `group` is open inside `component`; type = IF_OPENSUB modal mode (node +8)
std::vector<SubInterface> open_sub_interfaces();
// IF_CLOSESUB for an open sub-interface (`0x462384`); false when it isn't open. Game thread only.
bool close_sub_interface(int component, int group, bool unload);
// What TRIGGER_ONDIALOGABORT does (`0x4405cc(client, root, 0x17)`): onDialogAbort over the root's tree. Game thread only.
bool trigger_dialog_abort();
// Wheel notches (desktop's sign: positive scrolls down) for the next logged-in cycle's interface pass. Any thread.
void add_wheel(int rotation, int x, int y);
// IF_OPENSUB's helper `0x462640`: opens `group` inside `component` with modal `mode`; false on bad ids. Game thread only.
bool open_sub_interface(int component, int group, int mode);
// Game-state writes (RuneLite's setAllWidgetsAreOpTargetable, Widget.setForcedPosition,
// PlayerComposition.setHash, Scene.removeGameObject/removeTile). Game thread only.
bool set_all_op_targetable(bool on);  // false when the flags hook isn't installed
int64_t widget_target_flags(int component, int index);  // 0x460dac as the menu builder sees it; -1 no widget
bool widget_force_position(int component, int index, int x, int y);  // -1, -1 clears
uint64_t appearance_set_hash(int index, const int32_t equipment[12], const int32_t colors[5], int npc_id);  // 0: none
uint64_t appearance_hash(int index);
bool scene_remove_game_object(int plane, int x, int y, uint64_t tag);
bool scene_remove_tile(int plane, int x, int y, bool remove);  // remove false: whether the tile is there
// RuneLite's drawInstanceMap: the game's minimap image of `plane` in desktop's layout (736x736 RGB
// for the 184-tile extended scene, 4 px per tile; the mobile 104-tile scene fills the middle 416 px
// at 160,160), resampled when the minimap zoom isn't 4. The game's buffer is restored to the plane it showed. Game thread only.
constexpr int kInstanceMapSize = 736;
bool draw_minimap(int plane, std::vector<int32_t>& out);

constexpr int kSkillCount = 25;
struct Skills {
    int32_t current[kSkillCount] = {}, real[kSkillCount] = {}, xp[kSkillCount] = {};  // current = boosted
    bool operator==(const Skills& o) const {
        for (int i = 0; i < kSkillCount; i++)
            if (current[i] != o.current[i] || real[i] != o.real[i] || xp[i] != o.xp[i]) return false;
        return true;
    }
};
Skills skills();

struct Inventory {
    int id = -1;                       // InventoryID / inv id (93 = inventory, 94 = worn, 95 = bank)
    std::vector<int32_t> ids, quantities;  // per slot; id -1 = empty
};
// Every inventory the client holds. Game thread only.
std::vector<Inventory> read_inventories();
// The inventories as they were when chat message `id` was created (Message constructor hook);
// each id can be taken once. False if the hook didn't record it.
bool take_message_inventories(int id, std::vector<Inventory>& out);

struct ChatLine {
    int id = -1, type = 0, cycle = 0;
    std::string name, sender, text;
};
int next_message_id();
// Looks up a chat message by id (the game keeps the last 100 per type). Game thread only.
bool read_message(int id, ChatLine& out);
// Replaces a message's text (string assign 0x3f1498 on Message +0xa0). Game thread only.
bool message_set_text(int id, const std::string& text);

struct PlayerInfo {
    int index = -1;
    bool local = false;
    std::string name;
    int fine_x = 0, fine_y = 0, world_x = 0, world_y = 0;
    uintptr_t address = 0;
    ActorState actor;
    // Appearance (PLAYER_INFO mask 0x10, decoder 0x4762d4). Equipment uses RuneLite's encoding
    // (0 empty, kit + 0x100, item + 0x800); has_appearance is false before the first block.
    bool has_appearance = false;
    int equipment[12] = {}, kits[12] = {}, colors[5] = {};
    int gender = 0, npc_id = -1;
    int combat_level = 0, skull = -1, prayer = -1, team = 0;
};
// Walks the top-level WorldView's player table. Game thread only.
std::vector<PlayerInfo> read_players();

// Walks the top-level WorldView's NPC table. Game thread only (the table is rebuilt by
// NPC_INFO); other threads use client_thread::npcs().
std::vector<Npc> read_npcs();
// Health bar types on live actors' head bars (game.cpp health_bar_types). Game thread only.
struct HealthBarType { uintptr_t address; int id, front_sprite, back_sprite, width, padding; };
std::vector<HealthBarType> health_bar_types();
// Writes the width padding of the in-use type `id`; false if no live actor shows one.
bool health_bar_set_padding(int id, int padding);

// Client scripts (CS2). Runs a script synchronously in the game's VM, the way the client runs
// widget listeners; args[0] is the script id. Game thread only, and not from inside a script.
struct ScriptArg {
    bool is_string = false;
    int i = 0;
    std::string s;
};
struct ScriptResult {
    std::vector<int32_t> ints;               // int stack after the script returned
    std::vector<std::string> objects;        // object stack (strings; "" for other types)
    std::vector<bool> object_is_string;
};
bool run_script(const std::vector<ScriptArg>& args, ScriptResult& out, std::string& error);
// The VM's live stacks, for ScriptPreFired/PostFired listeners (game thread). The int stack is
// copied whole (size entries); set_script_int_stack writes values back and sets the size.
bool script_stacks(ScriptResult& out);
bool set_script_int_stack(const int32_t* values, int size);
// One int stack slot (depth 0 = top; writes *set first when non-null) / the top object if a string.
bool script_int_top(int depth, int32_t& value, const int32_t* set);
bool script_string_top(std::string& out);
// Script locals: frame 0 = the running frame, else a saved call frame (script_hooks::return_frame).
// Int arrays by desktop array id (object local slot); set_script_array keeps the array's length.
bool script_array(uintptr_t frame, int slot, std::vector<int32_t>& out);
bool set_script_array(uintptr_t frame, int slot, const int32_t* values, int count);
// Reads int local `index` (writes *set first when non-null).
bool script_int_local(uintptr_t frame, int index, int32_t& value, const int32_t* set);
// Replaces the string at object stack slot `index` (0 = bottom) through the game's string assign
// 0x3f1498, as CC_SETTEXT stores text. False when the slot isn't a string. Game thread only.
bool set_script_string(int index, const std::string& text);
// True when the top of the running script's object stack is a string starting with `prefix`.
bool script_top_string_starts_with(const std::string& prefix);
// A ScriptEvent's arguments (+0x08 argc, +0x10 0x28-byte variants); non-int/string args come
// back as an empty string with is_string = false and i = -1.
std::vector<ScriptArg> script_event_args(uintptr_t ev);
bool script_event_source(uintptr_t ev, int* component, int* index);
bool script_active_widget(bool dot, int* component, int* index);
int text_width(int font_id, const std::string& text);
int font_ascent(int font_id);
bool widget_ref(uintptr_t w, int* component, int* index);

// One tuple of a DB row's column, through the script VM's db_getfield (7502): values in column-type
// order, each an int (kind 0), long (1) or string (2). False if the row/column doesn't exist.
struct DbValue {
    int kind = 0;
    int64_t i = 0;
    std::string s;
};
bool db_field(int row, int column, int tuple, std::vector<DbValue>& out);
// The table a DB row belongs to (DBRowType +0x30), or -1 if the row doesn't exist. Game thread.
int db_row_table(int row);
// Number of tuples in a DB row column (db_getfieldcount, 7503); -1 if the row/column is missing.
int db_field_count(int row, int column);

// Item (obj) definitions, from the game's definition cache (loads on demand). Game thread only.
struct ItemDef {
    int id = -1;
    std::string name;
    int price = 0, weight = 0, category = -1;
    bool stackable = false, members = false, tradeable_guess = false;
    int linked_note = -1, note_template = -1, placeholder = -1, placeholder_template = -1;
    std::string inventory_actions[5];
    int zoom2d = 0, xan2d = 0, yan2d = 0, zan2d = 0;  // inventory icon view (CC_SETOBJECT copies them)
    int inventory_model = -1, ambient = 0, contrast = 0;
    // Recolours / retextures (opcodes 40/41): find, replace pairs.
    std::vector<int16_t> recolor_from, recolor_to, retexture_from, retexture_to;
};
bool item_def(int id, ItemDef& out);
// Item icon as the inventory draws it (createItemSprite 0x63ff9c): 36x32 ARGB pixels, 0 = transparent.
// border 0/1/2, shadow colour (0 none), text_mode 0 none / 1 quantity, noted. Game thread only.
// False while the model isn't loaded yet (retry later) or for an unknown id.
// Sprite by id (frame 0) from the game's sprite cache; false while not loaded. Game thread only.
bool sprite(int id, std::vector<uint32_t>& pixels, int& width, int& height);
// Frame 0's placement: {width, height, x offset, y offset, full width, full height} (Sprite +0x18..+0x2c;
// the pixels are the trimmed width x height block at the offsets in the full canvas). Game thread only.
bool sprite_info(int id, int out[6]);
// The game's own loaded sprite arrays (Client loader 0x412e78): which 0 = crosses (Client +0x2510, Sprite
// records 0x30 apart), 1 = mapscenes (Client +0x24b8, IndexedSprite records 0x48 apart). Per frame:
// {w, h, x offset, y offset, canvas w, canvas h, palette length n, w*h pixels (ARGB, or palette indices
// when n > 0), n palette ARGB}, after a leading frame count. Game thread only.
bool sprite_array(int which, std::vector<int32_t>& out);
// The texture provider (Client +0x4710, vtable 0xa75748, built in loader state 10 by 0x6d3bec).
// Per texture 8 ints {present, average colour (HSL), flag +0x34, sprite group, animation direction,
// animation speed, loaded, pixel count}. Game thread only.
bool textures(std::vector<int32_t>& out);
// A texture's size*size pixels: load = false reads them only if loaded; load = true goes through the
// provider's getTexturePixels (vtable slot 2, 0x6d4930), which loads and LRU-evicts as the game does.
bool texture_pixels(int id, bool load, std::vector<int32_t>& out);
// The provider's brightness (+0x38, double; desktop's Rasterizer3D brightness). NaN if absent.
// The scene's skybox colour: S = *(Client +0x90) (the scene renderer's settings), float RGBA 0..1 at
// S +0x90..+0x9c, written by the game's setter 0x6b083c(S, r, g, b, a); alpha 0 (the default) = no colour
// set. As ARGB with alpha 0 or 0xff. Game thread only.
bool skybox_colour(int& argb);
bool set_skybox_colour(int argb);
double texture_brightness();
bool set_texture_brightness(double b);
// Writes a same-size sprite's pixels into the game's cached copy (overrides). Game thread only.
bool sprite_write(int id, const uint32_t* pixels, int width, int height);
bool item_sprite(int id, int quantity, int border, int shadow, int text_mode, bool noted,
                 std::vector<uint32_t>& pixels, int& width, int& height);

// NPC definitions (NpcType), from the game's cache. Game thread only.
struct NpcDef {
    int id = -1;
    std::string name;
    int size = 1, combat_level = 0;
    bool minimap_visible = true, interactable = true;
    std::string actions[5];  // current text (varp/varbit-conditional ops resolved by the game)
    int varbit = -1, varp = -1;
    std::vector<int32_t> transforms;  // multi-NPC ids, last = default (-1 = none); empty if none
    std::vector<int32_t> models, chathead_models;
    std::vector<int16_t> recolor_from, recolor_to;
    int width_scale = 128, height_scale = 128;
    int stats[6] = {1, 1, 1, 1, 1, 1};  // attack, defence, strength, hitpoints, ranged, magic (ops 74-79)
};
bool npc_def(int id, NpcDef& out);
// Vertices {x, y, z}* of an Npc/Player's posed model this frame (unrotated, local units, y
// negative = up) and its height. Game thread only.
void drawlog_cycle();  // debug.osrsrl.drawlog: which Npc objects the renderer draws (game thread)
bool actor_model(uintptr_t actor, std::vector<int32_t>& xyz, int& height);
// Full posed mesh (vertices, faces, lit face colours) of an Npc/Player; layout in game.cpp
// read_mesh. Game thread only.
bool actor_mesh(uintptr_t actor, std::vector<int32_t>& out);
// Model id's ModelData from the game's model store (a fresh decode), packed as actor_mesh (game.cpp
// pack_mesh); false if the model isn't loaded. Game thread only.
bool model_data(int id, std::vector<int32_t>& out);
// Client.applyTransformations: model id lit with the defaults, its vertices replaced by xyz (x[n], y[n], z[n]),
// posed by classic-frame sequence a at frame_a (and b at frame_b, -1 for none) through the game's animate;
// packed as actor_mesh. False for an unloaded model, a vertex count other than n, or a frame out of range.
// Game thread only.
bool pose_model(int id, const std::vector<int32_t>& xyz, int seq_a, int frame_a, int seq_b, int frame_b, std::vector<int32_t>& out);
// The posed model's height (max -y over its vertices, as the desktop Model.height that
// Actor.getLogicalHeight reports), or -1. Game thread only.
int actor_height(uintptr_t actor);
// A scene object's model vertices this frame (layer = SceneLayer, tag = the object's tag; same
// vertex convention as actor_model, wall decoration offsets applied), with its local position,
// absolute height and orientation. Game thread only.
struct ObjectModel {
    int x = 0, y = 0, height = 0, orientation = 0, model_height = 0;
    std::vector<int32_t> xyz;
};
bool object_model(int plane, int sx, int sy, int layer, uint64_t tag, ObjectModel& out);
// An animated loc's renderable (RuneLite DynamicObject) in renderable slot `slot` (0, or 1 for a wall's or wall
// decoration's second part) of a scene object: f = {loc id, shape, orientation, plane, scene x, scene y, seq id,
// last cycle, frame, frame cycle}, or with `mesh` its model this cycle (packed as actor_mesh). False when that
// renderable is not one. Game thread only.
constexpr int kDynamicObjectFields = 10;
bool dynamic_object(int plane, int sx, int sy, int layer, uint64_t tag, int slot, int32_t f[kDynamicObjectFields],
                    std::vector<int32_t>* mesh);
// The live Npc*/Player* for an index at this moment (0 if not active). Game thread only.
uintptr_t live_npc(int index);
uintptr_t live_player(int index);

// Loc (scene object) definitions (LocType), from the game's cache. Game thread only.
struct LocDef {
    int id = -1;
    std::string name;
    int size_x = 1, size_y = 1;
    int varbit = -1, varp = -1;
    std::vector<int32_t> transforms;  // multiloc ids, last = default (-1 = none); empty if not a multiloc
    std::string actions[5];
};
bool loc_def(int id, LocDef& out);

// Ground heights of the top-level scene: heights[(plane * (size_x + 1) + x) * (size_y + 1) + y]
// (corner vertices, negative = up), settings[(plane * size_x + x) * size_y + y] (bit 2 = bridge).
struct SceneHeights {
    int planes = 0, size_x = 0, size_y = 0;
    std::vector<int32_t> heights;
    std::vector<uint8_t> settings;
};
bool scene_heights(SceneHeights& out);  // game thread only

// Enums (cache config type), from the game's cache. Game thread only.
struct EnumDef {
    int key_type = 0, value_type = 0, default_int = 0;
    std::string default_string;
    std::vector<int32_t> keys, int_values;
    std::vector<std::string> string_values;  // when value_type == 's'
};
bool enum_def(int id, EnumDef& out);
// Definition params (opcode 249). kind: 0 obj, 1 npc, 2 loc. Game thread only.
struct TypeParam { bool is_string = false; int int_value = 0; std::string string_value; };
bool type_param(int kind, int id, int key, TypeParam& out);
// Every param key of that definition (empty when it has none); false if the definition is missing.
bool type_param_keys(int kind, int id, std::vector<int32_t>& keys);
// Sub-op texts under an op of an npc (kind 1) or loc (2); present = the op has a sub-op list.
bool sub_ops(int kind, int id, int op, std::vector<std::string>& out, bool& present);

// Menus (long-press menu). Entries are what the game shows, top-level first as stored.
struct MenuEntry {
    std::string option, target;
    int32_t raw[10] = {};  // entry +0x120..+0x147: parameters (meaning per entry kind; see offsets.md)
    uint8_t kind = 0;      // entry +0x14f
    bool has_children = false;
    uintptr_t address = 0;
};
uintptr_t menu_object();  // the menu model (MenuManager +0x140), 0 if unavailable
bool menu_open();
// Closes the open menu as the game does when a tap lands outside it. Game thread only.
bool menu_close();
// Reorders the open menu: new top-level position i gets the entry at old position order[i] (0 = top).
// Permutes the menu's top-level nodes (as the game's sort does); rebuilds the display if one is shown.
// Game thread only.
bool menu_reorder(const std::vector<int>& order);
// Builds the game's long-press menu at (x, y) in UI canvas units without showing it (no menu may be
// open); read it with read_menu(), then menu_discard(). Game thread only.
bool menu_build_at(int x, int y);
// Scene picking for menu builds: pick_at asks the next frame to ray-pick (x, y) in screen pixels
// (as a touch down does); pick_ready once that pick is done, so menu_build_at sees its hits.
void pick_at(int screen_x, int screen_y);
bool pick_ready();
void menu_discard();
// The point the model was last built at (MenuManager +0x2a8/+0x2ac), open or not.
bool menu_point_raw(int& x, int& y);
// The client-side varp table (RuneLite getVarps()).
std::vector<int32_t> varps();
// The server's varp values (the table getServerVarpValue reads), whole.
std::vector<int32_t> server_varps();
int weight();      // kg (UPDATE_RUNWEIGHT)
int world();       // Client +0x3174 (CS2 map_world 3318; jav_config param 12)
int world_flags(); // Client +0x2f64 (CS2 worldflags 3324)
// A world as the client's world list (slr.ws) has it; listed = false when only the client fields
// (id +0x3174, flags +0x2f64, host +0x3098) describe it (a world the list lacks).
struct WorldEntry { int id = 0, flags = 0, players = 0, location = 0, index = 0; bool listed = false; std::string host, activity; };
// Reads the game's world list: game thread only (the login screen's switcher reloads and frees
// it from doCycle). Other threads use client_thread::world_entry, the per-cycle cached copy.
bool world_entry(int id, WorldEntry& out);
// The world list's {entries pointer, count}, to notice a reload. Game thread only.
std::pair<uintptr_t, uint64_t> world_list_key();
// REBUILD_REGION state: whether the scene is an instance, and the [level][x][z] template chunks
// packed as desktop (level << 24 | zoneX << 14 | zoneZ << 3 | rotation << 1; -1 = none).
bool instance_templates(bool& instanced, int32_t out[4 * 13 * 13]);
// The 8 GE slots: {state (0 empty, 1 adding, 2 stable, 5 finished; -1 none), type (0 buy, 1 sell),
// item, price, total, quantity done, gold spent} each (Client +0x4185c0 slot objects).
bool ge_offers(int64_t out[8 * 7]);
// Raw bytes of GE slot object `slot` (probe); returns its address (0 if none).
uintptr_t ge_slot_raw(int slot, uint8_t out[0x28]);

// Widget drag (docs/offsets.md "Widget drag"). out = {dragged component, dragged CC_CREATE index,
// dragged-on component, index, dragging (past the dead zone/time), drag cycles, touch state};
// components -1 when none. False when no widget is pressed for a drag.
bool drag_state(int out[7]);
// setDraggedOnWidget(null): clears the dragged-on Widget* so the release does no swap.
void drag_clear_target();
int touch_state();

// Friends chat and clan channels (docs/offsets.md "Friends chat / clan channels").
struct ChatMember { std::string name; int world = 0; int rank = 0; };
struct ChatChannel {
    std::string name, owner;
    int my_rank = 0, kick_rank = 0, talk_rank = 0;
    std::vector<ChatMember> members;
};
bool friends_chat(ChatChannel& out);
// which = -1: the listened (guest) clan channel; 0..: the affined channel of that clan id.
bool clan_channel(int which, ChatChannel& out);
// Inline hook on the menu-open check 0x58da0c; `cb` runs (game thread) when it is called from the
// drag block of the cycle (return address 0x41ffe8), i.e. once per cycle while a widget is held,
// before the release is processed.
void install_drag_hook(void (*cb)());
// Sound effects (SYNTH_SOUND packet `0x442944`, CS2 sound_synth `0x4ea718`, our play_sound_effect)
// all go through the queue function `0x4ae508(sys, id, loops, delay)`. The hook sits after its
// loops == 0 early-out; `cb` returns true to drop the sound (RuneLite's SoundEffectPlayed.consume).
void install_sound_hook(bool (*cb)(int id, int loops, int delay));
// Area sounds (SOUND_AREA zone prot, actor/loc animation frame sounds) go through `0x4ae650`;
// range 0 means a plain sound effect (animation sounds of the local player). Source: for animation
// sounds, the NPC/player at the animated thing's fine position (kind 1/2), else none (0).
struct AreaSound {
    int id = 0, scene_x = 0, scene_y = 0, plane = 0, range = 0, drop_off = 0, loops = 0, delay = 0;
    bool from_animation = false;
    int source_kind = 0, source_index = -1;
};
void install_area_sound_hook(bool (*cb)(const AreaSound&));
// 3D viewport projection: {zoom, x, y, width, height} in UI canvas pixels (Client +0x46d4, +0x46e0..+0x46ec,
// computed by 0x5561e8 from the CS2 viewport zoom/fov settings; RuneLite's getScale/viewport).
bool viewport_zoom(int out[5]);
int run_energy();  // 0..10000 (UPDATE_RUNENERGY)
// The game's addChatMessage (what MESSAGE_GAME calls). Game thread only.
bool add_chat_message(int type, const std::string& name, const std::string& text);
// Removes a message from its chat channel's ring (chatbox lines), by type and message id;
// chat_changed makes the chatbox rebuild. Game thread only.
bool chat_remove_message(int type, int id);
void chat_changed();
// Applies the members flag of the world flags the game received (setWorld's step the jav_config
// start skips). Game thread, per cycle; no-op when they agree.
void sync_members_world();
// Idle logout timeout in minutes (the game's AFKTimeout setting); -1 if unknown. Game thread only.
// World map (Client +0x418660, the object the CS2 worldmap_* opcodes 6600-6699 use; handler 0x4c3cf8).
struct WorldMapState {
    bool present = false;
    bool loaded = false;  // renderer loaded (worldmap_isloaded)
    float zoom = 0;       // pixels per tile (1, 1.5, 2, 3, 4, 8)
    int x = -1, y = -1;   // display position (worldmap_getdisplayposition)
    int map = -1;         // current map area id (worldmap_getcurrentmap)
};
WorldMapState world_map_state();
// worldmap_coordinmap: -1 = the map area isn't known (not loaded), else 0/1. Game thread.
int world_map_contains(int map, int level, int x, int z);
// worldmap_jumptosourcecoord (smooth). Game thread.
bool world_map_jump(int level, int x, int z);
// The current map's region grid (rows x cols) and its icons, {row, col, element, level, x, y} each;
// false when the map isn't loaded. Game thread.
bool world_map_icons(int& rows, int& cols, std::vector<int>& out);
// Map element config: category and sprite. False for an unknown id.
bool map_element(int id, int& category, int& sprite);
int afk_timeout_minutes();
bool set_afk_timeout_minutes(int minutes);
// Every world list entry, list order (game thread only; client_thread::world_list elsewhere).
bool world_list(std::vector<WorldEntry>& out);
// The connected world's host (Client +0x3098) and game ports (+0x30b0/+0x30b4).
bool connection(std::string& host, int ports[2]);
// Writes the SERVER_TICK_END count (Client +0x3058) that server_tick() reads.
bool set_server_tick(int tick);
// Client int fields by index (game.cpp kClientInts: 0 oculus orb state, 1/2 orb focal point X/Y, 3 chat
// cycle, 4 changed-skills count, 5 orb/free camera speed, 6 pitch relaxer).
bool client_int(int which, int& out);
bool set_client_int(int which, int value);
// The touch camera's drag sensitivity (settings +0x82c).
bool camera_sensitivity(float& out);
bool set_camera_sensitivity(float v);
// Music volume 0-255 (Client +0x2f58); set also applies it to the playing songs.
int music_volume();
bool set_music_volume(int v);
// The music player's playing songs: {group id, jingle}.
bool midi_requests(std::vector<std::pair<int, bool>>& out);
// Clears the camera shake flags (as CAM_RESET).
void clear_camera_shake();
// After a camera-drag cycle (touch state 8): the inverted axes' steps and deltas turned the other way.
void invert_camera_drag(bool pitch, bool yaw);
// Every varp as a long (long table where the varp is long, else the int table); client or server copy.
bool varps_long(bool server, std::vector<int64_t>& out);
bool set_varp_long(int id, int64_t v);
// The cross-world message id ring (100) and its next index.
bool cross_world_ids(int64_t out[100], int& index);
// Login screen page and input field (globals).
int login_index();
int login_field();
// Minimap zoom (2..8, set through the game's setter) and its zoom-input flag.
int minimap_zoom();
bool set_minimap_zoom(int zoom);
int minimap_zoom_input();
bool set_minimap_zoom_input(bool on);
// Long-press menu scroll offset (pixels) of the top-level menu.
int menu_scroll();
bool set_menu_scroll(int v);
// Sets the widget target state's active byte (Client.setWidgetSelected(true)).
bool begin_target_mode();
// Client varcs (VarClientInt/VarClientStr); false when unset. Game thread only.
bool varc_int(int id, int& out);
bool varc_string(int id, std::string& out);
// Every int (type 1) and string (type 2) varc; other types are left out. Game thread only.
// Game caches (0 obj, 1 loc, 2 seq, 3 item model, 4 item sprite, 5 health bar): entry count (-1 bad
// id) and the game's own clear. Game thread, between cycles.
// NPC model overrides (head = chathead +0x750, else body +0x758); false when the NPC has none.
struct NpcOverrides { std::vector<int32_t> models; std::vector<int16_t> recolours, retextures; bool use_local_player = false; };
bool npc_overrides(int index, bool head, NpcOverrides& out);
// The NPC's head icon archive ids and sprite indices (override object, else its NpcType's); false if none.
bool npc_head_icons(int index, std::vector<int32_t>& archives, std::vector<int32_t>& sprites);
long cache_size(int which);
// js5 archives by id (0..25): a group's file ids; a file's bytes (getFile 0x6be3dc). Game thread only.
bool archive_file_ids(int id, int group, std::vector<int32_t>& out);
bool archive_file(int id, int group, int file, std::vector<uint8_t>& out);
bool cache_reset(int which);
struct VarcEntry { int id = 0, type = 0, int_value = 0; std::string string_value; };
bool varc_entries(std::vector<VarcEntry>& out);
// Client-side writes through the game's own script-VM setters. Game thread only.
struct VarbitDef { int varp = -1, lsb = 0, msb = 0; };
bool varbit_def(int id, VarbitDef& out);
bool set_varbit(int id, int value);
bool set_varp(int id, int value);  // client varps table only (RuneLite queueChangedVarp semantics)
bool set_varc_int(int id, int value);
bool set_varc_string(int id, const std::string& value);
// Where the open menu was requested (UI-canvas units); false when no menu is shown.
bool menu_point(int& x, int& y);
std::vector<MenuEntry> read_menu();  // game thread only
std::vector<MenuEntry> read_menu(uintptr_t menu);  // a given menu model (the one a tap selects in)
bool menu_entry_for_path(uintptr_t menu, const uint64_t* path, size_t len, MenuEntry& out);
// Rewrites an open menu entry's option/target (null = keep); index as in read_menu().
bool menu_set_text(size_t index, const std::string* option, const std::string* target);
uintptr_t menu_vtable();  // menu model vtable; slot 1 = select
uintptr_t menu_select();  // expected value of that slot

// Scene objects of the top-level scene, all planes (docs/offsets.md "Scene objects"). Game objects
// are listed once, at their south-west tile, with their size in tiles. Game thread only.
enum SceneLayer : uint8_t { kWall = 0, kWallDecoration = 1, kGameObject = 2, kGroundDecoration = 3, kItemPile = 4 };
struct SceneObject {
    uint8_t layer, plane, x, y, size_x = 1, size_y = 1;
    int32_t id;
    uint64_t tag;  // the game's object tag: scene x bits 0-6, y bits 7-13, id bits 20-35
};
std::vector<SceneObject> scene_objects();
struct GroundItem {
    uint8_t plane, x, y;
    int32_t id, quantity;
    int32_t visible_tick, despawn_tick, ownership;  // server ticks (Client +0x3058 clock); ownership 0 none, 1 self, 2 other, 3 group
    bool is_private;
};
std::vector<GroundItem> ground_items();  // per tile in stack order (top first)

// Ambient (loc) sounds, RuneLite's AmbientSoundEffect: the top-level WorldView's vector of
// {ctrl, ObjectSound*} at +0xb78 (begin/end/cap), filled by 0x4b1424 (LOC_ADD and loads; LocType
// +0x1a8/+0x1b0 sound {id, range, ..}, +0x1b8/+0x1c0 random sounds {+0x08 count, +0x10 int* ids,
// +0x18/+0x1c min/max delay}). ObjectSound: +0x20 sound struct (id at +0), +0x38 random sounds,
// +0x40 plane, +0x60..+0x6c bounds (local units). 0x4b065c(wv) moves every entry to the pending
// vector +0xb90, which the per-cycle updater 0x4b1e90 stops and frees. Game thread only.
struct AmbientSound {
    uintptr_t obj;  // identity (never dereferenced from Java)
    int32_t id, plane;
    std::vector<int32_t> background;  // empty = none
};
// A projectile in the client's list (Client +0xcb30). Source/target are world tiles; indexes use the
// packet encoding (NPC index + 1, -(player index + 1), 0 none). x/y/z: current scene position in
// local units (z negative up), 0 until the projectile first moves (start cycle).
struct ProjectileInfo {
    uintptr_t obj = 0;
    int id = -1, source_level = 0, source_x = 0, source_y = 0, start_height = 0, source_index = 0;
    int target_level = 0, target_x = 0, target_y = 0, end_height = 0, target_index = 0;
    int start_cycle = 0, end_cycle = 0, slope = 0, start_pos = 0, orientation = 0;
    int animation = -1, animation_frame = 0;
    double x = 0, y = 0, z = 0;
};
std::vector<ProjectileInfo> projectiles();  // oldest first; game thread only
bool projectile_set_end_cycle(uintptr_t obj, int cycle);
// Spawns a projectile as PROJANIM_SPECIFIC does from its 0x3c-byte struct (ProjectileInfo's fields from
// source_level to start_pos, then id); returns the new object (0 when logged out). Game thread only.
uintptr_t projectile_create(const int32_t (&s)[15]);
// Ends a graphics object's animation as the game does when it runs out (clears its sequence);
// the game removes it on its next walk of the list. Game thread only.
bool graphics_object_finish(uintptr_t obj);
// A map spotanim in the top-level WorldView's graphics object list (+0x160). x/y local units.
struct GraphicsObjectInfo {
    uintptr_t obj = 0;
    int id = -1, level = 0, x = 0, y = 0, z = 0, start_cycle = 0;
    int32_t raw[24] = {};  // obj +0x240..+0x2a0, for discovery
};
std::vector<GraphicsObjectInfo> graphics_objects();  // game thread only  // game thread only; false if obj isn't listed
std::vector<AmbientSound> ambient_sounds();
bool ambient_clear();
// Moves one live entry to the pending vector (as 0x4b065c does for all of them). Game thread.
bool ambient_remove(uintptr_t obj);
// Moves an entry cleared in this cycle (still in the pending vector) back into the live vector.
bool ambient_restore(uintptr_t obj);

// Collision flags (RuneLite CollisionDataFlag values): Scene (WorldView +0x10) +0x2f8 -> per plane
// 0x28-byte maps {int x offset, int y offset, ..., +0x20 columns (0x18 each, int* at +0x10)},
// flags[x - xoff][y - yoff] (0x5ed6dc clears floor-decoration 0x40000; 0x5ecf7c walls 0x80/0x8/..).
// Writes size*size flags per plane (scene x major) into out; false when unreadable. Game thread.
bool collision_flags(int plane, int size, std::vector<int32_t>& out);

// Mod icons (font <img=N> table): game_index >= 0 keeps the game's icon, else a palette sprite
// (index 0 transparent, palette ARGB with alpha 0xff). Applied by mod_icons_cycle on the game thread.
struct ModIcon {
    int game_index = -1;
    int w = 0, h = 0, ox = 0, oy = 0, full_w = 0, full_h = 0;
    std::vector<uint8_t> pixels;
    std::vector<uint32_t> palette;
};
void set_mod_icons(std::vector<ModIcon> icons);
std::vector<int32_t> mod_icon_sizes();  // {fullW, fullH} per game icon; empty before load
void mod_icons_cycle();
}  // namespace game
