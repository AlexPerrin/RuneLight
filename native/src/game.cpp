#include "game.h"

#include <elf.h>
#include <link.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>

#include "got_hook.h"
#include "inline_hook.h"
#include "log.h"

namespace game {
void install_message_hook();
void install_scene_load_hook();
void install_minimap_hook();
void install_fire_hook();
void install_wheel_hook();
void install_state_write_hooks();
namespace {

// Offsets are ELF virtual addresses (as in readelf/objdump; Ghidra shows them +0x100000)
// or struct field offsets. Found with tools/agent.py; see docs/offsets.md.
struct Build {
    const char* name;
    uint8_t build_id[20];
    uintptr_t client_ptr;   // global Client* (558 code refs)
    uintptr_t cycle;        // global int, written only by Client vtable slot 5 (0x411640)
    uint32_t f_game_state;  // Client::gameState
    uintptr_t client_vtable;  // Client vtable; slot 5 = doCycle (only reached through the vtable)
    uintptr_t do_cycle;       // expected value of that slot
    uintptr_t varps;          // {?, u32 count, int* values, ?}: client-side varps (setVarp 0x5d06f8)
    uintptr_t server_varps;   // same layout: last values sent by the server
    uint32_t f_server_tick;      // Client: count of SERVER_TICK_END packets (dispatcher case 55)
    uint32_t f_changed_varps;    // Client: int[32] ring of varp ids changed by VARP_SMALL/LARGE
    uint32_t f_changed_count;    // Client: total writes into that ring
    uint32_t f_dest_x, f_dest_y;    // Client: walk destination tile (-1 when none)
    uint32_t f_plane;               // Client: local player's plane
    uint32_t f_local_x, f_local_y;  // Client: local player's scene tile (world - scene base)
    uint32_t f_fine_x, f_fine_y;    // Client: local player's scene position, 128 units per tile
    uintptr_t get_varbit;           // int getVarbit(int id): looks up the varbit def, reads its varp bits
    uint32_t f_world_view;          // Client: WorldView* (NPC_INFO handlers pass Client[0x192a])
    uint32_t wv_plane;                             // WorldView: current plane (then size x, y = 104)
    uint32_t wv_base_x, wv_base_y;                 // WorldView: world tile of scene (0, 0)
    uint32_t wv_npc_buckets, wv_npc_bucket_count;  // WorldView: hash map index -> Npc*
    uint32_t wv_npc_indices, wv_npc_count;         // WorldView: int[] of active NPC indices
    uint32_t wv_player_buckets, wv_player_bucket_count;  // WorldView: hash map index -> Player*
    uint32_t f_local_player_index;                 // Client: local player's index in that map
    uint32_t player_name;                          // Player: pointer to a 24-byte string (name)
    uint32_t f_gpi;  // Client: player-info state; int count, then int[count] high-res player indices
    uint32_t npc_def, npc_fine_x, npc_fine_y;      // Npc: NpcType*, scene position (1/128 tile)
    uint32_t def_id, def_name;                     // NpcType: id, name (24-byte string)
    uint32_t f_camera;          // Client: camera x, height, y, pitch, yaw (14-bit angles: 16384 per turn)
    uint32_t f_interfaces;      // Client: InterfaceManager* (group table +0x6888 count, +0x6890 groups)
    uint32_t f_root_interface;  // Client: top-level interface id (IF_OPENTOP)
    uint32_t f_subs_buckets, f_subs_count;  // Client: open sub-interfaces, map id -> {id, group, ...}
    uint32_t w_hidden, w_text;  // Widget: bool hidden (IF_SETHIDE), text (IF_SETTEXT)
    // Widget geometry, from the script interpreter's CC_GETX/Y/WIDTH/HEIGHT/LAYER (0x4e5960):
    uint32_t w_x, w_y, w_width, w_height, w_parent, w_group;
    uint32_t f_skill_current, f_skill_real, f_skill_xp;  // Client: int[25] each (UPDATE_STAT, case 2)
    uintptr_t inventories;      // global map inv id -> {ids vector +0x08, quantities vector +0x20, next +0x38}
    uintptr_t next_message_id;  // global int: id the next chat message gets (0x3f0a04 increments it)
    uintptr_t messages_by_id;   // global hash map id -> Message* {?, buckets, u32 count}
    // Script VM (static object; int locals vector +0x08, object locals +0x38, int stack +0x50 with
    // size +0xff0, object stack of 0x28-byte variants +0xff8 with size +0xac38, running flag +0x04)
    // and runScriptEvent(vm, ScriptEvent*, maxOps).
    uintptr_t script_vm, run_script_event;
    // shared_ptr<ObjType> getObjType(int id) (result via x8): definition cache lookup/load (OC_* ops).
    uintptr_t get_obj_type;
    uintptr_t get_npc_type;  // shared_ptr<NpcType> getNpcType(int id), same convention (NPC_INFO 0x469778)
    uintptr_t op_text;       // const string* opText(opVector*, int index): picks varp/varbit overrides (0x65c798)
    // Scene (WorldView +0x10): +0x944 planes, +0x948/+0x94c size x/y, +0x3d0 int heights
    // [planes][sx+1][sy+1]; tileSettings(scene, plane, x, y) (bit 2 = bridge).
    uintptr_t tile_settings;
    uintptr_t get_enum_type;  // shared_ptr<EnumType> getEnumType(int id) (ENUM ops 3400-3411)
    // Menus: Client field holding the MenuManager* (built by the Client ctor 0x40c9b4 with
    // 0x58dab8); the menu model's vtable, whose slot 1 = select(menu, path, x, y, flag) (0x515854); ELF vaddrs.
    uint32_t f_menu_manager;
    uintptr_t menu_vtable, menu_select;
    // const shared_ptr<LocType>* getLocType(int id): cache lookup/load (pointer to the cached
    // {ctrl, LocType*}; the world menu builder 0x533a1c uses it for loc entries).
    uintptr_t get_loc_type;
    uintptr_t add_chat_message;  // addChatMessage(client, type, const string* name, const string* text) (MESSAGE_GAME)
    // Client-side setters, from the script VM (0x4dd08c): setVarbit(id, value) (op 27), and on the
    // varc store (Client +0x417b68) setVarcInt(store, id, int) (op 43) / setVarcString(store, id,
    // const variant*) (op 49; 0x28-byte variant, index at +0x20, 2 = string).
    uintptr_t set_varbit, set_varc_int, set_varc_string;
    uintptr_t get_varbit_type;  // shared_ptr<VarbitType> getVarbitType(int id) (x8): {int varp, lsb, msb}
    // Menu close, as the close branch of 0x599f68: clear the model (MenuManager+0x140) and the
    // displayed menu (+0x540), then shown (+0x7c0) = 0.
    uintptr_t menu_clear_model, menu_clear_display;
    // Builds the displayed menu (+0x540) from the model (+0x140): (display, model, MenuManager
    // +0x7c8 layout), as the show branch of 0x599f68.
    uintptr_t menu_build_display;
    // The long-press menu build: (MenuManager*, x, y) in UI canvas units (0x54ae24's touch handler).
    uintptr_t menu_build_at;
    // Scene picking for menus: set the pick point (screen px, mode; 0x72b758 as the touch handler
    // 0x518840), the "picked" flag the menu build checks, and the pick mode (2 pending, 3 done).
    uintptr_t pick_set, pick_ready, pick_mode;
    uintptr_t mod_icons;  // global shared_ptr obj of the font's <img=N> table (setModIcons 0x431a84)
    uintptr_t message_ctor;  // Message(this, type, name*, sender*, text*): every chat line, id = next id++
    // Client: int[32] ring of skill ids changed by UPDATE_STAT (case 2 of 0x541dd4: ring[count++ & 31] = skill);
    // widgets' onStatTransmit listeners run for the skills in it (RuneLite's changedSkills).
    uint32_t f_changed_skills, f_changed_skill_count;
    // AnimationState::setSequence(state*, seq id); -1 clears it. A graphics object's state is at +0x280,
    // and the per-cycle walk (0x455f24) drops objects whose state has no sequence (+0x288 == 0).
    uintptr_t seq_set;
    // Model store: shared_ptr<ModelData> load(holder, group = model id, file 0) (x8 result; ObjType::getModel
    // 0x63f1e8 calls it with x0 = *(*GOT 0xa83dd8)); a fresh ModelData per call (decoder 0x720c4c).
    uintptr_t model_store, model_store_got;
    // shared_ptr<SeqType> getSeqType(int id) (x8, as getObjType); animate(out x8 {ctrl, Model*}, AnimReq*)
    // (NpcType::getModel 0x6380d8 fills the request; the result is one global scratch Model).
    uintptr_t get_seq_type, animate;
};

constexpr Build kBuilds[] = {
    {"241.4",
     {0xf0, 0xff, 0xf1, 0x19, 0x1f, 0x3f, 0x23, 0x5d, 0x20, 0x85,
      0x7c, 0x02, 0x70, 0x44, 0xb9, 0xc9, 0xf7, 0x09, 0xcd, 0x8c},
     0xaee748, 0xaee6e8, 0x2020, 0xa69fe0, 0x411640, 0x1127ca0, 0x1127c60, 0x3058, 0x413c5c, 0x413cdc,
     0xecdc, 0xece0, 0xcb78, 0xcb7c, 0xcb80, 0x8950c, 0x89514, 0x5d0040,
     0xc950, 0x18, 0x24, 0x28, 0x98, 0xa0, 0xd0, 0xd8, 0x68, 0x70, 0xcb0c, 0x718, 0xcb90, 0x730, 0x1fc, 0x200, 0x0, 0x8,
     0x89488, 0x413a98, 0x413a88, 0x413a60, 0x413a68, 0x78, 0x158, 0x5c, 0x60, 0x64, 0x68, 0x74, 0x6, 0x3210, 0x3274, 0x32d8, 0xaab690, 0xaab658, 0xaab628,
     0xaef1d0, 0x4df54c, 0x63bfe8, 0x63633c, 0x65c798, 0x6de99c, 0x65cac8,
     0x4132b0, 0xa6d6c0, 0x515854, 0x62e5ec, 0x43a9c0,
     0x5d00fc, 0x3f804c, 0x3f8470, 0x64b450,
     0x514f48, 0x507398, 0x50621c, 0x58fa84,
     0x72b758, 0x1d2f52d, 0x1d7ddf0,
     0x18e2130, 0x3f0a04,
     0x413d64, 0x413de4,
     0x659274,
     0x71d824, 0xa83dd8,
     0x6447a0, 0x6325cc},
};

constexpr const char* kGameLib = "liblibs.hal.system.osclient.so";

const Build* g_build = nullptr;
uintptr_t g_base = 0;
thread_local bool g_own_model_call = false;  // our own getModel calls (drawlog ignores them)

struct NoteSearch {
    const uint8_t* id = nullptr;
    size_t id_len = 0;
};

int find_build_id(dl_phdr_info* info, size_t, void* data) {
    if (!info->dlpi_name || !strstr(info->dlpi_name, kGameLib)) return 0;
    auto* out = static_cast<NoteSearch*>(data);
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE) continue;
        auto p = reinterpret_cast<const uint8_t*>(info->dlpi_addr + ph.p_vaddr);
        const uint8_t* end = p + ph.p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            auto* nh = reinterpret_cast<const ElfW(Nhdr)*>(p);
            const uint8_t* name = p + sizeof(*nh);
            const uint8_t* desc = name + ((nh->n_namesz + 3) & ~3u);
            if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && memcmp(name, "GNU", 4) == 0) {
                out->id = desc;
                out->id_len = nh->n_descsz;
                return 1;
            }
            p = desc + ((nh->n_descsz + 3) & ~3u);
        }
    }
    return 1;
}

template <typename T>
T read_at(uintptr_t addr) {
    T v;
    memcpy(&v, reinterpret_cast<const void*>(addr), sizeof v);
    return v;
}

uintptr_t client() {
    return g_build ? read_at<uintptr_t>(g_base + g_build->client_ptr) : 0;
}

}  // namespace

bool init() {
    g_base = module_base(kGameLib);
    NoteSearch note;
    dl_iterate_phdr(find_build_id, &note);
    if (!g_base || note.id_len != 20) {
        LOGE("game: library or build id not found");
        return false;
    }
    for (const Build& b : kBuilds) {
        if (memcmp(b.build_id, note.id, 20) == 0) {
            g_build = &b;
            LOGI("game: build %s recognised", b.name);
            install_message_hook();
            install_scene_load_hook();
            install_minimap_hook();
            install_fire_hook();
            install_wheel_hook();
            install_state_write_hooks();
            return true;
        }
    }
    LOGE("game: unknown build id; game-state hooks disabled");
    return false;
}

bool available() { return g_build != nullptr; }
const char* build_name() { return g_build ? g_build->name : "unknown"; }

int cycle() { return g_build ? read_at<int32_t>(g_base + g_build->cycle) : -1; }

int game_state() {
    uintptr_t c = client();
    return c ? read_at<int32_t>(c + g_build->f_game_state) : -1;
}

uintptr_t client_vtable() { return g_build ? g_base + g_build->client_vtable : 0; }
uintptr_t do_cycle() { return g_build ? g_base + g_build->do_cycle : 0; }

namespace {
int field(uint32_t off, int fallback = -1) {
    uintptr_t c = client();
    return c ? read_at<int32_t>(c + off) : fallback;
}

int read_varp_table(uintptr_t table, int id) {
    if (!g_build || id < 0) return 0;
    uint32_t count = read_at<uint32_t>(g_base + table + 8);
    auto* values = read_at<const int32_t*>(g_base + table + 0x10);
    return values && static_cast<uint32_t>(id) < count ? values[id] : 0;
}
}  // namespace

int server_tick() { return g_build ? field(g_build->f_server_tick) : -1; }

int varp_count() { return g_build ? static_cast<int>(read_at<uint32_t>(g_base + g_build->varps + 8)) : 0; }
int varp(int id) { return g_build ? read_varp_table(g_build->varps, id) : 0; }

// UPDATE_RUNWEIGHT (dispatcher case 0x1a) stores s16 at Client+0x33a4; UPDATE_RUNENERGY (0x66)
// stores 0..10000 at Client+0x33a0.
int world() { uintptr_t c = client(); return c ? read_at<int32_t>(c + 0x3174) : 0; }
bool viewport_zoom(int out[5]) {
    uintptr_t c = client();
    if (!c) return false;
    out[0] = read_at<int32_t>(c + 0x46d4);
    for (int i = 0; i < 4; i++) out[1 + i] = read_at<int32_t>(c + 0x46e0 + 4 * i);
    return out[0] > 0 && out[0] < 100000 && out[3] > 0 && out[4] > 0;
}
int world_flags() { uintptr_t c = client(); return c ? read_at<int32_t>(c + 0x2f64) : 0; }

namespace { std::string read_string(uintptr_t addr); }

// World list loaded from slr.ws (parser 0x4320dc): a global container at 0xace6c0 with u64 count
// +0x08 and World** +0x10; each World (0x68 bytes) is +0x00 id, +0x04 flags, +0x08 players,
// +0x10 host and +0x28 activity strings, +0x40 location. setWorld (0x432748) copies the picked
// entry's id/flags/host to Client +0x3174/+0x2f64/+0x3098; the arg parser (0x43d39c) fills the
// same fields when the game starts on a world from its arguments (the host is --gameaddress).
// The parser (callers 0x412e78 and the login handler 0x41615c, each only from doCycle 0x411640, and the
// CS2 world list ops 0x4c2544) frees the old entries and fills the new array in place, so the list is
// read only on the game thread, like the game's own doCycle readers (offsets.md "World list").
std::pair<uintptr_t, uint64_t> world_list_key() {
    if (!g_build) return {0, 0};
    return {read_at<uintptr_t>(g_base + 0xace6d0), read_at<uint64_t>(g_base + 0xace6c8)};
}

bool world_entry(int id, WorldEntry& out) {
    if (!g_build || id <= 0) return false;
    auto count = read_at<uint64_t>(g_base + 0xace6c8);
    auto list = read_at<uintptr_t>(g_base + 0xace6d0);
    if (list && count <= 4096) {
        for (uint64_t i = 0; i < count; i++) {
            auto w = read_at<uintptr_t>(list + i * 8);
            if (!w || read_at<int32_t>(w) != id) continue;
            out.id = id;
            out.flags = read_at<int32_t>(w + 0x04);
            out.players = read_at<int32_t>(w + 0x08);
            out.host = read_string(w + 0x10);
            out.activity = read_string(w + 0x28);
            out.location = read_at<int32_t>(w + 0x40);
            out.listed = true;
            return true;
        }
    }
    uintptr_t c = client();
    if (!c || read_at<int32_t>(c + 0x3174) != id) return false;
    out.id = id;
    out.flags = read_at<int32_t>(c + 0x2f64);
    out.host = read_string(c + 0x3098);
    out.listed = false;
    return true;
}

// Grand Exchange offers: slot i is the object *(Client + 0x4185c0 + i*8), read by the CS2
// stockmarket_* ops (handler 0x4ed838, ops 3903-3913): +0x00 u8 state (0 empty, 1 adding, 2 stable,
// 5 finished: isofferempty/adding/stable/finished), +0x01 u8 type (0 buy, 1 sell; getoffertype),
// +0x04 int item, +0x08 s64 price, +0x10 int total quantity, +0x14 int quantity done, +0x18 s64 gold
// spent. out = 8 x {state, type, item, price, total, done, spent}; state -1 = no object.
bool ge_offers(int64_t out[8 * 7]) {
    uintptr_t c = client();
    if (!c) return false;
    for (int i = 0; i < 8; i++) {
        int64_t* o = out + 7 * i;
        uintptr_t off = read_at<uintptr_t>(c + 0x4185c0 + 8u * static_cast<uintptr_t>(i));
        if (!off) { o[0] = -1; for (int k = 1; k < 7; k++) o[k] = 0; continue; }
        o[0] = read_at<uint8_t>(off);
        o[1] = read_at<uint8_t>(off + 1);
        o[2] = read_at<int32_t>(off + 4);
        o[3] = read_at<int64_t>(off + 8);
        o[4] = read_at<int32_t>(off + 0x10);
        o[5] = read_at<int32_t>(off + 0x14);
        o[6] = read_at<int64_t>(off + 0x18);
    }
    return true;
}

uintptr_t ge_slot_raw(int slot, uint8_t out[0x28]) {
    uintptr_t c = client();
    if (!c || slot < 0 || slot > 7) return 0;
    uintptr_t off = read_at<uintptr_t>(c + 0x4185c0 + 8u * static_cast<uintptr_t>(slot));
    if (!off) return 0;
    for (int i = 0; i < 0x28; i++) out[i] = read_at<uint8_t>(off + i);
    return off;
}

// Widget drag, from the drag block of the cycle function 0x41d0a4 (Ghidra FUN_0051d0a4): Client
// +0x413ad8 dragged Widget* (+0x413ad0 its reference count holder), +0x413ae8 the drag parent,
// +0x413af0 press point, +0x413b00 dragged-on Widget* (+0x413af8), +0x413b20 u8 dragging (set
// once the widget's dead time +0x324 and dead zone +0x320 are exceeded), +0x413b24 int cycles held.
// Touch state Client+0x40e988: 3 held, 4 released (the release runs onDragComplete +0x500 and the
// swap 0x528b50(…, dragged, dragged-on)).
namespace {
constexpr uintptr_t kDragged = 0x413ad8, kDraggedOn = 0x413b00, kDragging = 0x413b20, kDragCycles = 0x413b24,
                    kTouchState = 0x40e988;
}
bool drag_state(int out[7]) {
    uintptr_t c = client();
    if (!c) return false;
    uintptr_t d = read_at<uintptr_t>(c + kDragged);
    if (!d) return false;
    out[0] = out[1] = out[2] = out[3] = -1;
    widget_ref(d, &out[0], &out[1]);
    uintptr_t on = read_at<uintptr_t>(c + kDraggedOn);
    if (on) widget_ref(on, &out[2], &out[3]);
    out[4] = read_at<uint8_t>(c + kDragging);
    out[5] = read_at<int32_t>(c + kDragCycles);
    out[6] = read_at<int32_t>(c + kTouchState);
    return true;
}
void drag_clear_target() {
    uintptr_t c = client();
    // Only the pointer: the game checks it before the swap and replaces both words (releasing the
    // reference holder) when the next target is set.
    if (c) *reinterpret_cast<volatile uintptr_t*>(c + kDraggedOn) = 0;
}
int touch_state() { uintptr_t c = client(); return c ? read_at<int32_t>(c + kTouchState) : 0; }

namespace { std::string read_string(uintptr_t addr); }

// Friends chat: *(Client+0x4608) (CS2 ops 3611-3627, handler 0x4ebdb0): +0x0c member count,
// +0x20 member pointers; member +0x08 name, +0x68 world, +0x88 rank; +0xa8 channel name, +0xd8
// owner, s8 +0xf0 kick rank, +0xf4 own rank.
// Clan channels (ops 3850-3861, handler 0x4ecf20): {ref, ClanChannel*} at Client+0x4660 (listened)
// and +0x4670 + clan id * 0x10 (affined); channel +0x30/+0x38 vector of 16-byte {?, member*}, +0x50
// name, s8 +0x69 talk rank, s8 +0x6a kick rank; member s8 +0x08 rank, +0x0c world, +0x10 name.
bool friends_chat(ChatChannel& out) {
    uintptr_t c = client();
    uintptr_t fc = c ? read_at<uintptr_t>(c + 0x4608) : 0;
    if (!fc) return false;
    out.name = read_string(fc + 0xa8);
    out.owner = read_string(fc + 0xd8);
    out.kick_rank = read_at<int8_t>(fc + 0xf0);
    out.my_rank = read_at<int32_t>(fc + 0xf4);
    int n = read_at<int32_t>(fc + 0x0c);
    uintptr_t arr = read_at<uintptr_t>(fc + 0x20);
    if (n < 0 || n > 2000 || !arr) n = 0;
    for (int i = 0; i < n; i++) {
        uintptr_t m = read_at<uintptr_t>(arr + 8u * static_cast<uintptr_t>(i));
        if (!m) continue;
        out.members.push_back({read_string(m + 0x08), read_at<int32_t>(m + 0x68), read_at<int32_t>(m + 0x88)});
    }
    return true;
}
bool clan_channel(int which, ChatChannel& out) {
    uintptr_t c = client();
    if (!c || which < -1 || which > 7) return false;
    uintptr_t ch = read_at<uintptr_t>(c + (which < 0 ? 0x4668 : 0x4678 + 0x10u * static_cast<uintptr_t>(which)));
    if (!ch) return false;
    out.name = read_string(ch + 0x50);
    out.talk_rank = read_at<int8_t>(ch + 0x69);
    out.kick_rank = read_at<int8_t>(ch + 0x6a);
    uintptr_t b = read_at<uintptr_t>(ch + 0x30), e = read_at<uintptr_t>(ch + 0x38);
    if (!b || e < b || (e - b) / 16 > 2000) return true;
    for (uintptr_t p = b; p < e; p += 16) {
        uintptr_t m = read_at<uintptr_t>(p + 8);
        if (!m) continue;
        out.members.push_back({read_string(m + 0x10), read_at<int32_t>(m + 0x0c), read_at<int8_t>(m + 0x08)});
    }
    return true;
}

// Instances (REBUILD_REGION). The scene loader manager is *(Client+0x4868) (passed to both rebuild
// handlers from the packet dispatcher, 0x442610). REBUILD_REGION (0x555510) sets byte +0x54dd to 1,
// both normal rebuild paths (0x5551dc, 0x5555xx) clear it. The region parser (0x5580e4) fills
// [4 levels][13 x][13 z] entries of 16 bytes at manager +0x38: int zoneX (ref >> 14 & 1023), zoneZ
// (ref >> 3 & 2047), level (ref >> 24 & 3), rotation (ref >> 1 & 3); absent zones are all -1.
// The array is not cleared by a normal rebuild (desktop keeps stale chunks too).
bool instance_templates(bool& instanced, int32_t out[4 * 13 * 13]) {
    uintptr_t c = client();
    if (!c) return false;
    uintptr_t mgr = read_at<uintptr_t>(c + 0x4868);
    if (!mgr) return false;
    instanced = read_at<uint8_t>(mgr + 0x54dd) != 0;
    for (int i = 0; i < 4 * 13 * 13; i++) {
        const uintptr_t e = mgr + 0x38 + 16 * static_cast<uintptr_t>(i);
        int32_t x = read_at<int32_t>(e), z = read_at<int32_t>(e + 4), lvl = read_at<int32_t>(e + 8), rot = read_at<int32_t>(e + 12);
        out[i] = (x < 0 || z < 0 || lvl < 0 || rot < 0) ? -1 : (lvl << 24 | x << 14 | z << 3 | rot << 1);
    }
    return true;
}
int weight() { uintptr_t c = client(); return c ? read_at<int16_t>(c + 0x33a4) : 0; }
int run_energy() { uintptr_t c = client(); return c ? read_at<int32_t>(c + 0x33a0) : 0; }

static std::vector<int32_t> varp_table(uintptr_t table) {
    if (!g_build) return {};
    uint32_t count = read_at<uint32_t>(g_base + table + 8);
    auto* values = read_at<const int32_t*>(g_base + table + 0x10);
    if (!values || count > 1 << 20) return {};  // OpenRune has 65660 varps
    return std::vector<int32_t>(values, values + count);
}
std::vector<int32_t> varps() { return g_build ? varp_table(g_build->varps) : std::vector<int32_t>{}; }
std::vector<int32_t> server_varps() { return g_build ? varp_table(g_build->server_varps) : std::vector<int32_t>{}; }

namespace {
// The game's 24-byte string: inline (byte 23 = 23 - length) up to 22 chars, else
// {char*, size, capacity | 0x80 << 56} (heap, owned here). See read_string.
struct GameString {
    alignas(8) uint8_t bytes[24] = {};
    char* heap = nullptr;
    explicit GameString(const std::string& s) {
        if (s.size() <= 22) {
            memcpy(bytes, s.data(), s.size());
            bytes[23] = static_cast<uint8_t>(23 - s.size());
        } else {
            heap = static_cast<char*>(malloc(s.size() + 1));
            memcpy(heap, s.data(), s.size());
            heap[s.size()] = 0;
            uint64_t words[3] = {reinterpret_cast<uint64_t>(heap), s.size(), s.size() | (0x80ull << 56)};
            memcpy(bytes, words, 24);
        }
    }
    ~GameString() { free(heap); }
    GameString(const GameString&) = delete;
    GameString& operator=(const GameString&) = delete;
};
}  // namespace

bool add_chat_message(int type, const std::string& name, const std::string& text) {
    uintptr_t c = client();
    if (!g_build || !g_build->add_chat_message || !c) return false;
    GameString n(name), t(text);
    reinterpret_cast<void (*)(uintptr_t, int, const void*, const void*)>(g_base + g_build->add_chat_message)(c, type, n.bytes, t.bytes);
    return true;
}
// setWorld 0x432748(World*) reads id +0x00, flags +0x04, host +0x10 and the beta-world string +0x48
// (offsets.md "World list"); it copies them, so a World on the stack is enough.
bool change_world(int id, int flags, const std::string& host) {
    if (!g_build || !client() || id <= 0) return false;
    auto set_world = reinterpret_cast<void (*)(const void*)>(g_base + 0x432748);
    auto count = read_at<uint64_t>(g_base + 0xace6c8);
    auto list = read_at<uintptr_t>(g_base + 0xace6d0);
    for (uint64_t i = 0; list && count <= 4096 && i < count; i++) {
        auto w = read_at<uintptr_t>(list + i * 8);
        if (w && read_at<int32_t>(w) == id) {
            set_world(reinterpret_cast<const void*>(w));
            return true;
        }
    }
    GameString h(host), empty{std::string()};
    alignas(8) uint8_t w[0x68] = {};
    memcpy(w, &id, 4);
    memcpy(w + 4, &flags, 4);
    memcpy(w + 0x10, h.bytes, 24);
    memcpy(w + 0x28, empty.bytes, 24);
    memcpy(w + 0x48, empty.bytes, 24);
    set_world(w);
    return true;
}

int server_varp(int id) { return g_build ? read_varp_table(g_build->server_varps, id) : 0; }

int varbit(int id) {
    if (!g_build || id < 0) return 0;
    auto fn = reinterpret_cast<int (*)(int)>(g_base + g_build->get_varbit);
    return fn(id);
}

uint32_t changed_varp_count() { return g_build ? static_cast<uint32_t>(field(g_build->f_changed_count, 0)) : 0; }
int changed_varp(uint32_t n) {
    return g_build ? field(g_build->f_changed_varps + (n & 31) * 4) : -1;
}

namespace {
// The game's strings are 24 bytes, folly-fbstring style: small strings are inline with
// 23 - length in the last byte; otherwise {char* data, size_t size, capacity | category}.
std::string read_string(uintptr_t addr) {
    uint8_t last = read_at<uint8_t>(addr + 23);
    if ((last & 0xc0) == 0) {
        size_t len = last <= 23 ? 23 - last : 0;
        return std::string(reinterpret_cast<const char*>(addr), len);
    }
    auto data = read_at<const char*>(addr);
    size_t len = read_at<size_t>(addr + 8);
    return data && len < 4096 ? std::string(data, len) : std::string();
}

uintptr_t npc_lookup(uintptr_t wv, int index) {
    auto buckets = read_at<uintptr_t>(wv + g_build->wv_npc_buckets);
    auto nb = read_at<uint64_t>(wv + g_build->wv_npc_bucket_count) & 0xffffffff;
    if (!buckets || !nb) return 0;
    for (auto node = read_at<uintptr_t>(buckets + (static_cast<uint64_t>(index) % nb) * 8); node;
         node = read_at<uintptr_t>(node + 24)) {
        if (read_at<int32_t>(node) == index) return read_at<uintptr_t>(node + 16);
    }
    return 0;
}
}  // namespace

// Client varcs (Client +0x417b68 → store): sorted vector at +0x48/+0x50 of 0x30-byte entries
// {int id; variant at +8 (int, or 24-byte string); u32 type at +0x28: 1 int, 2 string, 4 object}
// (getVarcInt 0x3f82ac, getVarcString 0x3f8588; opcodes 42/49 in 0x4dd08c).
static uintptr_t varc_entry(int id, int type) {
    uintptr_t c = client();
    if (!c) return 0;
    uintptr_t store = read_at<uintptr_t>(c + 0x417b68);
    if (!store) return 0;
    uintptr_t b = read_at<uintptr_t>(store + 0x48), e = read_at<uintptr_t>(store + 0x50);
    if (!b || e < b || (e - b) / 0x30 > 100000) return 0;
    size_t lo = 0, hi = (e - b) / 0x30;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (read_at<int32_t>(b + mid * 0x30) < id) lo = mid + 1; else hi = mid;
    }
    uintptr_t ent = b + lo * 0x30;
    if (ent >= e || read_at<int32_t>(ent) != id || read_at<int32_t>(ent + 0x28) != type) return 0;
    return ent;
}

bool varc_int(int id, int& out) {
    uintptr_t ent = varc_entry(id, 1);
    if (!ent) return false;
    out = read_at<int32_t>(ent + 8);
    return true;
}

bool varc_string(int id, std::string& out) {
    uintptr_t ent = varc_entry(id, 2);
    if (!ent) return false;
    out = read_string(ent + 8);
    return true;
}

// The game's definition/model/sprite caches (one LRU-over-hash-map container each: u64 entry count
// at base +0x20) and the clear functions its full cache reset 0x429f28 calls (build 241.4, static RE
// client-api-cache-config): which 0 ObjType (base 0x1217a30; clear 0x642cbc also empties item models
// and item sprites), 1 LocType (0x1159220; clear 0x633ad8 with the loc caches next to it), 2 SeqType
// (0x1854100; clear 0x648204), 3 item models (0x1218f28; cleared by 0x642cbc), 4 item sprites
// (0x1219fc0; clear 0x642ebc), 5 HealthBarType + health bar sprites (0x18d8f80 and 0x18da478;
// clear 0x6604d0).
namespace {
struct GameCache { uintptr_t base, base2, clear; };
constexpr GameCache kCaches[] = {
    {0x1217a30, 0, 0x642cbc}, {0x1159220, 0, 0x633ad8}, {0x1854100, 0, 0x648204},
    {0x1218f28, 0, 0x642cbc}, {0x1219fc0, 0, 0x642ebc}, {0x18d8f80, 0x18da478, 0x6604d0},
};
}  // namespace

long cache_size(int which) {
    if (!g_build || which < 0 || which >= static_cast<int>(sizeof kCaches / sizeof kCaches[0])) return -1;
    const GameCache& k = kCaches[which];
    long n = static_cast<long>(read_at<uint64_t>(g_base + k.base + 0x20));
    if (k.base2) n += static_cast<long>(read_at<uint64_t>(g_base + k.base2 + 0x20));
    return n;
}

// Between cycles only (the game thread outside game code): a cleared node's {ctrl, type} slot is
// what getLocType hands out, so nothing may hold one across the call.
bool cache_reset(int which) {
    if (cache_size(which) < 0) return false;
    reinterpret_cast<void (*)()>(g_base + kCaches[which].clear)();
    return true;
}

void release_shared_ptr(uintptr_t ctrl);

// js5 archives (static RE client-api-cache-config; created at 0x434b80..0x4350c8 by the js5 manager
// Client +0x2428, each slot a raw Archive*). Archive: +0x18 index crc (0 = index not loaded), +0x28
// u64 group count (dense), +0x30 groups (0x70 each: +0x04 version, -1 = absent; +0x28/+0x30 packed
// bytes {ctrl, Buffer*}; +0x40 u64 file count (dense); +0x48 files, 0x20 each: +0x08 i64 start, -1 =
// absent; +0x10/+0x18 unpacked {ctrl, Buffer*}). getFile 0x6be3dc(archive, out {ctrl, Buffer*}, group,
// file, xtea keys, keep) returns a status value (OK = **GOT 0xa828b0); Buffer +0x08 size, +0x10 data,
// +0x18 pos: the file is data[pos, size).
namespace {
uintptr_t archive_at(int id) {
    static constexpr int16_t kSlot[26] = {0x2330, 0x2340, 0x2348, 0x2358, 0x2360, 0x2368, 0x2370, 0x2378, 0x2388,
        0x2390, 0x2398, 0x23a0, 0x23a8, 0x23b0, 0x23b8, 0x23c0, 0, 0x23e0, 0x23d0, 0x23c8, 0x23d8, 0x23e8,
        0x2338, 0x23f0, 0, 0x2380};
    uintptr_t c = client();
    if (!g_build || !c || id < 0 || id >= 26 || !kSlot[id] || game_state() < 10) return 0;
    uintptr_t a = read_at<uintptr_t>(c + kSlot[id]);
    return a && read_at<uint32_t>(a + 0x18) ? a : 0;
}
uintptr_t archive_group(uintptr_t a, int group) {
    if (!a || group < 0 || static_cast<uint64_t>(group) >= read_at<uint64_t>(a + 0x28)) return 0;
    uintptr_t g = read_at<uintptr_t>(a + 0x30) + static_cast<uintptr_t>(group) * 0x70;
    return read_at<int32_t>(g + 4) == -1 ? 0 : g;
}
}  // namespace

bool archive_file_ids(int id, int group, std::vector<int32_t>& out) {
    out.clear();
    uintptr_t g = archive_group(archive_at(id), group);
    if (!g) return false;
    uint64_t n = read_at<uint64_t>(g + 0x40);
    uintptr_t files = read_at<uintptr_t>(g + 0x48);
    if (!files || n > 0x100000) return false;
    for (uint64_t i = 0; i < n; i++)
        if (read_at<int64_t>(files + i * 0x20 + 8) != -1) out.push_back(static_cast<int32_t>(i));
    return true;
}

// keep = whether the file was already unpacked: a file this call unpacks is handed out (not left
// cached), so the archive is as it was; one the game had unpacked stays shared.
bool archive_file(int id, int group, int file, std::vector<uint8_t>& out) {
    out.clear();
    uintptr_t a = archive_at(id);
    uintptr_t g = archive_group(a, group);
    if (!g || file < 0 || static_cast<uint64_t>(file) >= read_at<uint64_t>(g + 0x40)) return false;
    uintptr_t f = read_at<uintptr_t>(g + 0x48) + static_cast<uintptr_t>(file) * 0x20;
    if (read_at<int64_t>(f + 8) == -1) return false;
    unsigned keep = read_at<uintptr_t>(f + 0x18) ? 1 : 0;
    struct { uintptr_t ctrl, obj; } sp = {0, 0};
    using Fn = uintptr_t (*)(uintptr_t, void*, int, int, const int*, unsigned);
    uintptr_t status = reinterpret_cast<Fn>(g_base + 0x6be3dc)(a, &sp, group, file, nullptr, keep);
    uintptr_t ok = read_at<uintptr_t>(read_at<uintptr_t>(g_base + 0xa828b0));
    bool good = status == ok && sp.obj;
    if (good) {
        uint64_t size = read_at<uint64_t>(sp.obj + 8), pos = read_at<uint64_t>(sp.obj + 0x18);
        auto data = read_at<const uint8_t*>(sp.obj + 0x10);
        good = data && pos <= size && size < (64u << 20);
        if (good) out.assign(data + pos, data + size);
    }
    release_shared_ptr(sp.ctrl);
    return good;
}

