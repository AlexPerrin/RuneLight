#include "client_thread.h"

#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <algorithm>
#include <mutex>

#include "game.h"
#include "log.h"

namespace client_thread {
namespace {

using DoCycleFn = void (*)(void*);
DoCycleFn g_orig = nullptr;
std::atomic<pid_t> g_tid{0};
std::atomic<pid_t> g_delegate_tid{0};
std::atomic<uint64_t> g_cycles{0};

std::mutex g_listeners_mutex;
std::vector<Listener> g_listeners;

std::mutex g_tasks_mutex;
std::vector<std::function<void()>> g_tasks;

std::mutex g_log_mutex;
std::deque<std::string> g_log;
constexpr size_t kLogSize = 6;

// Game-thread-only state for deriving events.
int g_last_state = -1;
int g_last_tick = -1;
std::vector<int32_t> g_varp_shadow;

std::vector<int> g_open_groups;  // root + open sub-interface groups, sorted

int g_next_message = -1;
std::mutex g_messages_mutex;
std::deque<game::ChatLine> g_messages;  // newest last, guarded by g_messages_mutex
constexpr size_t kMessageHistory = 256;

std::mutex g_inv_mutex;
std::vector<game::Inventory> g_inventories;  // guarded by g_inv_mutex

std::mutex g_players_mutex;
std::vector<game::PlayerInfo> g_players;  // guarded by g_players_mutex

std::mutex g_npcs_mutex;
std::vector<game::Npc> g_npcs;  // guarded by g_npcs_mutex; written only on the game thread

std::mutex g_world_mutex;
game::WorldEntry g_world;  // guarded by g_world_mutex; id 0 = none
bool g_world_ok = false;   // guarded by g_world_mutex
std::vector<game::WorldEntry> g_worlds;  // the whole list, guarded by g_world_mutex
std::atomic<bool> g_shake_off{false};     // Client.setCameraShakeDisabled
std::atomic<float> g_cam_speed{1.0f};     // Client.setCameraSpeed
float g_sens_base = 0, g_sens_set = 0;    // game thread only: the game's own sensitivity and the one last written
bool g_sens_seen = false;
// The drag sensitivity as the game's own value times setCameraSpeed's multiplier; a value the game wrote
// since (its settings) becomes the new base.
void apply_camera_speed() {
    float cur;
    if (!game::camera_sensitivity(cur)) return;
    if (!g_sens_seen || cur != g_sens_set) { g_sens_base = cur; g_sens_seen = true; }
    g_sens_set = g_sens_base * g_cam_speed.load(std::memory_order_relaxed);
    if (cur != g_sens_set) game::set_camera_sensitivity(g_sens_set);
}
int g_world_id = -1;       // game thread only: the id and list the cache was read for
std::pair<uintptr_t, uint64_t> g_world_list{0, 0};
uint64_t g_world_read_at = 0;

const char* name(EventType t) {
    switch (t) {
        case EventType::ClientTick: return "ClientTick";
        case EventType::GameTick: return "GameTick";
        case EventType::GameStateChanged: return "GameStateChanged";
        case EventType::VarbitChanged: return "VarbitChanged";
        case EventType::NpcSpawned: return "NpcSpawned";
        case EventType::NpcDespawned: return "NpcDespawned";
        case EventType::ChatMessage: return "ChatMessage";
        case EventType::WidgetLoaded: return "WidgetLoaded";
        case EventType::WidgetClosed: return "WidgetClosed";
        case EventType::PlayerSpawned: return "PlayerSpawned";
        case EventType::PlayerDespawned: return "PlayerDespawned";
        case EventType::ItemContainerChanged: return "ItemContainerChanged";
        case EventType::StatChanged: return "StatChanged";
        case EventType::MenuOpened: return "MenuOpened";
        case EventType::MenuOptionClicked: return "MenuOptionClicked";
        case EventType::MenuTap: return "MenuTap";
        case EventType::WidgetDrag: return "WidgetDrag";
        case EventType::SoundEffect: return "SoundEffect";
        case EventType::AreaSoundEffect: return "AreaSoundEffect";
    }
    return "?";
}

void post(const Event& e) {
    if (e.type != EventType::ClientTick && e.type != EventType::WidgetDrag && e.type != EventType::SoundEffect
        && e.type != EventType::AreaSoundEffect) {
        char line[96];
        snprintf(line, sizeof line, "%llu %s %d %d", (unsigned long long)g_cycles.load(), name(e.type), e.a, e.b);
        std::lock_guard lock(g_log_mutex);
        g_log.emplace_back(line);
        if (g_log.size() > kLogSize) g_log.pop_front();
    }
    std::vector<Listener> listeners;
    {
        std::lock_guard lock(g_listeners_mutex);
        listeners = g_listeners;
    }
    for (auto& l : listeners) l(e);
}

void diff_varps() {
    int n = game::varp_count();
    // OpenRune's cache defines far more varps than the stock one; a 1 << 16 bound silently skipped
    // the whole diff there (no VarbitChanged at all).
    static int logged = -1;
    if (n != logged) { LOGI("varps: %d", n); logged = n; }
    if (n <= 0 || n > 1 << 22) return;
    std::vector<int32_t> now(n);
    for (int i = 0; i < n; i++) now[i] = game::varp(i);
    std::vector<int> posted;
    if (g_varp_shadow.size() == now.size()) {
        if (memcmp(now.data(), g_varp_shadow.data(), n * sizeof(int32_t)) != 0)
            for (int i = 0; i < n; i++)
                if (now[i] != g_varp_shadow[i]) {
                    post({EventType::VarbitChanged, i, now[i]});
                    posted.push_back(i);
                }
    }
    // Desktop posts VarbitChanged for every varp the server sends, changed or not (plugins such as
    // Attack Styles read their state on the login burst of VARP_SMALLs, whose values may be 0).
    // The client's ring of server-changed varp ids (32 entries) gives those; a burst of more than
    // 32 in one cycle loses the oldest.
    static uint32_t last_count = 0;
    static bool have_count = false;
    uint32_t count = game::changed_varp_count();
    if (have_count && count != last_count) {
        uint32_t from = count - last_count > 32 ? count - 32 : last_count;
        if (count - last_count > 32) {
            // The ring overflowed (the login burst sends hundreds): the lost ids are unknown, so
            // resync as if the server had sent every varp of the stock range (stock caches define
            // under 5000) plus every non-zero one, so plugins that read their state on login (Attack
            // Styles, varp 43 = 0) still get their event.
            int resync = 0;
            for (int i = 0; i < n; i++) {
                if (i >= 8192 && now[i] == 0) continue;
                if (std::find(posted.begin(), posted.end(), i) != posted.end()) continue;
                post({EventType::VarbitChanged, i, now[i]});
                resync++;
            }
            LOGI("varps: changed-varp ring overflowed (%u ids), resynced %d varps", count - last_count, resync);
            last_count = count;
            g_varp_shadow.swap(now);
            return;
        }
        for (uint32_t k = from; k != count; k++) {
            int id = game::changed_varp(k);
            if (id < 0 || id >= n) continue;
            if (std::find(posted.begin(), posted.end(), id) != posted.end()) continue;
            post({EventType::VarbitChanged, id, now[id]});
            posted.push_back(id);
        }
    }
    last_count = count;
    have_count = true;
    g_varp_shadow.swap(now);
}

void diff_npcs() {
    std::vector<game::Npc> now = game::read_npcs();
    // A just-added Npc has no position until its first movement update (fine 0,0, which is never a
    // real position: actors stand on tile centres). RuneLite posts NpcSpawned after placement, and
    // QH's NpcStep rejects an NPC outside its roam range at spawn time, so hold it back until then.
    std::erase_if(now, [](const game::Npc& n) { return n.fine_x == 0 && n.fine_y == 0; });
    std::vector<game::Npc> old;
    {
        std::lock_guard lock(g_npcs_mutex);
        old.swap(g_npcs);
        g_npcs = now;
    }
    auto same = [](const game::Npc& a, const game::Npc& b) { return a.index == b.index && a.address == b.address; };
    for (const auto& o : old) {
        bool kept = false;
        for (const auto& n : now) kept |= same(o, n);
        if (!kept) post({EventType::NpcDespawned, o.index, o.id});
    }
    for (const auto& n : now) {
        bool existed = false;
        for (const auto& o : old) existed |= same(o, n);
        if (!existed) post({EventType::NpcSpawned, n.index, n.id});
    }
}

// What getItemContainer sees while events are posted (game thread): set to the inventories as of
// the event being posted, so handlers see packet-order state (see game::take_message_inventories).
const std::vector<game::Inventory>* g_inv_view = nullptr;

void post_inventory_changes(const std::vector<game::Inventory>& state);

// Chat messages in id order. With the world up, the inventory changes made before each message
// (recorded by the Message constructor hook) are posted first, and the handlers see the
// inventories as of that message; diff_inventories then posts the rest.
void poll_messages(bool world_up) {
    int next = game::next_message_id();
    if (g_next_message < 0 || next < g_next_message) {  // first cycle: don't replay history
        g_next_message = next;
        return;
    }
    for (; g_next_message < next; g_next_message++) {
        std::vector<game::Inventory> at;
        bool snap = game::take_message_inventories(g_next_message, at) && world_up;
        game::ChatLine line;
        if (!game::read_message(g_next_message, line)) continue;
        {
            std::lock_guard lock(g_messages_mutex);
            g_messages.push_back(line);
            if (g_messages.size() > kMessageHistory) g_messages.pop_front();
        }
        if (snap) post_inventory_changes(at);
        const std::vector<game::Inventory>* saved = g_inv_view;
        if (snap) g_inv_view = &at;
        post({EventType::ChatMessage, line.id, line.type});
        g_inv_view = saved;
    }
}

void diff_interfaces() {
    std::vector<int> now;
    if (game::game_state() == 30) {
        int root = game::root_interface();
        if (root >= 0) now.push_back(root);
        for (const auto& sub : game::open_sub_interfaces()) now.push_back(sub.group);
        std::sort(now.begin(), now.end());
        now.erase(std::unique(now.begin(), now.end()), now.end());
    }
    for (int g : g_open_groups)
        if (!std::binary_search(now.begin(), now.end(), g)) post({EventType::WidgetClosed, g});
    for (int g : now)
        if (!std::binary_search(g_open_groups.begin(), g_open_groups.end(), g)) post({EventType::WidgetLoaded, g});
    g_open_groups.swap(now);
}

game::Skills g_skills;
bool g_skills_valid = false;

void diff_skills() {
    if (game::game_state() != 30) {
        g_skills_valid = false;
        return;
    }
    game::Skills now = game::skills();
    if (g_skills_valid) {
        for (int i = 0; i < game::kSkillCount; i++) {
            if (now.current[i] != g_skills.current[i] || now.real[i] != g_skills.real[i] || now.xp[i] != g_skills.xp[i])
                post({EventType::StatChanged, i});
        }
    }
    g_skills = now;
    g_skills_valid = true;
}

// Posts ItemContainerChanged for every inventory in `state` that differs from the posted state
// (g_inventories), updating it first, with getItemContainer showing `state`.
void post_inventory_changes(const std::vector<game::Inventory>& state) {
    const std::vector<game::Inventory>* saved = g_inv_view;
    g_inv_view = &state;
    for (const auto& n : state) {
        bool changed;
        {
            std::lock_guard lock(g_inv_mutex);
            game::Inventory* before = nullptr;
            for (auto& o : g_inventories) if (o.id == n.id) before = &o;
            changed = !before || before->ids != n.ids || before->quantities != n.quantities;
            if (before) *before = n; else if (changed) g_inventories.push_back(n);
        }
        if (changed) post({EventType::ItemContainerChanged, n.id});
    }
    g_inv_view = saved;
}

void diff_inventories() {
    post_inventory_changes(game::read_inventories());
}

void diff_players() {
    std::vector<game::PlayerInfo> now = game::read_players();
    std::erase_if(now, [](const game::PlayerInfo& p) { return p.fine_x == 0 && p.fine_y == 0; });  // see diff_npcs
    std::vector<game::PlayerInfo> old;
    {
        std::lock_guard lock(g_players_mutex);
        old.swap(g_players);
        g_players = now;
    }
    auto same = [](const game::PlayerInfo& a, const game::PlayerInfo& b) { return a.index == b.index && a.address == b.address; };
    for (const auto& o : old) {
        bool kept = false;
        for (const auto& n : now) kept |= same(o, n);
        if (!kept) post({EventType::PlayerDespawned, o.index});
    }
    for (const auto& n : now) {
        bool existed = false;
        for (const auto& o : old) existed |= same(o, n);
        if (!existed) post({EventType::PlayerSpawned, n.index});
    }
}

bool g_menu_was_open = false;

void diff_menu() {
    bool open = game::menu_open();
    if (open && !g_menu_was_open) {
        auto entries = game::read_menu();
        post({EventType::MenuOpened, static_cast<int>(entries.size())});
    }
    g_menu_was_open = open;
}

// Menu model vtable slot 1: select(menu, const std::vector<uint64_t>* path, x, y, flag).
using MenuSelectFn = uint64_t (*)(void*, const uint64_t* const*, int, int, bool);
MenuSelectFn g_orig_select = nullptr;
game::MenuEntry g_clicked;
bool g_click_consumed = false;
game::AreaSound g_area_sound;  // the AreaSoundEffect being posted
bool g_sound_consumed = false;  // set by consume_sound() while SoundEffect is posted
uintptr_t g_tap_menu = 0;
int g_tap_redirect = -1;
bool g_direct_select = false;  // select_menu_node: no tap handling (MenuTap/redirect)

uint64_t hook_menu_select(void* menu, const uint64_t* const* path, int x, int y, bool flag) {
    game::MenuEntry entry;
    // A plain tap runs the default node without opening the menu. Plugins may reorder the entries
    // (RuneLite's PostMenuSort, Menu Entry Swapper); run the node they put on top instead.
    uint64_t tap_index[1];
    const uint64_t* tap_path[2];
    if (!g_direct_select && path && path[0] && path[1] == path[0] + 1 && !game::menu_open() && on_client_thread()) {
        g_tap_menu = reinterpret_cast<uintptr_t>(menu);
        g_tap_redirect = -1;
        post({EventType::MenuTap, static_cast<int>(path[0][0])});
        g_tap_menu = 0;
        game::MenuEntry swapped;
        if (g_tap_redirect >= 0 && static_cast<uint64_t>(g_tap_redirect) != path[0][0]) {
            tap_index[0] = static_cast<uint64_t>(g_tap_redirect);
            if (game::menu_entry_for_path(reinterpret_cast<uintptr_t>(menu), tap_index, 1, swapped)) {
                LOGI("menu: tap redirected from node %llu to %d '%s'", (unsigned long long)path[0][0], g_tap_redirect,
                     swapped.option.c_str());
                tap_path[0] = tap_index;
                tap_path[1] = tap_index + 1;
                path = tap_path;
            }
        }
        g_tap_redirect = -1;
    }
    if (path && path[0] && path[1] > path[0] &&
        game::menu_entry_for_path(reinterpret_cast<uintptr_t>(menu), path[0], path[1] - path[0], entry)) {
        g_clicked = entry;
        g_click_consumed = false;
        LOGI("menu: select '%s' '%s' kind %d path[0] %llu len %zu x %d y %d flag %d open %d on %s thread",
             entry.option.c_str(), entry.target.c_str(), entry.kind, (unsigned long long)path[0][0],
             (size_t)(path[1] - path[0]), x, y, flag, game::menu_open(), on_client_thread() ? "client" : "other");
        post({EventType::MenuOptionClicked, static_cast<int>(path[0][0])});
        if (g_click_consumed) {
            g_click_consumed = false;
            return 1;
        }
    }
    return g_orig_select(menu, path, x, y, flag);
}

bool patch_slot(uintptr_t slot_addr, void* value, void** old) {
    auto** slot = reinterpret_cast<void**>(slot_addr);
    long page = sysconf(_SC_PAGESIZE);
    auto start = reinterpret_cast<void*>(slot_addr & ~(page - 1));
    if (mprotect(start, page, PROT_READ | PROT_WRITE) != 0) return false;
    if (old) *old = *slot;
    *slot = value;
    mprotect(start, page, PROT_READ);
    return true;
}

void install_menu_hook() {
    uintptr_t vt = game::menu_vtable();
    if (!vt) return;
    uintptr_t slot = vt + sizeof(void*);
    if (*reinterpret_cast<uintptr_t*>(slot) != game::menu_select()) {
        LOGE("menu: vtable slot 1 is %p, expected %p; not hooking", *reinterpret_cast<void**>(slot),
             reinterpret_cast<void*>(game::menu_select()));
        return;
    }
    void* old = nullptr;
    if (patch_slot(slot, reinterpret_cast<void*>(hook_menu_select), &old)) {
        g_orig_select = reinterpret_cast<MenuSelectFn>(old);
        LOGI("menu: select hooked");
    }
}

// Login screens: post despawns for what was there, without reading game memory.
void clear_actors() {
    std::vector<game::Npc> npcs;
    std::vector<game::PlayerInfo> players;
    {
        std::lock_guard lock(g_npcs_mutex);
        npcs.swap(g_npcs);
    }
    {
        std::lock_guard lock(g_players_mutex);
        players.swap(g_players);
    }
    for (const auto& o : npcs) post({EventType::NpcDespawned, o.index, o.id});
    for (const auto& o : players) post({EventType::PlayerDespawned, o.index});
}

// World Hopper and WorldService ask for the current world's entry from their own threads; the
// game frees and rebuilds the list on this thread, so it is copied here.
void cache_world() {
    int id = game::world();
    auto list = game::world_list_key();
    uint64_t now = g_cycles.load(std::memory_order_relaxed);
    if (id == g_world_id && list == g_world_list && now - g_world_read_at < 25) return;
    if (list != g_world_list)
        LOGI("world list: %llu entries at %p", static_cast<unsigned long long>(list.second),
             reinterpret_cast<void*>(list.first));
    game::WorldEntry w;
    bool ok = game::world_entry(id, w);
    std::vector<game::WorldEntry> all;
    game::world_list(all);
    g_world_id = id;
    g_world_list = list;
    g_world_read_at = now;
    std::lock_guard lock(g_world_mutex);
    g_world = std::move(w);
    g_world_ok = ok;
    g_worlds.swap(all);
}

void after_cycle() {
    g_cycles++;

    int state = game::game_state();
    if (state != g_last_state) {
        post({EventType::GameStateChanged, state, g_last_state});
        g_last_state = state;
    }
    diff_varps();
    // Actor/inventory/menu structures are read only while the world is up (LOADING, LOGGED_IN).
    // During CONNECTION_LOST/HOPPING (40/45) the game may be tearing them down (a session drop
    // after a desynced packet crashed here), so nothing is read and the last lists stand; on the
    // login screens they are empty, as on desktop.
    bool world_up = state == 25 || state == 30;
    if (world_up || state < 25) {
        if (world_up) {
            diff_npcs();
            diff_players();
        } else {
            clear_actors();
        }
    }
    diff_skills();
    game::sync_members_world();
    cache_world();
    // Shakes the server sends while disabled are dropped before the frame applies them (desktop skips them).
    if (g_shake_off.load(std::memory_order_relaxed)) game::clear_camera_shake();
    apply_camera_speed();
    poll_messages(world_up);
    if (world_up) diff_inventories();  // after the messages: changes made after the last one
    diff_interfaces();
    if (world_up) diff_menu();
    game::drawlog_cycle();
    game::mod_icons_cycle();
    // As on desktop, the events of a tick's packets (varps, hitsplats, spawns) come before that
    // tick's GameTick, then ClientTick. Special Attack Counter matches a hitsplat to the tick
    // count and timed out when GameTick came first.
    int tick = game::server_tick();
    if (tick != g_last_tick) {
        // The counter is reset on login; only an increase is a game tick.
        if (g_last_tick >= 0 && tick > g_last_tick) post({EventType::GameTick, tick});
        g_last_tick = tick;
    }
    post({EventType::ClientTick});

    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard lock(g_tasks_mutex);
        tasks.swap(g_tasks);
    }
    for (auto& t : tasks) t();
}

void hook_do_cycle(void* self) {
    g_tid.store(gettid(), std::memory_order_relaxed);
    g_orig(self);
    after_cycle();
}

}  // namespace

