#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ScriptPreFired / ScriptPostFired: inline hooks on the game's CS2 VM (build 241.4).
//  - runScriptEvent(vm, ScriptEvent*, maxOps) 0x4df54c: a root script (RootPre before, Post after).
//  - script lookup by id 0x3f5658, when called from the GOSUB case (0x28, return address
//    0x4dd988): a proc is entered; its arguments are still on the int/object stacks (Pre).
//  - the locals swap 0x4dfd5c, when called from the RETURN case (0x15, return address 0x4de0e0):
//    a proc returned, its results are on the stacks (Post).
// Like RuneLite (ScriptVMMixin: INVOKE → ScriptPreFired, RETURN → ScriptPostFired).
namespace script_hooks {

// JavaListener: a root event whose script id is kJavaListener | kind (a slot the host wrote for a
// plugin's Java listener) runs no script; the listener gets (JavaListener, kind) with current_event()
// set, whether or not set_enabled is on.
enum Phase { Pre = 0, Post = 1, RootPre = 2, OpPre = 3, OpPost = 4, JavaListener = 5 };
constexpr int32_t kJavaListener = 0x7fff0000;
using Listener = void (*)(int phase, int script_id);

// Call from JNI_OnLoad, after game::init() and before the game runs any script.
bool install();
bool installed();

// The listener runs on the game thread, inside the VM, only while enabled.
void set_listener(Listener listener);
void set_enabled(bool enabled);

// The ScriptEvent of the innermost root script (valid inside a RootPre callback), or 0.
uintptr_t current_event();

// Opcode callbacks: the handler of CS2 opcodes >= 100 (0x4e06dc(vm, opcode, ...)) is wrapped;
// for each watched (script id, opcode) pair, executed by that script (the innermost running one),
// the listener gets OpPre before and OpPost after the instruction, with
// script_id = opcode << 16 | script. Delivered whether or not set_enabled is on, so RuneLite's
// runelite_callback instructions (which sit next to such opcodes) can be emulated.
// At most kMaxOpWatches pairs; pairs is {script, opcode, script, opcode, ...}. Script -1 matches
// any script; then prefixes[i] (non-empty) must start the string on top of the object stack
// (e.g. any if_settext of "Total level: ..."), reported with script id 0xffff.
constexpr int kMaxOpWatches = 16;
void set_op_watches(const int32_t* pairs, const std::string* prefixes, int count);

// Inside a Post delivered from RETURN: the call frame being restored (the caller's saved locals,
// see game::script_int_local); 0 elsewhere.
uintptr_t return_frame();
// The innermost running script id (in a RETURN Post: the caller), -1 if none.
int current_script();

// The opcodes and int operands of script `id` (RuneLite Script.getInstructions/getIntOperands), through
// the game's script cache. Game thread only.
bool script_code(int id, std::vector<int32_t>& ops, std::vector<int32_t>& ints);

struct Stats { uint64_t roots, gosubs, returns, delivered; };
Stats stats();

}  // namespace script_hooks