// Every int (type 1) and string (type 2) varc in the store, in id order.
bool varc_entries(std::vector<VarcEntry>& out) {
    out.clear();
    uintptr_t c = client();
    uintptr_t store = c ? read_at<uintptr_t>(c + 0x417b68) : 0;
    if (!store) return false;
    uintptr_t b = read_at<uintptr_t>(store + 0x48), e = read_at<uintptr_t>(store + 0x50);
    if (!b || e < b || (e - b) / 0x30 > 100000) return false;
    for (uintptr_t ent = b; ent < e; ent += 0x30) {
        VarcEntry v;
        v.id = read_at<int32_t>(ent);
        v.type = read_at<int32_t>(ent + 0x28);
        if (v.type == 1) v.int_value = read_at<int32_t>(ent + 8);
        else if (v.type == 2) v.string_value = read_string(ent + 8);
        else continue;
        out.push_back(std::move(v));
    }
    return true;
}

static ActorState read_actor(uintptr_t a) {
    ActorState st;
    if (!a) return st;
    st.animation = read_at<int32_t>(a + 0x4d8);
    st.animation_frame = read_at<int16_t>(a + 0x4de);
    st.orientation = read_at<int32_t>(a + 0x3e0) & 0x7ff;
    st.current_orientation = read_at<int32_t>(a + 0x270) & 0x7ff;
    // Face command: kind +0x3a5 (0 = entity), entity type +0x3a7 (1 NPC, 2 player, 3 world
    // entity), target index +0x3a8 (-1 none).
    int kind = read_at<uint8_t>(a + 0x3a5), type = read_at<uint8_t>(a + 0x3a7), index = read_at<int32_t>(a + 0x3a8);
    if (kind == 0 && index >= 0 && index < 32768) {
        if (type == 1) st.interacting = index;
        else if (type == 2) st.interacting = index + 32768;
    }
    for (int i = 0; i < 8; i++) st.pose[i] = read_at<int32_t>(a + 0x280 + 4 * i);
    st.pose_animation = read_at<int32_t>(a + 0x4b8);
    st.pose_frame = read_at<int16_t>(a + 0x4be);
    st.overhead_cycle = read_at<int32_t>(a + 0x2f4);
    st.overhead_text = read_string(a + 0x2d8);  // kept after the timer ends, as desktop's field
    // Hitmarks (adder 0x472240): std::vector at +0x330/+0x338 of 12-byte {type, amount, end cycle}
    // slots, end cycle = cycle + delay + HitmarkType duration; empty slots are -1.
    uintptr_t hb = read_at<uintptr_t>(a + 0x330), he = read_at<uintptr_t>(a + 0x338);
    if (hb && he > hb && he - hb <= 12 * 16) {
        int n = static_cast<int>((he - hb) / 12);
        for (int i = 0; i < n && i < 4; i++)
            for (int k = 0; k < 3; k++) st.hitsplats[i * 3 + k] = read_at<int32_t>(hb + i * 12 + k * 4);
    }
    // Head bars (adder 0x472780): std::list at +0x388 of nodes {next, prev, ctrl, HealthBar*};
    // HealthBar {ctrl, HealthBarType* +8, updates list +0x10}; update node obj {start cycle, health,
    // health2, cycle offset} (0x475130), ascending by cycle; HealthBarType width (op 14) +0x28.
    uintptr_t sentinel = a + 0x388, node = read_at<uintptr_t>(sentinel);
    if (node && node != sentinel) {
        uintptr_t bar = read_at<uintptr_t>(node + 0x18);
        uintptr_t type = bar ? read_at<uintptr_t>(bar + 8) : 0;
        uintptr_t us = bar + 0x10, last = bar ? read_at<uintptr_t>(us + 8) : 0;
        if (type && last && last != us) {
            uintptr_t upd = read_at<uintptr_t>(last + 0x18);
            if (upd) {
                st.health_ratio = read_at<int32_t>(upd + 4);
                st.health_scale = read_at<int32_t>(type + 0x28);
            }
        }
    }
    return st;
}

std::vector<Npc> read_npcs() {
    std::vector<Npc> out;
    uintptr_t c = client();
    if (!c || game_state() != 30) return out;
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return out;
    int count = read_at<int32_t>(wv + g_build->wv_npc_count);
    auto indices = read_at<const int32_t*>(wv + g_build->wv_npc_indices);
    if (count <= 0 || count > 65535 || !indices) return out;
    int base_x = read_at<int32_t>(wv + g_build->wv_base_x), base_y = read_at<int32_t>(wv + g_build->wv_base_y);
    for (int i = 0; i < count; i++) {
        uintptr_t npc = npc_lookup(wv, indices[i]);
        if (!npc) continue;
        auto def = read_at<uintptr_t>(npc + g_build->npc_def);
        Npc n;
        n.index = indices[i];
        n.address = npc;
        n.id = def ? read_at<int32_t>(def + g_build->def_id) : -1;
        if (def) n.name = read_string(def + g_build->def_name);
        n.fine_x = read_at<int32_t>(npc + g_build->npc_fine_x);
        n.fine_y = read_at<int32_t>(npc + g_build->npc_fine_y);
        if (n.fine_x == 0 && n.fine_y == 0) {
            // Not moved since NPC_INFO added it: +0x1fc/+0x200 are only written by movement, so use
            // the position the add set (+0x268/+0x26c; a runtime ::npc spawn stood still unreported).
            n.fine_x = read_at<int32_t>(npc + 0x268);
            n.fine_y = read_at<int32_t>(npc + 0x26c);
        }
        n.world_x = base_x + (n.fine_x >> 7);
        n.world_y = base_y + (n.fine_y >> 7);
        n.actor = read_actor(npc);
        out.push_back(std::move(n));
    }
    return out;
}

// Health bar types in use (RuneLite HealthBarConfig): every HealthBarType on a live actor's head bar list
// (Actor +0x388, nodes {next, prev, ctrl, HealthBar*}, HealthBar +8 HealthBarType*). The decoder is inlined in
// getHealthBarType 0x65f6c8 (static RE client-api-host-gaps-reach): +0x00 id, +0x20 front sprite (op 7),
// +0x24 back sprite (op 8), +0x28 width (op 14), +0x2c width padding (op 15). The draw 0x463f80 reads the
// padding at draw time (dropped when 2 * padding >= the front sprite's width). Game thread only.
std::vector<HealthBarType> health_bar_types() {
    std::vector<HealthBarType> out;
    std::vector<uintptr_t> actors;
    for (const Npc& n : read_npcs()) actors.push_back(n.address);
    for (const PlayerInfo& p : read_players()) actors.push_back(p.address);
    for (uintptr_t a : actors) {
        uintptr_t sentinel = a + 0x388;
        int guard = 0;
        for (uintptr_t node = read_at<uintptr_t>(sentinel); node && node != sentinel && guard++ < 16; node = read_at<uintptr_t>(node)) {
            uintptr_t bar = read_at<uintptr_t>(node + 0x18);
            uintptr_t type = bar ? read_at<uintptr_t>(bar + 8) : 0;
            if (!type) continue;
            bool seen = false;
            for (const HealthBarType& t : out) seen |= t.address == type;
            if (seen) continue;
            out.push_back({type, read_at<int32_t>(type), read_at<int32_t>(type + 0x20), read_at<int32_t>(type + 0x24),
                           read_at<int32_t>(type + 0x28), read_at<int32_t>(type + 0x2c)});
        }
    }
    return out;
}

bool health_bar_set_padding(int id, int padding) {
    for (const HealthBarType& t : health_bar_types())
        if (t.id == id) { *reinterpret_cast<int32_t*>(t.address + 0x2c) = padding; return true; }
    return false;
}

// The live Npc*/Player* for an index right now (the per-cycle snapshot's addresses can be freed
// later in the same cycle, e.g. by a region rebuild). 0 if not active. Game thread only.
uintptr_t live_npc(int index) {
    uintptr_t c = client();
    if (!c || game_state() != 30 || index < 0) return 0;
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return 0;
    int count = read_at<int32_t>(wv + g_build->wv_npc_count);
    auto indices = read_at<const int32_t*>(wv + g_build->wv_npc_indices);
    if (count <= 0 || count > 65535 || !indices) return 0;
    for (int i = 0; i < count; i++)
        if (indices[i] == index) return npc_lookup(wv, index);
    return 0;
}

// NPC_INFO model overrides (static RE client-api-cache-config, reader 0x469778): Npc +0x750 head
// (mask 0x10000), +0x758 body (masks 0x100000/0x1000/0x2000000), each an owning pointer to a 0x78-byte
// object (null = none): +0x08/+0x10 vector<int32> model ids, {u64 count +0x28, int16* +0x30}
// recolours, {+0x40, +0x48} retextures, u8 +0x54 mirror local player.
bool npc_overrides(int index, bool head, NpcOverrides& out) {
    uintptr_t npc = live_npc(index);
    uintptr_t o = npc ? read_at<uintptr_t>(npc + (head ? 0x750 : 0x758)) : 0;
    if (!o) return false;
    auto b = read_at<const int32_t*>(o + 0x08), e = read_at<const int32_t*>(o + 0x10);
    out.models.clear();
    if (b && e >= b && e - b < 256) out.models.assign(b, e);
    auto shorts = [](uintptr_t at, std::vector<int16_t>& v) {
        uint64_t n = read_at<uint64_t>(at);
        auto p = read_at<const int16_t*>(at + 8);
        v.clear();
        if (p && n < 256) v.assign(p, p + n);
    };
    shorts(o + 0x28, out.recolours);
    shorts(o + 0x40, out.retextures);
    out.use_local_player = read_at<uint8_t>(o + 0x54) != 0;
    return true;
}

// Head icons: Npc +0x760 (NPC_INFO mask 0x400000, NPC_HEADICON_SPECIFIC) points to a 0x90-byte object
// {+0x00/+0x08 int* archive ids begin/end, +0x48/+0x50 int* sprite indices}, 8 slots, -1 = none; its
// ctor 0x5ec334 starts from the NpcType's defaults (+0x1b8/+0x1c0 archives, +0x200/+0x208 indices),
// which are the NPC's icons while there is no object.
bool npc_head_icons(int index, std::vector<int32_t>& archives, std::vector<int32_t>& sprites) {
    archives.clear();
    sprites.clear();
    uintptr_t npc = live_npc(index);
    if (!npc) return false;
    uintptr_t o = read_at<uintptr_t>(npc + 0x760);
    uintptr_t a = o, s = o + 0x48;
    if (!o) {
        uintptr_t type = read_at<uintptr_t>(npc + 0x730);
        if (!type) return false;
        a = type + 0x1b8;
        s = type + 0x200;
    }
    auto ints = [](uintptr_t at, std::vector<int32_t>& v) {
        auto b = read_at<const int32_t*>(at), e = read_at<const int32_t*>(at + 8);
        if (b && e >= b && e - b <= 64) v.assign(b, e);
    };
    ints(a, archives);
    ints(s, sprites);
    return !archives.empty();
}

uintptr_t live_player(int index) {
    uintptr_t c = client();
    if (!c || game_state() != 30 || index < 0) return 0;
    int active_count = read_at<int32_t>(c + g_build->f_gpi);
    if (active_count < 0 || active_count > 2048) return 0;
    bool active = false;
    for (int i = 0; i < active_count && !active; i++) active = read_at<int32_t>(c + g_build->f_gpi + 4 + i * 4) == index;
    if (!active) return 0;
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return 0;
    auto buckets = read_at<uintptr_t>(wv + g_build->wv_player_buckets);
    auto count = read_at<uint64_t>(wv + g_build->wv_player_bucket_count) & 0xffffffff;
    if (!buckets || !count || count > 1 << 16) return 0;
    uintptr_t sentinel = read_at<uintptr_t>(buckets + count * 8);
    int guard = 0;
    for (auto node = read_at<uintptr_t>(buckets + (static_cast<uint64_t>(index) % count) * 8); node && node != sentinel && guard++ < 2048;
         node = read_at<uintptr_t>(node + 0x18))
        if (read_at<int32_t>(node) == index) return read_at<uintptr_t>(node + 0x10);
    return 0;
}

Point scene_base() {
    uintptr_t c = client();
    if (!c) return {-1, -1};
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return {-1, -1};
    return {read_at<int32_t>(wv + g_build->wv_base_x), read_at<int32_t>(wv + g_build->wv_base_y)};
}

// Player +0x728 = Appearance* (shared_ptr {ctrl +0x720, obj}); Player-level fields at +0x730..+0x790.
static void read_appearance(uintptr_t p, PlayerInfo& info) {
    info.combat_level = read_at<int32_t>(p + 0x780);
    info.skull = read_at<int32_t>(p + 0x730);
    info.prayer = read_at<int32_t>(p + 0x734);
    info.team = read_at<int32_t>(p + 0x790);
    uintptr_t app = read_at<uintptr_t>(p + 0x728);
    if (!app) return;
    info.has_appearance = true;
    for (int i = 0; i < 12; i++) {
        info.equipment[i] = read_at<int32_t>(app + 4 * i);
        info.kits[i] = read_at<int32_t>(app + 0x30 + 4 * i);
    }
    for (int i = 0; i < 5; i++) info.colors[i] = read_at<int32_t>(app + 0x60 + 4 * i);
    info.npc_id = read_at<int32_t>(app + 0x74);
    info.gender = read_at<int32_t>(app + 0xa8);
}

std::vector<PlayerInfo> read_players() {
    std::vector<PlayerInfo> out;
    uintptr_t c = client();
    if (!c || game_state() != 30) return out;
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return out;
    auto buckets = read_at<uintptr_t>(wv + g_build->wv_player_buckets);
    auto count = read_at<uint64_t>(wv + g_build->wv_player_bucket_count) & 0xffffffff;
    if (!buckets || !count || count > 1 << 16) return out;
    uintptr_t sentinel = read_at<uintptr_t>(buckets + count * 8);
    int base_x = read_at<int32_t>(wv + g_build->wv_base_x), base_y = read_at<int32_t>(wv + g_build->wv_base_y);
    int local = read_at<int32_t>(c + g_build->f_local_player_index);
    // The map keeps players that have left view; the GPI index list is the live set.
    int active_count = read_at<int32_t>(c + g_build->f_gpi);
    if (active_count < 0 || active_count > 2048) return out;
    std::vector<int32_t> active(active_count);
    for (int i = 0; i < active_count; i++) active[i] = read_at<int32_t>(c + g_build->f_gpi + 4 + i * 4);
    std::sort(active.begin(), active.end());
    for (uint64_t b = 0; b < count; b++) {
        int guard = 0;
        for (auto node = read_at<uintptr_t>(buckets + b * 8); node && node != sentinel && guard++ < 2048;
             node = read_at<uintptr_t>(node + 0x18)) {
            auto p = read_at<uintptr_t>(node + 0x10);
            if (!p || !std::binary_search(active.begin(), active.end(), read_at<int32_t>(node))) continue;
            PlayerInfo info;
            info.index = read_at<int32_t>(node);
            info.address = p;
            info.local = info.index == local;
            auto name = read_at<uintptr_t>(p + g_build->player_name);
            if (name) info.name = read_string(name);
            info.fine_x = read_at<int32_t>(p + g_build->npc_fine_x);
            info.fine_y = read_at<int32_t>(p + g_build->npc_fine_y);
            info.world_x = base_x + (info.fine_x >> 7);
            info.world_y = base_y + (info.fine_y >> 7);
            info.actor = read_actor(p);
            read_appearance(p, info);
            out.push_back(std::move(info));
        }
    }
    return out;
}

Point player_world() {
    if (!g_build || !client()) return {-1, -1};
    Point base = scene_base(), tile = player_local_tile();
    return {base.x + tile.x, base.y + tile.y};
}

bool play_sound_effect(int id, int loops, int delay) {
    uintptr_t c = client();
    if (!c || id < 0 || id > 0xffff || loops <= 0) return false;
    // The queue (0x4ae508, Ghidra 005ae508.c) is a vector of 0x28-byte entries at +0xc0/+0xc8 of
    // the sound system; the call adds one only while the system's "enabled" callback (slot 0,
    // presumably the sound-effect volume setting) returns non-zero and fewer than 50 are queued.
    // Report whether the game actually queued it, so a muted setting shows up as false.
    uintptr_t sys = c + 0x2610;
    uintptr_t before = read_at<uintptr_t>(sys + 0xc8);
    reinterpret_cast<void (*)(uintptr_t, uint32_t, char, int)>(g_base + 0x4ae508)(sys, id, static_cast<char>(std::min(loops, 127)), delay);
    bool queued = read_at<uintptr_t>(sys + 0xc8) != before;
    static int logs = 0;
    if (logs < 10) {
        logs++;
        LOGI("sound %d: %s by the game", id, queued ? "queued" : "not queued (sound effects off or queue full)");
    }
    return queued;
}

bool play_area_sound(int id, int x, int y, int range, int delay) {
    uintptr_t c = client();
    uintptr_t wv = c ? read_at<uintptr_t>(c + g_build->f_world_view) : 0;
    if (!wv || id < 0 || id > 0xffff) return false;
    // Desktop queues these as area sounds (position x << 16 | y << 8 | range). Here the game's area-sound
    // queue 0x4ae650, called as SOUND_AREA does (0x46f99c..0x46f9c0): key = the world view's short id
    // (+0), drop-off 0 (falloff over the whole range), flag 0. Entered through its hook, so
    // AreaSoundEffectPlayed posts.
    uintptr_t sys = c + 0x2610;
    uintptr_t before = read_at<uintptr_t>(sys + 0xc8);
    reinterpret_cast<void (*)(uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
        g_base + 0x4ae650)(sys, static_cast<uint32_t>(read_at<int16_t>(wv)), id, x & 0xff, y & 0xff, range & 0xff, 0, 1, delay, 0);
    return read_at<uintptr_t>(sys + 0xc8) != before;
}

bool play_sound_at_volume(int id, int volume) {
    uintptr_t c = client();
    if (!c || id < 0 || id > 0xffff) return false;
    // Desktop's playSoundEffect(id, volume): straight into the sound-effect mixer (no queue, no event),
    // at the sound-effect volume setting unless that is muted, then at `volume` (both 0..127).
    int setting = read_at<int32_t>(c + 0x2f60);
    int vol = setting != 0 ? setting : std::clamp(volume, 0, 127);
    // 0x4ad358(out handle, ctx, id, volume, loops) is the ambient sounds' "play now" (ctx = Client
    // +0x2ed0: +0 loader, +8 mixer, +0x20 resampler byte); handed the queue consumer's mixer
    // (*(Client +0x25c0)) it plays as a queued plain sound does, at our volume, once (loops 0).
    uintptr_t ctx[5] = {read_at<uintptr_t>(c + 0x2ed0), read_at<uintptr_t>(c + 0x25c0), 0, 0,
                        read_at<uint8_t>(c + 0x2ef0)};
    if (!ctx[0] || !ctx[1]) return false;
    uintptr_t handle = 0;
    reinterpret_cast<void (*)(uintptr_t*, uintptr_t*, int, int, int)>(g_base + 0x4ad358)(&handle, ctx, id, vol, 0);
    if (!handle) return false;
    // The mixer holds its own reference; drop the handle as the ambient code does when it replaces one
    // (release the control block at handle +0: counts at +8/+0xc, then the game's free 0x9f4394).
    auto add = reinterpret_cast<int (*)(int, uintptr_t)>(g_base + 0x3dc1b0);
    if (uintptr_t cb = read_at<uintptr_t>(handle); cb && add(-1, cb + 8) == 1) {
        auto vt = read_at<uintptr_t>(cb);
        reinterpret_cast<void (*)(uintptr_t)>(read_at<uintptr_t>(vt + 0x10))(cb);
        if (add(-1, cb + 0xc) == 1) reinterpret_cast<void (*)(uintptr_t)>(read_at<uintptr_t>(vt + 0x18))(cb);
    }
    reinterpret_cast<void (*)(uintptr_t)>(g_base + 0x9f4394)(handle);
    static int logs = 0;
    if (logs < 10) {
        logs++;
        LOGI("sound %d: played at volume %d (setting %d, asked %d; ambient mixer %s)", id, vol, setting, volume,
             read_at<uintptr_t>(c + 0x2ed8) == ctx[1] ? "same" : "differs");
    }
    return true;
}

namespace {
uintptr_t top_world_view();  // defined with the scene readers below
}

Viewport viewport() {
    Viewport v;
    uintptr_t c = client();
    if (!c) return v;
    uintptr_t holder_base = read_at<uintptr_t>(c + 0x90);
    if (!holder_base) return v;
    uintptr_t holder = holder_base + 0x1b0;
    uintptr_t vt = read_at<uintptr_t>(holder);
    if (!vt) return v;
    uintptr_t get = read_at<uintptr_t>(vt + 0x18);
    if (!get) return v;
    uintptr_t renderer = reinterpret_cast<uintptr_t (*)(uintptr_t)>(get)(holder);
    if (!renderer) return v;
    uintptr_t vp = read_at<uintptr_t>(renderer + 0x630c60);
    if (!vp) return v;
    v.valid = true;
    v.zoom = read_at<int32_t>(vp + 0x10);
    v.centre_x = read_at<int32_t>(vp + 0x14);
    v.centre_y = read_at<int32_t>(vp + 0x18);
    for (int i = 0; i < 4; i++) v.clip[i] = read_at<int32_t>(vp + 0x24 + 4 * i);
    v.far = read_at<int32_t>(vp + 0x6c);
    uintptr_t wv = top_world_view();
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    if (scene) {
        for (int i = 0; i < 3; i++) v.scene_cam[i] = read_at<int32_t>(scene + 0x288 + 4 * i);
        for (int i = 0; i < 4; i++) v.trig[i] = static_cast<int>(read_at<float>(scene + 0x418 + 4 * i) * 65536.0f);
    }
    return v;
}

bool set_camera_target(int which, int value) {
    uintptr_t c = client();
    if (!c || which < 0 || which > 1) return false;
    *reinterpret_cast<int32_t*>(c + g_build->f_camera + 20 + 4 * which) = which ? (value & 0x3fff) : value;
    return true;
}

bool set_camera_focal(int axis, int value) {
    uintptr_t c = client();
    if (!c || axis < 0 || axis > 2) return false;
    static constexpr int kOff[3] = {0x78, 0x80, 0x7c};
    *reinterpret_cast<int32_t*>(c + g_build->f_camera + kOff[axis]) = value;
    return true;
}

Camera camera() {
    Camera cam;
    uintptr_t c = client();
    if (!c) return cam;
    uintptr_t b = c + g_build->f_camera;
    cam.valid = true;
    cam.x = read_at<int32_t>(b);
    cam.z = read_at<int32_t>(b + 4);
    cam.y = read_at<int32_t>(b + 8);
    cam.pitch = read_at<int32_t>(b + 12) & 0x3fff;
    cam.yaw = read_at<int32_t>(b + 16) & 0x3fff;
    cam.pitch_target = read_at<int32_t>(b + 20);
    cam.yaw_target = read_at<int32_t>(b + 24) & 0x3fff;
    cam.focal_x = read_at<int32_t>(b + 0x78);  // camera block +0x89488, focal point +0x89500
    cam.focal_z = read_at<int32_t>(b + 0x7c);
    cam.focal_y = read_at<int32_t>(b + 0x80);
    // The camera/player-position code (0x51d0a4) works in the frame of Client[+0xc940]; the NPC
    // readers use Client[+0xc950]. Debug: report the former's base and whether they match.
    auto rwv = read_at<uintptr_t>(c + 0xc940);
    if (rwv) {
        cam.render_base_x = read_at<int32_t>(rwv + g_build->wv_base_x);
        cam.render_base_y = read_at<int32_t>(rwv + g_build->wv_base_y);
    }
    cam.render_wv_same = rwv == read_at<uintptr_t>(c + g_build->f_world_view);
    return cam;
}

int root_interface() { return g_build ? field(g_build->f_root_interface) : -1; }

// Mirrors the lookup in 0x5d1ad8 without loading missing groups (that has to happen on
// the game's own terms): groups are {?, u64 child count, slot*}, slots are {?, Widget*}.
int widget_dynamic_count(uintptr_t w) {
    if (!w) return 0;
    int n = read_at<int32_t>(w + 0xb50);
    return n < 0 || n > 4096 || !read_at<uintptr_t>(w + 0xb58) ? 0 : n;
}

uintptr_t widget_dynamic(uintptr_t w, int index) {
    if (index < 0 || index >= widget_dynamic_count(w)) return 0;
    return read_at<uintptr_t>(read_at<uintptr_t>(w + 0xb58) + static_cast<size_t>(index) * 0x10 + 8);
}

// IF_SETOBJECT (if3 widgets) and CC_SETOBJECT (op 1200) store {item id (-1 none), quantity}
// at Widget +0xb38 (one 8-byte store in the script VM, 0x5e06dc).
// Script-visible widget properties, from the CC_GET* handlers (0x4e5b20, opcodes 1600-1614) and
// the IF_SET* packet handlers: scroll x/y (+0x84/+0x88), scroll width/height (+0x8c/+0x90),
// model zoom (+0x128), model angle x/y/z (+0x11c/+0x120/+0x124), opacity (+0xb0), model type/id
// (+0xfc/+0x100), sequence (+0x10c).
// 12..: from the CC_SET* handlers (0x4e06dc, ops 1100-1120): 1105 graphic +0xc8, 1107 tiling
// +0xec (bool), 1116 outline +0xf0, 1117 graphic shadow +0xf4, 1118/1119 v/h flip +0xf8/+0xf9
// (bool), 1102 fill +0xa8 (bool), 1115 text shadow +0x194 (bool), 1114 text align x/y +0x18c/
// +0x190 and line height +0x188, 1113 font +0x150.
void widget_props(uintptr_t w, int32_t out[kWidgetProps]) {
    static constexpr struct { uint32_t off; bool byte; } kFields[kWidgetProps] = {
        {0x84}, {0x88}, {0x8c}, {0x90}, {0x128}, {0x11c}, {0x120}, {0x124}, {0xb0}, {0xfc}, {0x100}, {0x10c},
        {0xc8}, {0xec, true}, {0xf0}, {0xf4}, {0xf8, true}, {0xf9, true}, {0xa8, true}, {0x194, true},
        {0x18c}, {0x190}, {0x188}, {0x150},
        // CC_SETPOSITION (1000): original x/y +0x4c/+0x50, x/y modes +0x3c/+0x40; CC_SETSIZE (1001):
        // original width/height +0x54/+0x58, modes +0x44/+0x48; 1005/1006 no click/scroll through.
        {0x4c}, {0x50}, {0x3c}, {0x40}, {0x54}, {0x58}, {0x44}, {0x48}, {0xb98, true}, {0xb99, true},
        // Component type (WidgetType: 0 layer, 3 rectangle, 4 text, 5 graphic, 6 model, 9 line,
        // 12 = the game's text input type with its object at +0xba8), int at +0x28.
        {0x28}};
    for (int i = 0; i < kWidgetProps; i++)
        out[i] = !w ? 0 : kFields[i].byte ? read_at<uint8_t>(w + kFields[i].off) : read_at<int32_t>(w + kFields[i].off);
}

WidgetItem widget_item(uintptr_t w) {
    if (!w) return {};
    return {read_at<int32_t>(w + 0xb38), read_at<int32_t>(w + 0xb3c)};
}

// IF_SETCOLOUR (dispatcher case 0x1d) stores 0xRRGGBB at Widget +0x98, then, if the widget's
// redraw cycle (+0xb78) is the current one (Client +0x4132bc), flags its root area dirty:
// Client +0x4132c0 + Widget[+0xb74] = 1.
int widget_color(uintptr_t w) { return w ? read_at<int32_t>(w + 0x98) & 0xffffff : 0; }

void widget_set_color(uintptr_t w, int rgb) {
    uintptr_t c = client();
    if (!w || !c) return;
    rgb &= 0xffffff;
    auto* colour = reinterpret_cast<volatile int32_t*>(w + 0x98);
    if (*colour == rgb) return;
    *colour = rgb;
    if (read_at<int32_t>(w + 0xb78) == read_at<int32_t>(c + 0x4132bc)) {
        int area = read_at<int32_t>(w + 0xb74);
        if (area >= 0 && area < 0x400) *reinterpret_cast<volatile uint8_t*>(c + 0x4132c0 + area) = 1;
    }
}

namespace {
void widget_mark_dirty(uintptr_t w) {
    uintptr_t c = client();
    if (!w || !c) return;
    if (read_at<int32_t>(w + 0xb78) == read_at<int32_t>(c + 0x4132bc)) {
        int area = read_at<int32_t>(w + 0xb74);
        if (area >= 0 && area < 0x400) *reinterpret_cast<volatile uint8_t*>(c + 0x4132c0 + area) = 1;
    }
}
}  // namespace

void widget_set_hidden(uintptr_t w, bool hidden) {
    if (!w || read_at<uint8_t>(w + 0x78) == hidden) return;
    *reinterpret_cast<volatile uint8_t*>(w + 0x78) = hidden;  // CC_SETHIDE 1003
    widget_mark_dirty(w);
}

bool widget_set_text(uintptr_t w, const std::string& text) {
    if (!w || read_at<int32_t>(w + 0x28) == 12) return false;  // type 12 keeps its text elsewhere (+0xba8)
    GameString s(text);
    // CC_SETTEXT 1112: string assign 0x3f1498(widget +0x158, &text)
    reinterpret_cast<void* (*)(uintptr_t, const void*)>(g_base + 0x3f1498)(w + g_build->w_text, s.bytes);
    widget_mark_dirty(w);
    return true;
}

// Name (CC_SETOPBASE 1305, +0x2c0), target verb (CC_SETTARGETVERB 1306, +0x330) or op `which`
// (0-based, CC_SETOP 1300). An op past the vector's end is first added through setOp 0x5dfae8(widget,
// op, string*), which grows the vector (+0x2d8 header, size +0x2e0, data +0x2e8) and swaps the string
// in (an empty inline one here, so nothing is freed); then the slot is assigned with 0x3f1498 as
// CC_SETTEXT does. Game thread only.
bool widget_set_string(uintptr_t w, int which, const std::string& text) {
    if (!w || which < -2 || which > 31) return false;
    uintptr_t dst;
    if (which == -1) dst = w + 0x2c0;
    else if (which == -2) dst = w + 0x330;
    else {
        if (static_cast<uint64_t>(which) >= read_at<uint64_t>(w + 0x2e0)) {
            if (text.empty()) return true;
            GameString empty{std::string()};
            reinterpret_cast<void (*)(uintptr_t, int, void*)>(g_base + 0x5dfae8)(w, which, empty.bytes);
            if (static_cast<uint64_t>(which) >= read_at<uint64_t>(w + 0x2e0)) return false;
        }
        dst = read_at<uintptr_t>(w + 0x2e8) + static_cast<size_t>(which) * 0x18;
    }
    GameString s(text);
    reinterpret_cast<void* (*)(uintptr_t, const void*)>(g_base + 0x3f1498)(dst, s.bytes);
    return true;
}

// Event flags kept in the widget (+0x2b0): the fallback of the per-component ranges that
// IF_SETEVENTS stores (multimap at Client +0x413c30, lookups 0x460dac / 0x460e74). The menu builder
// 0x525c78 adds op i when bit i+1 is set (0x460e74 returns (flags >> 1) & 0x3ff) or the widget has
// an onOp listener (+0x6a0 / +0x6b0..+0x6b8).
int widget_flags(uintptr_t w) { return w ? read_at<int32_t>(w + 0x2b0) : 0; }
// As the lookups 0x460dac / 0x460e74: bucket = component % count (Client +0x413c38) in the table at
// +0x413c30; walk the chain (next at node +0x18) to the first node of this component, then over its
// nodes {+0 component, +4 start, +8 end, +0xc flags, +0x10 op mask} for a range holding the index.
bool widget_events(uintptr_t w, int out[2]) {
    uintptr_t c = client();
    if (!w || !c) return false;
    uint32_t count = read_at<uint32_t>(c + 0x413c38);
    uintptr_t buckets = read_at<uintptr_t>(c + 0x413c30);
    if (!count || !buckets) return false;
    uint32_t comp = read_at<uint32_t>(w + 4);
    int32_t index = read_at<int32_t>(w + 8);
    uintptr_t n = read_at<uintptr_t>(buckets + static_cast<uintptr_t>(comp % count) * 8);
    int guard = 0;
    while (n && read_at<uint32_t>(n) != comp && ++guard < 100000) n = read_at<uintptr_t>(n + 0x18);
    for (; n && read_at<uint32_t>(n) == comp && ++guard < 100000; n = read_at<uintptr_t>(n + 0x18)) {
        if (read_at<int32_t>(n + 4) <= index && index <= read_at<int32_t>(n + 8)) {
            out[0] = read_at<int32_t>(n + 0xc);
            out[1] = read_at<int32_t>(n + 0x10);
            return true;
        }
    }
    return false;
}
// Every IF_SETEVENTS range in the table at Client +0x413c30: {component, start, end, flags, op mask} per node.
bool widget_event_ranges(std::vector<int32_t>& out) {
    out.clear();
    uintptr_t c = client();
    if (!c) return false;
    uint32_t count = read_at<uint32_t>(c + 0x413c38);
    uintptr_t buckets = read_at<uintptr_t>(c + 0x413c30);
    if (!count || !buckets) return true;  // none set
    if (count > (1u << 20)) return false;
    // Chains may run on into other buckets' nodes (lookups skip to their component): each node once.
    std::unordered_set<uintptr_t> seen;
    for (uint32_t b = 0; b < count; b++) {
        int guard = 0;
        for (uintptr_t n = read_at<uintptr_t>(buckets + static_cast<uintptr_t>(b) * 8); n && ++guard < 100000; n = read_at<uintptr_t>(n + 0x18)) {
            if (!seen.insert(n).second) break;
            for (int k = 0; k < 5; k++) out.push_back(read_at<int32_t>(n + 4 * k));
        }
    }
    return true;
}

void widget_set_flags(uintptr_t w, int flags) {
    if (w) *reinterpret_cast<volatile int32_t*>(w + 0x2b0) = flags;
}

namespace {
// The {ctrl, Widget*} slot holding a component (group table) or a CC_CREATE child (+0xb58 array).
uintptr_t widget_slot(int component, int index) {
    uintptr_t c = client();
    if (!c || component < 0) return 0;
    auto im = read_at<uintptr_t>(c + g_build->f_interfaces);
    if (!im) return 0;
    uint64_t group = static_cast<uint32_t>(component) >> 16, child = component & 0xffff;
    if (group >= read_at<uint64_t>(im + 0x6888)) return 0;
    uintptr_t g = read_at<uintptr_t>(im + 0x6890) + group * 0x18;
    if (child >= read_at<uint64_t>(g + 8)) return 0;
    auto slots = read_at<uintptr_t>(g + 0x10);
    if (!slots) return 0;
    uintptr_t slot = slots + child * 0x10;
    if (index < 0) return read_at<uintptr_t>(slot + 8) ? slot : 0;
    uintptr_t w = read_at<uintptr_t>(slot + 8);
    if (!w || index >= widget_dynamic_count(w)) return 0;
    slot = read_at<uintptr_t>(w + 0xb58) + static_cast<size_t>(index) * 0x10;
    return read_at<uintptr_t>(slot + 8) ? slot : 0;
}
}  // namespace

// Writable int fields (CC_SET* handlers, docs/offsets.md): a whitelist so a bad offset can't be written.
bool widget_set_field(int component, int index, uint32_t off, int value) {
    static constexpr struct { uint32_t off; bool byte; } kFields[] = {
        {0x3c}, {0x40}, {0x44}, {0x48}, {0x4c}, {0x50}, {0x54}, {0x58},  // modes, original x/y/w/h
        {0xa8, true}, {0xb0}, {0xc8}, {0xec, true}, {0xf0}, {0xf4}, {0xf8, true}, {0xf9, true},
        {0x150}, {0x18c}, {0x190}, {0x194, true}, {0xb98, true}, {0xb99, true},
        {0xb38}, {0xb3c},  // item id / quantity (as widget_item reads them; Widget.setItemId/Quantity)
        {0x84}, {0x88}, {0x8c}, {0x90},  // scroll x/y, scroll width/height (IF_SETSCROLLPOS / CC_SETSCROLLSIZE fields)
        {0x128}, {0x11c}, {0x120}, {0x124},  // model zoom, model angle x/y/z
        {0x13c},  // item quantity mode (CC_SETOBJECT* 1200/1205/1212: 1/3, 0, 3)
        {0x320}, {0x324},  // drag dead zone / dead time (ints, read by the drag block of 0x41d0a4)
        {0xfc}, {0x100}, {0x10c},  // model type / id, animation (CC_SETMODEL* 1108, 1110)
        {0x188},  // line height (CC_SETTEXTALIGN 1114; type 12 keeps it in its +0xba8 object instead)
        {0x5c}, {0x60}, {0x64}, {0x68},  // laid-out x/y/width/height (what layout writes; desktop's setRelativeX..)
        {0x28}, {0x30},  // type (CC_CREATE writes it), content type (if1 client code; 1337 = viewport, read by the renderer 0x459bf8)
        {0x308}};  // target verb op index (CC 1312: -1 -> 4, else op - 1 if < 32; the menu builder 0x525c78 inserts the verb there)
    uintptr_t slot = widget_slot(component, index);
    uintptr_t w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!w) return false;
    // Type 12 keeps a text input object at +0xba8 the game dereferences by type: never switch into or out of it.
    if (off == 0x28 && (value == 12 || read_at<int32_t>(w + 0x28) == 12)) return false;
    if (off == 0x308 && static_cast<uint32_t>(value) >= 32) return false;  // the CC setter's range
    for (const auto& f : kFields) {
        if (f.off != off) continue;
        if (f.byte) *reinterpret_cast<volatile uint8_t*>(w + off) = value != 0;
        else *reinterpret_cast<volatile int32_t*>(w + off) = value;
        uintptr_t c = client();
        if (read_at<int32_t>(w + 0xb78) == read_at<int32_t>(c + 0x4132bc)) {
            int area = read_at<int32_t>(w + 0xb74);
            if (area >= 0 && area < 0x400) *reinterpret_cast<volatile uint8_t*>(c + 0x4132c0 + area) = 1;
        }
        return true;
    }
    return false;
}