bool install() {
    uintptr_t vtable = game::client_vtable();
    if (!vtable) return false;
    auto** slot = reinterpret_cast<void**>(vtable + 5 * sizeof(void*));
    if (reinterpret_cast<uintptr_t>(*slot) != game::do_cycle()) {
        LOGE("client_thread: vtable slot 5 is %p, expected %p; not hooking", *slot,
             reinterpret_cast<void*>(game::do_cycle()));
        return false;
    }
    long page = sysconf(_SC_PAGESIZE);
    auto start = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(slot) & ~(page - 1));
    if (mprotect(start, page, PROT_READ | PROT_WRITE) != 0) {
        LOGE("client_thread: mprotect failed");
        return false;
    }
    g_orig = reinterpret_cast<DoCycleFn>(*slot);
    *slot = reinterpret_cast<void*>(hook_do_cycle);
    mprotect(start, page, PROT_READ);  // .data.rel.ro is read-only after relocation
    LOGI("client_thread: doCycle hooked");
    install_menu_hook();
    game::install_drag_hook([] {
        if (on_client_thread()) post({EventType::WidgetDrag, game::touch_state()});
    });
    game::install_sound_hook([](int id, int, int delay) {
        if (!on_client_thread()) return false;
        g_sound_consumed = false;
        post({EventType::SoundEffect, id, delay});
        bool consumed = g_sound_consumed;
        g_sound_consumed = false;
        return consumed;
    });
    game::install_area_sound_hook([](const game::AreaSound& s) {
        if (!on_client_thread()) return false;
        g_sound_consumed = false;
        if (s.from_animation && s.range == 0) {
            // The local player's own animation sound (the game plays range-0 frame sounds only for
            // it): desktop queues these as plain sound effects (SoundEffectPlayed).
            post({EventType::SoundEffect, s.id, s.delay});
        } else {
            g_area_sound = s;
            post({EventType::AreaSoundEffect, s.id, s.delay});
        }
        bool consumed = g_sound_consumed;
        g_sound_consumed = false;
        return consumed;
    });
    return true;
}

