#pragma once
#include <functional>
#include <string>
#include <vector>

#include "game.h"

// The game's own thread, entered once per client cycle (20 ms) through a hook on
// Client::doCycle. Equivalent of RuneLite's ClientThread plus the events it posts.
namespace client_thread {

enum class EventType {
    ClientTick,        // every client cycle
    GameTick,          // a SERVER_TICK_END arrived (once per 600 ms game tick)
    GameStateChanged,  // a = new state (RuneLite GameState value), b = old state
    VarbitChanged,     // a = varp id, b = new value (any varp write, server or script)
    NpcSpawned,        // a = NPC index, b = NPC id
    NpcDespawned,      // a = NPC index, b = NPC id
    ChatMessage,       // a = message id (see message()), b = type
    WidgetLoaded,      // a = interface group id
    WidgetClosed,      // a = interface group id
    PlayerSpawned,     // a = player index
    PlayerDespawned,   // a = player index
    ItemContainerChanged,  // a = inventory id
    StatChanged,       // a = skill index
    MenuOpened,        // the long-press menu was shown (entries: game::read_menu())
    MenuOptionClicked, // an entry was selected (clicked_menu_entry()); runs before the game acts
    MenuTap,           // a plain tap is about to run node a (the default entry); entries: tap_menu();
                       // a listener may call redirect_menu_tap() (Menu Entry Swapper's left-click swaps)
    WidgetDrag,        // a widget is held for a drag, before the cycle's drag processing (game::drag_state);
                       // a = touch state (4 = this cycle releases it)
    SoundEffect,       // a sound effect is about to be queued: a = sound id, b = delay; listeners may
                       // call consume_sound() (game::install_sound_hook)
    AreaSoundEffect,   // an area sound is about to be queued (details: last_area_sound()); a = sound id,
                       // b = delay; listeners may call consume_sound() (game::install_area_sound_hook)
};

struct Event {
    EventType type;
    int a = 0, b = 0;
};

using Listener = std::function<void(const Event&)>;

// Patches Client vtable slot 5. False if the game build is unknown or the slot looks wrong.
bool install();
bool installed();
// True on the game thread, and on the delegate thread while it holds the game's turn (the host's
// Swing "EDT" runs plugin UI code while the game thread waits for it at a safe point in the cycle).
bool on_client_thread();
// Delegate: on = the calling thread counts as the client thread until set_delegate(false).
// Only the host's EDT calls it, while the game thread is parked waiting for it.
void set_delegate(bool on);

// Listeners run on the game thread, after the game's own cycle.
void subscribe(Listener listener);

// Runs `task` on the game thread: immediately if already there, else after the next cycle.
void invoke(std::function<void()> task);

// For the overlay: the most recent events (newest last), ClientTick excluded.
std::vector<std::string> recent_events();
uint64_t cycles();

// A chat message captured when it was added (the last 256 are kept); safe from any thread.
bool message(int id, game::ChatLine& out);

// Inventories as of the end of the last cycle; safe from any thread.
std::vector<game::Inventory> inventories();
// While an event is posted on the game thread: 1 = the inventory as of that event (packet order),
// 0 = it didn't exist then, -1 = no event view (read the live map).
int inventory_view(int id, game::Inventory& out);

// The current world's world list entry (game::world_entry) as of a recent cycle: re-read when the
// world id or the list changes, else every 25 cycles. Safe from any thread; false if `id` isn't
// the cached world.
bool world_entry(int id, game::WorldEntry& out);
// The whole world list, as world_entry: live on the game thread, else the copy made with it.
std::vector<game::WorldEntry> world_list();
// Client.setCameraShakeDisabled: while on, the shake flags are cleared after every cycle.
void set_camera_shake_disabled(bool off);
bool camera_shake_disabled();
// Client.setInvertPitch (axis 0) / setInvertYaw (axis 1): the touch camera drag turns that axis the other way.
void set_camera_invert(int axis, bool on);

// Players as of the end of the last cycle; safe from any thread.
std::vector<game::PlayerInfo> players();

// NPCs as of the end of the last cycle; safe from any thread.
std::vector<game::Npc> npcs();

// The entry being selected while MenuOptionClicked is posted; consume_menu_click() from a
// listener stops the game from running it.
const game::MenuEntry& clicked_menu_entry();
void consume_menu_click();
// During SoundEffect: drop the sound.
void consume_sound();
// The area sound being posted (AreaSoundEffect).
const game::AreaSound& last_area_sound();
// During MenuTap: the menu model being tapped (0 otherwise), and the top-level node to run instead.
uintptr_t tap_menu();
void redirect_menu_tap(int index);
// Runs the menu model's node `index` through its select (vtable slot 1, our hook included, so
// MenuOptionClicked fires) as a pick of that row; no tap redirection. Game thread only.
bool select_menu_node(int index);

}  // namespace client_thread