// Reads one int field of a widget (or a byte for +0x348, the "has a listener" flag the CC_SETON*
// setter 0x4e4fe0 sets); the offsets widget_set_field allows plus +0x348. INT32_MIN if unknown.
int widget_get_field(int component, int index, uint32_t off) {
    uintptr_t slot = widget_slot(component, index);
    uintptr_t w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!w) return INT32_MIN;
    switch (off) {
        case 0x348: return read_at<uint8_t>(w + off);
        case 0x320: case 0x324: case 0x13c: case 0x30: case 0x308: return read_at<int32_t>(w + off);
        default: return INT32_MIN;
    }
}

// Drag parent (CC_SETDRAGGABLE 1301 in 0x4e06dc): a shared_ptr {ctrl +0x310, Widget* +0x318} to the
// container slot the widget may be dragged in, read by the drag block of 0x41d0a4.
bool widget_drag_parent(int component, int index, int* parent, int* parent_index) {
    uintptr_t slot = widget_slot(component, index);
    uintptr_t w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    return w && widget_ref(read_at<uintptr_t>(w + 0x318), parent, parent_index);
}

// As CC_SETDRAGGABLE: takes a reference on the parent's slot and releases the old one; parent < 0 clears it.
bool widget_set_drag_parent(int component, int index, int parent, int parent_index) {
    uintptr_t slot = widget_slot(component, index);
    uintptr_t w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!w) return false;
    uintptr_t ctrl = 0, pw = 0;
    if (parent >= 0) {
        uintptr_t ps = widget_slot(parent, parent_index);
        if (!ps || !read_at<uintptr_t>(ps + 8)) return false;
        ctrl = read_at<uintptr_t>(ps);
        pw = read_at<uintptr_t>(ps + 8);
        if (ctrl) __atomic_fetch_add(reinterpret_cast<int32_t*>(ctrl + 8), 1, __ATOMIC_ACQ_REL);
    }
    uintptr_t old = read_at<uintptr_t>(w + 0x310);
    *reinterpret_cast<volatile uintptr_t*>(w + 0x318) = pw;
    *reinterpret_cast<volatile uintptr_t*>(w + 0x310) = ctrl;
    release_shared_ptr(old);
    return true;
}

// What CC_SETPOSITION/CC_SETSIZE run after storing (0x4e06dc ops 1000/1001): 0x44a788(client, slot),
// 0x45e304(client, slot), then for layers (type 0) 0x45ebf4(client, children, slot, 0) with the
// children container from 0x5d38c4(im, group) (static) or the parent's +0xb48 (CC_CREATE child).
bool widget_revalidate(int component, int index) {
    uintptr_t c = client();
    uintptr_t slot = widget_slot(component, index);
    uintptr_t w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!c || !w) return false;
    reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(g_base + 0x44a788)(c, slot);
    reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(g_base + 0x45e304)(c, slot);
    uintptr_t im = reinterpret_cast<uintptr_t (*)(uintptr_t)>(g_base + 0x459be8)(c);
    if (read_at<int32_t>(w + 0x28) == 0 && im) {
        uintptr_t children;
        if (read_at<int32_t>(w + 8) == -1) {
            children = reinterpret_cast<uintptr_t (*)(uintptr_t, long)>(g_base + 0x5d38c4)(im, read_at<int16_t>(w + 6));
        } else {
            uintptr_t parent = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + 0x5d1ad8)(im, read_at<int32_t>(w + 0x74));
            children = parent ? read_at<uintptr_t>(parent + 8) + 0xb48 : 0;
        }
        if (children) reinterpret_cast<void (*)(uintptr_t, uintptr_t, uintptr_t, int)>(g_base + 0x45ebf4)(c, children, slot, 0);
    }
    return true;
}

// Component id and CC_CREATE index (-1 static) of a Widget*: a created child keeps its index at
// +0x08 and its parent component at +0x74; a static one only its group (+0x06), so its slot is
// looked up in the group table. Confirmed against the slot, so a bad pointer gives false.
bool widget_ref(uintptr_t w, int* component, int* index) {
    if (!w || !client()) return false;
    int idx = read_at<int32_t>(w + 8);
    if (idx != -1) {
        int parent = read_at<int32_t>(w + 0x74);
        uintptr_t slot = widget_slot(parent, idx);
        if (!slot || read_at<uintptr_t>(slot + 8) != w) return false;
        *component = parent;
        *index = idx;
        return true;
    }
    int group = read_at<uint16_t>(w + 6);
    auto im = read_at<uintptr_t>(client() + g_build->f_interfaces);
    if (!im || static_cast<uint64_t>(group) >= read_at<uint64_t>(im + 0x6888)) return false;
    uintptr_t g = read_at<uintptr_t>(im + 0x6890) + static_cast<uint64_t>(group) * 0x18;
    uint64_t n = read_at<uint64_t>(g + 8);
    auto slots = read_at<uintptr_t>(g + 0x10);
    if (!slots || n > 0x10000) return false;
    for (uint64_t i = 0; i < n; i++)
        if (read_at<uintptr_t>(slots + i * 0x10 + 8) == w) {
            *component = group << 16 | static_cast<int>(i);
            *index = -1;
            return true;
        }
    return false;
}

// CC_CREATE (script op 100 in 0x4e06dc) for Widget.createChild: capacity/replace 0x4f2898(parent
// slot, index, 0) (an existing child at the index is deleted, as CC_CREATE without its 4th arg),
// make_shared<Widget>(out, &parent id) 0x4f290c, the same fields the op writes (+0x00 = 1, +0x08 index,
// +0x28 type, +0x74 parent id), then the move-assign 0x44a538 into the parent's +0xb58 slot.
// Static parents of layer type only; text inputs (type 12) need the op's extra setup. Game thread.
int widget_create_child(int component, int index, int type) {
    if (!client() || type < 0 || type > 11 || index > 4095) return -1;
    uintptr_t slot = widget_slot(component, -1);
    uintptr_t parent = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!parent || read_at<int32_t>(parent + 0x28) != 0) return -1;
    if (index < 0) index = widget_dynamic_count(parent);
    if (!(reinterpret_cast<uint64_t (*)(uintptr_t, int, bool)>(g_base + 0x4f2898)(slot, index, false) & 1)) return -1;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    // Not an x8 return: the out pointer is x0 and the id pointer x1 (disassembly at 0x4f290c).
    SharedPtr sp{0, 0};
    reinterpret_cast<void (*)(SharedPtr*, const int32_t*)>(g_base + 0x4f290c)(&sp, reinterpret_cast<const int32_t*>(parent + 4));
    if (!sp.obj) return -1;
    *reinterpret_cast<uint8_t*>(sp.obj) = 1;
    *reinterpret_cast<int32_t*>(sp.obj + 8) = index;
    *reinterpret_cast<int32_t*>(sp.obj + 0x28) = type;
    *reinterpret_cast<int32_t*>(sp.obj + 0x74) = read_at<int32_t>(parent + 4);
    uintptr_t dst = read_at<uintptr_t>(parent + 0xb58) + static_cast<size_t>(index) * 0x10;
    reinterpret_cast<void* (*)(uintptr_t, SharedPtr*)>(g_base + 0x44a538)(dst, &sp);  // moves sp out
    return index;
}

// Widget.setChildren(Arrays.copyOf(children, n)) (Bank Tags' rebuildTabs): the CC_CREATE child
// vector {?, u64 count +0xb50, data +0xb58} is resized with the game's own resize 0x40a25c(vec, n, 3)
// (flag 2 moves the first n shared_ptrs into the new storage, the rest are released; the same call
// the capacity check 0x4f2898 makes to grow it). Shrinks only. Game thread.
bool widget_truncate_children(int component, int count) {
    uintptr_t slot = widget_slot(component, -1);
    uintptr_t w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!client() || !w || count < 0) return false;
    int n = widget_dynamic_count(w);
    if (count >= n) return count == n;
    reinterpret_cast<void (*)(uintptr_t, uint64_t, uint32_t)>(g_base + 0x40a25c)(w + 0xb48, static_cast<uint64_t>(count), 3);
    return true;
}

// CC_DELETEALL (script op 101): 0x5df8e0(widget) releases every CC_CREATE child, then
// 0x44a788(client, slot) as the op does. Game thread.
bool widget_delete_children(int component) {
    uintptr_t c = client();
    uintptr_t slot = widget_slot(component, -1);
    uintptr_t w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!c || !w) return false;
    reinterpret_cast<void (*)(uintptr_t)>(g_base + 0x5df8e0)(w);
    reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(g_base + 0x44a788)(c, slot);
    return true;
}

uintptr_t widget(int component) {
    uintptr_t c = client();
    if (!c || component < 0) return 0;
    auto im = read_at<uintptr_t>(c + g_build->f_interfaces);
    if (!im) return 0;
    uint64_t group = static_cast<uint32_t>(component) >> 16, child = component & 0xffff;
    if (group >= read_at<uint64_t>(im + 0x6888)) return 0;
    uintptr_t g = read_at<uintptr_t>(im + 0x6890) + group * 0x18;
    if (child >= read_at<uint64_t>(g + 8)) return 0;
    auto slots = read_at<uintptr_t>(g + 0x10);
    return slots ? read_at<uintptr_t>(slots + child * 0x10 + 8) : 0;
}

WidgetGeometry widget_geometry(uintptr_t w) {
    WidgetGeometry g;
    if (!w) return g;
    g.x = read_at<int32_t>(w + g_build->w_x);
    g.y = read_at<int32_t>(w + g_build->w_y);
    g.width = read_at<int32_t>(w + g_build->w_width);
    g.height = read_at<int32_t>(w + g_build->w_height);
    g.parent = read_at<int32_t>(w + g_build->w_parent);
    g.group = read_at<uint16_t>(w + g_build->w_group);
    if (g.parent == -1) {
        // A group's root layer: its parent is the component the group is mounted in (as CC_GETLAYER does).
        for (const SubInterface& sub : open_sub_interfaces())
            if (sub.group == g.group) { g.parent = sub.component; break; }
    }
    return g;
}

int widget_child_count(int group) {
    uintptr_t c = client();
    if (!c || group < 0) return 0;
    auto im = read_at<uintptr_t>(c + g_build->f_interfaces);
    if (!im || static_cast<uint64_t>(group) >= read_at<uint64_t>(im + 0x6888)) return 0;
    return static_cast<int>(read_at<uint64_t>(read_at<uintptr_t>(im + 0x6890) + group * 0x18 + 8));
}

bool widget_hidden(uintptr_t w) { return w && read_at<uint8_t>(w + g_build->w_hidden) != 0; }
std::string widget_text(uintptr_t w) { return w ? read_string(w + g_build->w_text) : std::string(); }

std::vector<std::string> widget_strings(uintptr_t w) {
    std::vector<std::string> out;
    if (!w) return out;
    out.push_back(read_string(w + 0x2c0));
    out.push_back(read_string(w + 0x330));
    uint64_t n = read_at<uint64_t>(w + 0x2e0);
    uintptr_t ops = read_at<uintptr_t>(w + 0x2e8);
    if (ops && n <= 64)
        for (uint64_t i = 0; i < n; i++) out.push_back(read_string(ops + i * 0x18));
    return out;
}

std::vector<SubInterface> open_sub_interfaces() {
    std::vector<SubInterface> out;
    uintptr_t c = client();
    if (!c) return out;
    auto buckets = read_at<uintptr_t>(c + g_build->f_subs_buckets);
    auto count = read_at<uint64_t>(c + g_build->f_subs_count) & 0xffffffff;
    if (!buckets || !count || count > 1 << 20) return out;
    uintptr_t sentinel = read_at<uintptr_t>(buckets + count * 8);
    for (uint64_t b = 0; b < count; b++) {
        int guard = 0;
        for (auto node = read_at<uintptr_t>(buckets + b * 8); node && node != sentinel && guard++ < 1024;
             node = read_at<uintptr_t>(node + 0x10)) {
            out.push_back({read_at<int32_t>(node), read_at<int32_t>(node + 4), read_at<int32_t>(node + 8)});
        }
    }
    return out;
}

// Closes the sub-interface `group` mounted in `component`, as IF_CLOSESUB (dispatcher case 0x65) does:
// `0x462384(client, component, group, unload)` removes the component-table node, unloads the group
// when `unload` (bit 0), drops the group's IF_SETEVENTS ranges, marks the parent dirty and lays out
// the root again. Only called when the node is open (the packet handler checks the same).
// Game thread only. Verified 2026-10-17 gen 29 (closing the bank, group 12, in 601:27).
bool close_sub_interface(int component, int group, bool unload) {
    uintptr_t c = client();
    if (!c) return false;
    bool open = false;
    for (const SubInterface& s : open_sub_interfaces()) open |= s.component == component && s.group == group;
    if (!open) return false;
    reinterpret_cast<void (*)(uintptr_t, int, uint32_t, uint64_t)>(g_base + 0x462384)(c, component, static_cast<uint32_t>(group), unload ? 1 : 0);
    return true;
}

// TRIGGER_ONDIALOGABORT (server packet 107, case 0x6b of 0x441dd4 at 0x444a48): with a root interface
// (Client +0x413a88 != -1) `0x4405cc(client, root, 0x17)` runs onDialogAbort (+0x8d8) over the root's tree
// and its mounted sub-interfaces (0x45f5dc). Game thread only.
bool trigger_dialog_abort() {
    uintptr_t c = client();
    if (!c) return false;
    int root = read_at<int32_t>(c + 0x413a88);
    if (root == -1) return false;
    reinterpret_cast<void (*)(uintptr_t, int, int)>(g_base + 0x4405cc)(c, root, 0x17);
    return true;
}

// RuneLite's Client.openInterface: IF_OPENSUB's helper `0x462640(client, component, group, mode)` adds (or
// re-uses) the component-table node, loads `group` into `component`, runs its onLoad and lays out the root.
// Like desktop it doesn't close a node already in `component` (the packet handler does that first). It
// dereferences the lookup of `component` unchecked, so both ids are validated first. Game thread only.
bool open_sub_interface(int component, int group, int mode) {
    uintptr_t c = client();
    if (!c || group < 0 || group > 0xffff || !widget_slot(component, -1)) return false;
    auto im = read_at<uintptr_t>(c + g_build->f_interfaces);
    if (!im || static_cast<uint64_t>(group) >= read_at<uint64_t>(im + 0x6888)) return false;
    reinterpret_cast<uintptr_t (*)(uintptr_t, int, int, int)>(g_base + 0x462640)(c, component, group, mode);
    return true;
}

// Minimap (`mm = *(Client+0x40e970)`, built by 0x58776c/0x589120): ARGB raster {u64 pixel count
// +0x6440, int* pixels +0x6450, width/height +0x6458/+0x645c}, zoom (px per tile) +0x6470, image
// size zoom*104 + 96; +0xc = the plane drawn into it (the cycle redraws when the render world
// view's plane, Client[+0xc940]+0x18, differs: 0x41f22c). 0x589180(mm, plane, Scene*) clears it to
// 0xff000000 and draws every tile, wall and map scenery of `plane` (desktop's minimap renderer).
// Static RE gen 29 (subagent). Verified 2026-10-17 gen 29 with Instance Map (planes 0 and 2).
bool draw_minimap(int plane, std::vector<int32_t>& out) {
    uintptr_t c = client();
    if (!c || plane < 0 || plane > 3) return false;
    uintptr_t mm = read_at<uintptr_t>(c + 0x40e970);
    uintptr_t wv = read_at<uintptr_t>(c + 0xc940);
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    if (!mm || !scene || !read_at<uintptr_t>(mm + 0x65b8)) return false;
    auto* px = read_at<int32_t*>(mm + 0x6450);
    uint64_t count = read_at<uint64_t>(mm + 0x6440);
    int zoom = read_at<int32_t>(mm + 0x6470), w = read_at<int32_t>(mm + 0x6458), h = read_at<int32_t>(mm + 0x645c);
    int size = zoom * 104 + 96;
    if (!px || zoom < 2 || zoom > 8 || w != size || h != size || count != static_cast<uint64_t>(size) * size) return false;
    int shown = read_at<int32_t>(mm + 0xc);
    using Render = void (*)(uintptr_t, int, uintptr_t);
    auto render = reinterpret_cast<Render>(g_base + 0x589180);
    render(mm, plane, scene);
    // Desktop layout: the extended scene (184 tiles, 4 px each = 736 px); the mobile scene's 104
    // tiles are its middle (5 chunks in: 160 px), which is what Instance Map crops with
    // getExpandedMapLoading() = 0. The game's image has its 104 tiles at 48 px, zoom px each:
    // nearest-neighbour when zoom isn't 4.
    out.assign(kInstanceMapSize * kInstanceMapSize, 0);
    for (int y = 0; y < 416; y++) {
        int sy = 48 + y * zoom / 4;
        for (int x = 0; x < 416; x++) {
            int sx = 48 + x * zoom / 4;
            if (sx < size && sy < size) out[(160 + y) * kInstanceMapSize + 160 + x] = px[sy * size + sx] & 0xffffff;
        }
    }
    // Put the game's image back (0x589180 doesn't touch +0xc; at -1 the cycle redraws anyway).
    if (shown >= 0 && shown <= 3) render(mm, shown, scene);
    return true;
}

Skills skills() {
    Skills out;
    uintptr_t c = client();
    if (!c) return out;
    for (int i = 0; i < kSkillCount; i++) {
        out.current[i] = read_at<int32_t>(c + g_build->f_skill_current + i * 4);
        out.real[i] = read_at<int32_t>(c + g_build->f_skill_real + i * 4);
        out.xp[i] = read_at<int32_t>(c + g_build->f_skill_xp + i * 4);
    }
    return out;
}

std::vector<Inventory> read_inventories() {
    std::vector<Inventory> out;
    if (!g_build) return out;
    uintptr_t map = g_base + g_build->inventories;
    auto buckets = read_at<uintptr_t>(map + 8);
    auto count = read_at<uint64_t>(map + 0x10) & 0xffffffff;
    if (!buckets || !count || count > 1 << 16) return out;
    uintptr_t sentinel = read_at<uintptr_t>(buckets + count * 8);
    for (uint64_t b = 0; b < count; b++) {
        int guard = 0;
        for (auto node = read_at<uintptr_t>(buckets + b * 8); node && node != sentinel && guard++ < 1024;
             node = read_at<uintptr_t>(node + 0x38)) {
            Inventory inv;
            inv.id = read_at<int32_t>(node);
            auto ids = read_at<const int32_t*>(node + 0x08), ids_end = read_at<const int32_t*>(node + 0x10);
            auto qty = read_at<const int32_t*>(node + 0x20), qty_end = read_at<const int32_t*>(node + 0x28);
            size_t n = ids && ids_end > ids ? static_cast<size_t>(ids_end - ids) : 0;
            size_t nq = qty && qty_end > qty ? static_cast<size_t>(qty_end - qty) : 0;
            if (n > 4096) continue;
            inv.ids.assign(ids, ids + n);
            inv.quantities.assign(n, 0);
            for (size_t i = 0; i < n && i < nq; i++) inv.quantities[i] = qty[i];
            out.push_back(std::move(inv));
        }
    }
    return out;
}

int next_message_id() { return g_build ? read_at<int32_t>(g_base + g_build->next_message_id) : -1; }

// Message (built by 0x3f0a04): +0x08 cycle, +0x40 type, +0x48 name, +0x70 sender, +0xa0 text.
bool read_message(int id, ChatLine& out) {
    if (!g_build) return false;
    uintptr_t map = g_base + g_build->messages_by_id;
    auto buckets = read_at<uintptr_t>(map + 8);
    auto nb = read_at<uint32_t>(map + 0x10);
    if (!buckets || !nb) return false;
    for (auto node = read_at<uintptr_t>(buckets + (static_cast<uint32_t>(id) % nb) * 8); node;
         node = read_at<uintptr_t>(node + 0x18)) {
        if (read_at<int32_t>(node) != id) continue;
        auto msg = read_at<uintptr_t>(node + 0x10);
        if (!msg) return false;
        out.id = id;
        out.cycle = read_at<int32_t>(msg + 0x08);
        out.type = read_at<int32_t>(msg + 0x40);
        out.name = read_string(msg + 0x48);
        out.sender = read_string(msg + 0x70);
        out.text = read_string(msg + 0xa0);
        return true;
    }
    return false;
}

bool message_set_text(int id, const std::string& text) {
    if (!g_build) return false;
    uintptr_t map = g_base + g_build->messages_by_id;
    auto buckets = read_at<uintptr_t>(map + 8);
    auto nb = read_at<uint32_t>(map + 0x10);
    if (!buckets || !nb) return false;
    for (auto node = read_at<uintptr_t>(buckets + (static_cast<uint32_t>(id) % nb) * 8); node;
         node = read_at<uintptr_t>(node + 0x18)) {
        if (read_at<int32_t>(node) != id) continue;
        auto msg = read_at<uintptr_t>(node + 0x10);
        if (!msg) return false;
        GameString s(text);
        reinterpret_cast<void* (*)(uintptr_t, const void*)>(g_base + 0x3f1498)(msg + 0xa0, s.bytes);
        return true;
    }
    return false;
}

// Chat channels (global map 0xaab5e0, same layout as messages_by_id; node {int type; +0x08
// channel*; +0x10 next}): a ring of 100 {ctrl*, Message*} pairs, +0x640 count, +0x644 newest
// index (0x3ef964 adds). 0x43aacc(client) bumps the chat cycle (Client +0x413e6c) so the chatbox
// rebuilds, as after any new message.
namespace {
constexpr uintptr_t kChatChannels = 0xaab5e0, kChatChanged = 0x43aacc;
// All messages in arrival order: libc++ std::list {prev, next, size} at 0xaab610, newest first
// (push_front in the Message constructor 0x3f0a04); node {prev, next, int id}; Message +0 = node.
// The chatbox's All tab walks it by uid (chat_getprevuid 0x3f061c / chat_getnextuid 0x3f0694).
constexpr uintptr_t kChatList = 0xaab610;
uintptr_t chat_channel(int type) {
    uintptr_t map = g_base + kChatChannels;
    auto buckets = read_at<uintptr_t>(map + 8);
    auto nb = read_at<uint32_t>(map + 0x10);
    if (!buckets || !nb) return 0;
    for (auto node = read_at<uintptr_t>(buckets + (static_cast<uint32_t>(type) % nb) * 8); node;
         node = read_at<uintptr_t>(node + 0x10))
        if (read_at<int32_t>(node) == type) return read_at<uintptr_t>(node + 8);
    return 0;
}
}  // namespace

// Idle logout (0x41d0a4, IDLE = client prot 97 at 0x4216f8): the game logs out when
// settings->AFKTimeout (minutes; Settings* at Client +0x2278, int +0x154c, 5 by default) * 60000
// ms have passed since the last input (Client +0x413270, ms).
// ---- World map. Static RE gen 31 of the worldmap_* handler (ELF 0x4c3cf8): zoom 6603 = float
// wm+0xfc (0x4b7570 maps 1/1.5/2/3/4/else to 25/37/50/75/100/200), isloaded 6605 = byte
// *(wm+0xd8)+0x14 (0x4b6158), display position 6610 = wm+0xe0/+0xe4 + current area's
// regionLowX/Y (+0x50/+0x58) * 64 (0x4b795c/0x4b797c), current map 6616 = *(int*)area (area = wm+0xb8),
// coordinmap 6621 = area lookup by id in the wm+0x78 bucket table (0x4b6be0) and the area's sections
// (+0x68..+0x70, 16-byte {ctrl, section*}) vtable slot 6 contains(level, x, z) (0x5c57f4).
static constexpr uintptr_t kClientWorldMap = 0x418660;

static uintptr_t world_map() {
    uintptr_t c = client();
    return c ? read_at<uintptr_t>(c + kClientWorldMap) : 0;
}

WorldMapState world_map_state() {
    WorldMapState st;
    uintptr_t wm = world_map();
    if (!g_build || !wm) return st;
    st.present = true;
    uintptr_t r = read_at<uintptr_t>(wm + 0xd8);
    st.loaded = r && read_at<uint8_t>(r + 0x14) != 0;
    st.zoom = read_at<float>(wm + 0xfc);
    uintptr_t area = read_at<uintptr_t>(wm + 0xb8);
    if (area) {
        st.map = read_at<int32_t>(area);
        st.x = read_at<int32_t>(wm + 0xe0) + read_at<int32_t>(area + 0x50) * 64;
        st.y = read_at<int32_t>(wm + 0xe4) + read_at<int32_t>(area + 0x58) * 64;
    }
    return st;
}

int world_map_contains(int map, int level, int x, int z) {
    uintptr_t wm = world_map();
    if (!g_build || !wm) return -1;
    uintptr_t buckets = read_at<uintptr_t>(wm + 0x78);
    uint64_t count = read_at<uint64_t>(wm + 0x80);
    if (!buckets || count == 0 || count > 4096) return -1;
    uintptr_t area = 0;
    for (uint64_t b = 0; b < count && !area; b++) {
        int guard = 0;
        for (uintptr_t node = read_at<uintptr_t>(buckets + b * 8); node && guard < 4096; guard++) {
            uintptr_t a = read_at<uintptr_t>(node + 0x20);
            if (a && read_at<int32_t>(a) == map) { area = a; break; }
            uintptr_t next = read_at<uintptr_t>(node + 0x28);
            // The last node of a bucket chains into the next bucket's first node (0x4b6be0 walks
            // on until the end sentinel); stop at the bucket boundary instead.
            if (next == read_at<uintptr_t>(buckets + (b + 1) * 8)) break;
            node = next;
        }
    }
    if (!area) return -1;
    uintptr_t it = read_at<uintptr_t>(area + 0x68), end = read_at<uintptr_t>(area + 0x70);
    if (it > end || end - it > 0x10 * 4096) return -1;
    for (; it < end; it += 0x10) {
        uintptr_t sec = read_at<uintptr_t>(it + 8);
        if (!sec) continue;
        uintptr_t vt = read_at<uintptr_t>(sec);
        auto contains = reinterpret_cast<uint32_t (*)(uintptr_t, int, int, int)>(read_at<uintptr_t>(vt + 0x30));
        if (contains(sec, level, x, z) & 1) return 1;
    }
    return 0;
}

bool world_map_jump(int level, int x, int z) {
    uintptr_t wm = world_map();
    if (!g_build || !wm) return false;
    reinterpret_cast<void (*)(uintptr_t, int, int, int)>(g_base + 0x4b7854)(wm, level, x, z);
    return true;
}

// Map regions and their icons. Static RE gen 32: worldmap_listelement_start (6639, ELF 0x4b8f0c)
// requires the loaded byte *(wm+0xd8)+0x14 and *(*(wm+0xd0)+8), then has the icon map rebuilt from
// the region grid of the manager *(wm+0xd0) (0x5caf1c): rows at +0x68 (count +0x60), 0x18-byte
// {?, count +8, Region** +0x10}; a region holds a std::list of icon shared_ptrs at +0x38 (node: +0
// next, +0x10 control block, +0x18 icon) and a hash table at +0x58 (buckets) / +0x60 (bucket count;
// buckets[count] is the end sentinel; node: +8 control block, +0x10 icon, +0x18 next, a null next
// continues with the next bucket). Never call into the control block (slot 1 frees it). Icon: vtable
// slot 1 = element (map element id, -1 = none), +8 level, +0xc x, +0x10 y (the coordinate 6639
// pushes). Out: per icon {row, col, element, level, x, y}.
bool world_map_icons(int& rows, int& cols, std::vector<int>& out) {
    rows = cols = 0;
    uintptr_t wm = world_map();
    if (!g_build || !wm) return false;
    uintptr_t r = read_at<uintptr_t>(wm + 0xd8), mgr = read_at<uintptr_t>(wm + 0xd0);
    if (!r || !mgr || !read_at<uint8_t>(r + 0x14) || !read_at<uint8_t>(mgr + 8)) return false;
    uint64_t nrows = read_at<uint64_t>(mgr + 0x60);
    uintptr_t rowp = read_at<uintptr_t>(mgr + 0x68);
    if (!rowp || nrows > 512) return false;
    rows = static_cast<int>(nrows);
    auto add = [&](int row, int col, uintptr_t icon) {
        if (!icon || out.size() > 6 * 50000) return;
        uintptr_t vt = read_at<uintptr_t>(icon);
        if (!vt) return;
        int element = reinterpret_cast<int (*)(uintptr_t)>(read_at<uintptr_t>(vt + 8))(icon);
        if (element < 0) return;
        out.insert(out.end(), {row, col, element, read_at<int32_t>(icon + 8), read_at<int32_t>(icon + 0xc),
                               read_at<int32_t>(icon + 0x10)});
    };
    for (uint64_t i = 0; i < nrows; i++) {
        uint64_t ncols = read_at<uint64_t>(rowp + i * 0x18 + 8);
        uintptr_t regs = read_at<uintptr_t>(rowp + i * 0x18 + 0x10);
        if (!regs || ncols > 512) continue;
        if (static_cast<int>(ncols) > cols) cols = static_cast<int>(ncols);
        for (uint64_t j = 0; j < ncols; j++) {
            uintptr_t reg = read_at<uintptr_t>(regs + j * 8);
            if (!reg) continue;
            int guard = 0;
            for (uintptr_t n = read_at<uintptr_t>(reg + 0x38); n && n != reg + 0x38 && guard < 4096; guard++) {
                add(static_cast<int>(i), static_cast<int>(j), read_at<uintptr_t>(n + 0x18));
                n = read_at<uintptr_t>(n);
            }
            uintptr_t buckets = read_at<uintptr_t>(reg + 0x58);
            uint64_t nb = read_at<uint64_t>(reg + 0x60);
            if (!buckets || nb == 0 || nb > 65536) continue;
            // As 0x5caf1c: from the first non-null bucket follow +0x18, skipping to the next
            // non-null bucket on null, until the end sentinel buckets[nb].
            uintptr_t end = read_at<uintptr_t>(buckets + nb * 8);
            uint64_t b = 0;
            uintptr_t n = 0;
            while (b < nb && !(n = read_at<uintptr_t>(buckets + b * 8))) b++;
            for (guard = 0; b < nb && n && n != end && guard < 65536; guard++) {
                add(static_cast<int>(i), static_cast<int>(j), read_at<uintptr_t>(n + 0x10));
                n = read_at<uintptr_t>(n + 0x18);
                while (!n && ++b <= nb) n = read_at<uintptr_t>(buckets + b * 8);
            }
        }
    }
    return true;
}

// Map element (mel) configs, static RE gen 32: CS2 MEC_TEXT/TEXTSIZE/CATEGORY/SPRITE (6693-6696 in
// 0x4c3cf8) read 0x63529c(id) = table[id], the table being the vector {+8 count, +0x10 data} at ELF
// 0x1212620 (filled by the config loader 0x634394; opcode 19 = category +0x180, sprite +4, textsize +0x30).
bool map_element(int id, int& category, int& sprite) {
    if (!g_build || id < 0) return false;
    uint64_t count = read_at<uint64_t>(g_base + 0x1212628);
    uintptr_t data = read_at<uintptr_t>(g_base + 0x1212630);
    if (!data || static_cast<uint64_t>(id) >= count) return false;
    uintptr_t mel = read_at<uintptr_t>(data + static_cast<uintptr_t>(id) * 8);
    if (!mel) return false;
    category = read_at<int32_t>(mel + 0x180);
    sprite = read_at<int32_t>(mel + 4);
    return true;
}

int afk_timeout_minutes() {
    uintptr_t c = client();
    if (!g_build || !c) return -1;
    auto settings = read_at<uintptr_t>(c + 0x2278);
    return settings ? read_at<int32_t>(settings + 0x154c) : -1;
}
bool set_afk_timeout_minutes(int minutes) {
    uintptr_t c = client();
    if (!g_build || !c || minutes < 1 || minutes > 35000) return false;
    auto settings = read_at<uintptr_t>(c + 0x2278);
    if (!settings) return false;
    *reinterpret_cast<int32_t*>(settings + 0x154c) = minutes;
    return true;
}

// Client state (client-api-client-state). Game thread only, like the readers they mirror.

// Every world list entry in list order (world_entry's layout; +0x60 = list index).
bool world_list(std::vector<WorldEntry>& out) {
    out.clear();
    if (!g_build) return false;
    auto count = read_at<uint64_t>(g_base + 0xace6c8);
    auto list = read_at<uintptr_t>(g_base + 0xace6d0);
    if (!list || count > 4096) return false;
    for (uint64_t i = 0; i < count; i++) {
        auto w = read_at<uintptr_t>(list + i * 8);
        if (!w) continue;
        WorldEntry e;
        e.id = read_at<int32_t>(w);
        e.flags = read_at<int32_t>(w + 0x04);
        e.players = read_at<int16_t>(w + 0x08);
        e.host = read_string(w + 0x10);
        e.activity = read_string(w + 0x28);
        e.location = read_at<int32_t>(w + 0x40);
        e.index = read_at<int32_t>(w + 0x60);
        e.listed = true;
        out.push_back(std::move(e));
    }
    return true;
}

// The connected world's host and game ports, as setWorld (0x432748) / the arg parser (0x43d39c)
// store them: host string Client +0x3098, ports +0x30b0/+0x30b4.
bool connection(std::string& host, int ports[2]) {
    uintptr_t c = client();
    if (!c) return false;
    host = read_string(c + 0x3098);
    ports[0] = read_at<int32_t>(c + 0x30b0);
    ports[1] = read_at<int32_t>(c + 0x30b4);
    return true;
}

// Plain int fields of the Client the API reads and writes as they are (docs/offsets.md "Client state");
// a field of one byte is read and written as one byte (+0x8951d next to the pitch relaxer is the game's).
namespace {
struct ClientInt { uint32_t off; bool byte; };
constexpr ClientInt kClientInts[] = {
    {0x894bc, false},  // 0 oculus orb state / camera mode (CAM_MODE writes it; 0x41d0a4 acts on 1 and 2)
    {0x89500, false},  // 1 oculus orb focal point X (local units)
    {0x89508, false},  // 2 oculus orb focal point Y
    {0x413e6c, false}, // 3 chat cycle (refreshChat's 0x43aacc sets it; the chatbox rebuilds when it changes)
    {0x413de4, false}, // 4 changed-skills ring count (241.4's f_changed_skill_count; queueChangedSkill bumps it)
    {0x894c4, false},  // 5 orb/free camera normal speed (12; 0x41d0a4 moves the focal point this many units a cycle in modes 1 and 2)
    {0x8951c, true},   // 6 pitch relaxer (u8; pitch range 0x001..0x1040 instead of 0x400..0xbf8; packet case 0x80 sets it)
};
}  // namespace
bool client_int(int which, int& out) {
    uintptr_t c = client();
    if (!c || which < 0 || which >= static_cast<int>(sizeof kClientInts / sizeof kClientInts[0])) return false;
    const ClientInt& f = kClientInts[which];
    out = f.byte ? read_at<uint8_t>(c + f.off) : read_at<int32_t>(c + f.off);
    return true;
}
bool set_client_int(int which, int value) {
    uintptr_t c = client();
    if (!c || which < 0 || which >= static_cast<int>(sizeof kClientInts / sizeof kClientInts[0])) return false;
    const ClientInt& f = kClientInts[which];
    if (f.byte) *reinterpret_cast<volatile uint8_t*>(c + f.off) = static_cast<uint8_t>(value);
    else *reinterpret_cast<volatile int32_t*>(c + f.off) = value;
    return true;
}

// Touch camera drag sensitivity: float +0x82c of the settings object *(Client+0x2280); 0x41d0a4 scales the
// drag's pixel deltas by it into the pitch/yaw deltas (+0x894ac/+0x894b0) while the touch state is 8.
bool camera_sensitivity(float& out) {
    uintptr_t c = client();
    uintptr_t s = c ? read_at<uintptr_t>(c + 0x2280) : 0;
    if (!s) return false;
    out = read_at<float>(s + 0x82c);
    return true;
}
bool set_camera_sensitivity(float v) {
    uintptr_t c = client();
    uintptr_t s = c ? read_at<uintptr_t>(c + 0x2280) : 0;
    if (!s) return false;
    *reinterpret_cast<volatile float*>(s + 0x82c) = v;
    return true;
}