bool installed() { return g_orig != nullptr; }
bool on_client_thread() {
    pid_t t = gettid();
    return g_tid.load(std::memory_order_relaxed) == t || g_delegate_tid.load(std::memory_order_acquire) == t;
}
void set_delegate(bool on) { g_delegate_tid.store(on ? gettid() : 0, std::memory_order_release); }

void subscribe(Listener listener) {
    std::lock_guard lock(g_listeners_mutex);
    g_listeners.push_back(std::move(listener));
}

void invoke(std::function<void()> task) {
    if (on_client_thread()) {
        task();
        return;
    }
    std::lock_guard lock(g_tasks_mutex);
    g_tasks.push_back(std::move(task));
}

std::vector<std::string> recent_events() {
    std::lock_guard lock(g_log_mutex);
    return {g_log.begin(), g_log.end()};
}

uint64_t cycles() { return g_cycles.load(); }

bool message(int id, game::ChatLine& out) {
    std::lock_guard lock(g_messages_mutex);
    for (auto it = g_messages.rbegin(); it != g_messages.rend(); ++it) {
        if (it->id == id) {
            out = *it;
            return true;
        }
    }
    return false;
}

int inventory_view(int id, game::Inventory& out) {
    if (!g_inv_view || !on_client_thread()) return -1;
    for (const auto& inv : *g_inv_view)
        if (inv.id == id) {
            out = inv;
            return 1;
        }
    return 0;  // didn't exist yet at that point (trail_rewardinv before the casket is opened)
}