// Music volume 0-255 (Client +0x2f58; every song/jingle request copies it, a request is dropped at 0).
// Setting it as desktop does: the field, then the player's volume on every playing song (0x68ff50,
// as the settings observer 0x441a80 does once the music system is ready, 0x68e948).
int music_volume() {
    uintptr_t c = client();
    return c ? read_at<int32_t>(c + 0x2f58) : -1;
}
bool set_music_volume(int v) {
    uintptr_t c = client();
    if (!g_build || !c || v < 0 || v > 255) return false;
    *reinterpret_cast<volatile int32_t*>(c + 0x2f58) = v;
    if (reinterpret_cast<bool (*)()>(g_base + 0x68e948)()) reinterpret_cast<void (*)(int)>(g_base + 0x68ff50)(v);
    return true;
}

// Display and settings (client-api-display-engine-stubs, static RE run 302: re/decomp/007b0698.c, 00541b88.c,
// 005f3e8c.c). Settings S = *(Client+0x2278); each setting is an option object whose vtable +0x40 validates,
// then the value is written and the observer *(opt+8) slot +0x18 notified (+0x30 would commit and save it).
// Display *(Client+0x90): 0x6b0698(scale, disp) sets the UI canvas +0x6c/+0x70 from the surface +0x30/+0x34
// divided by the scale +0x84 (uniform, held at a minimum size); 0x441b88(client) calls it with UiScaling/100
// and lays the root interface out at the new canvas size.
bool display(int out[5]) {
    uintptr_t c = client();
    uintptr_t d = c ? read_at<uintptr_t>(c + 0x90) : 0;
    if (!d) return false;
    out[0] = read_at<int32_t>(d + 0x30);
    out[1] = read_at<int32_t>(d + 0x34);
    out[2] = read_at<int32_t>(d + 0x6c);
    out[3] = read_at<int32_t>(d + 0x70);
    out[4] = static_cast<int>(read_at<float>(d + 0x84) * 1000);
    return true;
}
static uintptr_t settings_obj() {
    uintptr_t c = client();
    return c && g_build ? read_at<uintptr_t>(c + 0x2278) : 0;
}
int ui_scaling() {
    uintptr_t s = settings_obj();
    return s ? read_at<int32_t>(s + 0x16bc) : -1;
}
bool relayout() {
    uintptr_t c = client();
    if (!c || !read_at<uintptr_t>(c + 0x90) || !settings_obj()) return false;
    reinterpret_cast<void (*)(uintptr_t)>(g_base + 0x441b88)(c);
    return true;
}
bool set_ui_scaling(int percent) {
    uintptr_t s = settings_obj();
    if (!s || percent < read_at<int32_t>(s + 0x1714) || percent > read_at<int32_t>(s + 0x1718)) return false;
    *reinterpret_cast<volatile int32_t*>(s + 0x16bc) = percent;
    return relayout();
}
// The frame limit the frame function 0x86bdc8 sleeps to (Engine +0x18680, Engine = *(0x1fa1070); 60 at start,
// 0 = no wait); the FpsLimit setting (settings +0x45c, console fpslimit) is pushed into it by 0x41c524 through
// 0x5a8c74. Static RE run 302 (re/decomp/006a8c74.c). Written directly: the setting itself is left as it is.
static uintptr_t engine_obj() { return g_build ? read_at<uintptr_t>(g_base + 0x1fa1070) : 0; }
int fps_limit() {
    uintptr_t e = engine_obj();
    return e ? read_at<int32_t>(e + 0x18680) : -1;
}
bool set_fps_limit(int fps) {
    uintptr_t e = engine_obj();
    if (!e || fps < 0) return false;
    *reinterpret_cast<volatile int32_t*>(e + 0x18680) = fps;
    return true;
}

// The minimap build 0x589180(mm, plane, scene) (draw_minimap above), hooked so a plugin's tile drawer runs after
// each redraw. Static RE run 302 (re/decomp/00689180.c): it calls the tile draw 0x6de9d4 for tiles 1..102 at
// pixel offset 48*size + 48 + x*zoom + (103-y)*zoom*size.
namespace {
constexpr uintptr_t kMinimapBuild = 0x589180;
constexpr uint32_t kMinimapBuildCode[4] = {0xd10203ff, 0xa9027bfd, 0xa9036ffc, 0xa90467fa};
using MinimapBuildFn = void (*)(uintptr_t, int, uintptr_t);
MinimapBuildFn g_minimap_build = nullptr;
std::atomic<bool> g_minimap_watch{false};
std::atomic<int> g_minimap_drawn{-1};
void hook_minimap_build(uintptr_t mm, int plane, uintptr_t scene) {
    g_minimap_build(mm, plane, scene);
    if (g_minimap_watch.load(std::memory_order_relaxed)) g_minimap_drawn = plane;
}
}  // namespace
void install_minimap_hook() {
    g_minimap_build = reinterpret_cast<MinimapBuildFn>(inline_hook(g_base + kMinimapBuild, kMinimapBuildCode, reinterpret_cast<void*>(hook_minimap_build)));
    LOGI("game: minimap build hook %s", g_minimap_build ? "installed" : "FAILED");
}
void set_minimap_watch(bool on) {
    g_minimap_watch = on;
    if (!on) g_minimap_drawn = -1;
}
int minimap_drawn() { return g_minimap_drawn.exchange(-1); }
bool minimap_raster(std::vector<int32_t>& out) {
    uintptr_t c = client();
    uintptr_t mm = c ? read_at<uintptr_t>(c + 0x40e970) : 0;
    if (!mm) return false;
    auto* px = read_at<int32_t*>(mm + 0x6450);
    int zoom = read_at<int32_t>(mm + 0x6470), w = read_at<int32_t>(mm + 0x6458), size = zoom * 104 + 96;
    if (!px || zoom < 2 || zoom > 8 || w != size || read_at<int32_t>(mm + 0x645c) != size
        || read_at<uint64_t>(mm + 0x6440) != static_cast<uint64_t>(size) * size) return false;
    out.assign({size, zoom});
    out.insert(out.end(), px, px + static_cast<size_t>(size) * size);
    return true;
}
bool set_minimap_raster(const int32_t* src, size_t n) {
    std::vector<int32_t> cur;
    if (!minimap_raster(cur) || n != cur.size() - 2) return false;
    uintptr_t mm = read_at<uintptr_t>(client() + 0x40e970);
    std::copy(src, src + n, read_at<int32_t*>(mm + 0x6450));
    return true;
}

// The music player's playing list (global vector 0x18e1cc8 of {ctrl, MusicSong*}): song +0x08 group id
// in its archive, +0x00 the archive (Client +0x23a0 = the jingle archive, +0x2370 = music).
bool midi_requests(std::vector<std::pair<int, bool>>& out) {
    out.clear();
    uintptr_t c = client();
    if (!g_build || !c) return false;
    auto begin = read_at<uintptr_t>(g_base + 0x18e1cc8), end = read_at<uintptr_t>(g_base + 0x18e1cd0);
    if (end < begin || end - begin > 16 * 64) return false;
    uintptr_t jingles = read_at<uintptr_t>(c + 0x23a0);
    for (uintptr_t e = begin; e < end; e += 16) {
        auto song = read_at<uintptr_t>(e + 8);
        if (song) out.emplace_back(read_at<int32_t>(song + 8), read_at<uintptr_t>(song) == jingles);
    }
    return true;
}

// Camera shake (CAM_SHAKE): 5 active flags at Client +0x894cd, applied to the camera each frame by
// 0x45203c and restored after the render. Cleared as CAM_RESET does.
void clear_camera_shake() {
    uintptr_t c = client();
    if (!c) return;
    for (int i = 0; i < 5; i++) *reinterpret_cast<volatile uint8_t*>(c + 0x894cd + i) = 0;
}

// Camera drag inversion (Client.setInvertPitch/setInvertYaw). In touch state 8 the cycle 0x41d0a4 turns the
// drag's pixel deltas into the pitch delta +0x894b0 and yaw delta +0x894ac (no sign flag of its own), stores
// half of each (+0x894a4 pitch, +0x894a8 yaw, truncated) and adds that to the targets: pitch +0x8949c clamped
// to 0x400..0xbf8 (0x001..0x1040 with the relaxer +0x8951c), yaw +0x894a0 & 0x3fff. After release the deltas
// halve each cycle (the glide). Run after a drag cycle: each inverted axis's step is applied the other way and
// its delta negated, so the glide that follows goes the inverted way too.
void invert_camera_drag(bool pitch, bool yaw) {
    uintptr_t c = client();
    if (!c) return;
    auto f = [c](uint32_t off) { return reinterpret_cast<volatile int32_t*>(c + off); };
    if (yaw) {
        int step = *f(0x894a8);
        *f(0x894a0) = (*f(0x894a0) - 2 * step) & 0x3fff;
        *f(0x894a8) = -step;
        *f(0x894ac) = -*f(0x894ac);
    }
    if (pitch) {
        bool relaxed = read_at<uint8_t>(c + 0x8951c) != 0;
        int lo = relaxed ? 1 : 0x400, hi = relaxed ? 0x1040 : 0xbf8;
        int step = *f(0x894a4), now = *f(0x8949c);
        // shortcut: a step clamped at the limit it pushed towards is taken as starting there (exact but for
        // the one cycle that reaches the limit, off by less than the step)
        int before = (now == hi && step > 0) || (now == lo && step < 0) ? now : now - step;
        *f(0x8949c) = std::min(hi, std::max(lo, before - step));
        *f(0x894a4) = -step;
        *f(0x894b0) = -*f(0x894b0);
    }
}

// 64-bit varps (VARP_LONG, case 53): beside the int tables, full-length long tables (server 0x1127c78,
// client 0x1127c38) and a per-id flag table (0x1127c20, 1 = the varp is long: readers take the long
// table, else the int one), all {?, u64 count, data*}. Values are read as the game's readers do.
namespace {
constexpr uintptr_t kVarpLongServer = 0x1127c78, kVarpLongClient = 0x1127c38, kVarpIsLong = 0x1127c20;
}
bool varps_long(bool server, std::vector<int64_t>& out) {
    out.clear();
    if (!g_build) return false;
    auto count = read_at<uint64_t>(g_base + (server ? g_build->server_varps : g_build->varps) + 8);
    auto* ints = read_at<const int32_t*>(g_base + (server ? g_build->server_varps : g_build->varps) + 0x10);
    auto* longs = read_at<const int64_t*>(g_base + (server ? kVarpLongServer : kVarpLongClient) + 0x10);
    auto* flags = read_at<const uint8_t*>(g_base + kVarpIsLong + 0x10);
    if (!ints || !longs || !flags || count > 100000 || read_at<uint64_t>(g_base + kVarpIsLong + 8) < count) return false;
    out.resize(count);
    for (uint64_t i = 0; i < count; i++) out[i] = flags[i] ? longs[i] : ints[i];
    return true;
}
// Client.setVarpLongValue: a value an int holds stays an int varp (both tables written, flag kept);
// a wider one makes it long as the script VM's long set (op 0x41) does: long table, flag 1.
bool set_varp_long(int id, int64_t v) {
    if (!g_build || id < 0) return false;
    auto count = read_at<uint64_t>(g_base + g_build->varps + 8);
    auto* ints = read_at<int32_t*>(g_base + g_build->varps + 0x10);
    auto* longs = read_at<int64_t*>(g_base + kVarpLongClient + 0x10);
    auto* flags = read_at<uint8_t*>(g_base + kVarpIsLong + 0x10);
    if (!ints || !longs || !flags || static_cast<uint64_t>(id) >= count) return false;
    longs[id] = v;
    if (!flags[id] && v == static_cast<int32_t>(v)) ints[id] = static_cast<int32_t>(v);
    else flags[id] = 1;
    return true;
}

// Cross-world message ids: Client +0x33e8 u64[100] and the next index +0x3708, which MESSAGE_PRIVATE,
// MESSAGE_FRIENDCHANNEL, MESSAGE_CLANCHANNEL(_SYSTEM) check and append to (id = u16 << 32 | u24).
bool cross_world_ids(int64_t out[100], int& index) {
    uintptr_t c = client();
    if (!c) return false;
    for (int i = 0; i < 100; i++) out[i] = read_at<int64_t>(c + 0x33e8 + 8 * i);
    index = read_at<int32_t>(c + 0x3708);
    return true;
}

// Login screen globals (desktop's static Login fields): page 0xace678 (the draw 0x4233b0 and input
// handler 0x41615c switch on it at gameState 10), input field 0xaae4f4 (0 username, 1 password).
int login_index() { return g_build ? read_at<int32_t>(g_base + 0xace678) : -1; }
int login_field() { return g_build ? read_at<int32_t>(g_base + 0xaae4f4) : -1; }

// The login screen's text (desktop's Login.username/password/otp): global 24-byte strings, username
// 0xace660, password 0xaeeba8, authenticator PIN 0xaeebd8 (typed by 0x479adc, drawn by 0x4233b0, sent by
// the submit 0x47bfd0). Written with the game's string assign 0x3f1498, as CC_SETTEXT. Game thread only.
// shortcut: an Android text field already showing (0x479770) keeps its old text until refocused.
static const uintptr_t kLoginText[3] = {0xace660, 0xaeeba8, 0xaeebd8};
std::string login_text(int which) {
    return g_build && which >= 0 && which < 3 ? read_string(g_base + kLoginText[which]) : std::string();
}
bool set_login_text(int which, const std::string& text) {
    if (!g_build || which < 0 || which > 2) return false;
    GameString s(text);
    reinterpret_cast<void* (*)(uintptr_t, const void*)>(g_base + 0x3f1498)(g_base + kLoginText[which], s.bytes);
    return true;
}

// Title background (desktop's Login.leftTitleSprite/rightTitleSprite): the loader 0x42e5fc decodes
// titlewide.png into the Sprite at *0xaee8b8 and its mirror at *0xaee8c8 (Sprite: +0x10 u32* ARGB
// pixels, +0x18 w, +0x1c h); the login draw puts them side by side, the mirror right. A plugin's
// background is stretched over both (left half, right half), the game's pixels kept to restore; the
// loader re-creates them, so it is re-applied whenever the sprite changes. Game thread (after a cycle).
namespace {
std::mutex g_bg_mutex;
std::vector<uint32_t> g_bg;  // the plugin's ARGB pixels (0 transparent), empty = the game's own
int g_bg_w, g_bg_h;
bool g_bg_dirty;
uintptr_t g_bg_applied;      // the left Sprite the plugin's pixels are in, 0 = none
std::vector<uint32_t> g_bg_saved[2];
uintptr_t title_sprite(int i) { return read_at<uintptr_t>(g_base + (i ? 0xaee8c8 : 0xaee8b8)); }
}  // namespace
void set_login_background(const int32_t* rgb, int w, int h) {
    std::lock_guard lock(g_bg_mutex);
    g_bg.clear();
    if (rgb && w > 0 && h > 0)
        for (int i = 0; i < w * h; i++) g_bg.push_back(rgb[i] ? 0xff000000u | static_cast<uint32_t>(rgb[i]) : 0);
    g_bg_w = w; g_bg_h = h; g_bg_dirty = true;
}
void login_background_cycle() {
    if (!g_build) return;
    std::lock_guard lock(g_bg_mutex);
    uintptr_t s[2] = {title_sprite(0), title_sprite(1)};
    if (s[0] != g_bg_applied && g_bg_applied) { g_bg_applied = 0; g_bg_dirty = true; }  // freed or reloaded: the game's own pixels
    if (!g_bg_dirty || !s[0] || !s[1]) return;
    int w = read_at<int32_t>(s[0] + 0x18), h = read_at<int32_t>(s[0] + 0x1c);
    if (w <= 0 || h <= 0 || read_at<int32_t>(s[1] + 0x18) != w || read_at<int32_t>(s[1] + 0x1c) != h) return;
    uint32_t* px[2] = {read_at<uint32_t*>(s[0] + 0x10), read_at<uint32_t*>(s[1] + 0x10)};
    if (!px[0] || !px[1]) return;
    if (g_bg_applied) for (int i = 0; i < 2; i++) std::copy(g_bg_saved[i].begin(), g_bg_saved[i].end(), px[i]);
    g_bg_applied = 0;
    if (!g_bg.empty()) {
        for (int i = 0; i < 2; i++) {
            g_bg_saved[i].assign(px[i], px[i] + static_cast<size_t>(w) * h);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    px[i][y * w + x] = g_bg[static_cast<size_t>(y) * g_bg_h / h * g_bg_w + static_cast<size_t>(i * w + x) * g_bg_w / (2 * w)];
        }
        g_bg_applied = s[0];
    }
    g_bg_dirty = false;
}
// {w, h, left pixel (0,0), right pixel (0,0), hash of both sprites' pixels, 0}; with no title sprite
// (freed in game) the background held until they load: {w, h, its (0,0), its (w/2,0), hash of its pixels, 1}.
bool login_background(int32_t out[6]) {
    if (!g_build) return false;
    uintptr_t s[2] = {title_sprite(0), title_sprite(1)};
    if (!s[0] || !s[1]) {
        std::lock_guard lock(g_bg_mutex);
        uint32_t hash = 0;
        for (uint32_t p : g_bg) hash = hash * 31 + p;
        bool any = !g_bg.empty();
        out[0] = any ? g_bg_w : 0; out[1] = any ? g_bg_h : 0;
        out[2] = any ? static_cast<int32_t>(g_bg[0]) : 0; out[3] = any ? static_cast<int32_t>(g_bg[g_bg_w / 2]) : 0;
        out[4] = static_cast<int32_t>(hash); out[5] = 1;
        return true;
    }
    int w = read_at<int32_t>(s[0] + 0x18), h = read_at<int32_t>(s[0] + 0x1c);
    const uint32_t* px[2] = {read_at<const uint32_t*>(s[0] + 0x10), read_at<const uint32_t*>(s[1] + 0x10)};
    if (w <= 0 || h <= 0 || !px[0] || !px[1]) return false;
    uint32_t hash = 0;
    for (int i = 0; i < 2; i++) for (size_t k = 0; k < static_cast<size_t>(w) * h; k++) hash = hash * 31 + px[i][k];
    out[0] = w; out[1] = h; out[2] = static_cast<int32_t>(px[0][0]); out[3] = static_cast<int32_t>(px[1][0]); out[4] = static_cast<int32_t>(hash); out[5] = 0;
    return true;
}

// The login screen's fire (desktop's loginScreenRunesAnimation): 0x4d5f30(anim, x, cycle, renderer)
// steps and draws it, called twice by the title draw (0x427218/0x427230) and for content type 1402
// (0x45a3b0). While off, the hook returns at once (desktop's shouldRenderLoginScreenFire false).
namespace {
using FireFn = void (*)(void*, int, int, void*);
FireFn g_fire_orig;
std::atomic<bool> g_fire_on{true};
void hook_fire(void* anim, int x, int cycle, void* r) {
    if (g_fire_on.load(std::memory_order_relaxed)) g_fire_orig(anim, x, cycle, r);
}
constexpr uintptr_t kFireDraw = 0x4d5f30;
constexpr uint32_t kFireDrawCode[4] = {0xa9bd7bfd, 0xf9000bf5, 0xa9024ff4, 0x910003fd};
}  // namespace
void install_fire_hook() {
    g_fire_orig = reinterpret_cast<FireFn>(inline_hook(g_base + kFireDraw, kFireDrawCode, reinterpret_cast<void*>(hook_fire)));
    LOGI("game: login fire hook %s", g_fire_orig ? "installed" : "FAILED");
}
void set_login_fire(bool on) { g_fire_on.store(on, std::memory_order_relaxed); }
bool login_fire() { return g_fire_on.load(std::memory_order_relaxed); }

// Minimap zoom (px per tile, int 2..8 at mm +0x6470, mm = *(Client+0x40e970)) and its zoom-input flag
// (u8 mm +0x64b4, CS2 7250/7251). The game's setter 0x463528(client, zoom) (CS2 7252) clamps, resizes
// the minimap image and redraws it with the render world view's scene and plane.
int minimap_zoom() {
    uintptr_t c = client();
    uintptr_t mm = c ? read_at<uintptr_t>(c + 0x40e970) : 0;
    return mm ? read_at<int32_t>(mm + 0x6470) : -1;
}
bool set_minimap_zoom(int zoom) {
    uintptr_t c = client();
    if (!g_build || !c || !read_at<uintptr_t>(c + 0x40e970) || !read_at<uintptr_t>(c + 0xc940)) return false;
    reinterpret_cast<void (*)(uintptr_t, int)>(g_base + 0x463528)(c, zoom);
    return true;
}
int minimap_zoom_input() {
    uintptr_t c = client();
    uintptr_t mm = c ? read_at<uintptr_t>(c + 0x40e970) : 0;
    return mm ? read_at<uint8_t>(mm + 0x64b4) : -1;
}
bool set_minimap_zoom_input(bool on) {
    uintptr_t c = client();
    uintptr_t mm = c ? read_at<uintptr_t>(c + 0x40e970) : 0;
    if (!mm) return false;
    *reinterpret_cast<volatile uint8_t*>(mm + 0x64b4) = on ? 1 : 0;
    return true;
}

// Long-press menu scroll: the top-level menu's offset in pixels (MenuManager +0x2b8, view +0x20 of the
// menu model); drags and flings write it and the next layout clamps it (0x66ee64).
int menu_scroll() {
    uintptr_t c = client();
    uintptr_t mm = c ? read_at<uintptr_t>(c + 0x4132b0) : 0;
    return mm ? read_at<int32_t>(mm + 0x2b8) : 0;
}
bool set_menu_scroll(int v) {
    uintptr_t c = client();
    uintptr_t mm = c ? read_at<uintptr_t>(c + 0x4132b0) : 0;
    if (!mm) return false;
    *reinterpret_cast<volatile int32_t*>(mm + 0x2b8) = v;
    return true;
}

// Client.setWidgetSelected(true): the target state's active byte alone, as desktop's flag (the game's
// own enter 0x5025c8 also sets the widget, verb and name and runs onTargetEnter).
bool begin_target_mode() {
    uintptr_t c = client();
    uintptr_t holder = c ? read_at<uintptr_t>(c + 0x4132b0) : 0;
    if (!holder) return false;
    *reinterpret_cast<volatile uint8_t*>(holder + 2000 + 0x9f) = 1;
    return true;
}

// Client +0x3058, the SERVER_TICK_END count getTickCount reads (dispatcher case 55 increments it).
bool set_server_tick(int tick) {
    uintptr_t c = client();
    if (!g_build || !c) return false;
    *reinterpret_cast<volatile int32_t*>(c + g_build->f_server_tick) = tick;
    return true;
}

// Members world. setWorld (0x432748), run when a world is picked from the world list or by a
// world-switch packet, sets Client+0x2070 = flags & 1 and calls setMembersWorld (0x642f14), which
// flushes the obj cache and sets the global members byte (0x1221e90: "(Members)" item names and
// hidden ops when 0). The jav_config path that starts the game on its world only writes the flags
// (Client+0x2f64), so a members world started that way stays free-to-play. Complete that step with
// the game's own function when the flags it received disagree with its members byte.
void sync_members_world() {
    uintptr_t c = client();
    if (!g_build || !c || game_state() < 10) return;
    bool members = read_at<int32_t>(c + 0x2f64) & 1;
    auto* cached = reinterpret_cast<uint8_t*>(c + 0x2070);
    if (static_cast<bool>(*cached & 1) == members) return;
    *cached = members ? 1 : 0;
    reinterpret_cast<void (*)(bool)>(g_base + 0x642f14)(members);
    LOGI("game: members world %d (flags %#x)", members ? 1 : 0, read_at<int32_t>(c + 0x2f64));
}

void chat_changed() {
    uintptr_t c = client();
    if (g_build && c) reinterpret_cast<void (*)(uintptr_t)>(g_base + kChatChanged)(c);
}

// The removed pair is rotated to the oldest position and the count drops by one, so every pair
// stays owned by the ring (the add path reuses or releases it later) and no refcount changes.
bool chat_remove_message(int type, int id) {
    if (!g_build) return false;
    uintptr_t ch = chat_channel(type);
    if (!ch) {
        LOGI("chat: no channel for type %d", type);
        return false;
    }
    int count = read_at<int32_t>(ch + 0x640), newest = read_at<int32_t>(ch + 0x644);
    if (count <= 0 || count > 100 || newest < 0 || newest >= 100) {
        LOGI("chat: channel %d count %d newest %d", type, count, newest);
        return false;
    }
    auto slot = [&](int age) { return reinterpret_cast<uintptr_t*>(ch + ((newest - age) % 100 + 100) % 100 * 0x10); };
    for (int k = 0; k < count; k++) {
        uintptr_t msg = slot(k)[1];
        if (!msg) continue;
        uintptr_t node = read_at<uintptr_t>(msg);
        if (!node || read_at<int32_t>(node + 0x10) != id) continue;
        uintptr_t saved[2] = {slot(k)[0], slot(k)[1]};
        for (int j = k; j < count - 1; j++) {
            slot(j)[0] = slot(j + 1)[0];
            slot(j)[1] = slot(j + 1)[1];
        }
        slot(count - 1)[0] = saved[0];
        slot(count - 1)[1] = saved[1];
        *reinterpret_cast<int32_t*>(ch + 0x640) = count - 1;
        // Unlink from the all-messages list too (else the All tab shows it again). The node is
        // left allocated and Message +0 points at the list head: the channel add (0x3ef964)
        // skips its own unlink/free for that value when it later reuses this ring slot.
        uintptr_t list = g_base + kChatList;
        uintptr_t prev = read_at<uintptr_t>(node), next = read_at<uintptr_t>(node + 8);
        if (node != list && prev && next) {
            *reinterpret_cast<uintptr_t*>(prev + 8) = next;
            *reinterpret_cast<uintptr_t*>(next) = prev;
            *reinterpret_cast<uint64_t*>(list + 0x10) = read_at<uint64_t>(list + 0x10) - 1;
            *reinterpret_cast<uintptr_t*>(msg) = list;
        }
        return true;
    }
    uintptr_t m0 = slot(0)[1], n0 = m0 ? read_at<uintptr_t>(m0) : 0;
    LOGI("chat: id %d not in channel %d (count %d, newest id %d)", id, type, count, n0 ? read_at<int32_t>(n0 + 0x10) : -1);
    return false;
}

int player_plane() { return g_build ? field(g_build->f_plane) : -1; }

int scene_plane() {
    uintptr_t c = client();
    if (!c) return -1;
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    return wv ? read_at<int32_t>(wv + g_build->wv_plane) : -1;
}

bool actor_set_overhead(uintptr_t a, const std::string* text, int cycle) {
    if (!a) return false;
    if (text) {
        GameString s(*text);
        reinterpret_cast<void* (*)(uintptr_t, const void*)>(g_base + 0x3f1498)(a + 0x2d8, s.bytes);
    }
    if (cycle >= 0) *reinterpret_cast<int32_t*>(a + 0x2f4) = cycle;
    return true;
}

// Actor sequence states (AnimationState: +0x00 ctrl, +0x08 SeqType*, +0x10 id, +0x14 u16 frame cycle,
// +0x16 u16 frame): +0x4c8 the animation, +0x4a8 the pose. Set as AnimationState::setSequence followed by
// the game's restart (0x65950c); the animation's start delay +0x4e8 is cleared, as desktop's setAnimation.
bool actor_set(uintptr_t a, bool player, int what, int value) {
    if (!a || !g_build || !g_build->seq_set) return false;
    auto set_seq = [&](uintptr_t st) {
        reinterpret_cast<void (*)(uintptr_t, int)>(g_base + g_build->seq_set)(st, value);
        if (value >= 0) {
            *reinterpret_cast<int32_t*>(st + 0x14) = 0;
            *reinterpret_cast<uint16_t*>(st + 0x18) = 0;
        }
    };
    switch (what) {
        case kActorAnimation: set_seq(a + 0x4c8); *reinterpret_cast<int32_t*>(a + 0x4e8) = 0; return true;
        case kActorAnimationFrame: *reinterpret_cast<int16_t*>(a + 0x4de) = static_cast<int16_t>(value); return true;
        case kActorPoseAnimation: set_seq(a + 0x4a8); return true;
        case kActorPoseFrame: *reinterpret_cast<int16_t*>(a + 0x4be) = static_cast<int16_t>(value); return true;
        case kActorSkull:
            if (!player) return false;
            *reinterpret_cast<int32_t*>(a + 0x730) = value;
            return true;
        default:
            if (what >= kActorPose && what < kActorPose + 8) {  // +0x280 idle .. +0x29c run
                *reinterpret_cast<int32_t*>(a + 0x280 + 4 * (what - kActorPose)) = value;
                return true;
            }
            return false;
    }
}

// Spot anims: Actor +0x6f0/+0x6f8 std::vector of 0x18-byte {int slot; ctrl +8; SpotAnim* +0x10}, sorted by
// slot. SpotAnim (ctor 0x5855b4): +0x25c spotanim id, +0x264 height, +0x268 start cycle, +0x26c loop flag,
// AnimationState at +0x270 (u16 frame cycle +0x284, u16 frame +0x286). The actor update (0x435240) drops an
// entry once started when its id is -1, it has no sequence or its sequence ended.
static const uint32_t kSpotAnimField[] = {0x25c, 0x264, 0x268, 0x286, 0x284};

bool actor_spot_anims(uintptr_t a, std::vector<int32_t>& out) {
    if (!a) return false;
    uintptr_t b = read_at<uintptr_t>(a + 0x6f0), e = read_at<uintptr_t>(a + 0x6f8);
    if (e < b || (e - b) / 0x18 > 256) return false;
    for (uintptr_t p = b; p < e; p += 0x18) {
        uintptr_t s = read_at<uintptr_t>(p + 0x10);
        if (!s) continue;
        out.push_back(read_at<int32_t>(p));
        out.push_back(read_at<int32_t>(s + 0x25c));
        out.push_back(read_at<int32_t>(s + 0x264));
        out.push_back(read_at<int32_t>(s + 0x268));
        out.push_back(read_at<int16_t>(s + 0x286));
        out.push_back(read_at<uint16_t>(s + 0x284));
    }
    return true;
}

bool actor_spot_anim_set(uintptr_t a, int slot, int field, int value) {
    if (!a || field < 0 || field > 4) return false;
    uintptr_t b = read_at<uintptr_t>(a + 0x6f0), e = read_at<uintptr_t>(a + 0x6f8);
    if (e < b || (e - b) / 0x18 > 256) return false;
    for (uintptr_t p = b; p < e; p += 0x18) {
        uintptr_t s = read_at<uintptr_t>(p + 0x10);
        if (read_at<int32_t>(p) != slot || !s) continue;
        if (field >= 3) *reinterpret_cast<uint16_t*>(s + kSpotAnimField[field]) = static_cast<uint16_t>(value);
        else *reinterpret_cast<int32_t*>(s + kSpotAnimField[field]) = value;
        return true;
    }
    return false;
}

// The game's adder 0x4714e8(actor, slot, id, height, delay, cycle, loop), as PLAYER_SPOTANIM_SPECIFIC
// (dispatcher case 0x62): replaces the slot's spot anim, starting at cycle + delay; id -1 removes the slot.
bool actor_create_spot_anim(uintptr_t a, int slot, int id, int height, int delay) {
    if (!a || !g_build) return false;
    reinterpret_cast<void (*)(uintptr_t, int, int, int, int, int, int)>(g_base + 0x4714e8)(a, slot, id, height, delay, cycle(), 0);
    return true;
}

bool reload_scene() {
    uintptr_t c = client();
    if (!c || game_state() != 30) return false;
    reinterpret_cast<void (*)(uintptr_t)>(g_base + 0x429900)(c);
    return true;
}

// Widget target mode ("Use"/spell/targetable widget selected): the state object at
// *(Client+0x4132b0)+2000 (FUN_00561520, ELF 0x461520). Byte +0x9f bit 0 = target mode on (menu builders
// 0x525c78 and others take the target-entry path, "%s -> %s"); +0x48 = the selected name string. Probe dump.
std::vector<uint8_t> target_state_bytes(int len) {
    std::vector<uint8_t> out;
    uintptr_t c = client();
    uintptr_t holder = c ? read_at<uintptr_t>(c + 0x4132b0) : 0;
    if (!holder || len <= 0 || len > 0x400) return out;
    out.resize(len);
    for (int i = 0; i < len; i++) out[i] = read_at<uint8_t>(holder + 2000 + i);
    return out;
}

// Layout seen on device 2026-10-18 (Wiki's "Lookup", tmodelog): +0x90 selected component id, +0x94 its
// CC_CREATE index (-1 for a static component; unverified), +0x9e target flags (clickMask >> 11 & 0x3f: 0x27 for
// Wiki's 0x13800), +0x9f non-zero while target mode is on (Cancel clears it), +0x20 verb, +0x68 selected name.
bool target_mode(int out[3]) {
    uintptr_t c = client();
    uintptr_t holder = c ? read_at<uintptr_t>(c + 0x4132b0) : 0;
    if (!holder) return false;
    uintptr_t st = holder + 2000;
    out[0] = read_at<uint8_t>(st + 0x9f) != 0;
    out[1] = read_at<int32_t>(st + 0x90);
    out[2] = read_at<int32_t>(st + 0x94);
    return true;
}

// Client.setWidgetSelected(false): clears the active byte, as the game's Cancel does (unverified: the game may
// also reset other fields there; the menu builders only test +0x9f).
bool end_target_mode() {
    uintptr_t c = client();
    uintptr_t holder = c ? read_at<uintptr_t>(c + 0x4132b0) : 0;
    if (!holder) return false;
    *reinterpret_cast<volatile uint8_t*>(holder + 2000 + 0x9f) = 0;
    return true;
}

int friend_list_state() {
    uintptr_t c = client();
    auto fm = c ? read_at<uintptr_t>(c + 0x46c0) : 0;
    return fm ? read_at<int32_t>(fm + 0x150) : 0;
}

bool friend_list(bool ignores, std::vector<FriendEntry>& out) {
    out.clear();
    uintptr_t c = client();
    auto fm = c ? read_at<uintptr_t>(c + 0x46c0) : 0;
    if (!fm || read_at<int32_t>(fm + 0x150) != 2) return false;
    uintptr_t box = fm + (ignores ? 0xb8 : 0x08);
    int n = read_at<int32_t>(box + 0xc);
    auto arr = read_at<uintptr_t>(box + 0x20);
    if (n < 0 || n > 1000 || (n > 0 && !arr)) return false;
    for (int i = 0; i < n; i++) {
        auto e = read_at<uintptr_t>(arr + 8 * i);
        if (!e) continue;
        FriendEntry f;
        f.name = read_string(e + 0x08);
        f.prev = read_string(e + 0x38);
        if (!ignores) {
            f.world = read_at<int32_t>(e + 0x68);
            f.rank = read_at<int32_t>(e + 0x88);
        }
        out.push_back(std::move(f));
    }
    return true;
}

// Friend login/logout notices not shown yet (RuneLite FriendContainer.getPendingLogins; static RE
// client-api-host-gaps-reach of the UPDATE_FRIENDLIST reader 0x56e448 and the drain 0x3fbd68): FriendSystem
// +0xa0/+0xa8 std::vector of 0x38-byte entries {+0x00 Username (two 24-byte strings), +0x30 int time (s),
// +0x34 s16 world (0 = logged out), +0x36 cancelled flag}. The reader queues one when a listed friend's world
// changes (a matching opposite notice is cancelled instead); the drain prints "<name> has logged in/out." for
// each uncancelled entry older than 5 s and erases it. Cancelled entries are left out here. Game thread.
bool pending_logins(std::vector<PendingLogin>& out) {
    out.clear();
    uintptr_t c = client();
    auto fm = c ? read_at<uintptr_t>(c + 0x46c0) : 0;
    if (!fm) return false;
    uintptr_t b = read_at<uintptr_t>(fm + 0xa0), e = read_at<uintptr_t>(fm + 0xa8);
    if (!b || e < b || (e - b) % 0x38 || (e - b) / 0x38 > 400) return false;
    for (uintptr_t p = b; p < e; p += 0x38) {
        if (read_at<uint8_t>(p + 0x36) & 1) continue;
        out.push_back({read_string(p), read_at<int16_t>(p + 0x34), read_at<int32_t>(p + 0x30)});
    }
    return true;
}

// The local or listed player's per-slot obj customisations (RuneLite ColorTextureOverride; static RE
// client-api-host-gaps-reach of the appearance decoder 0x4762d4 and the setter 0x5e4648): Appearance +0x90 u64
// count, +0x98 {ctrl, obj} per slot (null obj = none, 12 slots); obj (0x60 bytes, ctor 0x642f9c): recolour-to
// {+0x10 count, +0x18 int16*}, retexture-to {+0x28 count, +0x30 int16*}, defaults copied from the ObjType's
// replace arrays; the model hash 0x5e1bd4 and build 0x5e2658 read them. Game thread.
bool appearance_overrides(uintptr_t player, ObjOverride out[12]) {
    for (int i = 0; i < 12; i++) out[i] = {};
    uintptr_t app = player ? read_at<uintptr_t>(player + 0x728) : 0;
    if (!app) return false;
    uint64_t n = read_at<uint64_t>(app + 0x90);
    uintptr_t slots = read_at<uintptr_t>(app + 0x98);
    if (n > 12 || (n && !slots)) return false;
    for (uint64_t i = 0; i < n; i++) {
        uintptr_t o = read_at<uintptr_t>(slots + i * 0x10 + 8);
        if (!o) continue;
        out[i].present = true;
        for (int k = 0; k < 2; k++) {
            uint64_t m = read_at<uint64_t>(o + (k ? 0x28 : 0x10));
            auto arr = read_at<const int16_t*>(o + (k ? 0x30 : 0x18));
            if (m <= 64 && arr) (k ? out[i].retexture : out[i].recolour).assign(arr, arr + m);
        }
    }
    return true;
}

Point walk_destination() {
    if (!g_build || !client()) return {-1, -1};
    // The map flag (SET_MAP_FLAG_V2 and the client's own walk): Client +0x40e970 -> {scene x, y},
    // 0,0 when cleared (setter 0x4410b0). OpenRune flags every route; the older Client +0xecdc pair
    // only showed client-side walks on our old server.
    if (auto flag = read_at<uintptr_t>(client() + 0x40e970)) {
        int x = read_at<int32_t>(flag), y = read_at<int32_t>(flag + 4);
        if (x > 0 || y > 0) return {x, y};
    }
    return {field(g_build->f_dest_x), field(g_build->f_dest_y)};
}
Point player_local_tile() {
    if (!g_build || !client()) return {-1, -1};
    return {field(g_build->f_local_x), field(g_build->f_local_y)};
}
Point player_fine() {
    if (!g_build || !client()) return {-1, -1};
    return {field(g_build->f_fine_x), field(g_build->f_fine_y)};
}

}  // namespace game

namespace game {
namespace {
// A script value as the VM stores it: 32 bytes of payload, then a type tag (1 int, 2 string,
// 4 object, 5 long). Strings use the 24-byte layout read_string() describes.
struct Variant {
    uint8_t payload[32];
    uint32_t type;
    uint32_t pad;
};
static_assert(sizeof(Variant) == 0x28, "variant size");

void write_string(uint8_t* dst, const std::string& s, std::vector<std::unique_ptr<char[]>>& keep) {
    memset(dst, 0, 24);
    if (s.size() <= 22) {
        memcpy(dst, s.data(), s.size());
        dst[23] = static_cast<uint8_t>(23 - s.size());
        return;
    }
    // The VM copies argument strings, so the buffer only has to live for the call.
    keep.emplace_back(new char[s.size() + 1]);
    char* buf = keep.back().get();
    memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    uint64_t size = s.size(), cap = s.size() | 0x8000000000000000ull;
    memcpy(dst, &buf, 8);
    memcpy(dst + 8, &size, 8);
    memcpy(dst + 16, &cap, 8);
}
}  // namespace

bool run_script(const std::vector<ScriptArg>& args, ScriptResult& out, std::string& error) {
    if (!g_build || !g_build->run_script_event) { error = "unsupported build"; return false; }
    if (args.empty() || args[0].is_string) { error = "first argument must be the script id"; return false; }
    uintptr_t vm = g_base + g_build->script_vm;
    if (read_at<uint8_t>(vm + 4)) { error = "script VM busy"; return false; }

    std::vector<std::unique_ptr<char[]>> keep;
    std::vector<Variant> vars(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        memset(&vars[i], 0, sizeof(Variant));
        if (args[i].is_string) {
            write_string(vars[i].payload, args[i].s, keep);
            vars[i].type = 2;
        } else {
            memcpy(vars[i].payload, &args[i].i, 4);
            vars[i].type = 1;
        }
    }
    // ScriptEvent, as runScriptEvent (0x4df54c) reads it: +0x08 argc, +0x10 args, +0x40 source
    // widget, +0x48.. mouse/op values for the magic int args, +0x60 drag target, +0x70 opbase
    // string, +0x88 copied to vm+0xe0cc, +0x8c trigger type (0 = look the script up by id).
    alignas(16) uint8_t ev[0x100] = {};
    int32_t argc = static_cast<int32_t>(args.size());
    Variant* argv = vars.data();
    memcpy(ev + 0x08, &argc, 4);
    memcpy(ev + 0x10, &argv, 8);
    ev[0x70 + 23] = 23;  // empty inline string
    auto fn = reinterpret_cast<void (*)(uintptr_t, void*, int)>(g_base + g_build->run_script_event);
    fn(vm, ev, 500000);

    int n = read_at<int32_t>(vm + 0xff0);
    if (n < 0 || n > 1000) n = 0;
    out.ints.resize(n);
    if (n) memcpy(out.ints.data(), reinterpret_cast<const void*>(vm + 0x50), n * 4u);
    int m = read_at<int32_t>(vm + 0xac38);
    if (m < 0 || m > 1000) m = 0;
    out.objects.clear();
    out.object_is_string.clear();
    for (int i = 0; i < m; i++) {
        uintptr_t v = vm + 0xff8 + static_cast<uintptr_t>(i) * 0x28;
        bool str = read_at<uint32_t>(v + 0x20) == 2;
        out.object_is_string.push_back(str);
        out.objects.push_back(str ? read_string(v) : std::string());
    }
    return true;
}

bool script_stacks(ScriptResult& out) {
    if (!g_build || !g_build->script_vm) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    int n = read_at<int32_t>(vm + 0xff0);
    if (n < 0 || n > 1000) n = 0;
    out.ints.resize(n);
    if (n) memcpy(out.ints.data(), reinterpret_cast<const void*>(vm + 0x50), n * 4u);
    int m = read_at<int32_t>(vm + 0xac38);
    if (m < 0 || m > 1000) m = 0;
    out.objects.clear();
    out.object_is_string.clear();
    for (int i = 0; i < m; i++) {
        uintptr_t v = vm + 0xff8 + static_cast<uintptr_t>(i) * 0x28;
        bool str = read_at<uint32_t>(v + 0x20) == 2;
        out.object_is_string.push_back(str);
        out.objects.push_back(str ? read_string(v) : std::string());
    }
    return true;
}

bool set_script_int_stack(const int32_t* values, int size) {
    if (!g_build || !g_build->script_vm || size < 0 || size > 1000) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    if (size) memcpy(reinterpret_cast<void*>(vm + 0x50), values, size * 4u);
    *reinterpret_cast<int32_t*>(vm + 0xff0) = size;
    return true;
}

// Script locals of the running frame (frame 0: the VM's live locals, int vector vm+0x08, object
// vector vm+0x38) or of a saved call frame (int vector +0x18, object vector +0x48; the frame a
// RETURN is about to restore, script_hooks::return_frame()). Arrays (define_array, case 0x2c) are
// object locals at slot = desktop array id: a 0x28-byte variant with type 4 (+0x20) holding the
// array object pointer (+0); the array is {u8 dirty, variant storage +0x08, alternative index +0x38},
// alternative 0 = int array, storage a std::vector<int32_t> (static RE of 0x4dd08c cases 0x2c-0x2e,
// 0x5f1974/0x5f1fbc/0x5f200c). // unverified: the vector<int32_t> storage, on the device.
namespace {
bool locals_base(uintptr_t frame, uintptr_t& ints, uintptr_t& objs) {
    if (!g_build || !g_build->script_vm) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    ints = frame ? frame + 0x18 : vm + 0x08;
    objs = frame ? frame + 0x48 : vm + 0x38;
    return true;
}

int32_t* script_int_array(uintptr_t frame, int slot, size_t& n, uintptr_t* array_obj = nullptr) {
    uintptr_t ints, objs;
    if (slot < 0 || slot > 255 || !locals_base(frame, ints, objs)) return nullptr;
    uintptr_t b = read_at<uintptr_t>(objs), e = read_at<uintptr_t>(objs + 8);
    if (!b || e < b || b + static_cast<uintptr_t>(slot + 1) * 0x28 > e) return nullptr;
    uintptr_t v = b + static_cast<uintptr_t>(slot) * 0x28;
    if (read_at<uint32_t>(v + 0x20) != 4) return nullptr;
    uintptr_t arr = read_at<uintptr_t>(v);
    if (!arr || read_at<uint32_t>(arr + 0x38) != 0) return nullptr;
    auto* vb = read_at<int32_t*>(arr + 0x08);
    auto* ve = read_at<int32_t*>(arr + 0x10);
    if (!vb || ve < vb || ve - vb > 1 << 20) return nullptr;
    n = static_cast<size_t>(ve - vb);
    if (array_obj) *array_obj = arr;
    return vb;
}
}  // namespace

bool script_array(uintptr_t frame, int slot, std::vector<int32_t>& out) {
    size_t n = 0;
    int32_t* p = script_int_array(frame, slot, n);
    if (!p) return false;
    out.assign(p, p + n);
    return true;
}

bool set_script_array(uintptr_t frame, int slot, const int32_t* values, int count) {
    size_t n = 0;
    uintptr_t arr = 0;
    int32_t* p = script_int_array(frame, slot, n, &arr);
    if (!p || count < 0) return false;
    size_t k = std::min(n, static_cast<size_t>(count));  // the array keeps its length
    memcpy(p, values, k * 4u);
    *reinterpret_cast<uint8_t*>(arr) = 1;  // as the element write 0x5f200c
    return true;
}

bool script_int_local(uintptr_t frame, int index, int32_t& value, const int32_t* set) {
    uintptr_t ints, objs;
    if (index < 0 || !locals_base(frame, ints, objs)) return false;
    auto* b = read_at<int32_t*>(ints);
    auto* e = read_at<int32_t*>(ints + 8);
    if (!b || e < b || index >= e - b) return false;
    if (set) b[index] = *set;
    value = b[index];
    return true;
}

// Single-slot stack access (callbacks raised per item, e.g. bankSearchFilter: copying whole stacks
// over JNI each time cost ~25 fps). depth 0 = top. Game thread only.
bool script_int_top(int depth, int32_t& value, const int32_t* set) {
    if (!g_build || !g_build->script_vm || depth < 0) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    int n = read_at<int32_t>(vm + 0xff0);
    if (n <= depth || n > 1000) return false;
    auto* slot = reinterpret_cast<int32_t*>(vm + 0x50 + 4u * static_cast<uintptr_t>(n - 1 - depth));
    if (set) *slot = *set;
    value = *slot;
    return true;
}

bool script_string_top(std::string& out) {
    if (!g_build || !g_build->script_vm) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    int m = read_at<int32_t>(vm + 0xac38);
    if (m <= 0 || m > 1000) return false;
    uintptr_t v = vm + 0xff8 + static_cast<uintptr_t>(m - 1) * 0x28;
    if (read_at<uint32_t>(v + 0x20) != 2) return false;
    out = read_string(v);
    return true;
}

bool script_top_string_starts_with(const std::string& prefix) {
    if (!g_build || !g_build->script_vm) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    int m = read_at<int32_t>(vm + 0xac38);
    if (m <= 0 || m > 1000) return false;
    uintptr_t v = vm + 0xff8 + static_cast<uintptr_t>(m - 1) * 0x28;
    if (read_at<uint32_t>(v + 0x20) != 2) return false;
    return read_string(v).compare(0, prefix.size(), prefix) == 0;
}

bool set_script_string(int index, const std::string& text) {
    if (!g_build || !g_build->script_vm) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    int m = read_at<int32_t>(vm + 0xac38);
    if (m < 0 || m > 1000 || index < 0 || index >= m) return false;
    uintptr_t v = vm + 0xff8 + static_cast<uintptr_t>(index) * 0x28;
    if (read_at<uint32_t>(v + 0x20) != 2) return false;
    GameString s(text);
    reinterpret_cast<void* (*)(uintptr_t, const void*)>(g_base + 0x3f1498)(v, s.bytes);
    return true;
}

// Text width in a game font, as CS2 PARAWIDTH (4109, case 0x100d of Ghidra 005ef2ec.c): font =
// 0x43cce0(client, font id) (the font cache at Client +0x414fc8, loads on a miss), and the
// per-line width 0x685370(font, string*, 1) that PARAWIDTH's 0x6871ec takes the max of over the
// wrapped lines. -1 when the font isn't available. Game thread. Static RE (gen 22).
int text_width(int font_id, const std::string& text) {
    uintptr_t c = client();
    if (!c || font_id < 0) return -1;
    uintptr_t font = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + 0x43cce0)(c, font_id);
    if (!font) return -1;
    GameString s(text);
    return reinterpret_cast<int (*)(uintptr_t, const void*, int)>(g_base + 0x685370)(font, s.bytes, 1);
}

// A game font's ascent (RuneLite FontTypeFace.getBaseline): Font +0x00, written by the metrics decoder
// 0x6843dc (byte 256 of a 257-byte metrics file, else computed from the glyph metrics); -1 when the font isn't
// available. Game thread. Static RE client-api-host-gaps-reach.
int font_ascent(int font_id) {
    uintptr_t c = client();
    if (!c || font_id < 0) return -1;
    uintptr_t font = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + 0x43cce0)(c, font_id);
    return font ? read_at<int32_t>(font) : -1;
}

// The VM's active components (RuneLite's scriptActiveWidget / scriptDotWidget): {ctrl, Widget*}
// pairs at vm +0xded8 (cc_*) and +0xdee8 (.cc_*, the opcode's isLong flag), set by CC_CREATE /
// CC_FIND and read by every CC_* handler in 0x4e06dc (case 0x65 null-checks +0 and bumps its
// count at +8, so +0 is the control block). widget_ref validates the pointer. Static RE (gen 22).
bool script_active_widget(bool dot, int* component, int* index) {
    if (!g_build || !g_build->script_vm) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    return widget_ref(read_at<uintptr_t>(vm + (dot ? 0xdee8 : 0xded8) + 8), component, index);
}

// ScriptEvent +0x40: the source widget (Widget*), null for scripts not run from a widget hook.
bool script_event_source(uintptr_t ev, int* component, int* index) {
    return ev && widget_ref(read_at<uintptr_t>(ev + 0x40), component, index);
}

std::vector<ScriptArg> script_event_args(uintptr_t ev) {
    std::vector<ScriptArg> out;
    if (!ev) return out;
    int argc = read_at<int32_t>(ev + 0x08);
    uintptr_t argv = read_at<uintptr_t>(ev + 0x10);
    if (argc <= 0 || argc > 64 || !argv) return out;
    for (int i = 0; i < argc; i++) {
        uintptr_t v = argv + static_cast<uintptr_t>(i) * 0x28;
        ScriptArg a;
        uint32_t type = read_at<uint32_t>(v + 0x20);
        if (type == 1) a.i = read_at<int32_t>(v);
        else if (type == 2) { a.is_string = true; a.s = read_string(v); }
        else a.i = -1;
        out.push_back(std::move(a));
    }
    return out;
}

}  // namespace game

namespace game {
void release_shared_ptr(uintptr_t ctrl);

// db_getfield (CS2 7502) in 0x5cd7f4(vm, opcode): pops row, packed column (table << 12 |
// column << 4), tuple index; pushes the tuple's values onto the int (+0x50/+0xff0), object
// (+0xff8/+0xac38) and long (+0xac40/+0xcb80) stacks. DBRowType 0x74f824 (+0x30 table id),
// DBTableType 0x6eb2c4, column types 0x6ec320(table, column) → {+0x08 count, +0x10 type*[]},
// each type's +0x08 = the int/long/string descriptor (globals 0x1d8aff8/0x1d8b000/0x1d8b008).
int db_row_table(int row) {
    if (!g_build || !g_build->script_vm || row < 0 || row > 0xffff) return -1;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    SharedPtr r = reinterpret_cast<SharedPtr (*)(int)>(g_base + 0x64f824)(row);
    int table = r.obj ? read_at<int32_t>(r.obj + 0x30) : -1;
    release_shared_ptr(r.ctrl);
    return table;
}

int db_field_count(int row, int column) {
    if (!g_build || !g_build->script_vm || row < 0 || column < 0 || column > 255) return -1;
    uintptr_t vm = g_base + g_build->script_vm;
    if (read_at<uint8_t>(vm + 4)) return -1;
    int table = db_row_table(row);
    if (table < 0) return -1;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    // The column must exist (the handler reads its type list).
    SharedPtr t = reinterpret_cast<SharedPtr (*)(int)>(g_base + 0x5eb2c4)(table);
    if (!t.obj) { release_shared_ptr(t.ctrl); return -1; }
    uintptr_t types = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + 0x5ec320)(t.obj, column);
    uint64_t ntypes = types ? read_at<uint64_t>(types + 8) : 0;
    release_shared_ptr(t.ctrl);
    if (ntypes == 0) return -1;
    auto* ints = reinterpret_cast<int32_t*>(vm + 0x50);
    *reinterpret_cast<int32_t*>(vm + 0xff0) = 2;
    ints[0] = row;
    ints[1] = (table << 12) | (column << 4);
    int ok = reinterpret_cast<int (*)(uintptr_t, int)>(g_base + 0x4cd7f4)(vm, 7503);
    int n = read_at<int32_t>(vm + 0xff0);
    int count = ok == 1 && n == 1 ? ints[0] : -1;
    *reinterpret_cast<int32_t*>(vm + 0xff0) = 0;
    *reinterpret_cast<int32_t*>(vm + 0xac38) = 0;
    *reinterpret_cast<int32_t*>(vm + 0xcb80) = 0;
    return count;
}

bool db_field(int row, int column, int tuple, std::vector<DbValue>& out) {
    out.clear();
    if (!g_build || !g_build->script_vm || row < 0 || column < 0 || column > 255 || tuple < 0) return false;
    uintptr_t vm = g_base + g_build->script_vm;
    if (read_at<uint8_t>(vm + 4)) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    SharedPtr r = reinterpret_cast<SharedPtr (*)(int)>(g_base + 0x64f824)(row);
    if (!r.obj) { release_shared_ptr(r.ctrl); return false; }
    int table = read_at<int32_t>(r.obj + 0x30);
    release_shared_ptr(r.ctrl);
    SharedPtr t = reinterpret_cast<SharedPtr (*)(int)>(g_base + 0x5eb2c4)(table);
    if (!t.obj) { release_shared_ptr(t.ctrl); return false; }
    uintptr_t types = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + 0x5ec320)(t.obj, column);
    std::vector<int> kinds;
    uintptr_t int_t = read_at<uintptr_t>(g_base + 0x1d8aff8), long_t = read_at<uintptr_t>(g_base + 0x1d8b000),
              str_t = read_at<uintptr_t>(g_base + 0x1d8b008);
    uint64_t n = types ? read_at<uint64_t>(types + 8) : 0;
    uintptr_t arr = types ? read_at<uintptr_t>(types + 0x10) : 0;
    for (uint64_t k = 0; arr && k < n && k < 64; k++) {
        uintptr_t ty = read_at<uintptr_t>(read_at<uintptr_t>(arr + k * 8) + 8);
        kinds.push_back(ty == int_t ? 0 : ty == long_t ? 1 : ty == str_t ? 2 : -1);
    }
    release_shared_ptr(t.ctrl);
    if (kinds.empty()) return false;
    for (int k : kinds) if (k < 0) return false;  // unknown type: the handler would throw
    auto* ints = reinterpret_cast<int32_t*>(vm + 0x50);
    *reinterpret_cast<int32_t*>(vm + 0xff0) = 3;
    *reinterpret_cast<int32_t*>(vm + 0xac38) = 0;
    *reinterpret_cast<int32_t*>(vm + 0xcb80) = 0;
    ints[0] = row;
    ints[1] = (table << 12) | (column << 4);
    ints[2] = tuple;
    int ok = reinterpret_cast<int (*)(uintptr_t, int)>(g_base + 0x4cd7f4)(vm, 7502);
    int ni = read_at<int32_t>(vm + 0xff0), no = read_at<int32_t>(vm + 0xac38), nl = read_at<int32_t>(vm + 0xcb80);
    auto reset = [vm] {
        *reinterpret_cast<int32_t*>(vm + 0xff0) = 0;
        *reinterpret_cast<int32_t*>(vm + 0xac38) = 0;
        *reinterpret_cast<int32_t*>(vm + 0xcb80) = 0;
    };
    if (ok != 1 || ni < 0 || no < 0 || nl < 0 || ni > 64 || no > 64 || nl > 64) { reset(); return false; }
    int ii = 0, io = 0, il = 0;
    for (int k : kinds) {
        DbValue v;
        v.kind = k;
        if (k == 0 && ii < ni) v.i = ints[ii++];
        else if (k == 1 && il < nl) v.i = read_at<int64_t>(vm + 0xac40 + 8 * il++);
        else if (k == 2 && io < no) v.s = read_string(vm + 0xff8 + 0x28 * io++);
        else break;
        out.push_back(std::move(v));
    }
    reset();
    return !out.empty();
}
}  // namespace game

namespace game {
namespace {
// shared_ptr control block as the game uses it: vtable, int uses (+8), int weak (+0xc).
void release_shared(uintptr_t ctrl) {
    if (!ctrl) return;
    auto vcall = [](uintptr_t obj, size_t slot) {
        auto vt = read_at<uintptr_t>(obj);
        reinterpret_cast<void (*)(uintptr_t)>(read_at<uintptr_t>(vt + slot))(obj);
    };
    if (__atomic_fetch_add(reinterpret_cast<int*>(ctrl + 8), -1, __ATOMIC_ACQ_REL) == 1) {
        vcall(ctrl, 0x10);
        if (__atomic_fetch_add(reinterpret_cast<int*>(ctrl + 0xc), -1, __ATOMIC_ACQ_REL) == 1) vcall(ctrl, 0x18);
    }
}
}  // namespace

void release_shared_ptr(uintptr_t ctrl) { release_shared(ctrl); }

bool set_varbit(int id, int value) {
    VarbitDef def;  // also rejects ids without a definition (the setter would dereference null)
    if (!g_build || !g_build->set_varbit || !varbit_def(id, def)) return false;
    reinterpret_cast<void (*)(int, int)>(g_base + g_build->set_varbit)(id, value);
    return true;
}

bool varbit_def(int id, VarbitDef& out) {
    if (!g_build || !g_build->get_varbit_type || id < 0 || id > 0xffff) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    SharedPtr r = reinterpret_cast<SharedPtr (*)(int)>(g_base + g_build->get_varbit_type)(id);
    uintptr_t ctrl = r.ctrl, o = r.obj;
    if (!o) { release_shared(ctrl); return false; }
    out.varp = read_at<int32_t>(o);
    out.lsb = read_at<int32_t>(o + 4);
    out.msb = read_at<int32_t>(o + 8);
    release_shared(ctrl);
    return out.varp >= 0 && out.varp < varp_count() && out.lsb >= 0 && out.msb < 32 && out.lsb <= out.msb;
}

// GE item search results (OC_FIND 4210 fills them, OC_FINDNEXT 4211 reads them, OC_FINDRESET 4212
// zeroes the index; handlers in 0x4bcf44, cases 0x1072-0x1074): Client +0x417ba8
// std::vector<uint16_t> ids {begin, end, cap}, +0x417bc0 next index, +0x417bc4 count.
// Static RE (gen 21). // unverified: no plugin path exercised on the device yet
namespace {
constexpr uint32_t kGeIds = 0x417ba8, kGeIndex = 0x417bc0, kGeCount = 0x417bc4;
constexpr uintptr_t kOperatorNew = 0x9f4384, kOperatorDelete = 0x9f43f4;
}  // namespace

bool set_ge_search_ids(const int16_t* ids, int n) {
    uintptr_t c = client();
    if (!c || n < 0 || n > 0xffff) return false;
    auto* v = reinterpret_cast<uintptr_t*>(c + kGeIds);  // begin, end, cap
    size_t cap = (v[2] - v[0]) / 2;
    if (static_cast<size_t>(n) > cap) {
        auto* fresh = reinterpret_cast<void* (*)(size_t)>(g_base + kOperatorNew)(static_cast<size_t>(n) * 2);
        if (!fresh) return false;
        if (v[0]) reinterpret_cast<void (*)(void*)>(g_base + kOperatorDelete)(reinterpret_cast<void*>(v[0]));
        v[0] = reinterpret_cast<uintptr_t>(fresh);
        v[2] = v[0] + static_cast<size_t>(n) * 2;
    }
    if (n) memcpy(reinterpret_cast<void*>(v[0]), ids, static_cast<size_t>(n) * 2);
    v[1] = v[0] + static_cast<size_t>(n) * 2;
    return true;
}

bool ge_search(std::vector<int>& out) {
    uintptr_t c = client();
    if (!c) return false;
    auto* v = reinterpret_cast<const uintptr_t*>(c + kGeIds);
    size_t n = v[0] && v[1] >= v[0] ? (v[1] - v[0]) / 2 : 0;
    if (n > 0xffff) return false;
    out.assign({read_at<int32_t>(c + kGeCount), read_at<int32_t>(c + kGeIndex)});
    for (size_t i = 0; i < n; i++) out.push_back(read_at<int16_t>(v[0] + i * 2));
    return true;
}

bool set_ge_search_int(bool index, int value) {
    uintptr_t c = client();
    if (!c) return false;
    *reinterpret_cast<int32_t*>(c + (index ? kGeIndex : kGeCount)) = value;
    return true;
}

// Minimap dot styles (the `setminimapdotstyle` console command 0x594634): Minimap = *(Client
// +0x40e970) (also the map flag holder); per category 0-6 a shared_ptr dot sprite at +0x64c0 +
// cat*0x10 and dot colour / outline colour / outline thickness at +0x6530 / +0x6534 / +0x6538
// + cat*0x10. setDotStyle(minimap, cat, dot rgb, outline rgb, thickness) 0x588eac rebuilds the
// sprite from the base dot (+0x6610 +0x60). Static RE (gen 21); recolouring verified on device (gen 22)
// The base dot (*(mm +0x6610)) only exists once the minimap is set up after login; with it null,
// setDotStyle's rebuild reads null+0x88 (crashed the game at startup, gen 22). Both functions
// refuse until then.
static uintptr_t minimap_ready() {
    uintptr_t c = client();
    if (!c) return 0;
    uintptr_t mm = read_at<uintptr_t>(c + 0x40e970);
    if (!mm || !read_at<uintptr_t>(mm + 0x6610)) return 0;
    return mm;
}

bool minimap_dot_style(int cat, int dot_rgb, int outline_rgb, int thickness) {
    if (cat < 0 || cat > 6 || thickness < 0) return false;
    uintptr_t mm = minimap_ready();
    if (!mm) return false;
    reinterpret_cast<void (*)(uintptr_t, unsigned, int, int, int)>(g_base + 0x588eac)(mm, cat, dot_rgb, outline_rgb, thickness);
    return true;
}

bool minimap_dot_get(int cat, int out[4]) {
    if (cat < 0 || cat > 6) return false;
    uintptr_t mm = minimap_ready();
    if (!mm) return false;
    uintptr_t e = mm + static_cast<uintptr_t>(cat) * 0x10;
    out[0] = read_at<uintptr_t>(e + 0x64c8) != 0;  // a styled sprite exists
    out[1] = read_at<int32_t>(e + 0x6530);
    out[2] = read_at<int32_t>(e + 0x6534);
    out[3] = read_at<int32_t>(e + 0x6538);
    return true;
}

bool queue_changed_skill(int skill) {
    uintptr_t c = client();
    if (!c || skill < 0 || skill >= 25) return false;
    auto* count = reinterpret_cast<uint32_t*>(c + g_build->f_changed_skill_count);
    uint32_t n = *count;
    reinterpret_cast<int32_t*>(c + g_build->f_changed_skills)[n & 31] = skill;
    *count = n + 1;
    return true;
}

bool set_varp(int id, int value) {
    if (!g_build || id < 0) return false;
    uint32_t count = read_at<uint32_t>(g_base + g_build->varps + 8);
    auto* values = read_at<int32_t*>(g_base + g_build->varps + 0x10);
    if (!values || static_cast<uint32_t>(id) >= count) return false;
    values[id] = value;
    return true;
}

static uintptr_t varc_store() {
    uintptr_t c = client();
    return c ? read_at<uintptr_t>(c + 0x417b68) : 0;
}

bool set_varc_int(int id, int value) {
    uintptr_t store = varc_store();
    if (!store || !g_build->set_varc_int || id < 0 || id > 0xffff) return false;
    reinterpret_cast<void (*)(uintptr_t, int, int)>(g_base + g_build->set_varc_int)(store, id, value);
    return true;
}

bool set_varc_string(int id, const std::string& value) {
    uintptr_t store = varc_store();
    if (!store || !g_build->set_varc_string || id < 0 || id > 0xffff) return false;
    GameString str(value);
    alignas(8) uint8_t variant[0x28] = {};
    memcpy(variant, str.bytes, 24);
    uint32_t index = 2;  // string alternative; the setter copies it, `str` keeps ownership
    memcpy(variant + 0x20, &index, 4);
    reinterpret_cast<void (*)(uintptr_t, int, const void*)>(g_base + g_build->set_varc_string)(store, id, variant);
    return true;
}


// Sprite (graphic) by id through the game's sprite cache: getSprite(cache, id, outline, vflip,
// hflip, shadow, bool* ok) at 0x5d4074 returns (x0) a pointer to the cached {ctrl, Sprite*} slot,
// not add-ref'd; ok stays false while the sprites archive (8) isn't loaded. Cache = Client +0x413a98.
// Frame 0 only. Pixels are ARGB with alpha 0xff (0 = transparent).
bool sprite(int id, std::vector<uint32_t>& pixels, int& width, int& height) {
    uintptr_t c = client();
    if (!g_build || !c || id < 0 || id > 0xfffff) return false;
    // Only once the game is past its loading screens (state >= 10) and the cache has its
    // sprites archive (+0x10): a call during startup crashed the game thread.
    auto cache = read_at<uintptr_t>(c + 0x413a98);
    if (!cache || game_state() < 10 || !read_at<uintptr_t>(cache + 0x10)) return false;
    bool ok = false;
    using Fn = const uintptr_t* (*)(uintptr_t, int, int, bool, bool, int, bool*);
    const uintptr_t* slot = reinterpret_cast<Fn>(g_base + 0x5d4074)(cache, id, 0, false, false, 0, &ok);
    if (!ok || !slot || !slot[1]) return false;
    uintptr_t spr = slot[1];
    width = read_at<int32_t>(spr + 0x18);
    height = read_at<int32_t>(spr + 0x1c);
    auto px = read_at<uintptr_t>(spr + 0x10);
    if (!px || width <= 0 || height <= 0 || width > 2048 || height > 2048) return false;
    pixels.assign(reinterpret_cast<const uint32_t*>(px), reinterpret_cast<const uint32_t*>(px) + width * height);
    return true;
}

// Sprite +0x18/+0x1c trimmed width/height, +0x20/+0x24 x/y offset, +0x28/+0x2c full (canvas) width/height:
// the flips 0x6b1730 / 0x6b18bc recompute the offset as full - (size + offset) (static RE gen 46).
bool sprite_info(int id, int out[6]) {
    uintptr_t c = client();
    if (!g_build || !c || id < 0 || id > 0xfffff) return false;
    auto cache = read_at<uintptr_t>(c + 0x413a98);
    if (!cache || game_state() < 10 || !read_at<uintptr_t>(cache + 0x10)) return false;
    bool ok = false;
    using Fn = const uintptr_t* (*)(uintptr_t, int, int, bool, bool, int, bool*);
    const uintptr_t* slot = reinterpret_cast<Fn>(g_base + 0x5d4074)(cache, id, 0, false, false, 0, &ok);
    if (!ok || !slot || !slot[1]) return false;
    uintptr_t spr = slot[1];
    for (int i = 0; i < 6; i++) out[i] = read_at<int32_t>(spr + 0x18 + 4 * i);
    return out[0] > 0 && out[1] > 0;
}

bool sprite_array(int which, std::vector<int32_t>& out) {
    uintptr_t c = client();
    if (!g_build || !c || which < 0 || which > 1 || game_state() < 10) return false;
    uintptr_t obj = read_at<uintptr_t>(c + (which == 0 ? 0x2510 : 0x24b8));
    if (!obj) return false;
    uint64_t n = read_at<uint64_t>(obj + 8);
    uintptr_t arr = read_at<uintptr_t>(obj + 0x10);
    if (!arr || n == 0 || n > 4096) return false;
    out.assign(1, static_cast<int32_t>(n));
    for (uint64_t i = 0; i < n; i++) {
        uintptr_t r = arr + i * (which == 0 ? 0x30 : 0x48);
        int base = which == 0 ? 0x18 : 0x30;
        int w = read_at<int32_t>(r + base), h = read_at<int32_t>(r + base + 4);
        if (w < 0 || h < 0 || w > 2048 || h > 2048) return false;
        for (int k = 0; k < 6; k++) out.push_back(read_at<int32_t>(r + base + 4 * k));
        uintptr_t px = read_at<uintptr_t>(r + 0x10);
        uint64_t pal_n = which == 1 ? read_at<uint64_t>(r + 0x18) : 0;
        uintptr_t pal = which == 1 ? read_at<uintptr_t>(r + 0x28) : 0;
        if (pal_n > 256 || (pal_n && !pal)) return false;
        out.push_back(static_cast<int32_t>(pal_n));
        size_t count = static_cast<size_t>(w) * h;
        for (size_t k = 0; k < count; k++)
            out.push_back(!px ? 0 : which == 0 ? static_cast<int32_t>(reinterpret_cast<const uint32_t*>(px)[k])
                                               : reinterpret_cast<const uint8_t*>(px)[k]);
        for (uint64_t k = 0; k < pal_n; k++) out.push_back(static_cast<int32_t>(reinterpret_cast<const uint32_t*>(pal)[k]));
    }
    return true;
}

namespace {
uintptr_t texture_provider() {
    uintptr_t c = client();
    if (!g_build || !c || game_state() < 10) return 0;
    uintptr_t p = read_at<uintptr_t>(c + 0x4710);
    return p && read_at<uintptr_t>(p) == g_base + 0xa75748 ? p : 0;  // its vtable, else not built yet
}
uintptr_t texture_at(uintptr_t p, int id) {
    uintptr_t b = read_at<uintptr_t>(p + 8), e = read_at<uintptr_t>(p + 0x10);
    if (id < 0 || b + static_cast<uintptr_t>(id) * 16 >= e) return 0;
    return read_at<uintptr_t>(b + static_cast<uintptr_t>(id) * 16 + 8);
}
}  // namespace

bool textures(std::vector<int32_t>& out) {
    uintptr_t p = texture_provider();
    if (!p) return false;
    uintptr_t b = read_at<uintptr_t>(p + 8), e = read_at<uintptr_t>(p + 0x10);
    if (!b || e < b || (e - b) / 16 > 65536) return false;
    out.clear();
    for (int id = 0; id < static_cast<int>((e - b) / 16); id++) {
        uintptr_t t = texture_at(p, id);
        uintptr_t px = t ? read_at<uintptr_t>(t + 0x58) : 0;
        int32_t v[8] = {t != 0, t ? read_at<uint16_t>(t + 0x30) : 0, t ? read_at<uint8_t>(t + 0x34) : 0,
                        t ? read_at<int32_t>(t + 0x38) : -1, t ? read_at<int32_t>(t + 0x3c) : 0,
                        t ? read_at<int32_t>(t + 0x40) : 0, t ? read_at<uint8_t>(t + 0x60) : 0,
                        px ? static_cast<int32_t>(read_at<uint64_t>(px + 8)) : 0};
        out.insert(out.end(), v, v + 8);
    }
    return true;
}

bool texture_pixels(int id, bool load, std::vector<int32_t>& out) {
    uintptr_t p = texture_provider();
    uintptr_t t = p ? texture_at(p, id) : 0;
    if (!t) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    uintptr_t ctrl = 0, obj = read_at<uintptr_t>(t + 0x58);
    if (load) {
        SharedPtr r = reinterpret_cast<SharedPtr (*)(uintptr_t, int)>(g_base + 0x6d4930)(p, id);
        ctrl = r.ctrl;
        obj = r.obj;
    }
    uint64_t n = obj ? read_at<uint64_t>(obj + 8) : 0;
    auto px = obj ? read_at<const int32_t*>(obj + 0x10) : nullptr;
    bool ok = px && n > 0 && n <= 128 * 128;
    if (ok) out.assign(px, px + n);
    if (ctrl) release_shared(ctrl);
    return ok;
}

bool skybox_colour(int& argb) {
    uintptr_t c = client();
    uintptr_t sc = c ? read_at<uintptr_t>(c + 0x90) : 0;
    if (!sc) return false;
    auto ch = [&](int i) {
        float f = read_at<float>(sc + 0x90 + 4 * i);
        return static_cast<uint32_t>(f <= 0 ? 0 : f >= 1 ? 255 : static_cast<int>(f * 255 + 0.5f));
    };
    argb = static_cast<int>(ch(3) << 24 | ch(0) << 16 | ch(1) << 8 | ch(2));
    return true;
}

bool set_skybox_colour(int argb) {
    uintptr_t c = client();
    uintptr_t sc = c ? read_at<uintptr_t>(c + 0x90) : 0;
    if (!sc) return false;
    auto f = [&](int shift) { return static_cast<float>((static_cast<uint32_t>(argb) >> shift) & 0xff) / 255.0f; };
    reinterpret_cast<void (*)(uintptr_t, float, float, float, float)>(g_base + 0x6b083c)(sc, f(16), f(8), f(0), f(24));
    return true;
}

double texture_brightness() {
    uintptr_t p = texture_provider();
    return p ? read_at<double>(p + 0x38) : __builtin_nan("");
}

bool set_texture_brightness(double b) {
    uintptr_t p = texture_provider();
    if (!p || !(b > 0 && b < 2)) return false;
    *reinterpret_cast<double*>(p + 0x38) = b;
    return true;
}

// Overwrites the cached sprite's pixels in place (sprite overrides); same size only. The cache
// reloads the original after an LRU eviction, so callers re-apply periodically.
bool sprite_write(int id, const uint32_t* pixels, int width, int height) {
    uintptr_t c = client();
    if (!g_build || !c || id < 0 || id > 0xfffff) return false;
    // Only once the game is past its loading screens (state >= 10) and the cache has its
    // sprites archive (+0x10): a call during startup crashed the game thread.
    auto cache = read_at<uintptr_t>(c + 0x413a98);
    if (!cache || game_state() < 10 || !read_at<uintptr_t>(cache + 0x10)) return false;
    bool ok = false;
    using Fn = const uintptr_t* (*)(uintptr_t, int, int, bool, bool, int, bool*);
    const uintptr_t* slot = reinterpret_cast<Fn>(g_base + 0x5d4074)(cache, id, 0, false, false, 0, &ok);
    if (!ok || !slot || !slot[1]) return false;
    uintptr_t spr = slot[1];
    auto px = read_at<uintptr_t>(spr + 0x10);
    if (!px || read_at<int32_t>(spr + 0x18) != width || read_at<int32_t>(spr + 0x1c) != height) return false;
    memcpy(reinterpret_cast<void*>(px), pixels, static_cast<size_t>(width) * height * 4);
    return true;
}

bool item_sprite(int id, int quantity, int border, int shadow, int text_mode, bool noted,
                 std::vector<uint32_t>& pixels, int& width, int& height) {
    if (!g_build || !g_build->get_obj_type || id < 0 || id > 0xfffff) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    // createItemSprite dereferences the ObjType unchecked: validate the id first.
    {
        SharedPtr t = reinterpret_cast<SharedPtr (*)(int)>(g_base + g_build->get_obj_type)(id);
        uintptr_t ctrl = t.ctrl, o = t.obj;
        bool ok = o && read_at<int32_t>(o) != -1;
        release_shared(ctrl);
        if (!ok) return false;
    }
    // void createItemSprite(out x8, id, quantity, border, shadow, textMode, flags, width, height,
    // const string* text): the native ItemComposition.getItemSprite with its LRU cache (docs/offsets.md).
    // Only 36x32 is safe (the note overlay and text assume that stride).
    GameString empty{std::string()};
    using Fn = SharedPtr (*)(int, int, int, int, int, int, int, int, const void*);
    SharedPtr r = reinterpret_cast<Fn>(g_base + 0x63ff9c)(id, quantity, border, shadow, text_mode, noted ? 1 : 0, 36, 32, empty.bytes);
    uintptr_t ctrl = r.ctrl, spr = r.obj;
    if (!spr) { release_shared(ctrl); return false; }
    width = read_at<int32_t>(spr + 0x18);
    height = read_at<int32_t>(spr + 0x1c);
    auto px = read_at<uintptr_t>(spr + 0x10);
    bool ok = px && width > 0 && height > 0 && width <= 512 && height <= 512;
    if (ok) pixels.assign(reinterpret_cast<const uint32_t*>(px), reinterpret_cast<const uint32_t*>(px) + width * height);
    release_shared(ctrl);
    return ok;
}

bool item_def(int id, ItemDef& out) {
    if (!g_build || !g_build->get_obj_type || id < 0 || id > 0xfffff) return false;
    // Returns shared_ptr<ObjType> by value: a non-trivial type, so the result goes through x8
    // (AAPCS64 indirect result), not x0. A struct with a user-provided destructor gets the same ABI.
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    SharedPtr r = reinterpret_cast<SharedPtr (*)(int)>(g_base + g_build->get_obj_type)(id);
    uintptr_t sp[2] = {r.ctrl, r.obj};
    uintptr_t o = sp[1];
    if (!o) { release_shared(sp[0]); return false; }
    out.id = id;
    out.name = read_string(o + 0x08);
    out.price = read_at<int32_t>(o + 0xd8);
    out.stackable = read_at<int32_t>(o + 0xd4) == 1;
    out.members = read_at<uint8_t>(o + 0xe8) != 0;
    out.linked_note = read_at<int32_t>(o + 0x2b0);
    out.note_template = read_at<int32_t>(o + 0x2b4);
    out.placeholder = read_at<int32_t>(o + 0x314);
    out.placeholder_template = read_at<int32_t>(o + 0x318);
    out.weight = read_at<int32_t>(o + 0x2d0);
    out.category = read_at<int32_t>(o + 0x278);
    out.tradeable_guess = read_at<uint8_t>(o + 0x308) != 0;
    // Icon view, as CC_SETOBJECT (0x4e06dc) copies it into the widget's model zoom/angles: zoom2d +0xbc,
    // xan2d/yan2d/zan2d +0xc0/+0xc4/+0xc8 (static RE).
    out.zoom2d = read_at<int32_t>(o + 0xbc);
    out.xan2d = read_at<int32_t>(o + 0xc0);
    out.yan2d = read_at<int32_t>(o + 0xc4);
    out.zan2d = read_at<int32_t>(o + 0xc8);
    auto iops = read_at<uintptr_t>(o + 0x108);
    for (int i = 0; i < 5; i++) out.inventory_actions[i] = iops ? read_string(iops + i * 0x18) : std::string();
    // Model fields (static RE of the decoder 0x63bfe8 and ObjType::getModel 0x63f1e8): +0x04 inventory
    // model (mobile opcode 0x2c, int32; noted items copy their template's), ambient +0x2c4 (op 113),
    // contrast +0x2c8 (op 114, stored * 5 as on desktop). Recolour / retexture arrays (ops 40/41):
    // {u64 count, int16* data} at +0x60/+0x68 (find), +0x78/+0x80 (replace), +0x90/+0x98, +0xa8/+0xb0.
    out.inventory_model = read_at<int32_t>(o + 0x04);
    out.ambient = read_at<int32_t>(o + 0x2c4);
    out.contrast = read_at<int32_t>(o + 0x2c8);
    auto shorts = [&](uintptr_t at, std::vector<int16_t>& v) {
        auto n = read_at<uint64_t>(at);
        auto data = read_at<uintptr_t>(at + 8);
        if (!data || n == 0 || n > 255) return;
        v.assign(reinterpret_cast<const int16_t*>(data), reinterpret_cast<const int16_t*>(data) + n);
    };
    shorts(o + 0x60, out.recolor_from);
    shorts(o + 0x78, out.recolor_to);
    shorts(o + 0x90, out.retexture_from);
    shorts(o + 0xa8, out.retexture_to);
    release_shared(sp[0]);
    return true;
}

}  // namespace game