std::vector<game::Inventory> inventories() {
    std::lock_guard lock(g_inv_mutex);
    return g_inventories;
}

void set_camera_shake_disabled(bool off) { g_shake_off = off; }
bool camera_shake_disabled() { return g_shake_off; }
void set_camera_speed(float m) {
    g_cam_speed = m;
    if (on_client_thread()) apply_camera_speed();
}

std::vector<game::WorldEntry> world_list() {
    std::vector<game::WorldEntry> out;
    if (on_client_thread()) {
        game::world_list(out);
        return out;
    }
    std::lock_guard lock(g_world_mutex);
    return g_worlds;
}

bool world_entry(int id, game::WorldEntry& out) {
    if (on_client_thread()) return game::world_entry(id, out);
    std::lock_guard lock(g_world_mutex);
    if (!g_world_ok || g_world.id != id) return false;
    out = g_world;
    return true;
}

std::vector<game::PlayerInfo> players() {
    std::lock_guard lock(g_players_mutex);
    return g_players;
}

const game::MenuEntry& clicked_menu_entry() { return g_clicked; }
void consume_menu_click() { g_click_consumed = true; }
void consume_sound() { g_sound_consumed = true; }
const game::AreaSound& last_area_sound() { return g_area_sound; }
uintptr_t tap_menu() { return g_tap_menu; }
void redirect_menu_tap(int index) { g_tap_redirect = index; }

bool select_menu_node(int index) {
    uintptr_t menu = game::menu_object();
    if (!menu || index < 0 || !on_client_thread()) return false;
    auto entries = game::read_menu(menu);
    if (static_cast<size_t>(index) >= entries.size()) return false;
    int x = 0, y = 0;
    game::menu_point_raw(x, y);
    uint64_t node[1] = {static_cast<uint64_t>(index)};
    const uint64_t* vec[3] = {node, node + 1, node + 1};  // std::vector<uint64_t> {begin, end, cap}
    using Select = uint64_t (*)(void*, const uint64_t* const*, int, int, bool);
    auto select = *reinterpret_cast<Select*>(*reinterpret_cast<uintptr_t*>(menu) + sizeof(void*));
    g_direct_select = true;
    select(reinterpret_cast<void*>(menu), vec, x, y, game::menu_open());
    g_direct_select = false;
    return true;
}

std::vector<game::Npc> npcs() {
    std::lock_guard lock(g_npcs_mutex);
    return g_npcs;
}

}  // namespace client_thread