namespace game {

// Posed model of an Npc/Player: vtable slot 0 = getModel(this, cycle) → shared_ptr<Model> (x8),
// what the renderer calls every frame (Npc 0x475908, Player 0x45df78). Model: +0x20 vertex count,
// +0x38/+0x50/+0x68 int* x/y/z (local units, y negative = up), +0x18 height. Game thread only.
namespace {
// Renderable vtable slot 0: shared_ptr<Model> getModel(this, cycle) (x8 result). Actors build a
// posed model per call; a static loc's Model returns itself (0x71d604). Appends the vertices,
// offset by (dx, dz), to xyz; height = the model's height.
bool append_model(uintptr_t renderable, int dx, int dz, std::vector<int32_t>& xyz, int& height) {
    if (!renderable) return false;
    uintptr_t vt = read_at<uintptr_t>(renderable);
    if (!vt) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    auto fn = reinterpret_cast<SharedPtr (*)(uintptr_t, int)>(read_at<uintptr_t>(vt));
    g_own_model_call = true;
    SharedPtr r = fn(renderable, cycle());
    g_own_model_call = false;
    uintptr_t ctrl = r.ctrl, m = r.obj;
    if (!m) { release_shared(ctrl); return false; }
    int n = read_at<int32_t>(m + 0x20);
    auto xs = read_at<const int32_t*>(m + 0x38), ys = read_at<const int32_t*>(m + 0x50), zs = read_at<const int32_t*>(m + 0x68);
    height = std::max(height, read_at<int32_t>(m + 0x18));
    bool ok = false;
    if (n > 0 && n < 20000 && xs && ys && zs) {
        size_t base = xyz.size();
        xyz.resize(base + static_cast<size_t>(n) * 3);
        for (int i = 0; i < n; i++) {
            xyz[base + 3 * i] = xs[i] + dx;
            xyz[base + 3 * i + 1] = ys[i];
            xyz[base + 3 * i + 2] = zs[i] + dz;
        }
        ok = true;
    }
    release_shared(ctrl);
    return ok;
}

// Packed mesh of a game Model (lit) or ModelData (unlit), for the host's AndroidModel/AndroidModelData:
// {height, n, f, flags, t, priority, singleTile, x[n], y[n], z[n], a[f], b[f], c[f], then for each flag bit
// set, in order: 1 lit colours 1/2/3 [3f], 2 unlit HSL colours [f], 4 render types [f], 8 priorities [f],
// 16 alpha [f], 32 textures [f] (-1 none), 64 texture coords [f] (-1 none), 128 texture triangle types [t];
// then P/M/N [3t] when t > 0}. Both share the Mesh layout (x/y/z int* +0x38/+0x50/+0x68, faces int* +0x88/
// +0xa0/+0xb8, face count +0x70); optional arrays are shared_ptr {ctrl, obj} with obj {?, u64 count +8,
// data +0x10}, null when absent. Model (light 0x72a520): vertex count +0x20, lit colours int* +0xd0/+0xe8/
// +0x100 (colours3 -1 flat, -2 hidden), priorities +0x110, alpha +0x120, texture coords +0x130, textures
// +0x140, priority byte +0x158, texture triangles +0x15c with P/M/N int* +0x170/+0x188/+0x1a0, height +0x18,
// single-tile +0x1d. ModelData (decoder 0x720c4c): vertex count +0x24, render types +0xc8, priorities
// +0xd8, alpha +0xe8, texture coords +0xf8, colours (short) +0x108, textures +0x118, priority byte +0x140,
// texture triangles +0x144 with types byte* +0x158 and P/M/N u16* +0x170/+0x188/+0x1a0.
struct MeshLayout {
    uint32_t n, types, prio, alpha, coords, unlit, tex, priority, t, tri_types;
    bool model;
};
constexpr MeshLayout kModel{0x20, 0, 0x110, 0x120, 0x130, 0, 0x140, 0x158, 0x15c, 0, true};
constexpr MeshLayout kModelData{0x24, 0xc8, 0xd8, 0xe8, 0xf8, 0x108, 0x118, 0x140, 0x144, 0x158, false};

// data* of an optional array with at least `count` elements, else null.
template <typename T>
const T* opt_array(uintptr_t m, uint32_t off, int count) {
    if (!off) return nullptr;
    uintptr_t obj = read_at<uintptr_t>(m + off);
    if (!obj || read_at<uint64_t>(obj + 8) < static_cast<uint64_t>(count)) return nullptr;
    return read_at<const T*>(obj + 0x10);
}

template <typename T>
void put(std::vector<int32_t>& out, const T* p, int n) {
    for (int i = 0; i < n; i++) out.push_back(static_cast<int32_t>(p[i]));
}

bool pack_mesh(uintptr_t m, const MeshLayout& L, std::vector<int32_t>& out) {
    out.clear();
    int n = read_at<int32_t>(m + L.n), f = read_at<int32_t>(m + 0x70);
    int t = read_at<int32_t>(m + L.t);
    auto xs = read_at<const int32_t*>(m + 0x38), ys = read_at<const int32_t*>(m + 0x50), zs = read_at<const int32_t*>(m + 0x68);
    auto fa = read_at<const int32_t*>(m + 0x88), fb = read_at<const int32_t*>(m + 0xa0), fc = read_at<const int32_t*>(m + 0xb8);
    if (n <= 0 || n >= 20000 || f < 0 || f >= 40000 || !xs || !ys || !zs || (f > 0 && (!fa || !fb || !fc))) return false;
    if (t < 0 || t > 4096) t = 0;
    const int32_t *c1 = nullptr, *c2 = nullptr, *c3 = nullptr;
    if (L.model && f > 0) {
        c1 = read_at<const int32_t*>(m + 0xd0), c2 = read_at<const int32_t*>(m + 0xe8), c3 = read_at<const int32_t*>(m + 0x100);
        if (!c1 || !c2 || !c3) c1 = c2 = c3 = nullptr;
    }
    auto unlit = opt_array<int16_t>(m, L.unlit, f);
    auto types = opt_array<uint8_t>(m, L.types, f);
    auto prio = opt_array<uint8_t>(m, L.prio, f);
    auto alpha = opt_array<int8_t>(m, L.alpha, f);
    auto tex = opt_array<int16_t>(m, L.tex, f);
    auto coords = opt_array<int8_t>(m, L.coords, f);
    const uint8_t* tri_types = L.tri_types && t > 0 ? read_at<const uint8_t*>(m + L.tri_types) : nullptr;
    const int32_t *pm = nullptr, *mm = nullptr, *nm = nullptr;      // Model: int
    const uint16_t *pd = nullptr, *md = nullptr, *nd = nullptr;     // ModelData: u16
    if (t > 0) {
        if (L.model) pm = read_at<const int32_t*>(m + 0x170), mm = read_at<const int32_t*>(m + 0x188), nm = read_at<const int32_t*>(m + 0x1a0);
        else pd = read_at<const uint16_t*>(m + 0x170), md = read_at<const uint16_t*>(m + 0x188), nd = read_at<const uint16_t*>(m + 0x1a0);
        if (L.model ? !(pm && mm && nm) : !(pd && md && nd)) t = 0;
    }
    int flags = (c1 ? 1 : 0) | (unlit ? 2 : 0) | (types ? 4 : 0) | (prio ? 8 : 0) | (alpha ? 16 : 0) | (tex ? 32 : 0) |
                (coords ? 64 : 0) | (tri_types && t > 0 ? 128 : 0);
    out.reserve(7 + 3 * static_cast<size_t>(n) + 12 * static_cast<size_t>(f) + 4 * static_cast<size_t>(t));
    out.push_back(L.model ? read_at<int32_t>(m + 0x18) : 0);
    out.push_back(n);
    out.push_back(f);
    out.push_back(flags);
    out.push_back(t);
    out.push_back(read_at<int8_t>(m + L.priority));
    out.push_back(L.model ? read_at<uint8_t>(m + 0x1d) : 0);
    out.insert(out.end(), xs, xs + n);
    out.insert(out.end(), ys, ys + n);
    out.insert(out.end(), zs, zs + n);
    for (auto arr : {fa, fb, fc}) out.insert(out.end(), arr, arr + f);
    // A face index outside the vertex range would make the Java side throw: reject the model.
    for (size_t i = 7 + 3 * static_cast<size_t>(n), e = i + 3 * static_cast<size_t>(f); i < e; i++)
        if (out[i] < 0 || out[i] >= n) { out.clear(); return false; }
    if (c1)
        for (auto arr : {c1, c2, c3}) out.insert(out.end(), arr, arr + f);
    if (unlit) put(out, unlit, f);
    if (types) put(out, types, f);
    if (prio) put(out, prio, f);
    if (alpha) put(out, alpha, f);
    if (tex) put(out, tex, f);
    if (coords) put(out, coords, f);
    if (flags & 128) put(out, tri_types, t);
    if (t > 0) {
        if (L.model) for (auto arr : {pm, mm, nm}) put(out, arr, t);
        else for (auto arr : {pd, md, nd}) put(out, arr, t);
    }
    return true;
}

struct ModelSp {
    uintptr_t ctrl, obj;
    ~ModelSp() {}  // non-trivial: returned through x8, as the game's shared_ptr
};

// The posed model of an actor this frame (Renderable vtable slot 0), packed by pack_mesh.
bool read_mesh(uintptr_t renderable, std::vector<int32_t>& out) {
    out.clear();
    if (!renderable) return false;
    uintptr_t vt = read_at<uintptr_t>(renderable);
    if (!vt) return false;
    auto fn = reinterpret_cast<ModelSp (*)(uintptr_t, int)>(read_at<uintptr_t>(vt));
    g_own_model_call = true;
    ModelSp r = fn(renderable, cycle());
    g_own_model_call = false;
    bool ok = r.obj && pack_mesh(r.obj, kModel, out);
    release_shared(r.ctrl);
    return ok;
}
}  // namespace

namespace {
ModelSp load_model_data(int id) {
    ModelSp none{0, 0};
    if (!g_build || !g_build->model_store || id < 0 || id > 0xffff) return none;
    uintptr_t got = read_at<uintptr_t>(g_base + g_build->model_store_got);
    uintptr_t holder = got ? read_at<uintptr_t>(got) : 0;
    if (!holder) return none;
    return reinterpret_cast<ModelSp (*)(uintptr_t, int, int)>(g_base + g_build->model_store)(holder, id, 0);
}

// A classic-frame SeqType (+4 skeletal id < 0) with `frame` inside its frame ids (plain array: count +0x48,
// data +0x50 = frameSet << 16 | frame), else null: the game indexes the frames unchecked.
ModelSp seq_frame(int id, int frame) {
    ModelSp none{0, 0};
    if (id < 0 || frame < 0 || !g_build->get_seq_type) return none;
    ModelSp s = reinterpret_cast<ModelSp (*)(int)>(g_base + g_build->get_seq_type)(id);
    if (!s.obj) { release_shared(s.ctrl); return none; }
    uint64_t frames = read_at<uint64_t>(s.obj + 0x48);
    if (read_at<int32_t>(s.obj + 4) >= 0 || !read_at<uintptr_t>(s.obj + 0x50) || frames > 10000 ||
        static_cast<uint64_t>(frame) >= frames) {
        release_shared(s.ctrl);
        return none;
    }
    return s;
}
}  // namespace

bool pose_model(int id, const std::vector<int32_t>& xyz, int seq_a, int frame_a, int seq_b, int frame_b, std::vector<int32_t>& out) {
    out.clear();
    if (!g_build || !g_build->animate) return false;
    ModelSp md = load_model_data(id);
    if (!md.obj) { release_shared(md.ctrl); return false; }
    // ModelData vtable slot 0x70: light(out x8, this, ambient, contrast, x, y, z) -> a fresh lit Model.
    auto light = reinterpret_cast<ModelSp (*)(uintptr_t, int, int, int, int, int)>(read_at<uintptr_t>(read_at<uintptr_t>(md.obj) + 0x70));
    ModelSp lit = light(md.obj, 64, 768, -50, -10, -50);
    ModelSp a = seq_frame(seq_a, frame_a), b = seq_frame(seq_b, frame_b);
    bool ok = false;
    int n = lit.obj ? read_at<int32_t>(lit.obj + 0x20) : 0;
    if (lit.obj && a.obj && n > 0 && xyz.size() == static_cast<size_t>(n) * 3) {
        // The caller's vertices (its translate/rotate/scale) replace the store's before posing.
        auto xs = read_at<int32_t*>(lit.obj + 0x38), ys = read_at<int32_t*>(lit.obj + 0x50), zs = read_at<int32_t*>(lit.obj + 0x68);
        for (int i = 0; i < n; i++) { xs[i] = xyz[i]; ys[i] = xyz[n + i]; zs[i] = xyz[2 * n + i]; }
        struct AnimReq {
            uintptr_t model_data, model;
            int32_t rotate, pad0;
            uintptr_t seq_a;
            int32_t frame_a, pad1;
            uintptr_t seq_b;
            int32_t frame_b, ambient, contrast, x, y, z;
        } req{md.obj, lit.obj, 0, 0, a.obj, frame_a, 0, b.obj, b.obj ? frame_b : -1, 64, 768, -50, -10, -50};
        static_assert(offsetof(AnimReq, seq_a) == 0x18 && offsetof(AnimReq, frame_b) == 0x30 && offsetof(AnimReq, z) == 0x44);
        ModelSp posed = reinterpret_cast<ModelSp (*)(AnimReq*)>(g_base + g_build->animate)(&req);
        ok = posed.obj && pack_mesh(posed.obj, kModel, out);  // the scratch Model: packed before anything reuses it
        release_shared(posed.ctrl);
    }
    release_shared(b.ctrl);
    release_shared(a.ctrl);
    release_shared(lit.ctrl);
    release_shared(md.ctrl);
    return ok;
}

bool model_data(int id, std::vector<int32_t>& out) {
    out.clear();
    ModelSp r = load_model_data(id);
    bool ok = r.obj && pack_mesh(r.obj, kModelData, out);
    return ok;
}

bool actor_mesh(uintptr_t actor, std::vector<int32_t>& out) { return read_mesh(actor, out); }

// Draw log (setprop debug.osrsrl.drawlog 1): hooks the Npc vtable slot 0 (getModel) to record
// which Npc objects the renderer asks for a model, then logs them every ~50 cycles next to the
// WorldView's hash map nodes and index list (which entity the game draws vs which one we read).
namespace {
struct DrawSp {
    uintptr_t ctrl, obj;
    ~DrawSp() {}
};
using DrawGetModel = DrawSp (*)(uintptr_t, int);
DrawGetModel g_draw_orig = nullptr;
std::mutex g_draw_mutex;
std::unordered_map<uintptr_t, int> g_drawn;
uintptr_t g_draw_caller = 0;
DrawSp draw_hook(uintptr_t self, int cyc) {
    if (!g_own_model_call) {
        std::lock_guard lock(g_draw_mutex);
        g_drawn[self]++;
        if (!g_draw_caller) g_draw_caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    }
    return g_draw_orig(self, cyc);
}
}  // namespace

void drawlog_cycle() {
    static int counter = 0;
    static bool enabled = false;
    if (++counter % 50 != 0) return;
    char value[PROP_VALUE_MAX] = {};
    __system_property_get("debug.osrsrl.drawlog", value);
    enabled = value[0] == '1';
    if (!enabled) return;
    uintptr_t c = client();
    if (!c || game_state() != 30) return;
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return;
    auto buckets = read_at<uintptr_t>(wv + g_build->wv_npc_buckets);
    auto nb = read_at<uint64_t>(wv + g_build->wv_npc_bucket_count) & 0xffffffff;
    if (!buckets || !nb || nb > 1000000) return;
    // Install the hook on the first Npc's vtable.
    if (!g_draw_orig) {
        for (uint64_t b = 0; b < nb && !g_draw_orig; b++)
            for (auto node = read_at<uintptr_t>(buckets + b * 8); node; node = read_at<uintptr_t>(node + 24)) {
                uintptr_t npc = read_at<uintptr_t>(node + 16);
                if (!npc) continue;
                uintptr_t vt = read_at<uintptr_t>(npc);
                LOGI("drawlog: Npc vtable %#lx (elf %#lx) slot0 %#lx (elf %#lx)", (unsigned long)vt,
                     (unsigned long)(vt - g_base), (unsigned long)read_at<uintptr_t>(vt),
                     (unsigned long)(read_at<uintptr_t>(vt) - g_base));
                void* old = nullptr;
                auto** slot = reinterpret_cast<void**>(vt);
                long page = sysconf(_SC_PAGESIZE);
                auto start = reinterpret_cast<void*>(vt & ~(page - 1));
                if (mprotect(start, page * 2, PROT_READ | PROT_WRITE) == 0) {
                    old = *slot;
                    g_draw_orig = reinterpret_cast<DrawGetModel>(old);
                    *slot = reinterpret_cast<void*>(draw_hook);
                    mprotect(start, page * 2, PROT_READ);
                    LOGI("drawlog: getModel hooked");
                } else {
                    LOGE("drawlog: mprotect failed");
                }
                break;
            }
        return;
    }
    std::unordered_map<uintptr_t, int> drawn;
    uintptr_t caller;
    {
        std::lock_guard lock(g_draw_mutex);
        drawn.swap(g_drawn);
        caller = g_draw_caller;
    }
    int px = read_at<int32_t>(c + 0xcb7c), py = read_at<int32_t>(c + 0xcb80);
    int bx = read_at<int32_t>(wv + g_build->wv_base_x), by = read_at<int32_t>(wv + g_build->wv_base_y);
    int count = read_at<int32_t>(wv + g_build->wv_npc_count);
    auto indices = read_at<const int32_t*>(wv + g_build->wv_npc_indices);
    std::unordered_map<int, int> in_list;
    for (int i = 0; indices && i < count && i < 65536; i++) in_list[indices[i]]++;
    LOGI("drawlog: player scene %d,%d base %d,%d list %d buckets %lu drawn objects %zu caller elf %#lx world %d flags %#x", px, py, bx, by,
         count, (unsigned long)nb, drawn.size(), (unsigned long)(caller ? caller - g_base : 0), world(), world_flags());
    std::unordered_map<uintptr_t, int> seen;
    for (uint64_t b = 0; b < nb; b++)
        for (auto node = read_at<uintptr_t>(buckets + b * 8); node; node = read_at<uintptr_t>(node + 24)) {
            int index = read_at<int32_t>(node);
            uintptr_t npc = read_at<uintptr_t>(node + 16);
            if (!npc) continue;
            seen[npc]++;
            int fx = read_at<int32_t>(npc + 0x1fc), fy = read_at<int32_t>(npc + 0x200);
            if (std::abs((fx >> 7) - px) > 16 || std::abs((fy >> 7) - py) > 16) continue;
            auto def = read_at<uintptr_t>(npc + g_build->npc_def);
            auto it = drawn.find(npc);
            LOGI("drawlog: node idx %d npc %#lx id %d '%s' fine %d,%d (tile %d,%d) +268 %d,%d path0 %d,%d upd %d list %d drawn %d bucket %lu",
                 index, (unsigned long)npc, def ? read_at<int32_t>(def) : -1, def ? read_string(def + g_build->def_name).c_str() : "",
                 fx, fy, bx + (fx >> 7), by + (fy >> 7), read_at<int32_t>(npc + 0x268), read_at<int32_t>(npc + 0x26c),
                 read_at<int32_t>(npc + 0x3f0), read_at<int32_t>(npc + 0x418), read_at<uint8_t>(npc + 0x768),
                 in_list.count(index) ? in_list[index] : 0, it != drawn.end() ? it->second : 0, (unsigned long)b);
        }
    // Pointers not in the map may be freed already (region rebuild): don't dereference them.
    for (auto& [p, n] : drawn)
        if (!seen.count(p)) LOGI("drawlog: drawn but not in map: %#lx calls %d", (unsigned long)p, n);
}

bool actor_model(uintptr_t actor, std::vector<int32_t>& xyz, int& height) {
    xyz.clear();
    height = 0;
    return append_model(actor, 0, 0, xyz, height);
}

int actor_height(uintptr_t actor) {
    std::vector<int32_t> xyz;
    int field = 0;
    if (!append_model(actor, 0, 0, xyz, field)) return -1;
    int h = 0;
    for (size_t i = 1; i < xyz.size(); i += 3) h = std::max(h, -xyz[i]);
    static int logs = 0;
    if (logs < 3) { logs++; LOGI("actor_height %d (model +0x18 = %d)", h, field); }
    return h;
}

// Scene objects (static RE of the tile draw 0x6e1684 and the object constructors; positions are
// local units, heights absolute). Renderable holders point at {ctrl, obj} pairs.
//   GameObject: Renderable* +0x230, x +0x1dc, height +0x1d8, y +0x1e0, orientation +0x1e4, tag +0x1f0
//   Wall (tile +0x108): A *(*(+0x410)+8), B *(*(+0x418)+8); x/height/y +0x28/+0x2c/+0x30, tag +0x38
//   Wall decoration (+0x110): A *(*(+0x418)+8) offset +0x48/+0x4c, B *(*(+0x420)+8) offset +0x50/+0x54
//   Ground decoration (+0x118): *(*(+0x220)+8)
//   Item pile (+0x120): *(*(+0x5e8)+8), *(*(+0x5f0)+8), *(*(+0x5e0)+8) (unverified)
bool object_model(int plane, int sx, int sy, int layer, uint64_t tag, ObjectModel& out) {
    out = {};
    uintptr_t c = client();
    uintptr_t wv = c && g_build ? read_at<uintptr_t>(c + g_build->f_world_view) : 0;
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    if (!scene) return false;
    int planes = read_at<int32_t>(scene + 0x944), w = read_at<int32_t>(scene + 0x948), h = read_at<int32_t>(scene + 0x94c);
    uintptr_t slots = read_at<uintptr_t>(scene + 0x968);
    if (!slots || plane < 0 || plane >= planes || sx < 0 || sx >= w || sy < 0 || sy >= h || planes > 4 || w > 256 || h > 256) return false;
    uintptr_t tile = read_at<uintptr_t>(slots + ((static_cast<size_t>(plane) * w + sx) * h + sy) * 0x10 + 8);
    if (!tile) return false;
    auto held = [](uintptr_t o, uint32_t holder) {
        uintptr_t pair = read_at<uintptr_t>(o + holder);
        return pair ? read_at<uintptr_t>(pair + 8) : 0;
    };
    auto simple = [&](uintptr_t o) {
        out.x = read_at<int32_t>(o + 0x28);
        out.height = read_at<int32_t>(o + 0x2c);
        out.y = read_at<int32_t>(o + 0x30);
    };
    int mh = 0;
    switch (layer) {
        case kGameObject: {
            int n = read_at<int32_t>(tile + 0x34);
            uintptr_t objs = read_at<uintptr_t>(tile + 0x38);
            if (n <= 0 || n > 16 || !objs) return false;
            for (int i = 0; i < n; i++) {
                uintptr_t o = read_at<uintptr_t>(objs + static_cast<size_t>(i) * 0x10 + 8);
                if (!o || read_at<uint64_t>(o + 0x1f0) != tag) continue;
                out.x = read_at<int32_t>(o + 0x1dc);
                out.height = read_at<int32_t>(o + 0x1d8);
                out.y = read_at<int32_t>(o + 0x1e0);
                out.orientation = read_at<int32_t>(o + 0x1e4);
                append_model(read_at<uintptr_t>(o + 0x230), 0, 0, out.xyz, mh);
                break;
            }
            break;
        }
        case kWall: {
            uintptr_t o = read_at<uintptr_t>(tile + 0x108);
            if (!o || read_at<uint64_t>(o + 0x38) != tag) return false;
            simple(o);
            append_model(held(o, 0x410), 0, 0, out.xyz, mh);
            append_model(held(o, 0x418), 0, 0, out.xyz, mh);
            break;
        }
        case kWallDecoration: {
            uintptr_t o = read_at<uintptr_t>(tile + 0x110);
            if (!o || read_at<uint64_t>(o + 0x38) != tag) return false;
            simple(o);
            if (!append_model(held(o, 0x418), read_at<int32_t>(o + 0x48), read_at<int32_t>(o + 0x4c), out.xyz, mh))
                append_model(held(o, 0x420), read_at<int32_t>(o + 0x50), read_at<int32_t>(o + 0x54), out.xyz, mh);
            break;
        }
        case kGroundDecoration: {
            uintptr_t o = read_at<uintptr_t>(tile + 0x118);
            if (!o || read_at<uint64_t>(o + 0x38) != tag) return false;
            simple(o);
            append_model(held(o, 0x220), 0, 0, out.xyz, mh);
            break;
        }
        case kItemPile: {  // tile +0x120: three item renderables (holders +0x5e8/+0x5f0/+0x5e0); tag ignored
            uintptr_t o = read_at<uintptr_t>(tile + 0x120);
            if (!o) return false;
            out.x = sx * 128 + 64;
            out.y = sy * 128 + 64;
            for (uint32_t holder : {0x5e8u, 0x5f0u, 0x5e0u}) append_model(held(o, holder), 0, 0, out.xyz, mh);
            break;
        }
        default: return false;
    }
    out.model_height = mh;
    return !out.xyz.empty();
}

// Animated locs (RuneLite DynamicObject; static RE client-api-host-gaps-reach of the constructor 0x71328c and
// getModel 0x7136dc): the loc adder 0x710de4 and LOC_ANIM build one for a loc with an animation (LocType +0xd8)
// or a multiloc, in any renderable slot object_model reads (game object +0x230, wall A/B, wall decoration A/B,
// ground decoration), else a static Model. Object (block +0x20 of a 0xa8 make_shared), vptr 0xa77620: +0x28 loc
// id, +0x2c shape, +0x30 orientation, +0x34 plane, +0x38/+0x3c scene x/y, AnimationState +0x40 {+0x48 SeqType*,
// +0x50 seq id (-1 none), +0x54 u16 frame cycle, +0x56 u16 frame}, a second state +0x60 (the replaced loc's
// animation), +0x80 last cycle (desktop's cycleStart; getModel advances from it and stores the cycle).
namespace {
constexpr uintptr_t kDynamicObjectVtable = 0xa77620;

uintptr_t object_renderable(int plane, int sx, int sy, int layer, uint64_t tag, int slot) {
    uintptr_t c = client();
    uintptr_t wv = c && g_build ? read_at<uintptr_t>(c + g_build->f_world_view) : 0;
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    if (!scene) return 0;
    int planes = read_at<int32_t>(scene + 0x944), w = read_at<int32_t>(scene + 0x948), h = read_at<int32_t>(scene + 0x94c);
    uintptr_t slots = read_at<uintptr_t>(scene + 0x968);
    if (!slots || plane < 0 || plane >= planes || sx < 0 || sx >= w || sy < 0 || sy >= h || planes > 4 || w > 256 || h > 256) return 0;
    uintptr_t tile = read_at<uintptr_t>(slots + ((static_cast<size_t>(plane) * w + sx) * h + sy) * 0x10 + 8);
    if (!tile) return 0;
    auto held = [](uintptr_t o, uint32_t holder) {
        uintptr_t pair = read_at<uintptr_t>(o + holder);
        return pair ? read_at<uintptr_t>(pair + 8) : 0;
    };
    switch (layer) {
        case kGameObject: {
            int n = read_at<int32_t>(tile + 0x34);
            uintptr_t objs = read_at<uintptr_t>(tile + 0x38);
            if (slot != 0 || n <= 0 || n > 16 || !objs) return 0;
            for (int i = 0; i < n; i++) {
                uintptr_t o = read_at<uintptr_t>(objs + static_cast<size_t>(i) * 0x10 + 8);
                if (o && read_at<uint64_t>(o + 0x1f0) == tag) return read_at<uintptr_t>(o + 0x230);
            }
            return 0;
        }
        case kWall: case kWallDecoration: case kGroundDecoration: {
            uintptr_t o = read_at<uintptr_t>(tile + (layer == kWall ? 0x108 : layer == kWallDecoration ? 0x110 : 0x118));
            if (!o || read_at<uint64_t>(o + 0x38) != tag) return 0;
            if (layer == kGroundDecoration) return slot == 0 ? held(o, 0x220) : 0;
            uint32_t a = layer == kWall ? 0x410 : 0x418;
            return slot == 0 ? held(o, a) : slot == 1 ? held(o, a + 8) : 0;
        }
        default: return 0;
    }
}
}  // namespace

bool dynamic_object(int plane, int sx, int sy, int layer, uint64_t tag, int slot, int32_t f[kDynamicObjectFields],
                    std::vector<int32_t>* mesh) {
    uintptr_t r = object_renderable(plane, sx, sy, layer, tag, slot);
    if (!r || read_at<uintptr_t>(r) != g_base + kDynamicObjectVtable) return false;
    if (mesh) return read_mesh(r, *mesh);
    const uint32_t ints[] = {0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x50, 0x80};
    for (int i = 0; i < 8; i++) f[i] = read_at<int32_t>(r + ints[i]);
    f[8] = read_at<uint16_t>(r + 0x56);
    f[9] = read_at<uint16_t>(r + 0x54);
    return true;
}

bool npc_def(int id, NpcDef& out) {
    if (!g_build || !g_build->get_npc_type || id < 0 || id > 0xfffff) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    SharedPtr r = reinterpret_cast<SharedPtr (*)(int)>(g_base + g_build->get_npc_type)(id);
    uintptr_t ctrl = r.ctrl, o = r.obj;
    if (!o) { release_shared(ctrl); return false; }
    out.id = read_at<int32_t>(o);
    out.name = read_string(o + 0x08);
    out.size = read_at<int32_t>(o + 0x20);
    out.combat_level = read_at<int32_t>(o + 0x11c);
    out.minimap_visible = read_at<uint8_t>(o + 0x118) != 0;
    out.interactable = read_at<uint8_t>(o + 0x158) != 0;
    // Multi-NPC (ops 106/118): same layout as LocType's multiloc.
    out.varbit = read_at<int32_t>(o + 0x150);
    out.varp = read_at<int32_t>(o + 0x154);
    auto tn = read_at<uint64_t>(o + 0x140);
    auto tp = read_at<const int32_t*>(o + 0x148);
    out.transforms.clear();
    if (tn > 0 && tn < 4096 && tp) out.transforms.assign(tp, tp + tn);
    auto op_text = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + g_build->op_text);
    for (int i = 0; i < 5; i++) {
        uintptr_t str = op_text(o + 0x100, i);
        out.actions[i] = str ? read_string(str) : std::string();
    }
    // Static RE of the inlined decoder (gen 28; unverified live): models {u64 count, int32*} at
    // +0x30/+0x38 (mobile opcode 61, u8 count then int32s), chathead models +0x48/+0x50 (opcode 62),
    // recolour find/replace {u64 count, int16*} +0xa8/+0xb0 and +0xc0/+0xc8 (opcode 40); width/height
    // scale +0x120/+0x124 (NpcType::getModel 0x6380d8 rescales when either isn't 128).
    auto ints = [&](uintptr_t at, std::vector<int32_t>& v) {
        auto n = read_at<uint64_t>(at);
        auto data = read_at<const int32_t*>(at + 8);
        if (data && n > 0 && n <= 255) v.assign(data, data + n);
    };
    auto shorts = [&](uintptr_t at, std::vector<int16_t>& v) {
        auto n = read_at<uint64_t>(at);
        auto data = read_at<const int16_t*>(at + 8);
        if (data && n > 0 && n <= 255) v.assign(data, data + n);
    };
    out.models.clear(); out.chathead_models.clear(); out.recolor_from.clear(); out.recolor_to.clear();
    ints(o + 0x30, out.models);
    ints(o + 0x48, out.chathead_models);
    shorts(o + 0xa8, out.recolor_from);
    shorts(o + 0xc0, out.recolor_to);
    out.width_scale = read_at<int32_t>(o + 0x120);
    for (int i = 0; i < 6; i++) out.stats[i] = read_at<int32_t>(o + 0x1a0 + 4 * i);  // ops 74-79 (static RE gen 39)
    out.height_scale = read_at<int32_t>(o + 0x124);
    release_shared(ctrl);
    return true;
}

}  // namespace game

namespace game {

namespace {
// Definition params (opcode 249, decoder 0x62d550): a hash map {+0x08 buckets, +0x10 bucket count,
// +0x18 size}, bucket = key % count; nodes (0x40) {+0x00 u64 key, +0x08 int value, +0x10 string
// (24-byte), +0x28 long value, +0x30 is-string byte, +0x38 next}.
bool read_param(uintptr_t map, int key, TypeParam& out) {
    auto buckets = read_at<uintptr_t>(map + 0x08);
    uint64_t count = read_at<uint64_t>(map + 0x10);
    if (!buckets || !count || count > (1u << 20) || key < 0) return false;
    for (uintptr_t n = read_at<uintptr_t>(buckets + (static_cast<uint64_t>(key) % count) * 8), i = 0; n && i < 4096;
         n = read_at<uintptr_t>(n + 0x38), i++) {
        if (read_at<uint64_t>(n) != static_cast<uint64_t>(key)) continue;
        out.is_string = read_at<uint8_t>(n + 0x30) == 1;
        out.int_value = read_at<int32_t>(n + 0x08);
        if (out.is_string) out.string_value = read_string(n + 0x10);
        return true;
    }
    return false;
}
}  // namespace

// Params of an obj (kind 0, ObjType +0x2d8), npc (1, NpcType +0x170), loc (2, LocType +0x160) or
// struct (3, StructType +0x08; getStructType 0x64a69c loads config group 34, returns via x8).
// `f(definition)` runs while the definition is held; locs stay referenced by their cache.
template <typename F>
static bool with_type(int kind, int id, F f) {
    if (!g_build || id < 0 || id > 0xfffff) return false;
    if (kind == 2) {
        if (!g_build->get_loc_type) return false;
        auto sp = reinterpret_cast<const uintptr_t* (*)(int)>(g_base + g_build->get_loc_type)(id);
        return sp && sp[1] && f(sp[1]);
    }
    constexpr uintptr_t kGetStructType = 0x64a69c;  // build 241.4
    uintptr_t fn = kind == 0 ? g_build->get_obj_type : kind == 1 ? g_build->get_npc_type : kind == 3 ? kGetStructType : 0;
    if (!fn) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    SharedPtr r = reinterpret_cast<SharedPtr (*)(int)>(g_base + fn)(id);
    uintptr_t ctrl = r.ctrl, o = r.obj;
    bool ok = o && f(o);
    release_shared(ctrl);
    return ok;
}

static uintptr_t param_map(int kind, uintptr_t type) { return type + (kind == 0 ? 0x2d8 : kind == 1 ? 0x170 : kind == 2 ? 0x160 : 0x08); }

// Sub-ops under op `op` (NpcType ops +0x100, LocType +0xe8): op entry +0x38 = the nested op vector
// (shared_ptr {+0x30 ctrl, +0x38 vector}; decoder 0x65c2b8: NPC opcodes 251/253, loc 100/102,
// sub-op ids 1-based on the wire, stored at id - 1). Texts through opText like the ops.
bool sub_ops(int kind, int id, int op, std::vector<std::string>& out, bool& present) {
    out.clear();
    present = false;
    if ((kind != 1 && kind != 2) || op < 0 || op > 31 || !g_build->op_text) return false;
    return with_type(kind, id, [&](uintptr_t t) {
        uintptr_t vec = t + (kind == 1 ? 0x100 : 0xe8);
        uintptr_t b = read_at<uintptr_t>(vec), e = read_at<uintptr_t>(vec + 8);
        if (!b || e < b || static_cast<uintptr_t>(op) >= (e - b) / 0x40) return true;
        uintptr_t sub = read_at<uintptr_t>(b + static_cast<uintptr_t>(op) * 0x40 + 0x38);
        if (!sub) return true;
        uintptr_t sb = read_at<uintptr_t>(sub), se = read_at<uintptr_t>(sub + 8);
        if (!sb || se < sb || (se - sb) / 0x40 > 256) return true;
        present = true;
        auto op_text = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + g_build->op_text);
        for (int i = 0, n = static_cast<int>((se - sb) / 0x40); i < n; i++) {
            uintptr_t str = op_text(sub, i);
            out.push_back(str ? read_string(str) : std::string());
        }
        return true;
    });
}

bool type_param(int kind, int id, int key, TypeParam& out) {
    return with_type(kind, id, [&](uintptr_t t) { return read_param(param_map(kind, t), key, out); });
}

// Every key in the params map (bucket chains as in read_param).
bool type_param_keys(int kind, int id, std::vector<int32_t>& keys) {
    keys.clear();
    return with_type(kind, id, [&](uintptr_t t) {
        uintptr_t map = param_map(kind, t);
        auto buckets = read_at<uintptr_t>(map + 0x08);
        uint64_t count = read_at<uint64_t>(map + 0x10);
        if (!buckets || !count) return true;  // no params
        if (count > (1u << 20)) return false;
        for (uint64_t b = 0; b < count; b++)
            for (uintptr_t n = read_at<uintptr_t>(buckets + b * 8), i = 0; n && i < 4096; n = read_at<uintptr_t>(n + 0x38), i++)
                keys.push_back(static_cast<int32_t>(read_at<uint64_t>(n)));
        return true;
    });
}

bool loc_def(int id, LocDef& out) {
    if (!g_build || !g_build->get_loc_type || id < 0 || id > 0xfffff) return false;
    auto sp = reinterpret_cast<const uintptr_t* (*)(int)>(g_base + g_build->get_loc_type)(id);
    if (!sp) return false;
    uintptr_t o = sp[1];  // the cache keeps its reference while we read (game thread)
    if (!o) return false;
    out.id = read_at<int32_t>(o + 0x08);
    out.name = read_string(o + 0x40);
    out.size_x = read_at<int32_t>(o + 0xbc);
    out.size_y = read_at<int32_t>(o + 0xc0);
    out.varbit = read_at<int32_t>(o + 0x150);
    out.varp = read_at<int32_t>(o + 0x154);
    auto n = read_at<uint64_t>(o + 0x140);
    auto t = read_at<const int32_t*>(o + 0x148);
    out.transforms.clear();
    if (n > 0 && n < 4096 && t) out.transforms.assign(t, t + n);
    auto op_text = reinterpret_cast<uintptr_t (*)(uintptr_t, int)>(g_base + g_build->op_text);
    for (int i = 0; i < 5; i++) {
        uintptr_t str = op_text(o + 0xe8, i);
        out.actions[i] = str ? read_string(str) : std::string();
    }
    return true;
}

bool scene_heights(SceneHeights& out) {
    if (!g_build || !g_build->tile_settings) return false;
    uintptr_t c = client();
    if (!c) return false;
    uintptr_t wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return false;
    uintptr_t scene = read_at<uintptr_t>(wv + 0x10);
    if (!scene) return false;
    int planes = read_at<int32_t>(scene + 0x944), sx = read_at<int32_t>(scene + 0x948), sy = read_at<int32_t>(scene + 0x94c);
    auto heights = read_at<const int32_t*>(scene + 0x3d0);
    if (planes < 1 || planes > 4 || sx < 1 || sx > 512 || sy < 1 || sy > 512 || !heights) return false;
    out.planes = planes;
    out.size_x = sx;
    out.size_y = sy;
    size_t n = static_cast<size_t>(planes) * (sx + 1) * (sy + 1);
    out.heights.assign(heights, heights + n);
    out.settings.assign(static_cast<size_t>(planes) * sx * sy, 0);
    auto settings = reinterpret_cast<uint32_t (*)(uintptr_t, int, int, int)>(g_base + g_build->tile_settings);
    for (int p = 0; p < planes; p++)
        for (int x = 0; x < sx; x++)
            for (int y = 0; y < sy; y++) out.settings[(static_cast<size_t>(p) * sx + x) * sy + y] = static_cast<uint8_t>(settings(scene, p, x, y));
    return true;
}

}  // namespace game

namespace game {

bool enum_def(int id, EnumDef& out) {
    if (!g_build || !g_build->get_enum_type || id < 0 || id > 0xfffff) return false;
    struct SharedPtr {
        uintptr_t ctrl, obj;
        ~SharedPtr() {}
    };
    SharedPtr r = reinterpret_cast<SharedPtr (*)(int)>(g_base + g_build->get_enum_type)(id);
    uintptr_t ctrl = r.ctrl, e = r.obj;
    if (!e) { release_shared(ctrl); return false; }
    // EnumType: +0 u16 key type, +2 u16 value type ('s' strings, 0xcf longs), +8 default string,
    // +0x20 default int, +0x30 count, +0x48 int keys, +0x60 int values, +0x90 strings (24 bytes).
    out.key_type = read_at<uint16_t>(e);
    out.value_type = read_at<uint16_t>(e + 2);
    out.default_string = read_string(e + 8);
    out.default_int = read_at<int32_t>(e + 0x20);
    int n = read_at<int32_t>(e + 0x30);
    if (n < 0 || n > 100000) n = 0;
    auto keys = read_at<const int32_t*>(e + 0x48);
    auto ints = read_at<const int32_t*>(e + 0x60);
    auto strs = read_at<uintptr_t>(e + 0x90);
    out.keys.clear();
    out.int_values.clear();
    out.string_values.clear();
    for (int i = 0; i < n; i++) {
        out.keys.push_back(keys ? keys[i] : 0);
        if (out.value_type == 's') out.string_values.push_back(strs ? read_string(strs + i * 0x18) : std::string());
        else out.int_values.push_back(ints ? ints[i] : 0);
    }
    release_shared(ctrl);
    return true;
}

}  // namespace game

namespace game {
namespace {
// MenuManager: +0x140 menu model (vtable menu_vtable), +0x7c0 menu shown (0 when closed).
// Menu model: +0x140 vector of 0x18-byte nodes {EntryList*, u64 index, children vector*};
// EntryList: +0x00 callbacks (0xa0 each: invoker at +0, storage at +0x10), +0x18 entries
// (0x150 each). Entry: +0x50 target, +0xb8 option (24-byte strings, each followed by a small
// arena), +0x120..+0x148 parameters, +0x14f kind byte.
constexpr uint32_t kMgrMenu = 0x140, kMgrShown = 0x7c0, kMenuNodes = 0x140, kListEntries = 0x18;
constexpr uint32_t kEntrySize = 0x150, kNodeSize = 0x18;

uintptr_t menu_manager() {
    uintptr_t c = client();
    return c ? read_at<uintptr_t>(c + g_build->f_menu_manager) : 0;
}

void read_entry(uintptr_t e, MenuEntry& out) {
    out.address = e;
    out.option = read_string(e + 0xb8);
    out.target = read_string(e + 0x50);
    memcpy(out.raw, reinterpret_cast<const void*>(e + 0x120), sizeof out.raw);
    out.kind = read_at<uint8_t>(e + 0x14f);
}

uintptr_t node_entry(uintptr_t node) {
    auto list = read_at<uintptr_t>(node);
    if (!list) return 0;
    auto entries = read_at<uintptr_t>(list + kListEntries);
    auto end = read_at<uintptr_t>(list + kListEntries + 8);
    uint64_t index = read_at<uint64_t>(node + 8);
    if (!entries || index >= (end - entries) / kEntrySize) return 0;
    return entries + index * kEntrySize;
}
}  // namespace

uintptr_t menu_object() {
    uintptr_t m = menu_manager();
    return m ? m + kMgrMenu : 0;
}

bool menu_open() {
    uintptr_t m = menu_manager();
    return m && read_at<uintptr_t>(m + kMgrShown) != 0;
}

bool menu_close() {
    uintptr_t m = menu_manager();
    if (!m || !g_build->menu_clear_model) return false;
    if (!read_at<uintptr_t>(m + kMgrShown)) return true;
    reinterpret_cast<void (*)(uintptr_t)>(g_base + g_build->menu_clear_model)(m + kMgrMenu);
    reinterpret_cast<void (*)(uintptr_t)>(g_base + g_build->menu_clear_display)(m + 0x540);
    *reinterpret_cast<uintptr_t*>(m + kMgrShown) = 0;
    return true;
}

bool menu_reorder(const std::vector<int>& order) {
    uintptr_t m = menu_manager();
    if (!m || !g_build->menu_build_display) return false;
    uintptr_t display = m + 0x540, model = m + kMgrMenu;
    uintptr_t layout = read_at<uintptr_t>(m + 0x7c8);
    // Shown is the model itself for long-press menus (drawn from it directly), or the display
    // built from it (0x599f68), which is rebuilt below.
    uintptr_t shown = read_at<uintptr_t>(m + kMgrShown);
    bool rebuild = shown == display;
    if ((shown != display && shown != model) || (rebuild && !layout)) {
        LOGE("menu reorder: shown %#lx, model %#lx", (unsigned long)shown, (unsigned long)model);
        return false;
    }
    uintptr_t nb = read_at<uintptr_t>(model + kMenuNodes), ne = read_at<uintptr_t>(model + kMenuNodes + 8);
    size_t n = nb && ne > nb ? (ne - nb) / kNodeSize : 0;
    if (n < 2 || n > 512 || order.empty() || order.size() > n) {
        LOGE("menu reorder: %zu nodes, %zu in the order", n, order.size());
        return false;
    }
    std::vector<bool> seen(n);
    for (size_t i = 0; i < order.size(); i++) {
        if (order[i] < 0 || static_cast<size_t>(order[i]) >= n || seen[order[i]]) return false;
        seen[order[i]] = true;
    }
    // Dropped nodes (a plugin removed the entry): only childless ones, so the shortened node vector
    // owns nothing that is left behind (+0x10 = the children vector of a submenu node).
    for (size_t i = 0; i < n; i++)
        if (!seen[i] && read_at<uintptr_t>(nb + i * kNodeSize + 0x10)) {
            LOGE("menu reorder: node %zu has children, not dropped", i);
            return false;
        }
    // The game sorts a menu by ordering these {EntryList*, entry index, children*} nodes over its
    // unsorted entry list; do the same.
    std::vector<uint8_t> nodes(reinterpret_cast<const uint8_t*>(nb), reinterpret_cast<const uint8_t*>(ne));
    if (rebuild) reinterpret_cast<void (*)(uintptr_t)>(g_base + g_build->menu_clear_display)(display);
    for (size_t i = 0; i < order.size(); i++)
        memcpy(reinterpret_cast<void*>(nb + i * kNodeSize), nodes.data() + order[i] * kNodeSize, kNodeSize);
    if (order.size() < n) {
        memset(reinterpret_cast<void*>(nb + order.size() * kNodeSize), 0, (n - order.size()) * kNodeSize);
        *reinterpret_cast<uintptr_t*>(model + kMenuNodes + 8) = nb + order.size() * kNodeSize;
    }
    if (rebuild)
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, uintptr_t)>(g_base + g_build->menu_build_display)(display, model, layout);
    return true;
}

void pick_at(int screen_x, int screen_y) {
    if (!g_build || !g_build->pick_set) return;
    // As a touch down: one-shot pick of this point on the next rendered frame; clears "picked".
    reinterpret_cast<void (*)(int, int, unsigned)>(g_base + g_build->pick_set)(screen_x, screen_y, 0);
}

bool pick_ready() {
    return g_build && g_build->pick_ready && read_at<uint8_t>(g_base + g_build->pick_ready) != 0 &&
           read_at<int32_t>(g_base + g_build->pick_mode) == 3;
}

bool menu_build_at(int x, int y) {
    uintptr_t m = menu_manager();
    if (!m || !g_build->menu_build_at || read_at<uintptr_t>(m + kMgrShown)) return false;
    // The build also reads the touch handler's point (MenuManager +0x878 -> +0x2c/+0x30, canvas
    // units, 0x518838) for widget entries: point it at (x, y) for the build, then restore it.
    uintptr_t handler = read_at<uintptr_t>(m + 0x878);
    int32_t saved[2] = {0, 0};
    if (handler) {
        saved[0] = read_at<int32_t>(handler + 0x2c);
        saved[1] = read_at<int32_t>(handler + 0x30);
        *reinterpret_cast<int32_t*>(handler + 0x2c) = x;
        *reinterpret_cast<int32_t*>(handler + 0x30) = y;
    }
    reinterpret_cast<void (*)(uintptr_t, int, int)>(g_base + g_build->menu_build_at)(m, x, y);
    if (handler) {
        *reinterpret_cast<int32_t*>(handler + 0x2c) = saved[0];
        *reinterpret_cast<int32_t*>(handler + 0x30) = saved[1];
    }
    // +0xa20 = 2 once built (1 = deferred to the next frame, which we don't use).
    if (read_at<uint8_t>(m + 0xa20) != 2) {
        menu_discard();
        return false;
    }
    return true;
}

void menu_discard() {
    uintptr_t m = menu_manager();
    if (!m || read_at<uintptr_t>(m + kMgrShown)) return;
    // As the touch handler after a tap's select: clear the model, build state back to 0, so the
    // show branch of 0x599f68 (nodes non-empty) never shows it.
    reinterpret_cast<void (*)(uintptr_t)>(g_base + g_build->menu_clear_model)(m + kMgrMenu);
    *reinterpret_cast<uint8_t*>(m + 0xa20) = 0;
}

bool menu_point_raw(int& x, int& y) {
    uintptr_t m = menu_manager();
    if (!m) return false;
    x = read_at<int32_t>(m + 0x2a8);
    y = read_at<int32_t>(m + 0x2ac);
    return true;
}

bool menu_point(int& x, int& y) {
    uintptr_t m = menu_manager();
    if (!m || !read_at<uintptr_t>(m + kMgrShown)) return false;
    // Long-press point in UI-canvas units (menu model +0x168); the menu is drawn centred on x
    // with its top at y. Found by diffing MenuManager with menus open at two points (2026-10-16).
    x = read_at<int32_t>(m + 0x2a8);
    y = read_at<int32_t>(m + 0x2ac);
    return true;
}

std::vector<MenuEntry> read_menu() { return read_menu(menu_object()); }

std::vector<MenuEntry> read_menu(uintptr_t menu) {
    std::vector<MenuEntry> out;
    if (!menu) return out;
    auto b = read_at<uintptr_t>(menu + kMenuNodes), e = read_at<uintptr_t>(menu + kMenuNodes + 8);
    if (!b || e < b || (e - b) / kNodeSize > 512) return out;
    for (uintptr_t n = b; n < e; n += kNodeSize) {
        uintptr_t entry = node_entry(n);
        if (!entry) continue;
        MenuEntry me;
        read_entry(entry, me);
        me.has_children = read_at<uintptr_t>(n + 0x10) != 0;
        out.push_back(std::move(me));
    }
    return out;
}

// Mirrors the walk in select (0x515854): path[0] indexes the top-level nodes, later elements
// index the children vector of the previous node.
bool menu_entry_for_path(uintptr_t menu, const uint64_t* path, size_t len, MenuEntry& out) {
    if (!menu || !len) return false;
    uintptr_t vec = menu + kMenuNodes, node = 0;
    for (size_t i = 0; i < len; i++) {
        if (!vec) return false;
        auto b = read_at<uintptr_t>(vec), e = read_at<uintptr_t>(vec + 8);
        if (path[i] >= (e - b) / kNodeSize) return false;
        node = b + path[i] * kNodeSize;
        vec = read_at<uintptr_t>(node + 0x10);
    }
    uintptr_t entry = node_entry(node);
    if (!entry) return false;
    read_entry(entry, out);
    out.has_children = vec != 0;
    return true;
}

namespace {
// Menu entry strings (+0x50 target, +0xb8 option) are the game's arena-backed string: the usual
// 24-byte layout (inline when byte 23 >= 0, else {char*, size, cap | 1 << 63}) followed by an arena
// whose address is at +0x20; the entry constructor (0x4bc564) fills them with append 0x48e860(str,
// begin, end), which reallocates past the capacity and frees the old buffer unless it is the arena.
void set_entry_string(uintptr_t str, const std::string& value) {
    auto* b = reinterpret_cast<uint8_t*>(str);
    if (static_cast<int8_t>(b[23]) < 0) {
        auto* words = reinterpret_cast<uint64_t*>(str);
        words[1] = 0;
        if (words[0]) *reinterpret_cast<char*>(words[0]) = 0;
    } else {
        b[0] = 0;
        b[23] = 23;
    }
    if (!value.empty())
        reinterpret_cast<void* (*)(uintptr_t, const char*, const char*)>(g_base + 0x48e860)(str, value.data(), value.data() + value.size());
}
}  // namespace

// index = position in read_menu() (top-level nodes, game order). Game thread only.
bool menu_set_text(size_t index, const std::string* option, const std::string* target) {
    uintptr_t menu = menu_object();
    if (!menu || !menu_open()) return false;
    auto b = read_at<uintptr_t>(menu + kMenuNodes), e = read_at<uintptr_t>(menu + kMenuNodes + 8);
    if (!b || e < b || index >= (e - b) / kNodeSize) return false;
    uintptr_t entry = node_entry(b + index * kNodeSize);
    if (!entry) return false;
    if (option) set_entry_string(entry + 0xb8, *option);
    if (target) set_entry_string(entry + 0x50, *target);
    return true;
}

uintptr_t menu_vtable() { return g_build ? g_base + g_build->menu_vtable : 0; }
uintptr_t menu_select() { return g_build ? g_base + g_build->menu_select : 0; }

namespace {
uintptr_t top_world_view() {
    uintptr_t c = client();
    return c && g_build ? read_at<uintptr_t>(c + g_build->f_world_view) : 0;
}
}  // namespace

// Scene (WorldView +0x10): +0x944 planes, +0x948/+0x94c size, +0x968 tile slots (0x10 each,
// Tile* at +8) indexed ((plane * sx) + x) * sy + y. Tile: +0x108 wall, +0x110 wall decoration,
// +0x118 ground decoration (object*, tag at +0x38), +0x34 game object count, +0x38 game object
// slots (0x10 each, object* at +8; tag +0x1f0, scene min x/max x/min y/max y +0x210..+0x21c).
std::vector<SceneObject> scene_objects() {
    std::vector<SceneObject> out;
    uintptr_t wv = top_world_view();
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    if (!scene) return out;
    int planes = read_at<int32_t>(scene + 0x944), sx = read_at<int32_t>(scene + 0x948), sy = read_at<int32_t>(scene + 0x94c);
    uintptr_t slots = read_at<uintptr_t>(scene + 0x968);
    if (planes < 1 || planes > 4 || sx < 1 || sx > 256 || sy < 1 || sy > 256 || !slots) return out;
    out.reserve(4096);
    for (int p = 0; p < planes; p++)
        for (int x = 0; x < sx; x++)
            for (int y = 0; y < sy; y++) {
                uintptr_t tile = read_at<uintptr_t>(slots + ((static_cast<size_t>(p) * sx + x) * sy + y) * 0x10 + 8);
                if (!tile) continue;
                static constexpr struct { uint32_t off; SceneLayer layer; } kSimple[] = {
                    {0x108, kWall}, {0x110, kWallDecoration}, {0x118, kGroundDecoration}};
                for (const auto& l : kSimple) {
                    uintptr_t o = read_at<uintptr_t>(tile + l.off);
                    if (!o) continue;
                    uint64_t tag = read_at<uint64_t>(o + 0x38);
                    out.push_back({l.layer, static_cast<uint8_t>(p), static_cast<uint8_t>(x), static_cast<uint8_t>(y), 1, 1,
                                   static_cast<int32_t>(tag >> 20 & 0xffff), tag});
                }
                int n = read_at<int32_t>(tile + 0x34);
                uintptr_t objs = read_at<uintptr_t>(tile + 0x38);
                if (n <= 0 || n > 16 || !objs) continue;
                for (int i = 0; i < n; i++) {
                    uintptr_t o = read_at<uintptr_t>(objs + static_cast<size_t>(i) * 0x10 + 8);
                    if (!o) continue;
                    int x0 = read_at<int32_t>(o + 0x210), x1 = read_at<int32_t>(o + 0x214);
                    int y0 = read_at<int32_t>(o + 0x218), y1 = read_at<int32_t>(o + 0x21c);
                    if (x0 != x || y0 != y) continue;  // listed on every tile it covers; report it once
                    uint64_t tag = read_at<uint64_t>(o + 0x1f0);
                    out.push_back({kGameObject, static_cast<uint8_t>(p), static_cast<uint8_t>(x), static_cast<uint8_t>(y),
                                   static_cast<uint8_t>(std::clamp(x1 - x0 + 1, 1, 255)),
                                   static_cast<uint8_t>(std::clamp(y1 - y0 + 1, 1, 255)),
                                   static_cast<int32_t>(tag >> 20 & 0xffff), tag});
                }
            }
    return out;
}

bool remove_ground_decoration(int plane, int x, int y) {
    uintptr_t wv = top_world_view();
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    if (!scene) return false;
    int planes = read_at<int32_t>(scene + 0x944), sx = read_at<int32_t>(scene + 0x948), sy = read_at<int32_t>(scene + 0x94c);
    uintptr_t slots = read_at<uintptr_t>(scene + 0x968);
    if (!slots || plane < 0 || plane >= planes || x < 0 || x >= sx || y < 0 || y >= sy) return false;
    uintptr_t tile = read_at<uintptr_t>(slots + ((static_cast<size_t>(plane) * sx + x) * sy + y) * 0x10 + 8);
    if (!tile || !read_at<uintptr_t>(tile + 0x118)) return false;
    reinterpret_cast<void (*)(uintptr_t, int, int, int)>(g_base + 0x6dca24)(scene, plane, x, y);
    return read_at<uintptr_t>(tile + 0x118) == 0;
}

// Scene terrain (docs/offsets.md "Scene terrain"): scene fields, tile paint/model, the loader's
// map arrays and the skybox.
namespace {
uintptr_t top_scene() {
    uintptr_t wv = top_world_view();
    return wv ? read_at<uintptr_t>(wv + 0x10) : 0;
}

uintptr_t scene_tile(uintptr_t scene, int plane, int x, int y) {
    int planes = read_at<int32_t>(scene + 0x944), sx = read_at<int32_t>(scene + 0x948), sy = read_at<int32_t>(scene + 0x94c);
    uintptr_t slots = read_at<uintptr_t>(scene + 0x968);
    if (!slots || plane < 0 || plane >= planes || x < 0 || x >= sx || y < 0 || y >= sy) return 0;
    return read_at<uintptr_t>(slots + ((static_cast<size_t>(plane) * sx + x) * sy + y) * 0x10 + 8);
}

// The loader frees the map arrays after the build, so they are copied whenever it builds a plane
// of the top-level scene (0x70d30c(scene, map data, ..., plane); map data 0x1d10e78).
constexpr uintptr_t kSceneLoad = 0x70d30c, kMapData = 0x1d10e78;
constexpr uint32_t kSceneLoadCode[4] = {0xd106c3ff, 0x6d1423e9, 0xa9157bfd, 0xa9166ffc};
using SceneLoadFn = void (*)(uintptr_t, uintptr_t, uint64_t, int, int, int);
SceneLoadFn g_scene_load = nullptr;
std::vector<int32_t> g_map[3];  // {planes, size x, size y, values}

template <typename T>
void copy_map(int which, std::vector<int32_t>& out) {
    uintptr_t d = g_base + kMapData + static_cast<uintptr_t>(which) * 0x20;
    auto data = read_at<const T*>(d + 0x10);
    int sx = read_at<uint8_t>(d + 0x18), sy = read_at<uint8_t>(d + 0x19);
    if (!data || sx < 1 || sy < 1) return;
    out = {4, sx, sy};
    out.insert(out.end(), data, data + 4 * static_cast<size_t>(sx) * sy);
}

void hook_scene_load(uintptr_t scene, uintptr_t map, uint64_t a, int b, int c, int plane) {
    if (map == g_base + kMapData && scene == top_scene()) {
        copy_map<uint16_t>(0, g_map[0]);
        copy_map<uint16_t>(1, g_map[1]);
        copy_map<uint8_t>(2, g_map[2]);
    }
    if (plane == 0) LOGI("scene load: map arrays %s (%zu values)", map == g_base + kMapData && scene == top_scene() ? "copied" : "skipped", g_map[0].size());
    g_scene_load(scene, map, a, b, c, plane);
}

void push_ptr(std::vector<int32_t>& out, uintptr_t p) {
    out.push_back(static_cast<int32_t>(p & 0xffffffff));
    out.push_back(static_cast<int32_t>(static_cast<uint64_t>(p) >> 32));
}
}  // namespace

bool scene_terrain(int what, int plane, int x, int y, std::vector<int32_t>& out) {
    out.clear();
    uintptr_t scene = top_scene();
    if (!scene) return false;
    switch (what) {
    case 0:
        out = {read_at<int32_t>(scene + 0x8e0), read_at<int32_t>(scene + 0x54d54), read_at<int32_t>(scene + 0x2f0)};
        return true;
    case 1: {
        uintptr_t t = scene_tile(scene, plane, x, y), p = t ? read_at<uintptr_t>(t + 0x128) : 0;
        if (!p) return false;
        push_ptr(out, p);
        for (int off = 0; off <= 0x14; off += 4) out.push_back(read_at<int32_t>(p + off));
        out.push_back(read_at<uint8_t>(p + 0x18));
        return true;
    }
    case 2: {
        uintptr_t t = scene_tile(scene, plane, x, y), m = t ? read_at<uintptr_t>(t + 0x130) : 0;
        if (!m) return false;
        auto vb = read_at<const int32_t*>(m + 0x8), ve = read_at<const int32_t*>(m + 0x10);
        auto cb = read_at<const int32_t*>(m + 0x38), ce = read_at<const int32_t*>(m + 0x40);
        auto fb = read_at<const int32_t*>(m + 0x50), fe = read_at<const int32_t*>(m + 0x58);
        if (!vb || !fb || !cb) return false;
        size_t nv = (ve - vb) / 3, nf = (fe - fb) / 3;
        if (nv > 64 || nf > 64 || static_cast<size_t>(ce - cb) != 3 * nf) return false;
        push_ptr(out, m);
        for (int off = 0x6c; off <= 0x7c; off += 4) out.push_back(read_at<int32_t>(m + off));  // shape..texture
        out.push_back(read_at<int32_t>(m + 0x68));
        out.push_back(static_cast<int32_t>(nv));
        out.push_back(static_cast<int32_t>(nf));
        out.insert(out.end(), vb, vb + 3 * nv);
        out.insert(out.end(), fb, fb + 3 * nf);
        out.insert(out.end(), cb, cb + 3 * nf);
        for (size_t i = 10 + 3 * nv, e = i + 3 * nf; i < e; i++)
            if (out[i] < 0 || static_cast<size_t>(out[i]) >= nv) { out.clear(); return false; }
        return true;
    }
    case 3: case 4: case 5:
        out = g_map[what - 3];
        return !out.empty();
    case 6: {
        uintptr_t m = read_at<uintptr_t>(scene + 0x54dc8);
        // Only a static Model (vptr 0xa778f0) has the layout pack_mesh reads.
        return m && read_at<uintptr_t>(m) == g_base + 0xa778f0 && pack_mesh(m, kModel, out);
    }
    default: return false;
    }
}

// Mouse wheel: the cycle function zeroes Client +0x413248 (wheel rotation, +0x41324c a scroll layer's own)
// at 0x41d218 each logged-in cycle and nothing on Android sets it, though the interface pass 0x44ae24 reads
// it for onScrollWheel and scroll layers (and 0x58d438 for the minimap zoom). The call right after the
// zeroing (0x73f8f4, return address 0x41d220) is hooked to put the notches the host took from the app's
// ACTION_SCROLL events there, as desktop's MouseWheel.useRotation at the start of the cycle.
namespace {
constexpr uintptr_t kAfterWheelReset = 0x73f8f4, kAfterWheelResetRet = 0x41d220;
constexpr uint32_t kAfterWheelResetCode[4] = {0xa9bf7bfd, 0x910003fd, 0x9100a001, 0x2a1f03e0};
using AfterWheelResetFn = int (*)(uintptr_t);
AfterWheelResetFn g_after_wheel_reset = nullptr;
std::atomic<int> g_wheel{0}, g_wheel_x{-1}, g_wheel_y{-1};

int hook_after_wheel_reset(uintptr_t x0) {
    if (reinterpret_cast<uintptr_t>(__builtin_return_address(0)) == g_base + kAfterWheelResetRet) {
        int r = g_wheel.exchange(0);
        if (r) {
            uintptr_t c = x0 - 0x1c80;  // x0 = Client + 0x1c80
            *reinterpret_cast<volatile int32_t*>(c + 0x413248) = r;
            // a wheel turns where the mouse is: a touch released left the game's mouse off the widget, so the
            // notch's own point is the mouse for this cycle's interface pass, as desktop's mouse over the widget
            int x = g_wheel_x.load(), y = g_wheel_y.load();
            LOGI("game: wheel %d at %d,%d (mouse was %d,%d)", r, x, y, read_at<int32_t>(c + 0x40e9c0), read_at<int32_t>(c + 0x40e9d0));
            if (x >= 0) {
                *reinterpret_cast<volatile int32_t*>(c + 0x40e9c0) = x;
                *reinterpret_cast<volatile int32_t*>(c + 0x40e9d0) = y;
            }
        }
    }
    return g_after_wheel_reset(x0);
}
}  // namespace

void add_wheel(int rotation, int x, int y) {
    g_wheel_x.store(x);
    g_wheel_y.store(y);
    g_wheel.fetch_add(rotation);
}

void install_wheel_hook() {
    g_after_wheel_reset = reinterpret_cast<AfterWheelResetFn>(
        inline_hook(g_base + kAfterWheelReset, kAfterWheelResetCode, reinterpret_cast<void*>(hook_after_wheel_reset)));
    LOGI("game: wheel hook %s", g_after_wheel_reset ? "installed" : "FAILED");
}

void install_scene_load_hook() {
    g_scene_load = reinterpret_cast<SceneLoadFn>(inline_hook(g_base + kSceneLoad, kSceneLoadCode, reinterpret_cast<void*>(hook_scene_load)));
    LOGI("game: scene load hook %s", g_scene_load ? "installed" : "FAILED");
}

bool scene_set(int what, int plane, int x, int y, uint64_t ptr, int field, int value) {
    uintptr_t scene = top_scene();
    if (!scene) return false;
    if (what == 0 && (field == 0 || field == 1)) {
        *reinterpret_cast<volatile int32_t*>(scene + (field == 0 ? 0x8e0 : 0x54d54)) = value;
        return true;
    }
    if (what != 1 || field < 0 || field > 5) return false;
    uintptr_t t = scene_tile(scene, plane, x, y), p = t ? read_at<uintptr_t>(t + 0x128) : 0;
    if (!p || p != ptr) return false;
    *reinterpret_cast<volatile int32_t*>(p + 4 * field) = value;
    return true;
}

// Projectiles: Client +0xcb30 is a std::list (libc++: node {next, prev, ctrl, Projectile*}; header
// {first, last, size}) filled by PROJANIM_SPECIFIC (dispatcher case 76) and the zone MAP_PROJANIM
// (0x46edb0 case 6); emptied on logout/rebuild (0x4684d0). Projectile ctor 0x477b14 copies the
// packet's 0x3c-byte struct to obj +0x25c; the rest was read live (gen 37, docs/offsets.md "Projectiles").
std::vector<ProjectileInfo> projectiles() {
    std::vector<ProjectileInfo> out;
    uintptr_t c = client();
    if (!c) return out;
    uintptr_t head = c + 0xcb30;
    uint64_t n = read_at<uint64_t>(head + 0x10);
    if (n > 4096) return out;
    uintptr_t node = read_at<uintptr_t>(head);
    for (uint64_t i = 0; node && node != head && i < n; i++, node = read_at<uintptr_t>(node)) {
        uintptr_t o = read_at<uintptr_t>(node + 0x18);
        if (!o) continue;
        ProjectileInfo p;
        p.obj = o;
        p.id = read_at<int32_t>(o + 0x294);
        p.source_level = read_at<int32_t>(o + 0x25c);
        p.source_x = read_at<int32_t>(o + 0x260);
        p.source_y = read_at<int32_t>(o + 0x264);
        p.start_height = read_at<int32_t>(o + 0x268);
        p.source_index = read_at<int32_t>(o + 0x26c);
        p.target_level = read_at<int32_t>(o + 0x270);
        p.target_x = read_at<int32_t>(o + 0x274);
        p.target_y = read_at<int32_t>(o + 0x278);
        p.end_height = read_at<int32_t>(o + 0x27c);
        p.target_index = read_at<int32_t>(o + 0x280);
        p.start_cycle = read_at<int32_t>(o + 0x284);
        p.end_cycle = read_at<int32_t>(o + 0x288);
        p.slope = read_at<int32_t>(o + 0x28c);
        p.start_pos = read_at<int32_t>(o + 0x290);
        p.x = read_at<double>(o + 0x298);
        p.z = read_at<double>(o + 0x2a0);
        p.y = read_at<double>(o + 0x2a8);
        p.orientation = read_at<int32_t>(o + 0x2b0);
        p.animation = read_at<int32_t>(o + 0x2d8);
        p.animation_frame = read_at<uint16_t>(o + 0x2dc);
        out.push_back(p);
    }
    return out;
}

// Graphics objects (map spotanims): WorldView +0x160 std::list, same node layout as projectiles, filled
// by MAP_ANIM_SPECIFIC (dispatcher case 111) / zone MAP_ANIM. Ctor 0x3fee04(obj, worldView, id, level,
// x, y, height, delay, cycle): +0x25c start cycle (cycle + delay), +0x260 level, +0x264/+0x268 local
// x/y (tile centre, 128 units/tile), +0x26c height, +0x270 spotanim id, +0x278 WorldView, +0x280
// animation (gen 37 static RE; the rest of the object is dumped raw for discovery).
std::vector<GraphicsObjectInfo> graphics_objects() {
    std::vector<GraphicsObjectInfo> out;
    uintptr_t wv = top_world_view();
    if (!wv) return out;
    uintptr_t head = wv + 0x160;
    uint64_t n = read_at<uint64_t>(head + 0x10);
    if (n > 4096) return out;
    uintptr_t node = read_at<uintptr_t>(head);
    for (uint64_t i = 0; node && node != head && i < n; i++, node = read_at<uintptr_t>(node)) {
        uintptr_t o = read_at<uintptr_t>(node + 0x18);
        if (!o) continue;
        GraphicsObjectInfo g;
        g.obj = o;
        g.start_cycle = read_at<int32_t>(o + 0x25c);
        g.level = read_at<int32_t>(o + 0x260);
        g.x = read_at<int32_t>(o + 0x264);
        g.y = read_at<int32_t>(o + 0x268);
        g.z = read_at<int32_t>(o + 0x26c);
        g.id = read_at<int32_t>(o + 0x270);
        memcpy(g.raw, reinterpret_cast<const void*>(o + 0x240), sizeof g.raw);
        out.push_back(g);
    }
    return out;
}

bool graphics_object_finish(uintptr_t obj) {
    if (!g_build || !g_build->seq_set) return false;
    for (const auto& g : graphics_objects())
        if (g.obj == obj) {
            if (!read_at<uintptr_t>(obj + 0x288)) return true;  // already finished
            reinterpret_cast<void (*)(uintptr_t, int)>(g_base + g_build->seq_set)(obj + 0x280, -1);
            return true;
        }
    return false;
}

bool projectile_set_end_cycle(uintptr_t obj, int cycle) {
    for (const auto& p : projectiles())
        if (p.obj == obj) {
            *reinterpret_cast<int32_t*>(obj + 0x288) = cycle;
            return true;
        }
    return false;
}

// PROJANIM_SPECIFIC's tail (0x4467cc): make_shared 0x46e490(&sp, &struct) (sp = {ctrl, obj}, ctor
// 0x477b14), list push_back 0x46e5d4(Client + 0xcb30, &sp) (copies sp, ref + 1), release 0x46e64c(ctrl).
uintptr_t projectile_create(const int32_t (&s)[15]) {
    uintptr_t c = client();
    if (!c || game_state() != 30) return 0;
    alignas(16) int32_t st[16] = {};
    memcpy(st, s, sizeof s);
    alignas(16) uintptr_t sp[2] = {0, 0};
    reinterpret_cast<void (*)(uintptr_t*, const int32_t*)>(g_base + 0x46e490)(sp, st);
    if (!sp[0]) return 0;
    reinterpret_cast<void (*)(uintptr_t, uintptr_t*)>(g_base + 0x46e5d4)(c + 0xcb30, sp);
    uintptr_t obj = sp[1];
    reinterpret_cast<void (*)(uintptr_t)>(g_base + 0x46e64c)(sp[0]);
    return obj;
}

namespace {
constexpr uint32_t kAmbientLive = 0xb78, kAmbientPending = 0xb90;
}  // namespace

std::vector<AmbientSound> ambient_sounds() {
    std::vector<AmbientSound> out;
    uintptr_t wv = top_world_view();
    if (!wv) return out;
    uintptr_t b = read_at<uintptr_t>(wv + kAmbientLive), e = read_at<uintptr_t>(wv + kAmbientLive + 8);
    if (!b || e < b || (e - b) / 16 > 4096) return out;
    for (uintptr_t p = b; p < e; p += 16) {
        uintptr_t obj = read_at<uintptr_t>(p + 8);
        if (!obj) continue;
        AmbientSound a{obj, -1, read_at<int32_t>(obj + 0x40), {}};
        if (uintptr_t snd = read_at<uintptr_t>(obj + 0x20)) a.id = read_at<int32_t>(snd);
        if (uintptr_t rnd = read_at<uintptr_t>(obj + 0x38)) {
            uint64_t n = read_at<uint64_t>(rnd + 8);
            uintptr_t ids = read_at<uintptr_t>(rnd + 0x10);
            if (ids && n > 0 && n < 256)
                for (uint64_t i = 0; i < n; i++) a.background.push_back(read_at<int32_t>(ids + i * 4));
        }
        out.push_back(std::move(a));
    }
    return out;
}

bool ambient_clear() {
    uintptr_t wv = top_world_view();
    if (!wv) return false;
    reinterpret_cast<void (*)(uintptr_t)>(g_base + 0x4b065c)(wv);
    return true;
}

bool ambient_remove(uintptr_t obj) {
    uintptr_t wv = top_world_view();
    if (!wv || !obj) return false;
    auto* live = reinterpret_cast<uintptr_t*>(wv + kAmbientLive);
    auto* pending = reinterpret_cast<uintptr_t*>(wv + kAmbientPending);
    for (uintptr_t p = live[0]; p && p < live[1]; p += 16) {
        if (read_at<uintptr_t>(p + 8) != obj) continue;
        uintptr_t ctrl = read_at<uintptr_t>(p);
        // Copy into pending (+1 owner), as 0x4b065c: inline when there is room, else its push_back 0x4bb59c.
        if (pending[0] && pending[1] + 16 <= pending[2]) {
            std::memcpy(reinterpret_cast<void*>(pending[1]), reinterpret_cast<void*>(p), 16);
            pending[1] += 16;
            if (ctrl) __atomic_fetch_add(reinterpret_cast<int32_t*>(ctrl + 8), 1, __ATOMIC_ACQ_REL);
        } else {
            reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(g_base + 0x4bb59c)(wv + kAmbientPending, p);
        }
        // Erase from live (-1 owner; pending still holds one, so it never reaches zero here).
        std::memmove(reinterpret_cast<void*>(p), reinterpret_cast<void*>(p + 16), live[1] - p - 16);
        live[1] -= 16;
        std::memset(reinterpret_cast<void*>(live[1]), 0, 16);
        if (ctrl) __atomic_fetch_sub(reinterpret_cast<int32_t*>(ctrl + 8), 1, __ATOMIC_ACQ_REL);
        return true;
    }
    return false;
}

bool ambient_restore(uintptr_t obj) {
    uintptr_t wv = top_world_view();
    if (!wv || !obj) return false;
    auto* live = reinterpret_cast<uintptr_t*>(wv + kAmbientLive);        // begin, end, cap
    auto* pending = reinterpret_cast<uintptr_t*>(wv + kAmbientPending);
    if (!live[0] || live[1] + 16 > live[2]) return false;  // no spare capacity (a cleared vector keeps its own)
    for (uintptr_t p = pending[0]; p && p < pending[1]; p += 16) {
        if (read_at<uintptr_t>(p + 8) != obj) continue;
        // Raw move of the {ctrl, obj} pair: the reference goes with it, no count changes.
        std::memcpy(reinterpret_cast<void*>(live[1]), reinterpret_cast<void*>(p), 16);
        live[1] += 16;
        std::memmove(reinterpret_cast<void*>(p), reinterpret_cast<void*>(p + 16), pending[1] - p - 16);
        pending[1] -= 16;
        std::memset(reinterpret_cast<void*>(pending[1]), 0, 16);
        return true;
    }
    return false;
}

bool collision_flags(int plane, int size, std::vector<int32_t>& out) {
    uintptr_t wv = top_world_view();
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    if (!scene || plane < 0 || plane > 3 || size < 1 || size > 256) return false;
    int planes = read_at<int32_t>(scene + 0x944);
    uintptr_t maps = read_at<uintptr_t>(scene + 0x2f8);
    if (!maps || plane >= planes) return false;
    uintptr_t cm = maps + static_cast<size_t>(plane) * 0x28;
    int xoff = read_at<int32_t>(cm), yoff = read_at<int32_t>(cm + 4);
    int w = read_at<int32_t>(cm + 8), h = read_at<int32_t>(cm + 0xc);
    uintptr_t cols = read_at<uintptr_t>(cm + 0x20);
    if (!cols || w < 1 || w > 512 || h < 1 || h > 512 || xoff < -64 || xoff > 64 || yoff < -64 || yoff > 64) return false;
    static int logged = 0;
    if (logged++ < 4) LOGI("collision plane %d: offset %d,%d size %dx%d", plane, xoff, yoff, w, h);
    out.assign(static_cast<size_t>(size) * size, 0);
    for (int x = 0; x < size; x++) {
        int cx = x - xoff;
        if (cx < 0 || cx >= w) continue;
        uintptr_t data = read_at<uintptr_t>(cols + static_cast<size_t>(cx) * 0x18 + 0x10);
        if (!data) continue;
        for (int y = 0; y < size; y++) {
            int cy = y - yoff;
            if (cy >= 0 && cy < h) out[static_cast<size_t>(x) * size + y] = read_at<int32_t>(data + static_cast<size_t>(cy) * 4);
        }
    }
    return true;
}

// Ground items: WorldView +0x140 → planes (0x18 each, +0x10 → x columns (0x18 each, +0x10 → y
// list heads (0x18 each: libc++ std::list {prev, next, size}))). Nodes {prev, next, Obj*, ctrl};
// Obj +0x40 id, +0x44 quantity, +0x48/+0x4c visible/despawn tick, +0x50 ownership, +0x54 private
// (OBJ_ADD 0x45775c).
std::vector<GroundItem> ground_items() {
    std::vector<GroundItem> out;
    uintptr_t wv = top_world_view();
    uintptr_t scene = wv ? read_at<uintptr_t>(wv + 0x10) : 0;
    uintptr_t planes_arr = wv ? read_at<uintptr_t>(wv + 0x140) : 0;
    if (!scene || !planes_arr) return out;
    int planes = read_at<int32_t>(scene + 0x944), sx = read_at<int32_t>(scene + 0x948), sy = read_at<int32_t>(scene + 0x94c);
    if (planes < 1 || planes > 4 || sx < 1 || sx > 256 || sy < 1 || sy > 256) return out;
    for (int p = 0; p < planes; p++) {
        uintptr_t cols = read_at<uintptr_t>(planes_arr + static_cast<size_t>(p) * 0x18 + 0x10);
        if (!cols) continue;
        for (int x = 0; x < sx; x++) {
            uintptr_t heads = read_at<uintptr_t>(cols + static_cast<size_t>(x) * 0x18 + 0x10);
            if (!heads) continue;
            for (int y = 0; y < sy; y++) {
                uintptr_t head = heads + static_cast<size_t>(y) * 0x18;
                uint64_t n = read_at<uint64_t>(head + 0x10);
                if (!n || n > 256) continue;
                uintptr_t node = read_at<uintptr_t>(head + 8);
                for (uint64_t i = 0; i < n && node && node != head; i++) {
                    uintptr_t obj = read_at<uintptr_t>(node + 0x10);
                    if (obj)
                        out.push_back({static_cast<uint8_t>(p), static_cast<uint8_t>(x), static_cast<uint8_t>(y),
                                       read_at<int32_t>(obj + 0x40), read_at<int32_t>(obj + 0x44),
                                       read_at<int32_t>(obj + 0x48), read_at<int32_t>(obj + 0x4c),
                                       read_at<int32_t>(obj + 0x50), read_at<uint8_t>(obj + 0x54) != 0});
                    node = read_at<uintptr_t>(node + 8);
                }
            }
        }
    }
    return out;
}

}  // namespace game

// ---- Mod icons (<img=N> in text). The font reads the table at the global obj pointer on every
// call (game thread); we point it at our own header holding shallow copies of the game's sprites
// plus plugin sprites, and never touch the game's ctrl or array (docs/offsets.md "Mod icons").
namespace game {
namespace {
struct IdxSprite {
    uint64_t unused, pixel_count;
    const uint8_t* pixels;
    uint64_t palette_count;
    uintptr_t palette_ctrl;
    const uint32_t* palette;
    int32_t w, h, ox, oy, full_w, full_h;
};
static_assert(sizeof(IdxSprite) == 0x48, "IndexedSprite layout");
struct IconHeader {
    uint64_t unused;
    uint64_t count;  // read as int32
    IdxSprite* data;
};
using Entries = std::vector<ModIcon>;
std::mutex g_icon_mutex;
std::shared_ptr<const Entries> g_icon_want;  // guarded by g_icon_mutex
struct BuiltIcons {
    IconHeader header{};
    std::vector<IdxSprite> sprites;
    std::shared_ptr<const Entries> data;
    uintptr_t game_obj = 0;
};
std::unique_ptr<BuiltIcons> g_icons_built, g_icons_prev;  // game thread only

uintptr_t game_icon_obj() {
    if (!g_build || !g_build->mod_icons) return 0;
    uintptr_t cur = read_at<uintptr_t>(g_base + g_build->mod_icons);
    if (g_icons_built && cur == reinterpret_cast<uintptr_t>(&g_icons_built->header)) return g_icons_built->game_obj;
    return cur;
}
}  // namespace

void set_mod_icons(std::vector<ModIcon> icons) {
    auto e = std::make_shared<const Entries>(std::move(icons));
    std::lock_guard<std::mutex> lock(g_icon_mutex);
    g_icon_want = std::move(e);
}

std::vector<int32_t> mod_icon_sizes() {
    std::vector<int32_t> out;
    uintptr_t obj = game_icon_obj();
    if (!obj) return out;
    int n = read_at<int32_t>(obj + 8);
    auto* data = read_at<const IdxSprite*>(obj + 0x10);
    if (n <= 0 || n > 4096 || !data) return out;
    for (int i = 0; i < n; i++) {
        out.push_back(data[i].full_w);
        out.push_back(data[i].full_h);
    }
    return out;
}

void mod_icons_cycle() {
    std::shared_ptr<const Entries> want;
    {
        std::lock_guard<std::mutex> lock(g_icon_mutex);
        want = g_icon_want;
    }
    if (!want || !g_build || !g_build->mod_icons) return;
    auto* slot = reinterpret_cast<uintptr_t*>(g_base + g_build->mod_icons);
    uintptr_t cur = *slot;
    bool ours = g_icons_built && cur == reinterpret_cast<uintptr_t>(&g_icons_built->header);
    if (ours && g_icons_built->data == want) return;
    uintptr_t game_obj = ours ? g_icons_built->game_obj : cur;
    if (!game_obj) return;  // the table isn't loaded yet
    int gcount = read_at<int32_t>(game_obj + 8);
    auto* gdata = read_at<const IdxSprite*>(game_obj + 0x10);
    if (gcount < 0 || gcount > 4096 || (gcount && !gdata)) return;
    auto b = std::make_unique<BuiltIcons>();
    b->data = want;
    b->game_obj = game_obj;
    b->sprites.resize(want->size());
    for (size_t i = 0; i < want->size(); i++) {
        const ModIcon& m = (*want)[i];
        IdxSprite& d = b->sprites[i];
        d = {};
        if (m.game_index >= 0) {
            if (m.game_index < gcount) d = gdata[m.game_index];
            continue;
        }
        if (m.w <= 0 || m.h <= 0 || m.pixels.size() < static_cast<size_t>(m.w) * m.h || m.palette.empty()) continue;
        d.pixel_count = static_cast<uint64_t>(m.w) * m.h;
        d.pixels = m.pixels.data();
        d.palette_count = m.palette.size();
        d.palette = m.palette.data();
        d.w = m.w;
        d.h = m.h;
        d.ox = m.ox;
        d.oy = m.oy;
        d.full_w = m.full_w;
        d.full_h = m.full_h;
    }
    b->header.count = b->sprites.size();
    b->header.data = b->sprites.data();
    *slot = reinterpret_cast<uintptr_t>(&b->header);
    LOGI("mod icons: %zu (game %d)%s", b->sprites.size(), gcount, ours ? "" : " installed");
    g_icons_prev = std::move(g_icons_built);  // the font may still hold it within this cycle? keep one
    g_icons_built = std::move(b);
}
}  // namespace game

// ---- Inventories as of each chat message. Desktop posts events in packet order, so a ChatMessage
// handler sees the containers before a later UPDATE_INV in the same cycle (Loot Tracker snapshots
// the reward inventory on the casket message, then diffs on its ItemContainerChanged). Our events
// are derived after the cycle, so the Message constructor is hooked to copy the inventories at the
// moment each line is created; client_thread replays the changes in that order.
namespace game {
namespace {
const uint32_t kMessageCtorCode[4] = {0xd102c3ff, 0xa9067bfd, 0xf9003bf9, 0xa9085ff8};
using MessageCtor = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
MessageCtor g_message_ctor = nullptr;
std::mutex g_msg_inv_mutex;
std::unordered_map<int, std::vector<Inventory>> g_msg_inv;  // message id -> inventories at creation

uintptr_t hook_message_ctor(uintptr_t self, uintptr_t type, uintptr_t name, uintptr_t sender, uintptr_t text) {
    // The constructor takes id = counter, then increments it (0x3f0a04: iVar7 = counter; counter++),
    // so the new line's id is the counter's current value. (Was +1: each line replayed the
    // inventories of the previous line's creation; Consumable Cooldowns got a pre-spawn inventory.)
    int id = next_message_id();
    std::vector<Inventory> inv = read_inventories();
    {
        std::lock_guard<std::mutex> lock(g_msg_inv_mutex);
        if (g_msg_inv.size() > 256) g_msg_inv.clear();  // never collected (no cycle hook)
        g_msg_inv[id] = std::move(inv);
    }
    return g_message_ctor(self, type, name, sender, text);
}
}  // namespace

void install_message_hook() {
    if (!g_build || !g_build->message_ctor) return;
    g_message_ctor = reinterpret_cast<MessageCtor>(
        inline_hook(g_base + g_build->message_ctor, kMessageCtorCode, reinterpret_cast<void*>(hook_message_ctor)));
    LOGI("game: message hook %s", g_message_ctor ? "installed" : "FAILED");
}

namespace {
constexpr uintptr_t kMenuOpenCheck = 0x58da0c, kDragCheckRet = 0x41ffe8;
constexpr uint32_t kMenuOpenCheckCode[4] = {0xf943e008, 0xf100011f, 0x1a9f07e0, 0xd65f03c0};
void (*g_drag_cb)() = nullptr;
}
}  // namespace game

// The menu-open check is a leaf the compiler may call assuming only x0/x8 change, so the stub
// (script_stubs.S) saves every caller-saved register before calling this.
extern "C" {
__attribute__((visibility("hidden"))) void* osrsrl_drag_check_orig = nullptr;
void osrsrl_drag_check_stub();
void osrsrl_drag_check_cb(uintptr_t lr) {
    if (game::g_drag_cb && lr == game::g_base + game::kDragCheckRet) game::g_drag_cb();
}
}

namespace game {
void install_drag_hook(void (*cb)()) {
    g_drag_cb = cb;
    osrsrl_drag_check_orig = inline_hook(g_base + kMenuOpenCheck, kMenuOpenCheckCode, reinterpret_cast<void*>(osrsrl_drag_check_stub));
    LOGI("game: drag hook %s", osrsrl_drag_check_orig ? "installed" : "FAILED");
}

namespace {
// 0x4ae508 starts `tst w2, #0xff; b.eq` (PC-relative), so the hook goes at 0x4ae510, where no stack
// or register has changed yet: returning from the replacement returns to 0x4ae508's caller.
constexpr uintptr_t kSoundQueueBody = 0x4ae510;
constexpr uint32_t kSoundQueueBodyCode[4] = {0xd101c3ff, 0xa9037bfd, 0xf90023f7, 0xa90557f6};
using SoundQueueFn = void (*)(uintptr_t, uint32_t, uint32_t, uint32_t);
SoundQueueFn g_sound_orig = nullptr;
bool (*g_sound_cb)(int, int, int) = nullptr;
void hook_sound_queue(uintptr_t sys, uint32_t id, uint32_t loops, uint32_t delay) {
    if (g_sound_cb && g_sound_cb(static_cast<int>(id & 0xffff), static_cast<int>(loops & 0xff), static_cast<int>(delay)))
        return;
    g_sound_orig(sys, id, loops, delay);
}
}  // namespace

void install_sound_hook(bool (*cb)(int id, int loops, int delay)) {
    g_sound_cb = cb;
    g_sound_orig = reinterpret_cast<SoundQueueFn>(
        inline_hook(g_base + kSoundQueueBody, kSoundQueueBodyCode, reinterpret_cast<void*>(hook_sound_queue)));
    LOGI("game: sound hook %s", g_sound_orig ? "installed" : "FAILED");
}

namespace {
// Area sounds: 0x4ae650(sys, plane, id, x, z, range, dropOff, loops, delay, flag) appends a
// 0x28-byte entry to the same queue as 0x4ae508 (x/z are scene tiles, packed as bytes). Callers:
// the SOUND_AREA zone prot (0x46f9c0, in the zone handler 0x46edb0) and the animation frame-sound
// handler (0x480168 in 0x47ff8c). Its prologue has no PC-relative code, so the hook sits on entry.
constexpr uintptr_t kAreaSound = 0x4ae650;
constexpr uint32_t kAreaSoundCode[4] = {0xd10243ff, 0xa9037bfd, 0xa9046ffc, 0xa90567fa};
// Frame-sound handler: a stack object {vtable 0xa6c5a8, +0x08 plane*, +0x10/+0x14 fine x/z of the
// animated thing, +0x18 "is the local player", +0x20 sound system} built by the actor update
// (0x435240: x/z = actor +0x268/+0x26c), loc animations (0x49af40: the loc's x/z) and others, and
// passed to the sequence advance 0x659534, which calls slot 2 = 0x47ff8c(handler, SeqType*, frame).
constexpr uintptr_t kFrameSoundSlot = 0xa6c5b8;
using AreaSoundFn = void (*)(uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using FrameSoundFn = void (*)(uintptr_t, uintptr_t, int);
AreaSoundFn g_area_orig = nullptr;
FrameSoundFn g_frame_orig = nullptr;
uintptr_t g_frame_handler = 0;  // the handler while its slot 2 runs (game thread)
bool (*g_area_cb)(const AreaSound&) = nullptr;

void hook_frame_sound(uintptr_t self, uintptr_t seq, int frame) {
    uintptr_t prev = g_frame_handler;
    g_frame_handler = self;
    g_frame_orig(self, seq, frame);
    g_frame_handler = prev;
}

// The NPC or player standing exactly at the fine position (actor +0x268/+0x26c, as the actor
// update copies into the handler); kind 1 = NPC, 2 = player, 0 = none.
void actor_at(int fx, int fz, bool local, AreaSound& out) {
    uintptr_t c = client();
    if (!c) return;
    auto wv = read_at<uintptr_t>(c + g_build->f_world_view);
    if (!wv) return;
    int me = read_at<int32_t>(c + g_build->f_local_player_index);
    if (local) {
        out.source_kind = 2;
        out.source_index = me;
        return;
    }
    auto at = [&](uintptr_t a) { return a && read_at<int32_t>(a + 0x268) == fx && read_at<int32_t>(a + 0x26c) == fz; };
    int active_count = read_at<int32_t>(c + g_build->f_gpi);
    for (int i = 0; active_count > 0 && active_count <= 2048 && i < active_count; i++) {
        int index = read_at<int32_t>(c + g_build->f_gpi + 4 + i * 4);
        if (index != me && at(live_player(index))) {
            out.source_kind = 2;
            out.source_index = index;
            return;
        }
    }
    int count = read_at<int32_t>(wv + g_build->wv_npc_count);
    auto indices = read_at<const int32_t*>(wv + g_build->wv_npc_indices);
    for (int i = 0; indices && count > 0 && count <= 65535 && i < count; i++) {
        if (at(npc_lookup(wv, indices[i]))) {
            out.source_kind = 1;
            out.source_index = indices[i];
            return;
        }
    }
}

void hook_area_sound(uintptr_t sys, uint32_t plane, uint32_t id, uint32_t x, uint32_t z, uint32_t range,
                     uint32_t drop_off, uint32_t loops, uint32_t delay, uint32_t flag) {
    if (g_area_cb) {
        AreaSound s;
        s.id = static_cast<int>(id & 0xffff);
        s.scene_x = static_cast<int>(x & 0xff);
        s.scene_y = static_cast<int>(z & 0xff);
        s.range = static_cast<int>(range & 0xff);
        s.drop_off = static_cast<int>(drop_off & 0xff);
        s.loops = static_cast<int>(loops & 0xff);
        s.delay = static_cast<int>(delay);
        s.plane = static_cast<int>(plane);
        if (g_frame_handler) {
            s.from_animation = true;
            actor_at(read_at<int32_t>(g_frame_handler + 0x10), read_at<int32_t>(g_frame_handler + 0x14),
                     read_at<uint8_t>(g_frame_handler + 0x18) != 0, s);
        }
        if (g_area_cb(s)) return;
    }
    g_area_orig(sys, plane, id, x, z, range, drop_off, loops, delay, flag);
}
}  // namespace

void install_area_sound_hook(bool (*cb)(const AreaSound&)) {
    g_area_cb = cb;
    g_area_orig = reinterpret_cast<AreaSoundFn>(inline_hook(g_base + kAreaSound, kAreaSoundCode, reinterpret_cast<void*>(hook_area_sound)));
    auto** slot = reinterpret_cast<void**>(g_base + kFrameSoundSlot);
    if (g_area_orig && *slot == reinterpret_cast<void*>(g_base + 0x47ff8c)) {
        long page = sysconf(_SC_PAGESIZE);
        auto start = reinterpret_cast<void*>((g_base + kFrameSoundSlot) & ~static_cast<uintptr_t>(page - 1));
        if (mprotect(start, page, PROT_READ | PROT_WRITE) == 0) {
            g_frame_orig = reinterpret_cast<FrameSoundFn>(*slot);
            *slot = reinterpret_cast<void*>(hook_frame_sound);
            mprotect(start, page, PROT_READ);  // .data.rel.ro
        }
    }
    LOGI("game: area sound hook %s, frame-sound slot %s", g_area_orig ? "installed" : "FAILED", g_frame_orig ? "patched" : "not patched");
}

bool take_message_inventories(int id, std::vector<Inventory>& out) {
    std::lock_guard<std::mutex> lock(g_msg_inv_mutex);
    auto it = g_msg_inv.find(id);
    if (it == g_msg_inv.end()) return false;
    out = std::move(it->second);
    g_msg_inv.erase(it);
    return true;
}

// ---- Game-state writes (docs/offsets.md "Game-state writes") ----

namespace {
// RuneLite's allWidgetsAreOpTargetable: the menu builder 0x525c78 takes a widget as a target only when
// the flags from 0x460dac(client, Widget_sp*) have bit 21 (0x200000); that call returns to 0x525ce0.
constexpr uintptr_t kWidgetFlags = 0x460dac, kTargetFlagsRet = 0x525ce0;
constexpr uint32_t kWidgetFlagsCode[4] = {0xf9400428, 0x91504c09, 0xb94c392a, 0xf9461929};
// The widget position layout 0x45ea08(clamp, Widget*, parent width, parent height) writes +0x5c/+0x60.
constexpr uintptr_t kWidgetAlign = 0x45ea08;
constexpr uint32_t kWidgetAlignCode[4] = {0xb9403c28, 0x7100051f, 0x5400026d, 0x7100091f};
volatile bool g_all_op_targetable = false;
bool g_flags_query = false;  // game thread: widget_target_flags asks 0x460dac as the menu builder does
struct Forced { uint32_t comp; int32_t index; int x, y; };
std::unordered_map<uintptr_t, Forced> g_forced;  // by Widget*; game thread only
}  // namespace
}  // namespace game

extern "C" {
__attribute__((visibility("hidden"))) void* osrsrl_widget_flags_orig = nullptr;
__attribute__((visibility("hidden"))) void* osrsrl_widget_align_orig = nullptr;
void osrsrl_widget_flags_stub();
void osrsrl_widget_align_stub();
void osrsrl_widget_flags_cb(const uint64_t*, uint64_t* regs, uintptr_t lr) {
    if (game::g_all_op_targetable && (lr == game::g_base + game::kTargetFlagsRet || game::g_flags_query)) regs[0] |= 0x200000;
}
void osrsrl_widget_align_cb(const uint64_t* args, uint64_t*, uintptr_t) {
    if (game::g_forced.empty()) return;
    auto it = game::g_forced.find(args[1]);
    if (it == game::g_forced.end()) return;
    uintptr_t w = args[1];
    const game::Forced& f = it->second;
    // The Widget* may have been freed and reused by another widget: only the one it was set on.
    if (game::read_at<uint32_t>(w + 4) != f.comp || game::read_at<int32_t>(w + 8) != f.index) return;
    if (f.x != -1) *reinterpret_cast<volatile int32_t*>(w + 0x5c) = f.x;
    if (f.y != -1) *reinterpret_cast<volatile int32_t*>(w + 0x60) = f.y;
}
}

namespace game {
void install_state_write_hooks() {
    osrsrl_widget_flags_orig = inline_hook(g_base + kWidgetFlags, kWidgetFlagsCode, reinterpret_cast<void*>(osrsrl_widget_flags_stub));
    osrsrl_widget_align_orig = inline_hook(g_base + kWidgetAlign, kWidgetAlignCode, reinterpret_cast<void*>(osrsrl_widget_align_stub));
    LOGI("game: widget flags hook %s, widget align hook %s", osrsrl_widget_flags_orig ? "installed" : "FAILED",
         osrsrl_widget_align_orig ? "installed" : "FAILED");
}

bool set_all_op_targetable(bool on) {
    g_all_op_targetable = on;
    return osrsrl_widget_flags_orig != nullptr;
}

// The flags 0x460dac gives the menu builder for a widget (its IF_SETEVENTS range, else +0x2b0), with
// setAllWidgetsAreOpTargetable applied as it is there; -1 for no widget. Game thread.
int64_t widget_target_flags(int component, int index) {
    uintptr_t c = client(), slot = widget_slot(component, index);
    if (!c || !slot || !osrsrl_widget_flags_orig) return -1;
    g_flags_query = true;
    uint32_t v = reinterpret_cast<uint32_t (*)(uintptr_t, uintptr_t)>(g_base + kWidgetFlags)(c, slot);
    g_flags_query = false;
    return v;
}

// RuneLite's Widget.setForcedPosition: the layout keeps x/y (-1 = its own) for this widget; both -1
// clear it. The next layout pass (revalidate) applies it. Game thread.
bool widget_force_position(int component, int index, int x, int y) {
    uintptr_t slot = widget_slot(component, index), w = slot ? read_at<uintptr_t>(slot + 8) : 0;
    if (!w || !osrsrl_widget_align_orig) return false;
    if (x == -1 && y == -1) g_forced.erase(w);
    else g_forced[w] = {read_at<uint32_t>(w + 4), read_at<int32_t>(w + 8), x, y};
    return true;
}

// RuneLite's PlayerComposition.setHash: the composition's equipment, colours and transformed NPC id
// into the player's Appearance (Player +0x728), then its model hash 0x5e1bd4(Appearance*), which stores
// +0x78 and drops the old hash's cached model (the next draw builds the new one). Returns the hash, 0
// with no appearance. Game thread.
uint64_t appearance_set_hash(int index, const int32_t equipment[12], const int32_t colors[5], int npc_id) {
    uintptr_t p = live_player(index), app = p ? read_at<uintptr_t>(p + 0x728) : 0;
    if (!app) return 0;
    for (int i = 0; i < 12; i++) *reinterpret_cast<volatile int32_t*>(app + 4 * i) = equipment[i];
    for (int i = 0; i < 5; i++) *reinterpret_cast<volatile int32_t*>(app + 0x60 + 4 * i) = colors[i];
    *reinterpret_cast<volatile int32_t*>(app + 0x74) = npc_id;
    reinterpret_cast<void (*)(uintptr_t)>(g_base + 0x5e1bd4)(app);
    return read_at<uint64_t>(app + 0x78);
}
uint64_t appearance_hash(int index) {
    uintptr_t p = live_player(index), app = p ? read_at<uintptr_t>(p + 0x728) : 0;
    return app ? read_at<uint64_t>(app + 0x78) : 0;
}

// RuneLite's Scene.removeGameObject: the scenery object whose min corner is (plane, x, y) with `tag`
// (+0x1f0) leaves every tile it covers through the game's own removal 0x6dc300(scene, obj, 0), the
// scene-only half of LOC_DEL (collision stays). The object may be freed by it. Game thread.
bool scene_remove_game_object(int plane, int x, int y, uint64_t tag) {
    uintptr_t scene = top_scene(), t = scene ? scene_tile(scene, plane, x, y) : 0;
    if (!t) return false;
    int n = read_at<int32_t>(t + 0x34);
    uintptr_t objs = read_at<uintptr_t>(t + 0x38);
    if (n <= 0 || n > 16 || !objs) return false;
    for (int i = 0; i < n; i++) {
        uintptr_t o = read_at<uintptr_t>(objs + static_cast<size_t>(i) * 0x10 + 8);
        if (!o || read_at<uint64_t>(o + 0x1f0) != tag || read_at<int32_t>(o + 0x210) != x || read_at<int32_t>(o + 0x218) != y) continue;
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, uint64_t)>(g_base + 0x6dc300)(scene, o, 0);
        return true;
    }
    return false;
}

// RuneLite's Scene.removeTile: the tile slot (Scene +0x968, shared_ptr {ctrl, Tile*}) emptied and its
// reference released, as the scene's tile creation 0x6db200 replaces one; the scene walkers take a
// null tile (tiles are created lazily). The scene's reload builds it again. With remove false, only
// whether the tile is there. Game thread.
bool scene_remove_tile(int plane, int x, int y, bool remove) {
    uintptr_t scene = top_scene();
    if (!scene || !scene_tile(scene, plane, x, y)) return false;
    if (!remove) return true;
    int sx = read_at<int32_t>(scene + 0x948), sy = read_at<int32_t>(scene + 0x94c);
    auto* slot = reinterpret_cast<uintptr_t*>(read_at<uintptr_t>(scene + 0x968) + ((static_cast<size_t>(plane) * sx + x) * sy + y) * 0x10);
    uintptr_t ctrl = slot[0];
    slot[1] = 0;
    slot[0] = 0;
    release_shared_ptr(ctrl);
    return true;
}
// The handler reads the slot's widget unchecked (a missing one is an empty slot), so it must exist.
bool widget_op(int component, int sub, int op) {
    uintptr_t c = client();
    if (!g_build || !c || op < 1 || op > 10 || !widget_slot(component, sub)) return false;
    reinterpret_cast<void (*)(uintptr_t, uint32_t, int, int, int, int, uint8_t)>(g_base + 0x528764)(c, component, sub, op, 0, -1, 0);
    return true;
}
}  // namespace game
