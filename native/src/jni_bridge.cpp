#include "jni_bridge.h"
#include "gl_trace.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "client_thread.h"
#include "draw_hide.h"
#include "frame_capture.h"
#include "game.h"
#include "log.h"
#include "overlay.h"
#include "script_hooks.h"

extern "C" const char osrsrl_host_jar[];
extern "C" const char osrsrl_host_jar_end[];

namespace {

JavaVM* g_vm = nullptr;
jclass g_host = nullptr;  // global ref
jmethodID g_on_event = nullptr;
jmethodID g_on_script = nullptr;
std::atomic<const char*> g_status{"starting"};

bool check(JNIEnv* env, const char* what) {
    if (!env->ExceptionCheck()) return true;
    LOGE("jni: %s threw", what);
    env->ExceptionDescribe();  // prints the Java stack trace to logcat
    env->ExceptionClear();
    return false;
}

std::string package_name() {
    char buf[256] = {};
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        read(fd, buf, sizeof buf - 1);
        close(fd);
    }
    return buf;
}

bool write_file(const std::string& path, const char* data, size_t len) {
    unlink(path.c_str());  // the previous copy is read-only
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n <= 0) break;
        off += n;
    }
    close(fd);
    chmod(path.c_str(), 0400);  // Android 14+ refuses writable dex files
    return off == len;
}

// net.runelite.host.Native
jint n_game_state(JNIEnv*, jclass) { return game::game_state(); }
jint n_game_cycle(JNIEnv*, jclass) { return game::cycle(); }
jint n_server_tick(JNIEnv*, jclass) { return game::server_tick(); }
// Widget drag: {dragged comp, index, dragged-on comp, index, dragging, cycles, touch state} or null.
jintArray n_drag_state(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    int v[7];
    if (!game::drag_state(v)) return nullptr;
    jintArray a = env->NewIntArray(7);
    if (a) env->SetIntArrayRegion(a, 0, 7, reinterpret_cast<const jint*>(v));
    return a;
}
void n_drag_clear_target(JNIEnv*, jclass) { if (client_thread::on_client_thread()) game::drag_clear_target(); }
jint n_touch_state(JNIEnv*, jclass) { return game::touch_state(); }
// Friends chat (which = -2) or clan channel (-1 listened, 0.. affined): {String name, String owner,
// int[] {myRank, kickRank, talkRank}, String[] names, int[] worlds, int[] ranks}, or null.
jobjectArray n_chat_channel(JNIEnv* env, jclass, jint which) {
    if (!client_thread::on_client_thread()) return nullptr;
    game::ChatChannel ch;
    if (!(which == -2 ? game::friends_chat(ch) : game::clan_channel(which, ch))) return nullptr;
    jclass str = env->FindClass("java/lang/String");
    jsize n = static_cast<jsize>(ch.members.size());
    jobjectArray names = env->NewObjectArray(n, str, nullptr);
    std::vector<jint> worlds(n), ranks(n);
    for (jsize i = 0; i < n; i++) {
        jstring s = env->NewStringUTF(ch.members[i].name.c_str());
        env->SetObjectArrayElement(names, i, s);
        env->DeleteLocalRef(s);
        worlds[i] = ch.members[i].world;
        ranks[i] = ch.members[i].rank;
    }
    jintArray w = env->NewIntArray(n), r = env->NewIntArray(n), info = env->NewIntArray(3);
    env->SetIntArrayRegion(w, 0, n, worlds.data());
    env->SetIntArrayRegion(r, 0, n, ranks.data());
    jint iv[3] = {ch.my_rank, ch.kick_rank, ch.talk_rank};
    env->SetIntArrayRegion(info, 0, 3, iv);
    jobjectArray out = env->NewObjectArray(6, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, env->NewStringUTF(ch.name.c_str()));
    env->SetObjectArrayElement(out, 1, env->NewStringUTF(ch.owner.c_str()));
    env->SetObjectArrayElement(out, 2, info);
    env->SetObjectArrayElement(out, 3, names);
    env->SetObjectArrayElement(out, 4, w);
    env->SetObjectArrayElement(out, 5, r);
    return out;
}
// GE slot object probe: 8 bytes address (little endian) + 0x28 raw bytes, or null.
jbyteArray n_ge_slot_raw(JNIEnv* env, jclass, jint slot) {
    uint8_t buf[8 + 0x28] = {};
    uintptr_t p = game::ge_slot_raw(slot, buf + 8);
    if (!p) return nullptr;
    for (int i = 0; i < 8; i++) buf[i] = static_cast<uint8_t>(p >> (8 * i));
    jbyteArray out = env->NewByteArray(sizeof buf);
    if (out) env->SetByteArrayRegion(out, 0, sizeof buf, reinterpret_cast<const jbyte*>(buf));
    return out;
}
jint n_varp(JNIEnv*, jclass, jint id) { return game::varp(id); }
jint n_weight(JNIEnv*, jclass) { return game::weight(); }
jint n_world(JNIEnv*, jclass) { return game::world(); }
// {String host, String activity, int[] {id, flags, players, location, listed}}, or null.
jobjectArray n_world_entry(JNIEnv* env, jclass, jint id) {
    game::WorldEntry w;
    if (!client_thread::world_entry(id, w)) return nullptr;
    jintArray info = env->NewIntArray(5);
    jint iv[5] = {w.id, w.flags, w.players, w.location, w.listed ? 1 : 0};
    env->SetIntArrayRegion(info, 0, 5, iv);
    jobjectArray out = env->NewObjectArray(3, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, env->NewStringUTF(w.host.c_str()));
    env->SetObjectArrayElement(out, 1, env->NewStringUTF(w.activity.c_str()));
    env->SetObjectArrayElement(out, 2, info);
    return out;
}
// Each entry as worldEntry's {host, activity, int[] {id, flags, players, location, listed, index}}.
jobjectArray n_world_list(JNIEnv* env, jclass) {
    auto all = client_thread::world_list();
    jclass obj = env->FindClass("java/lang/Object");
    jobjectArray out = env->NewObjectArray(static_cast<jsize>(all.size()), obj, nullptr);
    for (size_t i = 0; i < all.size(); i++) {
        const auto& w = all[i];
        jintArray info = env->NewIntArray(6);
        jint iv[6] = {w.id, w.flags, w.players, w.location, 1, w.index};
        env->SetIntArrayRegion(info, 0, 6, iv);
        jobjectArray e = env->NewObjectArray(3, obj, nullptr);
        env->SetObjectArrayElement(e, 0, env->NewStringUTF(w.host.c_str()));
        env->SetObjectArrayElement(e, 1, env->NewStringUTF(w.activity.c_str()));
        env->SetObjectArrayElement(e, 2, info);
        env->SetObjectArrayElement(out, static_cast<jsize>(i), e);
        env->DeleteLocalRef(info);
        env->DeleteLocalRef(e);
    }
    return out;
}
// {String host, int[] {port, port2}} of the connected world (game thread), or null.
jobjectArray n_connection(JNIEnv* env, jclass) {
    std::string host;
    int ports[2];
    if (!client_thread::on_client_thread() || !game::connection(host, ports)) return nullptr;
    jintArray p = env->NewIntArray(2);
    env->SetIntArrayRegion(p, 0, 2, ports);
    jobjectArray out = env->NewObjectArray(2, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, env->NewStringUTF(host.c_str()));
    env->SetObjectArrayElement(out, 1, p);
    return out;
}
jint n_client_int(JNIEnv*, jclass, jint which) {
    int v = 0;
    return game::client_int(which, v) ? v : 0;
}
jboolean n_set_client_int(JNIEnv*, jclass, jint which, jint value) {
    return client_thread::on_client_thread() && game::set_client_int(which, value);
}
jint n_music_volume(JNIEnv*, jclass) { return game::music_volume(); }
jboolean n_set_music_volume(JNIEnv*, jclass, jint v) {
    return client_thread::on_client_thread() && game::set_music_volume(v);
}
// {group id, jingle (0/1)} per playing song; null off the client thread.
jintArray n_midi_requests(JNIEnv* env, jclass) {
    std::vector<std::pair<int, bool>> r;
    if (!client_thread::on_client_thread() || !game::midi_requests(r)) return nullptr;
    std::vector<jint> v;
    for (auto& e : r) { v.push_back(e.first); v.push_back(e.second ? 1 : 0); }
    jintArray out = env->NewIntArray(static_cast<jsize>(v.size()));
    if (out && !v.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
    return out;
}
void n_set_camera_shake_disabled(JNIEnv*, jclass, jboolean off) { client_thread::set_camera_shake_disabled(off); }
void n_set_camera_invert(JNIEnv*, jclass, jint axis, jboolean on) { client_thread::set_camera_invert(axis, on); }
jboolean n_camera_shake_disabled(JNIEnv*, jclass) { return client_thread::camera_shake_disabled(); }
void n_set_camera_speed(JNIEnv*, jclass, jfloat m) { client_thread::set_camera_speed(m); }
jfloat n_camera_sensitivity(JNIEnv*, jclass) { float v = 0; return game::camera_sensitivity(v) ? v : 0; }
jlongArray n_varps_long(JNIEnv* env, jclass, jboolean server) {
    std::vector<int64_t> v;
    if (!game::varps_long(server, v)) return nullptr;
    jlongArray out = env->NewLongArray(static_cast<jsize>(v.size()));
    if (out && !v.empty()) env->SetLongArrayRegion(out, 0, static_cast<jsize>(v.size()), reinterpret_cast<const jlong*>(v.data()));
    return out;
}
jboolean n_set_varp_long(JNIEnv*, jclass, jint id, jlong v) {
    return client_thread::on_client_thread() && game::set_varp_long(id, v);
}
// long[101]: the 100 ring entries, then the next index.
jlongArray n_cross_world_ids(JNIEnv* env, jclass) {
    int64_t ids[101];
    int index = 0;
    if (!game::cross_world_ids(ids, index)) return nullptr;
    ids[100] = index;
    jlongArray out = env->NewLongArray(101);
    if (out) env->SetLongArrayRegion(out, 0, 101, reinterpret_cast<const jlong*>(ids));
    return out;
}
jint n_login_index(JNIEnv*, jclass) { return game::login_index(); }
jint n_login_field(JNIEnv*, jclass) { return game::login_field(); }
jstring n_login_text(JNIEnv* env, jclass, jint which) { return env->NewStringUTF(game::login_text(which).c_str()); }
jboolean n_set_login_text(JNIEnv* env, jclass, jint which, jstring text) {
    if (!client_thread::on_client_thread() || !text) return false;
    const char* c = env->GetStringUTFChars(text, nullptr);
    bool ok = c && game::set_login_text(which, c);
    if (c) env->ReleaseStringUTFChars(text, c);
    return ok;
}
void n_set_login_background(JNIEnv* env, jclass, jintArray px, jint w, jint h) {
    if (!px || env->GetArrayLength(px) < w * h) { game::set_login_background(nullptr, 0, 0); return; }
    jint* p = env->GetIntArrayElements(px, nullptr);
    game::set_login_background(p, w, h);
    env->ReleaseIntArrayElements(px, p, JNI_ABORT);
}
jintArray n_login_background(JNIEnv* env, jclass) {
    int32_t v[5];
    if (!client_thread::on_client_thread() || !game::login_background(v)) return nullptr;
    jintArray out = env->NewIntArray(5);
    env->SetIntArrayRegion(out, 0, 5, v);
    return out;
}
void n_set_login_fire(JNIEnv*, jclass, jboolean on) { game::set_login_fire(on); }
jboolean n_login_fire(JNIEnv*, jclass) { return game::login_fire(); }
jint n_minimap_zoom(JNIEnv*, jclass) { return game::minimap_zoom(); }
jboolean n_set_minimap_zoom(JNIEnv*, jclass, jint z) { return client_thread::on_client_thread() && game::set_minimap_zoom(z); }
jint n_minimap_zoom_input(JNIEnv*, jclass) { return game::minimap_zoom_input(); }
jboolean n_set_minimap_zoom_input(JNIEnv*, jclass, jboolean on) { return client_thread::on_client_thread() && game::set_minimap_zoom_input(on); }
jint n_menu_scroll(JNIEnv*, jclass) { return game::menu_scroll(); }
jboolean n_set_menu_scroll(JNIEnv*, jclass, jint v) { return client_thread::on_client_thread() && game::set_menu_scroll(v); }
jboolean n_begin_target_mode(JNIEnv*, jclass) { return client_thread::on_client_thread() && game::begin_target_mode(); }
jboolean n_set_server_tick(JNIEnv*, jclass, jint tick) {
    return client_thread::on_client_thread() && game::set_server_tick(tick);
}
jlongArray n_ge_offers(JNIEnv* env, jclass) {
    int64_t o[8 * 7];
    if (!game::ge_offers(o)) return nullptr;
    jlongArray out = env->NewLongArray(8 * 7);
    if (out) env->SetLongArrayRegion(out, 0, 8 * 7, reinterpret_cast<const jlong*>(o));
    return out;
}
// int[1 + 4*13*13]: [0] = 1 if instanced, then the packed template chunks; null before login.
jintArray n_instance_templates(JNIEnv* env, jclass) {
    bool inst = false;
    int32_t t[1 + 4 * 13 * 13];
    if (!game::instance_templates(inst, t + 1)) return nullptr;
    t[0] = inst ? 1 : 0;
    jintArray out = env->NewIntArray(1 + 4 * 13 * 13);
    if (out) env->SetIntArrayRegion(out, 0, 1 + 4 * 13 * 13, t);
    return out;
}
jint n_world_flags(JNIEnv*, jclass) { return game::world_flags(); }
jintArray n_viewport_zoom(JNIEnv* env, jclass) {
    int v[5];
    if (!game::viewport_zoom(v)) return nullptr;
    jintArray a = env->NewIntArray(5);
    env->SetIntArrayRegion(a, 0, 5, v);
    return a;
}
// getDBTableField: Object[] of Integer/Long/String, or null (game thread).
jobjectArray n_db_field(JNIEnv* env, jclass, jint row, jint column, jint tuple) {
    std::vector<game::DbValue> vals;
    if (!game::db_field(row, column, tuple, vals)) return nullptr;
    jclass obj = env->FindClass("java/lang/Object");
    jclass ic = env->FindClass("java/lang/Integer"), lc = env->FindClass("java/lang/Long");
    jmethodID iv = env->GetStaticMethodID(ic, "valueOf", "(I)Ljava/lang/Integer;");
    jmethodID lv = env->GetStaticMethodID(lc, "valueOf", "(J)Ljava/lang/Long;");
    jobjectArray a = env->NewObjectArray(static_cast<jsize>(vals.size()), obj, nullptr);
    for (size_t k = 0; k < vals.size(); k++) {
        jobject o = vals[k].kind == 0 ? env->CallStaticObjectMethod(ic, iv, static_cast<jint>(vals[k].i))
                  : vals[k].kind == 1 ? env->CallStaticObjectMethod(lc, lv, static_cast<jlong>(vals[k].i))
                                      : static_cast<jobject>(env->NewStringUTF(vals[k].s.c_str()));
        env->SetObjectArrayElement(a, static_cast<jsize>(k), o);
        env->DeleteLocalRef(o);
    }
    return a;
}
jint n_db_row_table(JNIEnv*, jclass, jint row) { return game::db_row_table(row); }
jint n_db_field_count(JNIEnv*, jclass, jint row, jint column) { return game::db_field_count(row, column); }
// The GPU's ViewTransforms block (100 floats), or null before one was captured.
jfloatArray n_view_transforms(JNIEnv* env, jclass) {
    float v[100];
    if (!gl_view_transforms(v)) return nullptr;
    jfloatArray a = env->NewFloatArray(100);
    env->SetFloatArrayRegion(a, 0, 100, v);
    return a;
}
jint n_run_energy(JNIEnv*, jclass) { return game::run_energy(); }
// {height, x0, y0, z0, x1, ...} of an NPC's (player=false) or player's posed model; null if none.
jintArray n_actor_model(JNIEnv* env, jclass, jboolean player, jint index) {
    if (!client_thread::on_client_thread()) return nullptr;
    // Resolve now, not from the cycle's snapshot: entities can be freed after it was taken.
    uintptr_t a = player ? game::live_player(index) : game::live_npc(index);
    std::vector<int32_t> xyz;
    int height = 0;
    if (!game::actor_model(a, xyz, height)) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(xyz.size() + 1));
    env->SetIntArrayRegion(arr, 0, 1, &height);
    env->SetIntArrayRegion(arr, 1, static_cast<jsize>(xyz.size()), xyz.data());
    return arr;
}
// Posed mesh of an NPC's (player=false) or player's model (layout: game.cpp read_mesh); null if none.
jintArray n_actor_mesh(JNIEnv* env, jclass, jboolean player, jint index) {
    if (!client_thread::on_client_thread()) return nullptr;
    uintptr_t a = player ? game::live_player(index) : game::live_npc(index);
    std::vector<int32_t> mesh;
    if (!game::actor_mesh(a, mesh)) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(mesh.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(mesh.size()), mesh.data());
    return arr;
}
// Model id's ModelData from the game's model store (layout: game.cpp pack_mesh); null if not loaded.
jintArray n_model_data(JNIEnv* env, jclass, jint id) {
    if (!client_thread::on_client_thread()) return nullptr;
    std::vector<int32_t> mesh;
    if (!game::model_data(id, mesh)) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(mesh.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(mesh.size()), mesh.data());
    return arr;
}
// Client.applyTransformations (game.cpp pose_model); null if it can't be posed.
jintArray n_pose_model(JNIEnv* env, jclass, jint id, jintArray xyz, jint seqA, jint frameA, jint seqB, jint frameB) {
    if (!client_thread::on_client_thread() || !xyz) return nullptr;
    std::vector<int32_t> v(static_cast<size_t>(env->GetArrayLength(xyz)));
    env->GetIntArrayRegion(xyz, 0, static_cast<jsize>(v.size()), v.data());
    std::vector<int32_t> mesh;
    if (!game::pose_model(id, v, seqA, frameA, seqB, frameB, mesh)) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(mesh.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(mesh.size()), mesh.data());
    return arr;
}
// meta: 7 ints per icon {gameIndex, w, h, ox, oy, fullW, fullH}; pixels/palettes null for game icons.
void n_set_mod_icons(JNIEnv* env, jclass, jintArray meta, jobjectArray pixels, jobjectArray palettes) {
    if (!meta) return;
    jsize n = env->GetArrayLength(meta) / 7;
    std::vector<jint> m(static_cast<size_t>(n) * 7);
    env->GetIntArrayRegion(meta, 0, n * 7, m.data());
    std::vector<game::ModIcon> icons(static_cast<size_t>(n));
    for (jsize i = 0; i < n; i++) {
        game::ModIcon& o = icons[i];
        const jint* v = &m[static_cast<size_t>(i) * 7];
        o.game_index = v[0];
        o.w = v[1]; o.h = v[2]; o.ox = v[3]; o.oy = v[4]; o.full_w = v[5]; o.full_h = v[6];
        if (o.game_index >= 0) continue;
        auto px = static_cast<jbyteArray>(env->GetObjectArrayElement(pixels, i));
        auto pal = static_cast<jintArray>(env->GetObjectArrayElement(palettes, i));
        if (px) {
            o.pixels.resize(static_cast<size_t>(env->GetArrayLength(px)));
            env->GetByteArrayRegion(px, 0, static_cast<jsize>(o.pixels.size()), reinterpret_cast<jbyte*>(o.pixels.data()));
            env->DeleteLocalRef(px);
        }
        if (pal) {
            o.palette.resize(static_cast<size_t>(env->GetArrayLength(pal)));
            env->GetIntArrayRegion(pal, 0, static_cast<jsize>(o.palette.size()), reinterpret_cast<jint*>(o.palette.data()));
            env->DeleteLocalRef(pal);
            for (size_t k = 0; k < o.palette.size(); k++)  // as the game's loader: index 0 clear, others opaque
                o.palette[k] = k == 0 ? 0 : (o.palette[k] | 0xff000000u);
        }
    }
    game::set_mod_icons(std::move(icons));
}
jintArray n_mod_icon_sizes(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    std::vector<int32_t> v = game::mod_icon_sizes();
    if (v.empty()) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
jint n_actor_height(JNIEnv*, jclass, jboolean player, jint index) {
    if (!client_thread::on_client_thread()) return -1;
    uintptr_t a = player ? game::live_player(index) : game::live_npc(index);
    return a ? game::actor_height(a) : -1;
}
// {x, y, height, orientation, modelHeight, vx, vy, vz, ...} of a scene object's model, or null.
jintArray n_object_model(JNIEnv* env, jclass, jint plane, jint sx, jint sy, jint layer, jlong tag) {
    if (!client_thread::on_client_thread()) return nullptr;
    game::ObjectModel m;
    if (!game::object_model(plane, sx, sy, layer, static_cast<uint64_t>(tag), m)) return nullptr;
    jint head[5] = {m.x, m.y, m.height, m.orientation, m.model_height};
    jintArray arr = env->NewIntArray(static_cast<jsize>(m.xyz.size() + 5));
    env->SetIntArrayRegion(arr, 0, 5, head);
    env->SetIntArrayRegion(arr, 5, static_cast<jsize>(m.xyz.size()), m.xyz.data());
    return arr;
}
// An animated loc's fields (game::dynamic_object) or, with mesh, its model packed as actorMesh; null if none.
jintArray n_dynamic_object(JNIEnv* env, jclass, jint plane, jint sx, jint sy, jint layer, jlong tag, jint slot, jboolean mesh) {
    if (!client_thread::on_client_thread()) return nullptr;
    int32_t f[game::kDynamicObjectFields];
    std::vector<int32_t> m;
    if (!game::dynamic_object(plane, sx, sy, layer, static_cast<uint64_t>(tag), slot, f, mesh ? &m : nullptr)) return nullptr;
    jsize n = mesh ? static_cast<jsize>(m.size()) : game::kDynamicObjectFields;
    jintArray arr = env->NewIntArray(n);
    env->SetIntArrayRegion(arr, 0, n, mesh ? m.data() : f);
    return arr;
}
// {id, front sprite, back sprite, width, padding} per health bar type on a live actor; null off the game thread.
jintArray n_health_bar_types(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    std::vector<int32_t> v;
    for (const game::HealthBarType& t : game::health_bar_types()) v.insert(v.end(), {t.id, t.front_sprite, t.back_sprite, t.width, t.padding});
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
jboolean n_health_bar_set_padding(JNIEnv*, jclass, jint id, jint padding) {
    return client_thread::on_client_thread() && game::health_bar_set_padding(id, padding);
}
jintArray n_varps(JNIEnv* env, jclass) {
    std::vector<int32_t> v = game::varps();
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
jintArray n_server_varps(JNIEnv* env, jclass) {
    std::vector<int32_t> v = game::server_varps();
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
jint n_varc_int(JNIEnv*, jclass, jint id) { int v = 0; return game::varc_int(id, v) ? v : 0; }
jstring latin1_string(JNIEnv* env, const std::string& s);
jstring n_varc_string(JNIEnv* env, jclass, jint id) {
    std::string s;
    return game::varc_string(id, s) ? latin1_string(env, s) : nullptr;
}
jboolean n_add_chat_message(JNIEnv* env, jclass, jint type, jstring name, jstring text) {
    if (!client_thread::on_client_thread()) return false;
    auto str = [env](jstring s) {
        if (!s) return std::string();
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string r(c ? c : "");
        if (c) env->ReleaseStringUTFChars(s, c);
        return r;
    };
    return game::add_chat_message(type, str(name), str(text));
}
jboolean n_chat_remove(JNIEnv*, jclass, jint type, jint id) {
    return client_thread::on_client_thread() && game::chat_remove_message(type, id);
}
jint n_next_message_id(JNIEnv*, jclass) { return game::next_message_id(); }
// World map {present, loaded, zoom * 1000, x, y, map}, or null off the client thread.
jintArray n_world_map(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    game::WorldMapState st = game::world_map_state();
    jint v[6] = {st.present, st.loaded, static_cast<jint>(st.zoom * 1000.0f), st.x, st.y, st.map};
    jintArray out = env->NewIntArray(6);
    env->SetIntArrayRegion(out, 0, 6, v);
    return out;
}
jint n_world_map_contains(JNIEnv*, jclass, jint map, jint level, jint x, jint z) {
    return client_thread::on_client_thread() ? game::world_map_contains(map, level, x, z) : -1;
}
jboolean n_world_map_jump(JNIEnv*, jclass, jint level, jint x, jint z) {
    return client_thread::on_client_thread() && game::world_map_jump(level, x, z);
}
// {rows, cols, then per icon row, col, element, level, x, y}, or null (off the client thread / not loaded).
jintArray n_world_map_icons(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    int rows = 0, cols = 0;
    std::vector<int> v;
    if (!game::world_map_icons(rows, cols, v)) return nullptr;
    v.insert(v.begin(), {rows, cols});
    jintArray out = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
    return out;
}
// {category, sprite}, or null for an unknown element.
jintArray n_map_element(JNIEnv* env, jclass, jint id) {
    int category = -1, sprite = -1;
    if (!game::map_element(id, category, sprite)) return nullptr;
    jint v[2] = {category, sprite};
    jintArray out = env->NewIntArray(2);
    env->SetIntArrayRegion(out, 0, 2, v);
    return out;
}
jint n_afk_timeout(JNIEnv*, jclass) { return game::afk_timeout_minutes(); }
jboolean n_set_afk_timeout(JNIEnv*, jclass, jint minutes) {
    return client_thread::on_client_thread() && game::set_afk_timeout_minutes(minutes);
}
void n_chat_changed(JNIEnv*, jclass) {
    if (client_thread::on_client_thread()) game::chat_changed();
}
jboolean n_message_set_text(JNIEnv* env, jclass, jint id, jstring text) {
    if (!client_thread::on_client_thread() || !text) return false;
    const char* c = env->GetStringUTFChars(text, nullptr);
    std::string t(c ? c : "");
    if (c) env->ReleaseStringUTFChars(text, c);
    return game::message_set_text(id, t);
}
jint n_server_varp(JNIEnv*, jclass, jint id) { return game::server_varp(id); }
// Client-side setters (game thread only; false off it or for unknown ids).
jboolean n_set_varbit(JNIEnv*, jclass, jint id, jint value) {
    return client_thread::on_client_thread() && game::set_varbit(id, value);
}
jboolean n_set_varp(JNIEnv*, jclass, jint id, jint value) {
    return client_thread::on_client_thread() && game::set_varp(id, value);
}
jboolean n_set_varc_int(JNIEnv*, jclass, jint id, jint value) {
    return client_thread::on_client_thread() && game::set_varc_int(id, value);
}
jboolean n_set_varc_string(JNIEnv* env, jclass, jint id, jstring value) {
    if (!client_thread::on_client_thread()) return false;
    std::string latin1;
    if (value) {
        const jchar* c = env->GetStringChars(value, nullptr);
        jsize n = env->GetStringLength(value);
        for (jsize i = 0; i < n; i++) latin1.push_back(c[i] < 256 ? static_cast<char>(c[i]) : '?');
        env->ReleaseStringChars(value, c);
    }
    return game::set_varc_string(id, latin1);
}
// {varp, lsb, msb}, or null.
jintArray n_varbit_def(JNIEnv* env, jclass, jint id) {
    game::VarbitDef d;
    if (!client_thread::on_client_thread() || !game::varbit_def(id, d)) return nullptr;
    jint v[3] = {d.varp, d.lsb, d.msb};
    jintArray arr = env->NewIntArray(3);
    env->SetIntArrayRegion(arr, 0, 3, v);
    return arr;
}
jint n_varbit(JNIEnv* env, jclass, jint id) {
    if (!client_thread::on_client_thread()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "getVarbitValue must be called on the client thread");
        return 0;
    }
    return game::varbit(id);
}
// Object[] {int[] intStack, String[] objectStack (null for non-string entries)}.
jobjectArray n_run_script(JNIEnv* env, jclass, jobjectArray jargs) {
    if (!client_thread::on_client_thread()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "runScript must be called on the client thread");
        return nullptr;
    }
    jclass integer = env->FindClass("java/lang/Integer");
    jclass string = env->FindClass("java/lang/String");
    jmethodID int_value = env->GetMethodID(integer, "intValue", "()I");
    std::vector<game::ScriptArg> args;
    jsize n = jargs ? env->GetArrayLength(jargs) : 0;
    for (jsize i = 0; i < n; i++) {
        jobject o = env->GetObjectArrayElement(jargs, i);
        game::ScriptArg a;
        if (o && env->IsInstanceOf(o, integer)) {
            a.i = env->CallIntMethod(o, int_value);
        } else if (o && env->IsInstanceOf(o, string)) {
            a.is_string = true;
            const char* c = env->GetStringUTFChars(static_cast<jstring>(o), nullptr);
            a.s = c ? c : "";
            if (c) env->ReleaseStringUTFChars(static_cast<jstring>(o), c);
        } else {
            env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "runScript: only Integer and String arguments are supported");
            return nullptr;
        }
        env->DeleteLocalRef(o);
        args.push_back(std::move(a));
    }
    game::ScriptResult r;
    std::string error;
    if (!game::run_script(args, r, error)) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), ("runScript: " + error).c_str());
        return nullptr;
    }
    jintArray ints = env->NewIntArray(static_cast<jsize>(r.ints.size()));
    if (!r.ints.empty()) env->SetIntArrayRegion(ints, 0, static_cast<jsize>(r.ints.size()), r.ints.data());
    jobjectArray objs = env->NewObjectArray(static_cast<jsize>(r.objects.size()), string, nullptr);
    for (size_t i = 0; i < r.objects.size(); i++) {
        if (!r.object_is_string[i]) continue;
        jstring js = env->NewStringUTF(r.objects[i].c_str());
        env->SetObjectArrayElement(objs, static_cast<jsize>(i), js);
        env->DeleteLocalRef(js);
    }
    jobjectArray res = env->NewObjectArray(2, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(res, 0, ints);
    env->SetObjectArrayElement(res, 1, objs);
    return res;
}
// Object[] {int[] {price, weight, category, stackable, members, tradeable?, linkedNote,
// noteTemplate, placeholder, placeholderTemplate}, String[] {name, 5 inventory actions}}.
jobjectArray n_item_def(JNIEnv* env, jclass, jint id) {
    if (!client_thread::on_client_thread()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "getItemDefinition must be called on the client thread");
        return nullptr;
    }
    game::ItemDef d;
    if (!game::item_def(id, d)) return nullptr;
    jint v[17] = {d.price, d.weight, d.category, d.stackable, d.members, d.tradeable_guess,
                  d.linked_note, d.note_template, d.placeholder, d.placeholder_template,
                  d.zoom2d, d.xan2d, d.yan2d, d.zan2d, d.inventory_model, d.ambient, d.contrast};
    jintArray ints = env->NewIntArray(17);
    env->SetIntArrayRegion(ints, 0, 17, v);
    jclass string = env->FindClass("java/lang/String");
    jobjectArray strs = env->NewObjectArray(6, string, nullptr);
    for (int i = 0; i < 6; i++) {
        const std::string& t = i == 0 ? d.name : d.inventory_actions[i - 1];
        if (i > 0 && t.empty()) continue;
        jstring js = env->NewStringUTF(t.c_str());
        env->SetObjectArrayElement(strs, i, js);
        env->DeleteLocalRef(js);
    }
    // short[][4] {recolour find, replace, retexture find, replace}; an empty array is null.
    jobjectArray recol = env->NewObjectArray(4, env->FindClass("[S"), nullptr);
    const std::vector<int16_t>* lists[4] = {&d.recolor_from, &d.recolor_to, &d.retexture_from, &d.retexture_to};
    for (int i = 0; i < 4; i++) {
        if (lists[i]->empty()) continue;
        jshortArray a = env->NewShortArray(static_cast<jsize>(lists[i]->size()));
        env->SetShortArrayRegion(a, 0, static_cast<jsize>(lists[i]->size()), lists[i]->data());
        env->SetObjectArrayElement(recol, i, a);
        env->DeleteLocalRef(a);
    }
    jobjectArray res = env->NewObjectArray(3, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(res, 0, ints);
    env->SetObjectArrayElement(res, 1, strs);
    env->SetObjectArrayElement(res, 2, recol);
    return res;
}
// Object[] {int[] {id, size, combatLevel, minimapVisible, interactable, varbit, varp}, String[] {name, 5 actions}, int[] transforms or null}.
jobjectArray n_npc_def(JNIEnv* env, jclass, jint id) {
    if (!client_thread::on_client_thread()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "getNpcDefinition must be called on the client thread");
        return nullptr;
    }
    game::NpcDef d;
    if (!game::npc_def(id, d)) return nullptr;
    jint v[15] = {d.id, d.size, d.combat_level, d.minimap_visible, d.interactable, d.varbit, d.varp,
                  d.width_scale, d.height_scale, d.stats[0], d.stats[1], d.stats[2], d.stats[3], d.stats[4], d.stats[5]};
    jintArray ints = env->NewIntArray(15);
    env->SetIntArrayRegion(ints, 0, 15, v);
    jclass string = env->FindClass("java/lang/String");
    jobjectArray strs = env->NewObjectArray(6, string, nullptr);
    for (int i = 0; i < 6; i++) {
        const std::string& t = i == 0 ? d.name : d.actions[i - 1];
        if (i > 0 && t.empty()) continue;
        jstring js = env->NewStringUTF(t.c_str());
        env->SetObjectArrayElement(strs, i, js);
        env->DeleteLocalRef(js);
    }
    jintArray tr = nullptr;
    if (!d.transforms.empty()) {
        tr = env->NewIntArray(static_cast<jsize>(d.transforms.size()));
        env->SetIntArrayRegion(tr, 0, static_cast<jsize>(d.transforms.size()), d.transforms.data());
    }
    // int[][2] {models, chathead models}, short[][2] {recolour find, replace}; empty = null.
    jobjectArray models = env->NewObjectArray(2, env->FindClass("[I"), nullptr);
    const std::vector<int32_t>* ml[2] = {&d.models, &d.chathead_models};
    for (int i = 0; i < 2; i++) {
        if (ml[i]->empty()) continue;
        jintArray a = env->NewIntArray(static_cast<jsize>(ml[i]->size()));
        env->SetIntArrayRegion(a, 0, static_cast<jsize>(ml[i]->size()), ml[i]->data());
        env->SetObjectArrayElement(models, i, a);
        env->DeleteLocalRef(a);
    }
    jobjectArray recol = env->NewObjectArray(2, env->FindClass("[S"), nullptr);
    const std::vector<int16_t>* rl[2] = {&d.recolor_from, &d.recolor_to};
    for (int i = 0; i < 2; i++) {
        if (rl[i]->empty()) continue;
        jshortArray a = env->NewShortArray(static_cast<jsize>(rl[i]->size()));
        env->SetShortArrayRegion(a, 0, static_cast<jsize>(rl[i]->size()), rl[i]->data());
        env->SetObjectArrayElement(recol, i, a);
        env->DeleteLocalRef(a);
    }
    jobjectArray res = env->NewObjectArray(5, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(res, 0, ints);
    env->SetObjectArrayElement(res, 1, strs);
    env->SetObjectArrayElement(res, 2, tr);
    env->SetObjectArrayElement(res, 3, models);
    env->SetObjectArrayElement(res, 4, recol);
    return res;
}
// Object[] {int[] {id, sizeX, sizeY, varbit, varp}, String[] {name, 5 actions}, int[] transforms or null}.
jobjectArray n_loc_def(JNIEnv* env, jclass, jint id) {
    if (!client_thread::on_client_thread()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "getObjectDefinition must be called on the client thread");
        return nullptr;
    }
    game::LocDef d;
    if (!game::loc_def(id, d)) return nullptr;
    jint v[5] = {d.id, d.size_x, d.size_y, d.varbit, d.varp};
    jintArray ints = env->NewIntArray(5);
    env->SetIntArrayRegion(ints, 0, 5, v);
    jclass string = env->FindClass("java/lang/String");
    jobjectArray strs = env->NewObjectArray(6, string, nullptr);
    for (int i = 0; i < 6; i++) {
        const std::string& t = i == 0 ? d.name : d.actions[i - 1];
        if (i > 0 && t.empty()) continue;
        jstring js = env->NewStringUTF(t.c_str());
        env->SetObjectArrayElement(strs, i, js);
        env->DeleteLocalRef(js);
    }
    jintArray tr = nullptr;
    if (!d.transforms.empty()) {
        tr = env->NewIntArray(static_cast<jsize>(d.transforms.size()));
        env->SetIntArrayRegion(tr, 0, static_cast<jsize>(d.transforms.size()), d.transforms.data());
    }
    jobjectArray res = env->NewObjectArray(3, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(res, 0, ints);
    env->SetObjectArrayElement(res, 1, strs);
    env->SetObjectArrayElement(res, 2, tr);
    return res;
}
void n_sidebar_set_model(JNIEnv* env, jclass, jstring model) {
    const char* c = model ? env->GetStringUTFChars(model, nullptr) : nullptr;
    sidebar_set_model(c ? c : "");
    if (c) env->ReleaseStringUTFChars(model, c);
}
jintArray n_sidebar_poll_events(JNIEnv* env, jclass) {
    std::vector<int> ev = sidebar_poll_events();
    if (ev.empty()) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(ev.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(ev.size()), ev.data());
    return arr;
}
jboolean n_sidebar_open(JNIEnv*, jclass) { return sidebar_open(); }
void n_sidebar_set_open(JNIEnv*, jclass, jboolean open) { sidebar_set_open(open); }
void n_sidebar_icon(JNIEnv* env, jclass, jint id, jint w, jint h, jintArray argb) {
    if (!argb || w <= 0 || h <= 0 || env->GetArrayLength(argb) < w * h) return;
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    env->GetIntArrayRegion(argb, 0, w * h, reinterpret_cast<jint*>(px.data()));
    sidebar_set_icon(id, w, h, px);
}
jboolean n_shift_held(JNIEnv*, jclass) { return virtual_shift(); }
void n_plugin_menu(JNIEnv* env, jclass, jobjectArray labels, jint x, jint y, jint w, jint canvas_h) {
    std::vector<std::string> out;
    jsize n = labels ? env->GetArrayLength(labels) : 0;
    for (jsize i = 0; i < n; i++) {
        auto s = static_cast<jstring>(env->GetObjectArrayElement(labels, i));
        const char* c = s ? env->GetStringUTFChars(s, nullptr) : nullptr;
        out.emplace_back(c ? c : "");
        if (c) env->ReleaseStringUTFChars(s, c);
        if (s) env->DeleteLocalRef(s);
    }
    plugin_menu_set(out, x, y, w, canvas_h);
}
jboolean n_menu_close(JNIEnv*, jclass) { return game::menu_close(); }
jboolean n_overlay_touch(JNIEnv*, jclass, jint action, jfloat x, jfloat y) { return overlay_touch(action, x, y); }
// {planes, sizeX, sizeY, heights..., settings...} (layouts in game.h), or null.
jintArray n_scene_heights(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    game::SceneHeights h;
    if (!game::scene_heights(h)) return nullptr;
    std::vector<jint> v = {h.planes, h.size_x, h.size_y};
    v.insert(v.end(), h.heights.begin(), h.heights.end());
    v.insert(v.end(), h.settings.begin(), h.settings.end());
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
// Object[] {int[] {keyType, valueType, defaultInt}, String defaultString, int[] keys, int[] intValues, String[] stringValues}.
jobjectArray n_enum_def(JNIEnv* env, jclass, jint id) {
    if (!client_thread::on_client_thread()) return nullptr;
    game::EnumDef d;
    if (!game::enum_def(id, d)) return nullptr;
    jclass string = env->FindClass("java/lang/String");
    jint head[3] = {d.key_type, d.value_type, d.default_int};
    jintArray h = env->NewIntArray(3);
    env->SetIntArrayRegion(h, 0, 3, head);
    jintArray keys = env->NewIntArray(static_cast<jsize>(d.keys.size()));
    if (!d.keys.empty()) env->SetIntArrayRegion(keys, 0, static_cast<jsize>(d.keys.size()), d.keys.data());
    jintArray ints = env->NewIntArray(static_cast<jsize>(d.int_values.size()));
    if (!d.int_values.empty()) env->SetIntArrayRegion(ints, 0, static_cast<jsize>(d.int_values.size()), d.int_values.data());
    jobjectArray strs = env->NewObjectArray(static_cast<jsize>(d.string_values.size()), string, nullptr);
    for (size_t i = 0; i < d.string_values.size(); i++) {
        jstring js = env->NewStringUTF(d.string_values[i].c_str());
        env->SetObjectArrayElement(strs, static_cast<jsize>(i), js);
        env->DeleteLocalRef(js);
    }
    jobjectArray res = env->NewObjectArray(5, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(res, 0, h);
    env->SetObjectArrayElement(res, 1, env->NewStringUTF(d.default_string.c_str()));
    env->SetObjectArrayElement(res, 2, keys);
    env->SetObjectArrayElement(res, 3, ints);
    env->SetObjectArrayElement(res, 4, strs);
    return res;
}
jboolean n_is_client_thread(JNIEnv*, jclass) { return client_thread::on_client_thread(); }
void n_set_client_delegate(JNIEnv*, jclass, jboolean on) { client_thread::set_delegate(on); }
void n_set_hidden_actors(JNIEnv* env, jclass, jintArray npcs, jintArray players) {
    if (!client_thread::on_client_thread()) return;
    std::vector<uintptr_t> ptrs;
    auto add = [&](jintArray a, uintptr_t (*lookup)(int)) {
        if (!a) return;
        jsize n = env->GetArrayLength(a);
        std::vector<jint> v(n);
        env->GetIntArrayRegion(a, 0, n, v.data());
        for (jint i : v)
            if (uintptr_t p = lookup(i)) ptrs.push_back(p);
    };
    add(npcs, game::live_npc);
    size_t before = ptrs.size();
    add(players, game::live_player);
    static int logs = 0;
    if (logs < 20 && (npcs || players)) {
        logs++;
        LOGI("draw_hide: %zu npc and %zu player pointers resolved", before, ptrs.size() - before);
    }
    draw_hide::set_hidden(ptrs);
}
jintArray n_minimap_dot(JNIEnv* env, jclass, jint cat, jint dot, jint outline, jint thickness) {
    if (!client_thread::on_client_thread()) return nullptr;
    if (thickness >= 0 && !game::minimap_dot_style(cat, dot, outline, thickness)) return nullptr;
    int v[4];
    if (!game::minimap_dot_get(cat, v)) return nullptr;
    jintArray a = env->NewIntArray(4);
    env->SetIntArrayRegion(a, 0, 4, v);
    return a;
}
jboolean n_set_ge_search(JNIEnv* env, jclass, jshortArray ids, jint count, jint index) {
    if (!client_thread::on_client_thread()) return false;
    bool ok = true;
    if (ids) {
        jsize n = env->GetArrayLength(ids);
        std::vector<jshort> v(n);
        env->GetShortArrayRegion(ids, 0, n, v.data());
        ok = game::set_ge_search_ids(reinterpret_cast<const int16_t*>(v.data()), n);
    }
    if (count != INT32_MIN) ok = game::set_ge_search_int(false, count) && ok;
    if (index != INT32_MIN) ok = game::set_ge_search_int(true, index) && ok;
    return ok;
}
jintArray n_ge_search(JNIEnv* env, jclass) {
    std::vector<int> v;
    if (!client_thread::on_client_thread() || !game::ge_search(v)) return nullptr;
    jintArray a = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(a, 0, static_cast<jsize>(v.size()), v.data());
    return a;
}
jboolean n_queue_changed_skill(JNIEnv*, jclass, jint skill) { return client_thread::on_client_thread() && game::queue_changed_skill(skill); }

void n_set_script_events(JNIEnv*, jclass, jboolean on) { script_hooks::set_enabled(on); }
jboolean n_set_script_string(JNIEnv* env, jclass, jint index, jstring text) {
    if (!client_thread::on_client_thread() || !text) return false;
    std::string latin1;  // game strings are Latin-1 (as set_varc_string)
    const jchar* c = env->GetStringChars(text, nullptr);
    jsize n = env->GetStringLength(text);
    for (jsize i = 0; i < n; i++) latin1.push_back(c[i] < 256 ? static_cast<char>(c[i]) : '?');
    env->ReleaseStringChars(text, c);
    return game::set_script_string(index, latin1);
}
void n_set_op_watches(JNIEnv* env, jclass, jintArray pairs, jobjectArray prefixes) {
    if (!pairs) { script_hooks::set_op_watches(nullptr, nullptr, 0); return; }
    jsize n = env->GetArrayLength(pairs);
    std::vector<jint> v(static_cast<size_t>(n));
    env->GetIntArrayRegion(pairs, 0, n, v.data());
    std::vector<std::string> p(static_cast<size_t>(n / 2));
    for (jsize i = 0; prefixes && i < n / 2 && i < env->GetArrayLength(prefixes); i++) {
        auto s = static_cast<jstring>(env->GetObjectArrayElement(prefixes, i));
        if (!s) continue;
        const char* c = env->GetStringUTFChars(s, nullptr);
        if (c) { p[static_cast<size_t>(i)] = c; env->ReleaseStringUTFChars(s, c); }
        env->DeleteLocalRef(s);
    }
    script_hooks::set_op_watches(reinterpret_cast<const int32_t*>(v.data()), p.data(), n / 2);
}

jobjectArray n_script_stacks(JNIEnv* env, jclass) {
    game::ScriptResult r;
    if (!client_thread::on_client_thread() || !game::script_stacks(r)) return nullptr;
    jclass string = env->FindClass("java/lang/String");
    jintArray ints = env->NewIntArray(static_cast<jsize>(r.ints.size()));
    if (!r.ints.empty()) env->SetIntArrayRegion(ints, 0, static_cast<jsize>(r.ints.size()), r.ints.data());
    jobjectArray objs = env->NewObjectArray(static_cast<jsize>(r.objects.size()), string, nullptr);
    for (size_t i = 0; i < r.objects.size(); i++) {
        if (!r.object_is_string[i]) continue;
        jstring js = env->NewStringUTF(r.objects[i].c_str());
        env->SetObjectArrayElement(objs, static_cast<jsize>(i), js);
        env->DeleteLocalRef(js);
    }
    jobjectArray res = env->NewObjectArray(2, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(res, 0, ints);
    env->SetObjectArrayElement(res, 1, objs);
    return res;
}

// Script locals for runelite_callback emulation: `caller` true = the frame a RETURN restores
// (inside ScriptPostFired), false = the running frame.
jintArray n_script_array(JNIEnv* env, jclass, jboolean caller, jint slot) {
    uintptr_t frame = caller ? script_hooks::return_frame() : 0;
    if (caller && !frame) return nullptr;
    std::vector<int32_t> v;
    if (!game::script_array(frame, slot, v)) return nullptr;
    jintArray out = env->NewIntArray(static_cast<jsize>(v.size()));
    if (out && !v.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
    return out;
}
jboolean n_set_script_array(JNIEnv* env, jclass, jboolean caller, jint slot, jintArray values) {
    uintptr_t frame = caller ? script_hooks::return_frame() : 0;
    if ((caller && !frame) || !values) return JNI_FALSE;
    jsize n = env->GetArrayLength(values);
    std::vector<int32_t> v(static_cast<size_t>(n));
    if (n) env->GetIntArrayRegion(values, 0, n, v.data());
    return game::set_script_array(frame, slot, v.data(), n) ? JNI_TRUE : JNI_FALSE;
}
// Returns the int local (after writing `value` when `write`), or Integer.MIN_VALUE if unavailable.
jint n_script_int_local(JNIEnv*, jclass, jboolean caller, jint index, jboolean write, jint value) {
    uintptr_t frame = caller ? script_hooks::return_frame() : 0;
    if (caller && !frame) return INT32_MIN;
    int32_t out = 0, in = value;
    return game::script_int_local(frame, index, out, write ? &in : nullptr) ? out : INT32_MIN;
}
jint n_current_script(JNIEnv*, jclass) { return script_hooks::current_script(); }
// Int stack slot `depth` from the top (written first when `write`); Integer.MIN_VALUE if none.
jint n_script_int_top(JNIEnv*, jclass, jint depth, jboolean write, jint value) {
    if (!client_thread::on_client_thread()) return INT32_MIN;
    int32_t out = 0, in = value;
    return game::script_int_top(depth, out, write ? &in : nullptr) ? out : INT32_MIN;
}
jstring n_script_string_top(JNIEnv* env, jclass) {
    std::string s;
    if (!client_thread::on_client_thread() || !game::script_string_top(s)) return nullptr;
    return env->NewStringUTF(s.c_str());
}
jboolean n_set_script_int_stack(JNIEnv* env, jclass, jintArray values, jint size) {
    if (!client_thread::on_client_thread() || !values || size < 0 || size > env->GetArrayLength(values)) return false;
    std::vector<int32_t> v(size);
    if (size) env->GetIntArrayRegion(values, 0, size, v.data());
    return game::set_script_int_stack(v.data(), size);
}

static jobjectArray script_args_to_java(JNIEnv* env, const std::vector<game::ScriptArg>& args) {
    jclass integer = env->FindClass("java/lang/Integer");
    jmethodID value_of = env->GetStaticMethodID(integer, "valueOf", "(I)Ljava/lang/Integer;");
    jobjectArray res = env->NewObjectArray(static_cast<jsize>(args.size()), env->FindClass("java/lang/Object"), nullptr);
    for (size_t i = 0; i < args.size(); i++) {
        jobject o = args[i].is_string ? static_cast<jobject>(env->NewStringUTF(args[i].s.c_str()))
                                      : env->CallStaticObjectMethod(integer, value_of, args[i].i);
        env->SetObjectArrayElement(res, static_cast<jsize>(i), o);
        env->DeleteLocalRef(o);
    }
    return res;
}

jobjectArray n_script_event_args(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    uintptr_t ev = script_hooks::current_event();
    if (!ev) return nullptr;
    return script_args_to_java(env, game::script_event_args(ev));
}

// A widget's stored CS2 listener (CC_SETON*/IF_SETON* slot, same {+8 count, +0x10 variants} layout
// as a ScriptEvent, docs/offsets.md) as {scriptId, args...}, or null when none is set. Offsets are
// whitelisted: onLoad, onDrag, onDragComplete, onVarTransmit, onInvTransmit, onStatTransmit, onTimer,
// onOp, onKey.
static uintptr_t widget_or_child(jint id, jint index);
jobjectArray n_widget_listener(JNIEnv* env, jclass, jint id, jint index, jint offset) {
    static constexpr int kSlots[] = {0x350, 0x4d0, 0x500, 0x590, 0x5d8, 0x620, 0x668, 0x698, 0x758};
    if (!client_thread::on_client_thread()) return nullptr;
    bool ok = false;
    for (int o : kSlots) ok |= o == offset;
    uintptr_t w = ok ? widget_or_child(id, index) : 0;
    if (!w || *reinterpret_cast<const uint64_t*>(w + offset + 8) == 0) return nullptr;
    auto args = game::script_event_args(w + offset);
    return args.empty() ? nullptr : script_args_to_java(env, args);
}

// Clears a widget's CS2 listener slot the way CC_SETON*/IF_SETON* with script -1 does (setter
// `0x4e4fe0`'s -1 path zeroes the slot's count `+8` and variants `+0x10`). The variants array is
// leaked, not freed (the game's path runs each variant's destructor, then frees it; a few bytes per
// plugin clear). Desktop's Widget.setOn*Listener writes the one listener slot, so a plugin's Java
// listener (or null) replaces the game's script there too.
// unverified: on the device (QH's bank tab clears 12:42's onTimer)
jboolean n_widget_clear_listener(JNIEnv*, jclass, jint id, jint index, jint offset) {
    static constexpr int kSlots[] = {0x4d0, 0x590, 0x668};  // onDrag, onVarTransmit, onTimer
    if (!client_thread::on_client_thread()) return false;
    bool ok = false;
    for (int o : kSlots) ok |= o == offset;
    uintptr_t w = ok ? widget_or_child(id, index) : 0;
    if (!w) return false;
    *reinterpret_cast<uint64_t*>(w + offset + 8) = 0;
    *reinterpret_cast<uint64_t*>(w + offset + 0x10) = 0;
    return true;
}

// A widget's transmit trigger list (the varps/invs/stats its onVarTransmit/onInvTransmit/
// onStatTransmit listener waits on): a std::vector<int32_t> {begin, end, cap} right after the
// listener slot (docs/offsets.md "Widget listeners"). null when unset.
// unverified: element type int32 (static RE says "vector"; checked only for a sane size here).
jintArray n_widget_triggers(JNIEnv* env, jclass, jint id, jint index, jint offset) {
    if (!client_thread::on_client_thread()) return nullptr;
    if (offset != 0x5c0 && offset != 0x608 && offset != 0x650) return nullptr;
    uintptr_t w = widget_or_child(id, index);
    if (!w) return nullptr;
    auto begin = *reinterpret_cast<const int32_t* const*>(w + offset);
    auto end = *reinterpret_cast<const int32_t* const*>(w + offset + 8);
    if (!begin || end < begin || end - begin > 256) return nullptr;
    jsize n = static_cast<jsize>(end - begin);
    jintArray a = env->NewIntArray(n);
    env->SetIntArrayRegion(a, 0, n, reinterpret_cast<const jint*>(begin));
    return a;
}

jintArray n_script_event_source(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) return nullptr;
    int id, index;
    if (!game::script_event_source(script_hooks::current_event(), &id, &index)) return nullptr;
    jint v[2] = {id, index};
    jintArray a = env->NewIntArray(2);
    env->SetIntArrayRegion(a, 0, 2, v);
    return a;
}

jint n_text_width(JNIEnv* env, jclass, jint font, jstring text) {
    if (!client_thread::on_client_thread() || !text) return -1;
    const char* c = env->GetStringUTFChars(text, nullptr);
    std::string s(c ? c : "");
    if (c) env->ReleaseStringUTFChars(text, c);
    return game::text_width(font, s);
}
jint n_font_ascent(JNIEnv*, jclass, jint font) {
    return client_thread::on_client_thread() ? game::font_ascent(font) : -1;
}
// {int[] opcodes, int[] int operands} of script `id` (script_hooks::script_code); null if not loaded.
jobjectArray n_script_code(JNIEnv* env, jclass, jint id) {
    if (!client_thread::on_client_thread()) return nullptr;
    std::vector<int32_t> ops, ints;
    if (!script_hooks::script_code(id, ops, ints)) return nullptr;
    jobjectArray out = env->NewObjectArray(2, env->FindClass("[I"), nullptr);
    int i = 0;
    for (auto* v : {&ops, &ints}) {
        jintArray a = env->NewIntArray(static_cast<jsize>(v->size()));
        env->SetIntArrayRegion(a, 0, static_cast<jsize>(v->size()), v->data());
        env->SetObjectArrayElement(out, i++, a);
        env->DeleteLocalRef(a);
    }
    return out;
}
jintArray n_script_active_widget(JNIEnv* env, jclass, jboolean dot) {
    if (!client_thread::on_client_thread()) return nullptr;
    int id, index;
    if (!game::script_active_widget(dot, &id, &index)) return nullptr;
    jint v[2] = {id, index};
    jintArray a = env->NewIntArray(2);
    env->SetIntArrayRegion(a, 0, 2, v);
    return a;
}

jlongArray n_script_stats(JNIEnv* env, jclass) {
    auto st = script_hooks::stats();
    jlong v[5] = {(jlong)st.roots, (jlong)st.gosubs, (jlong)st.returns, (jlong)st.delivered, script_hooks::installed()};
    jlongArray a = env->NewLongArray(5);
    env->SetLongArrayRegion(a, 0, 5, v);
    return a;
}
jintArray n_player_position(JNIEnv* env, jclass) {
    if (game::game_state() != 30) return nullptr;
    game::Point w = game::player_world(), s = game::player_local_tile(), l = game::player_fine();
    jint v[8] = {w.x, w.y, s.x, s.y, l.x, l.y, game::player_plane(), game::scene_plane()};
    jintArray arr = env->NewIntArray(8);
    env->SetIntArrayRegion(arr, 0, 8, v);
    return arr;
}

// NPC/player snapshots: 6 ints of identity/position, then the actor state (see Native.java).
constexpr int kActorStride = 36;
void pack_actor(std::vector<jint>& v, const game::ActorState& a) {
    v.insert(v.end(), {a.animation, a.animation_frame, a.orientation, a.current_orientation, a.interacting, a.overhead_cycle});
    v.insert(v.end(), a.pose, a.pose + 8);
    v.insert(v.end(), {a.pose_animation, a.pose_frame, a.health_ratio, a.health_scale});
    v.insert(v.end(), a.hitsplats, a.hitsplats + 12);
}
jstring latin1_string(JNIEnv* env, const std::string& s);
void set_overhead(JNIEnv* env, jobjectArray arr, jsize i, const game::ActorState& a) {
    if (a.overhead_text.empty()) return;
    jstring t = latin1_string(env, a.overhead_text);
    env->SetObjectArrayElement(arr, i, t);
    env->DeleteLocalRef(t);
}

jobjectArray n_npcs(JNIEnv* env, jclass) {
    std::vector<game::Npc> npcs = client_thread::npcs();
    jsize n = static_cast<jsize>(npcs.size());
    std::vector<jint> packed;
    packed.reserve(n * kActorStride);
    jobjectArray names = env->NewObjectArray(n, env->FindClass("java/lang/String"), nullptr);
    jobjectArray overheads = env->NewObjectArray(n, env->FindClass("java/lang/String"), nullptr);
    for (jsize i = 0; i < n; i++) {
        const game::Npc& npc = npcs[i];
        packed.insert(packed.end(), {npc.index, npc.id, npc.fine_x, npc.fine_y, npc.world_x, npc.world_y});
        pack_actor(packed, npc.actor);
        jstring name = env->NewStringUTF(npc.name.c_str());
        env->SetObjectArrayElement(names, i, name);
        env->DeleteLocalRef(name);
        set_overhead(env, overheads, i, npc.actor);
    }
    jintArray ints = env->NewIntArray(n * kActorStride);
    env->SetIntArrayRegion(ints, 0, n * kActorStride, packed.data());
    jobjectArray out = env->NewObjectArray(3, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, ints);
    env->SetObjectArrayElement(out, 1, names);
    env->SetObjectArrayElement(out, 2, overheads);
    return out;
}

// Game strings are CP1252/Latin-1 bytes; JNI wants (modified) UTF-8.
jstring latin1_string(JNIEnv* env, const std::string& s) {
    std::string u;
    u.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 0x80) {
            u.push_back(static_cast<char>(0xc0 | (c >> 6)));
            u.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else if (c) {
            u.push_back(static_cast<char>(c));
        }
    }
    return env->NewStringUTF(u.c_str());
}

// {String[] names, String[] previous names (null when none), int[] {world, rank} pairs}, or null
// while the list isn't loaded.
// {String[] names, int[] worlds} of the pending friend login/logout notices; null off the game thread.
jobjectArray n_pending_logins(JNIEnv* env, jclass) {
    std::vector<game::PendingLogin> list;
    if (!client_thread::on_client_thread() || !game::pending_logins(list)) return nullptr;
    jsize n = static_cast<jsize>(list.size());
    jobjectArray names = env->NewObjectArray(n, env->FindClass("java/lang/String"), nullptr);
    std::vector<jint> worlds;
    for (jsize i = 0; i < n; i++) {
        jstring t = latin1_string(env, list[i].name);
        env->SetObjectArrayElement(names, i, t);
        env->DeleteLocalRef(t);
        worlds.push_back(list[i].world);
    }
    jintArray w = env->NewIntArray(n);
    env->SetIntArrayRegion(w, 0, n, worlds.data());
    jobjectArray out = env->NewObjectArray(2, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, names);
    env->SetObjectArrayElement(out, 1, w);
    return out;
}
// Per equipment slot (12) a {short[] recolour-to, short[] retexture-to} pair or null, of a player's appearance;
// null off the game thread or before the first appearance.
jobjectArray n_player_overrides(JNIEnv* env, jclass, jint index) {
    if (!client_thread::on_client_thread()) return nullptr;
    game::ObjOverride o[12];
    if (!game::appearance_overrides(game::live_player(index), o)) return nullptr;
    jclass pair = env->FindClass("[[S"), shorts = env->FindClass("[S");
    jobjectArray out = env->NewObjectArray(12, pair, nullptr);
    for (int i = 0; i < 12; i++) {
        if (!o[i].present) continue;
        jobjectArray p = env->NewObjectArray(2, shorts, nullptr);
        int k = 0;
        for (auto* v : {&o[i].recolour, &o[i].retexture}) {
            jshortArray a = env->NewShortArray(static_cast<jsize>(v->size()));
            env->SetShortArrayRegion(a, 0, static_cast<jsize>(v->size()), v->data());
            env->SetObjectArrayElement(p, k++, a);
            env->DeleteLocalRef(a);
        }
        env->SetObjectArrayElement(out, i, p);
        env->DeleteLocalRef(p);
    }
    return out;
}
jobjectArray n_friends(JNIEnv* env, jclass, jboolean ignores) {
    std::vector<game::FriendEntry> list;
    if (!game::friend_list(ignores, list)) return nullptr;
    jsize n = static_cast<jsize>(list.size());
    jclass str = env->FindClass("java/lang/String");
    jobjectArray names = env->NewObjectArray(n, str, nullptr), prevs = env->NewObjectArray(n, str, nullptr);
    std::vector<jint> wr;
    for (jsize i = 0; i < n; i++) {
        jstring t = latin1_string(env, list[i].name);
        env->SetObjectArrayElement(names, i, t);
        env->DeleteLocalRef(t);
        if (!list[i].prev.empty()) {
            t = latin1_string(env, list[i].prev);
            env->SetObjectArrayElement(prevs, i, t);
            env->DeleteLocalRef(t);
        }
        wr.push_back(list[i].world);
        wr.push_back(list[i].rank);
    }
    jintArray ints = env->NewIntArray(2 * n);
    env->SetIntArrayRegion(ints, 0, 2 * n, wr.data());
    jobjectArray out = env->NewObjectArray(3, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, names);
    env->SetObjectArrayElement(out, 1, prevs);
    env->SetObjectArrayElement(out, 2, ints);
    return out;
}
jint n_friend_list_state(JNIEnv*, jclass) { return game::friend_list_state(); }
jintArray n_target_mode(JNIEnv* env, jclass) {
    int v[3];
    if (!game::target_mode(v)) return nullptr;
    jintArray a = env->NewIntArray(3);
    if (a) env->SetIntArrayRegion(a, 0, 3, reinterpret_cast<const jint*>(v));
    return a;
}
jboolean n_end_target_mode(JNIEnv*, jclass) { return game::end_target_mode(); }
jbyteArray n_target_state(JNIEnv* env, jclass, jint len) {
    std::vector<uint8_t> b = game::target_state_bytes(len);
    if (b.empty()) return nullptr;
    jbyteArray a = env->NewByteArray(static_cast<jsize>(b.size()));
    if (a) env->SetByteArrayRegion(a, 0, static_cast<jsize>(b.size()), reinterpret_cast<const jbyte*>(b.data()));
    return a;
}
jboolean n_close_sub_interface(JNIEnv*, jclass, jint component, jint group, jboolean unload) {
    return client_thread::on_client_thread() && game::close_sub_interface(component, group, unload);
}
jboolean n_open_sub_interface(JNIEnv*, jclass, jint component, jint group, jint mode) {
    return client_thread::on_client_thread() && game::open_sub_interface(component, group, mode);
}
jintArray n_draw_minimap(JNIEnv* env, jclass, jint plane) {
    std::vector<int32_t> px;
    if (!client_thread::on_client_thread() || !game::draw_minimap(plane, px)) return nullptr;
    jintArray a = env->NewIntArray(static_cast<jsize>(px.size()));
    env->SetIntArrayRegion(a, 0, static_cast<jsize>(px.size()), px.data());
    return a;
}
jintArray n_sub_interfaces(JNIEnv* env, jclass) {
    std::vector<jint> v;
    for (const auto& s : game::open_sub_interfaces()) v.insert(v.end(), {s.component, s.group, s.type});
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
jboolean n_reload_scene(JNIEnv*, jclass) { return client_thread::on_client_thread() && game::reload_scene(); }
bool require_client_thread(JNIEnv* env, const char* what);
jboolean n_remove_ground_object(JNIEnv* env, jclass, jint plane, jint x, jint y) {
    return require_client_thread(env, "removeGroundObject") && game::remove_ground_decoration(plane, x, y);
}
// game::scene_terrain's packing, or null.
jintArray n_scene_terrain(JNIEnv* env, jclass, jint what, jint plane, jint x, jint y) {
    if (!require_client_thread(env, "sceneTerrain")) return nullptr;
    std::vector<int32_t> v;
    if (!game::scene_terrain(what, plane, x, y, v)) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    if (arr) env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
jboolean n_scene_set(JNIEnv* env, jclass, jint what, jint plane, jint x, jint y, jlong ptr, jint field, jint value) {
    return require_client_thread(env, "sceneSet") && game::scene_set(what, plane, x, y, static_cast<uint64_t>(ptr), field, value);
}
// Per ambient sound: obj low, obj high, sound id, plane, n, n background ids.
jintArray n_ambient_sounds(JNIEnv* env, jclass) {
    if (!require_client_thread(env, "ambientSounds")) return nullptr;
    std::vector<jint> packed;
    for (const auto& a : game::ambient_sounds()) {
        packed.push_back(static_cast<jint>(a.obj & 0xffffffffu));
        packed.push_back(static_cast<jint>(static_cast<uint64_t>(a.obj) >> 32));
        packed.push_back(a.id);
        packed.push_back(a.plane);
        packed.push_back(static_cast<jint>(a.background.size()));
        packed.insert(packed.end(), a.background.begin(), a.background.end());
    }
    jintArray out = env->NewIntArray(static_cast<jsize>(packed.size()));
    if (out && !packed.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(packed.size()), packed.data());
    return out;
}
// Per projectile (kProjectileInts): obj low, obj high, id, source level/x/y, start height, source
// index, target level/x/y, end height, target index, start/end cycle, slope, start pos, orientation,
// animation, frame, then x, y, z as raw double bits (low, high each).
jintArray n_projectiles(JNIEnv* env, jclass) {
    if (!require_client_thread(env, "projectiles")) return nullptr;
    std::vector<jint> v;
    auto bits = [&v](double d) {
        uint64_t b;
        memcpy(&b, &d, 8);
        v.push_back(static_cast<jint>(b & 0xffffffffu));
        v.push_back(static_cast<jint>(b >> 32));
    };
    for (const auto& p : game::projectiles()) {
        v.push_back(static_cast<jint>(p.obj & 0xffffffffu));
        v.push_back(static_cast<jint>(static_cast<uint64_t>(p.obj) >> 32));
        v.insert(v.end(), {p.id, p.source_level, p.source_x, p.source_y, p.start_height, p.source_index,
                           p.target_level, p.target_x, p.target_y, p.end_height, p.target_index,
                           p.start_cycle, p.end_cycle, p.slope, p.start_pos, p.orientation,
                           p.animation, p.animation_frame});
        bits(p.x);
        bits(p.y);
        bits(p.z);
    }
    jintArray out = env->NewIntArray(static_cast<jsize>(v.size()));
    if (out && !v.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
    return out;
}
// Per graphics object (32 ints): obj low, obj high, id, level, x, y, z, start cycle, raw[24] (+0x240..).
jintArray n_graphics_objects(JNIEnv* env, jclass) {
    if (!require_client_thread(env, "graphicsObjects")) return nullptr;
    std::vector<jint> v;
    for (const auto& g : game::graphics_objects()) {
        v.push_back(static_cast<jint>(g.obj & 0xffffffffu));
        v.push_back(static_cast<jint>(static_cast<uint64_t>(g.obj) >> 32));
        v.insert(v.end(), {g.id, g.level, g.x, g.y, g.z, g.start_cycle});
        v.insert(v.end(), g.raw, g.raw + 24);
    }
    jintArray out = env->NewIntArray(static_cast<jsize>(v.size()));
    if (out && !v.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
    return out;
}
jboolean n_graphics_object_finish(JNIEnv* env, jclass, jlong obj) {
    return require_client_thread(env, "graphicsObjectFinish") && game::graphics_object_finish(static_cast<uintptr_t>(obj));
}
jboolean n_projectile_set_end_cycle(JNIEnv* env, jclass, jlong obj, jint cycle) {
    return require_client_thread(env, "projectileSetEndCycle") && game::projectile_set_end_cycle(static_cast<uintptr_t>(obj), cycle);
}
jboolean n_set_all_op_targetable(JNIEnv* env, jclass, jboolean on) {
    return require_client_thread(env, "setAllOpTargetable") && game::set_all_op_targetable(on);
}
jlong n_widget_target_flags(JNIEnv* env, jclass, jint component, jint index) {
    return require_client_thread(env, "widgetTargetFlags") ? game::widget_target_flags(component, index) : -1;
}
jboolean n_widget_force_position(JNIEnv* env, jclass, jint component, jint index, jint x, jint y) {
    return require_client_thread(env, "widgetForcePosition") && game::widget_force_position(component, index, x, y);
}
jlong n_player_set_hash(JNIEnv* env, jclass, jint index, jintArray equipment, jintArray colors, jint npc) {
    if (!require_client_thread(env, "playerSetHash") || !equipment || !colors || env->GetArrayLength(equipment) != 12 || env->GetArrayLength(colors) != 5) return 0;
    int32_t e[12], c[5];
    env->GetIntArrayRegion(equipment, 0, 12, reinterpret_cast<jint*>(e));
    env->GetIntArrayRegion(colors, 0, 5, reinterpret_cast<jint*>(c));
    return static_cast<jlong>(game::appearance_set_hash(index, e, c, npc));
}
jlong n_player_hash(JNIEnv* env, jclass, jint index) {
    return require_client_thread(env, "playerHash") ? static_cast<jlong>(game::appearance_hash(index)) : 0;
}
jboolean n_scene_remove_game_object(JNIEnv* env, jclass, jint plane, jint x, jint y, jlong tag) {
    return require_client_thread(env, "sceneRemoveGameObject") && game::scene_remove_game_object(plane, x, y, static_cast<uint64_t>(tag));
}
jboolean n_scene_remove_tile(JNIEnv* env, jclass, jint plane, jint x, jint y, jboolean remove) {
    return require_client_thread(env, "sceneRemoveTile") && game::scene_remove_tile(plane, x, y, remove);
}
jlong n_projectile_create(JNIEnv* env, jclass, jintArray in) {
    if (!require_client_thread(env, "projectileCreate") || !in || env->GetArrayLength(in) != 15) return 0;
    int32_t s[15];
    env->GetIntArrayRegion(in, 0, 15, reinterpret_cast<jint*>(s));
    return static_cast<jlong>(game::projectile_create(s));
}
// size*size collision flags of a plane (scene x major), or null.
jintArray n_collision_flags(JNIEnv* env, jclass, jint plane, jint size) {
    if (!require_client_thread(env, "collisionFlags")) return nullptr;
    std::vector<int32_t> f;
    if (!game::collision_flags(plane, size, f)) return nullptr;
    jintArray out = env->NewIntArray(static_cast<jsize>(f.size()));
    if (out) env->SetIntArrayRegion(out, 0, static_cast<jsize>(f.size()), f.data());
    return out;
}
jboolean n_ambient_clear(JNIEnv* env, jclass) { return require_client_thread(env, "ambientClear") && game::ambient_clear(); }
jboolean n_ambient_remove(JNIEnv* env, jclass, jlong obj) {
    return require_client_thread(env, "ambientRemove") && game::ambient_remove(static_cast<uintptr_t>(obj));
}
jboolean n_ambient_restore(JNIEnv* env, jclass, jlong obj) {
    return require_client_thread(env, "ambientRestore") && game::ambient_restore(static_cast<uintptr_t>(obj));
}

constexpr int kMenuInts = 12;  // per entry: kind, hasChildren, raw[10]
jobjectArray menu_array(JNIEnv* env, const std::vector<game::MenuEntry>& entries) {
    jsize n = static_cast<jsize>(entries.size());
    std::vector<jint> packed;
    jobjectArray strs = env->NewObjectArray(n * 2, env->FindClass("java/lang/String"), nullptr);
    for (jsize i = 0; i < n; i++) {
        const game::MenuEntry& e = entries[i];
        packed.push_back(e.kind);
        packed.push_back(e.has_children);
        packed.insert(packed.end(), e.raw, e.raw + 10);
        jstring o = latin1_string(env, e.option), t = latin1_string(env, e.target);
        env->SetObjectArrayElement(strs, i * 2, o);
        env->SetObjectArrayElement(strs, i * 2 + 1, t);
        env->DeleteLocalRef(o);
        env->DeleteLocalRef(t);
    }
    jintArray ints = env->NewIntArray(n * kMenuInts);
    if (n) env->SetIntArrayRegion(ints, 0, n * kMenuInts, packed.data());
    jobjectArray out = env->NewObjectArray(2, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, ints);
    env->SetObjectArrayElement(out, 1, strs);
    return out;
}

// {int[] packed (kMenuInts per entry), String[] {option, target} per entry}; game thread only.
jobjectArray n_menu_entries(JNIEnv* env, jclass) {
    if (!client_thread::on_client_thread()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "getMenuEntries must be called on the client thread");
        return nullptr;
    }
    uintptr_t tap = client_thread::tap_menu();
    return menu_array(env, tap ? game::read_menu(tap) : game::read_menu());
}
void n_redirect_menu_tap(JNIEnv*, jclass, jint index) { client_thread::redirect_menu_tap(index); }
// Game thread: builds the long-press menu at (x, y) (UI canvas units) without showing it.
jboolean n_menu_build_at(JNIEnv*, jclass, jint x, jint y) {
    return client_thread::on_client_thread() && game::menu_build_at(x, y) ? JNI_TRUE : JNI_FALSE;
}
void n_pick_at(JNIEnv*, jclass, jint x, jint y) {
    if (client_thread::on_client_thread()) game::pick_at(x, y);
}
jboolean n_pick_ready(JNIEnv*, jclass) { return game::pick_ready() ? JNI_TRUE : JNI_FALSE; }
void n_menu_discard(JNIEnv*, jclass) {
    if (client_thread::on_client_thread()) game::menu_discard();
}
jboolean n_menu_select(JNIEnv*, jclass, jint index) {
    return client_thread::select_menu_node(index) ? JNI_TRUE : JNI_FALSE;
}
void n_request_frame(JNIEnv*, jclass) { frame_capture_request(); }
// {width, height, argb...} of the frame captured since requestFrame(), or null.
jintArray n_take_frame(JNIEnv* env, jclass) {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (!frame_capture_take(px, w, h)) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(px.size() + 2));
    if (!arr) return nullptr;
    jint wh[2] = {w, h};
    env->SetIntArrayRegion(arr, 0, 2, wh);
    env->SetIntArrayRegion(arr, 2, static_cast<jsize>(px.size()), reinterpret_cast<const jint*>(px.data()));
    return arr;
}
// Game thread: reorders the open menu (order[i] = old top-first index shown at position i).
jboolean n_menu_reorder(JNIEnv* env, jclass, jintArray order) {
    if (!order || !client_thread::on_client_thread()) return JNI_FALSE;
    jsize n = env->GetArrayLength(order);
    std::vector<int> v(n);
    env->GetIntArrayRegion(order, 0, n, v.data());
    return game::menu_reorder(v) ? JNI_TRUE : JNI_FALSE;
}
jboolean n_menu_tapping(JNIEnv*, jclass) { return client_thread::tap_menu() != 0; }
jobjectArray n_clicked_menu_entry(JNIEnv* env, jclass) {
    return menu_array(env, {client_thread::clicked_menu_entry()});
}
jboolean n_menu_open(JNIEnv*, jclass) { return game::menu_open(); }
bool require_client_thread(JNIEnv* env, const char* what);
// {width, height, pixels...} (ARGB, 0 = transparent) or null while the model isn't loaded.
jintArray n_item_sprite(JNIEnv* env, jclass, jint id, jint quantity, jint border, jint shadow, jint text_mode, jboolean noted) {
    if (!require_client_thread(env, "createItemSprite")) return nullptr;
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (!game::item_sprite(id, quantity, border, shadow, text_mode, noted, px, w, h)) return nullptr;
    jintArray r = env->NewIntArray(2 + static_cast<jsize>(px.size()));
    jint wh[2] = {w, h};
    env->SetIntArrayRegion(r, 0, 2, wh);
    env->SetIntArrayRegion(r, 2, static_cast<jsize>(px.size()), reinterpret_cast<const jint*>(px.data()));
    return r;
}

jintArray n_sprite(JNIEnv* env, jclass, jint id) {
    if (!require_client_thread(env, "sprite")) return nullptr;
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (!game::sprite(id, px, w, h)) return nullptr;
    jintArray r = env->NewIntArray(2 + static_cast<jsize>(px.size()));
    jint wh[2] = {w, h};
    env->SetIntArrayRegion(r, 0, 2, wh);
    env->SetIntArrayRegion(r, 2, static_cast<jsize>(px.size()), reinterpret_cast<const jint*>(px.data()));
    return r;
}

jintArray n_sprite_info(JNIEnv* env, jclass, jint id) {
    if (!require_client_thread(env, "spriteInfo")) return nullptr;
    int v[6];
    if (!game::sprite_info(id, v)) return nullptr;
    jintArray r = env->NewIntArray(6);
    env->SetIntArrayRegion(r, 0, 6, reinterpret_cast<const jint*>(v));
    return r;
}

static jintArray int_array(JNIEnv* env, const std::vector<int32_t>& v) {
    jintArray r = env->NewIntArray(static_cast<jsize>(v.size()));
    if (r) env->SetIntArrayRegion(r, 0, static_cast<jsize>(v.size()), reinterpret_cast<const jint*>(v.data()));
    return r;
}
jintArray n_sprite_array(JNIEnv* env, jclass, jint which) {
    std::vector<int32_t> v;
    if (!client_thread::on_client_thread() || !game::sprite_array(which, v)) return nullptr;
    return int_array(env, v);
}
jintArray n_textures(JNIEnv* env, jclass) {
    std::vector<int32_t> v;
    if (!client_thread::on_client_thread() || !game::textures(v)) return nullptr;
    return int_array(env, v);
}
jintArray n_texture_pixels(JNIEnv* env, jclass, jint id, jboolean load) {
    std::vector<int32_t> v;
    if (!client_thread::on_client_thread() || !game::texture_pixels(id, load, v)) return nullptr;
    return int_array(env, v);
}
jint n_skybox_colour(JNIEnv*, jclass) {
    int v = 0;
    return client_thread::on_client_thread() && game::skybox_colour(v) ? v : 0;
}
jboolean n_set_skybox_colour(JNIEnv*, jclass, jint argb) {
    return client_thread::on_client_thread() && game::set_skybox_colour(argb);
}
jdouble n_texture_brightness(JNIEnv*, jclass) {
    return client_thread::on_client_thread() ? game::texture_brightness() : __builtin_nan("");
}
jboolean n_set_texture_brightness(JNIEnv*, jclass, jdouble b) {
    return client_thread::on_client_thread() && game::set_texture_brightness(b);
}

jboolean n_sprite_write(JNIEnv* env, jclass, jint id, jintArray pixels, jint w, jint h) {
    if (!client_thread::on_client_thread() || !pixels || w <= 0 || h <= 0 || env->GetArrayLength(pixels) < w * h) return false;
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    env->GetIntArrayRegion(pixels, 0, w * h, reinterpret_cast<jint*>(px.data()));
    return game::sprite_write(id, px.data(), w, h);
}

jintArray n_menu_point(JNIEnv* env, jclass) {
    int xy[2];
    if (!game::menu_point(xy[0], xy[1])) return nullptr;
    jintArray r = env->NewIntArray(2);
    env->SetIntArrayRegion(r, 0, 2, xy);
    return r;
}

bool require_client_thread(JNIEnv* env, const char* what) {
    if (client_thread::on_client_thread()) return true;
    std::string msg = std::string(what) + " must be called on the client thread";
    env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), msg.c_str());
    return false;
}

// 5 ints per object: plane << 24 | layer << 16 | x << 8 | y, id, tag low, tag high, size_x << 8 | size_y.
jintArray n_scene_objects(JNIEnv* env, jclass) {
    if (!require_client_thread(env, "sceneObjects")) return nullptr;
    auto objs = game::scene_objects();
    std::vector<jint> packed;
    packed.reserve(objs.size() * 5);
    for (const auto& o : objs) {
        packed.push_back(o.plane << 24 | o.layer << 16 | o.x << 8 | o.y);
        packed.push_back(o.id);
        packed.push_back(static_cast<jint>(o.tag & 0xffffffffu));
        packed.push_back(static_cast<jint>(o.tag >> 32));
        packed.push_back(o.size_x << 8 | o.size_y);
    }
    jintArray out = env->NewIntArray(static_cast<jsize>(packed.size()));
    if (out && !packed.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(packed.size()), packed.data());
    return out;
}

// 7 ints per item: plane << 16 | x << 8 | y, id, quantity, visible tick, despawn tick, ownership,
// private.
jintArray n_ground_items(JNIEnv* env, jclass) {
    if (!require_client_thread(env, "groundItems")) return nullptr;
    auto items = game::ground_items();
    std::vector<jint> packed;
    packed.reserve(items.size() * 7);
    for (const auto& i : items) {
        packed.push_back(i.plane << 16 | i.x << 8 | i.y);
        packed.push_back(i.id);
        packed.push_back(i.quantity);
        packed.push_back(i.visible_tick);
        packed.push_back(i.despawn_tick);
        packed.push_back(i.ownership);
        packed.push_back(i.is_private ? 1 : 0);
    }
    jintArray out = env->NewIntArray(static_cast<jsize>(packed.size()));
    if (out && !packed.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(packed.size()), packed.data());
    return out;
}
void n_consume_menu_click(JNIEnv*, jclass) { client_thread::consume_menu_click(); }
void n_consume_sound(JNIEnv*, jclass) { client_thread::consume_sound(); }
// {id, sceneX, sceneY, range, delay, loops, sourceKind (0 none, 1 NPC, 2 player), sourceIndex, plane, fromAnimation}
jintArray n_area_sound(JNIEnv* env, jclass) {
    const auto& s = client_thread::last_area_sound();
    jint v[10] = {s.id, s.scene_x, s.scene_y, s.range, s.delay, s.loops, s.source_kind, s.source_index, s.plane, s.from_animation ? 1 : 0};
    jintArray out = env->NewIntArray(10);
    if (out) env->SetIntArrayRegion(out, 0, 10, v);
    return out;
}
namespace {
std::string to_latin1(JNIEnv* env, jstring text) {
    std::string out;
    const jchar* c = env->GetStringChars(text, nullptr);
    jsize n = env->GetStringLength(text);
    for (jsize i = 0; i < n; i++) out.push_back(c[i] < 256 ? static_cast<char>(c[i]) : '?');
    env->ReleaseStringChars(text, c);
    return out;
}
}  // namespace
jboolean n_menu_set_text(JNIEnv* env, jclass, jint index, jstring option, jstring target) {
    if (!require_client_thread(env, "menuSetText") || index < 0) return false;
    std::string o, t;
    if (option) o = to_latin1(env, option);
    if (target) t = to_latin1(env, target);
    return game::menu_set_text(static_cast<size_t>(index), option ? &o : nullptr, target ? &t : nullptr);
}


// {name, sender, text}, or null if the message is no longer kept.
jobjectArray n_message(JNIEnv* env, jclass, jint id) {
    game::ChatLine line;
    // Not captured yet (a message made earlier in this cycle): read the game's on the client thread.
    if (!client_thread::message(id, line) && !(client_thread::on_client_thread() && game::read_message(id, line)))
        return nullptr;
    jobjectArray out = env->NewObjectArray(3, env->FindClass("java/lang/String"), nullptr);
    const std::string* parts[] = {&line.name, &line.sender, &line.text};
    for (int i = 0; i < 3; i++) {
        jstring str = env->NewStringUTF(parts[i]->c_str());
        env->SetObjectArrayElement(out, i, str);
        env->DeleteLocalRef(str);
    }
    return out;
}

// Walk destination {x, y} as stored (Client +0xecdc/+0xece0; scene tiles, -1 when none).
jintArray n_walk_destination(JNIEnv* env, jclass) {
    game::Point p = game::walk_destination();
    jint v[2] = {p.x, p.y};
    jintArray arr = env->NewIntArray(2);
    env->SetIntArrayRegion(arr, 0, 2, v);
    return arr;
}

// A definition param: Integer, String, or null when the definition has no such param.
jobject n_type_param(JNIEnv* env, jclass, jint kind, jint id, jint key) {
    if (!require_client_thread(env, "typeParam")) return nullptr;
    game::TypeParam p;
    if (!game::type_param(kind, id, key, p)) return nullptr;
    if (p.is_string) return latin1_string(env, p.string_value);
    jclass integer = env->FindClass("java/lang/Integer");
    return env->CallStaticObjectMethod(integer, env->GetStaticMethodID(integer, "valueOf", "(I)Ljava/lang/Integer;"), p.int_value);
}

jintArray n_screen_size(JNIEnv* env, jclass) {
    jint v[2];
    overlay_screen_size(&v[0], &v[1]);
    jintArray arr = env->NewIntArray(2);
    env->SetIntArrayRegion(arr, 0, 2, v);
    return arr;
}

jobject n_overlay_buffer(JNIEnv* env, jclass, jint w, jint h) {
    uint8_t* p = overlay_back_buffer(w, h);
    return p ? env->NewDirectByteBuffer(p, static_cast<jlong>(w) * h * 4) : nullptr;
}

void n_overlay_publish(JNIEnv*, jclass, jint w, jint h) { overlay_publish(w, h); }

jintArray n_camera(JNIEnv* env, jclass) {
    game::Camera c = game::camera();
    jint v[13] = {c.x, c.y, c.z, c.pitch, c.yaw, c.focal_x, c.focal_y, c.focal_z,
                  c.render_base_x, c.render_base_y, c.render_wv_same, c.pitch_target, c.yaw_target};
    jintArray arr = env->NewIntArray(13);
    env->SetIntArrayRegion(arr, 0, 13, v);
    return arr;
}

jboolean n_set_camera_target(JNIEnv*, jclass, jint which, jint value) {
    if (!client_thread::on_client_thread()) return false;
    return game::set_camera_target(which, value);
}

jboolean n_set_camera_focal(JNIEnv*, jclass, jint axis, jint value) {
    if (!client_thread::on_client_thread()) return false;
    return game::set_camera_focal(axis, value);
}

// {valid, zoom, centreX, centreY, clip x0, y0, x1, y1, far, scene camera x/h/y, sin/cos pitch, sin/cos yaw x65536} (game thread only; valid 0 otherwise).
jintArray n_viewport(JNIEnv* env, jclass) {
    game::Viewport v;
    if (client_thread::on_client_thread()) v = game::viewport();
    jint a[16] = {v.valid, v.zoom, v.centre_x, v.centre_y, v.clip[0], v.clip[1], v.clip[2], v.clip[3], v.far,
                  v.scene_cam[0], v.scene_cam[1], v.scene_cam[2], v.trig[0], v.trig[1], v.trig[2], v.trig[3]};
    jintArray arr = env->NewIntArray(16);
    env->SetIntArrayRegion(arr, 0, 16, a);
    return arr;
}

jboolean n_play_sound(JNIEnv*, jclass, jint id, jint loops, jint delay) {
    return client_thread::on_client_thread() && game::play_sound_effect(id, loops, delay);
}

jstring n_property(JNIEnv* env, jclass, jstring name) {
    const char* n = env->GetStringUTFChars(name, nullptr);
    char value[PROP_VALUE_MAX] = "";
    __system_property_get(n, value);
    env->ReleaseStringUTFChars(name, n);
    return env->NewStringUTF(value);
}

jboolean n_widget_exists(JNIEnv*, jclass, jint id) { return game::widget(id) != 0; }
jboolean n_widget_hidden(JNIEnv*, jclass, jint id) { return game::widget_hidden(game::widget(id)); }
jstring n_widget_text(JNIEnv* env, jclass, jint id) {
    uintptr_t w = game::widget(id);
    return w ? env->NewStringUTF(game::widget_text(w).c_str()) : nullptr;
}
jint n_root_interface(JNIEnv*, jclass) { return game::root_interface(); }
// {x, y, width, height, parentId}, or null if the widget isn't loaded.
jintArray n_widget_geometry(JNIEnv* env, jclass, jint id) {
    uintptr_t w = game::widget(id);
    if (!w) return nullptr;
    game::WidgetGeometry g = game::widget_geometry(w);
    jint v[5] = {g.x, g.y, g.width, g.height, g.parent};
    jintArray arr = env->NewIntArray(5);
    env->SetIntArrayRegion(arr, 0, 5, v);
    return arr;
}
jint n_widget_child_count(JNIEnv*, jclass, jint group) { return game::widget_child_count(group); }
// {itemId, quantity} of a component (index < 0) or of its CC_CREATE child `index`; null if absent.
jintArray n_widget_item(JNIEnv* env, jclass, jint id, jint index) {
    uintptr_t w = game::widget(id);
    if (w && index >= 0) w = game::widget_dynamic(w, index);
    if (!w) return nullptr;
    game::WidgetItem it = game::widget_item(w);
    jint v[2] = {it.id, it.quantity};
    jintArray arr = env->NewIntArray(2);
    env->SetIntArrayRegion(arr, 0, 2, v);
    return arr;
}

static uintptr_t widget_or_child(jint id, jint index) {
    uintptr_t w = game::widget(id);
    return w && index >= 0 ? game::widget_dynamic(w, index) : w;
}
jboolean n_widget_set_field(JNIEnv*, jclass, jint id, jint index, jint offset, jint value) {
    return client_thread::on_client_thread() && game::widget_set_field(id, index, static_cast<uint32_t>(offset), value);
}
jint n_widget_get_field(JNIEnv*, jclass, jint id, jint index, jint offset) {
    return game::widget_get_field(id, index, static_cast<uint32_t>(offset));
}
// {component, index} of the drag parent (index -1 for a static component), or null if none.
jintArray n_widget_drag_parent(JNIEnv* env, jclass, jint id, jint index) {
    int v[2];
    if (!game::widget_drag_parent(id, index, &v[0], &v[1])) return nullptr;
    jintArray arr = env->NewIntArray(2);
    env->SetIntArrayRegion(arr, 0, 2, v);
    return arr;
}
jboolean n_widget_set_drag_parent(JNIEnv*, jclass, jint id, jint index, jint parent, jint parentIndex) {
    return client_thread::on_client_thread() && game::widget_set_drag_parent(id, index, parent, parentIndex);
}
jint n_widget_create_child(JNIEnv*, jclass, jint id, jint index, jint type) {
    return client_thread::on_client_thread() ? game::widget_create_child(id, index, type) : -1;
}
jboolean n_widget_truncate_children(JNIEnv*, jclass, jint id, jint count) {
    return client_thread::on_client_thread() && game::widget_truncate_children(id, count);
}
jboolean n_widget_delete_children(JNIEnv*, jclass, jint id) {
    return client_thread::on_client_thread() && game::widget_delete_children(id);
}
jboolean n_widget_revalidate(JNIEnv*, jclass, jint id, jint index) {
    return client_thread::on_client_thread() && game::widget_revalidate(id, index);
}
jintArray n_widget_props(JNIEnv* env, jclass, jint id, jint index) {
    uintptr_t w = widget_or_child(id, index);
    if (!w) return nullptr;
    int32_t v[game::kWidgetProps];
    game::widget_props(w, v);
    jintArray arr = env->NewIntArray(game::kWidgetProps);
    env->SetIntArrayRegion(arr, 0, game::kWidgetProps, v);
    return arr;
}
// {name, targetVerb, op1..opN} (null entries for empty strings), or null.
jobjectArray n_widget_strings(JNIEnv* env, jclass, jint id, jint index) {
    uintptr_t w = widget_or_child(id, index);
    if (!w) return nullptr;
    std::vector<std::string> v = game::widget_strings(w);
    jobjectArray arr = env->NewObjectArray(static_cast<jsize>(v.size()), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < v.size(); i++)
        if (!v[i].empty()) {
            jstring js = latin1_string(env, v[i]);
            env->SetObjectArrayElement(arr, static_cast<jsize>(i), js);
            env->DeleteLocalRef(js);
        }
    return arr;
}
void n_widget_set_hidden(JNIEnv*, jclass, jint id, jint index, jboolean hidden) {
    if (client_thread::on_client_thread()) game::widget_set_hidden(widget_or_child(id, index), hidden);
}
jboolean n_actor_set_overhead(JNIEnv* env, jclass, jboolean player, jint index, jstring text, jint cycle) {
    if (!client_thread::on_client_thread()) return false;
    uintptr_t a = player ? game::live_player(index) : game::live_npc(index);
    std::string t;
    if (text) t = to_latin1(env, text);
    return game::actor_set_overhead(a, text ? &t : nullptr, cycle);
}
static uintptr_t live_actor(jboolean player, jint index) {
    return player ? game::live_player(index) : game::live_npc(index);
}
jboolean n_actor_set(JNIEnv*, jclass, jboolean player, jint index, jint what, jint value) {
    return client_thread::on_client_thread() && game::actor_set(live_actor(player, index), player, what, value);
}
// {slot, id, height, start cycle, frame, frame cycle} per spot anim, or null without the actor.
jintArray n_actor_spot_anims(JNIEnv* env, jclass, jboolean player, jint index) {
    if (!client_thread::on_client_thread()) return nullptr;
    std::vector<int32_t> v;
    if (!game::actor_spot_anims(live_actor(player, index), v)) return nullptr;
    jintArray arr = env->NewIntArray(static_cast<jsize>(v.size()));
    if (!v.empty()) env->SetIntArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}
jboolean n_actor_spot_anim_set(JNIEnv*, jclass, jboolean player, jint index, jint slot, jint field, jint value) {
    return client_thread::on_client_thread() && game::actor_spot_anim_set(live_actor(player, index), slot, field, value);
}
jboolean n_actor_create_spot_anim(JNIEnv*, jclass, jboolean player, jint index, jint slot, jint id, jint height, jint delay) {
    return client_thread::on_client_thread() && game::actor_create_spot_anim(live_actor(player, index), slot, id, height, delay);
}
void n_widget_set_text(JNIEnv* env, jclass, jint id, jint index, jstring text) {
    if (!client_thread::on_client_thread()) return;
    std::string latin1;
    if (text) {
        const jchar* c = env->GetStringChars(text, nullptr);
        jsize n = env->GetStringLength(text);
        for (jsize i = 0; i < n; i++) latin1.push_back(c[i] < 256 ? static_cast<char>(c[i]) : '?');
        env->ReleaseStringChars(text, c);
    }
    game::widget_set_text(widget_or_child(id, index), latin1);
}
void n_widget_set_string(JNIEnv* env, jclass, jint id, jint index, jint which, jstring text) {
    if (!client_thread::on_client_thread()) return;
    std::string t;
    if (text) t = to_latin1(env, text);
    game::widget_set_string(widget_or_child(id, index), which, t);
}
jint n_widget_flags(JNIEnv*, jclass, jint id, jint index) { return game::widget_flags(widget_or_child(id, index)); }
jintArray n_widget_events(JNIEnv* env, jclass, jint id, jint index) {
    if (!client_thread::on_client_thread()) return nullptr;
    int v[2];
    if (!game::widget_events(widget_or_child(id, index), v)) return nullptr;
    jintArray a = env->NewIntArray(2);
    if (a) env->SetIntArrayRegion(a, 0, 2, reinterpret_cast<const jint*>(v));
    return a;
}
void n_widget_set_flags(JNIEnv*, jclass, jint id, jint index, jint flags) {
    if (client_thread::on_client_thread()) game::widget_set_flags(widget_or_child(id, index), flags);
}
jlong n_widget_ptr(JNIEnv*, jclass, jint id, jint index) { return static_cast<jlong>(widget_or_child(id, index)); }
jint n_widget_color(JNIEnv*, jclass, jint id, jint index) { return game::widget_color(widget_or_child(id, index)); }
void n_widget_set_color(JNIEnv*, jclass, jint id, jint index, jint rgb) {
    if (client_thread::on_client_thread()) game::widget_set_color(widget_or_child(id, index), rgb);
}

// Dynamic (CC_CREATE) children: index into the component's created-children array.
jint n_widget_dynamic_count(JNIEnv*, jclass, jint id) { return game::widget_dynamic_count(game::widget(id)); }
jboolean n_widget_dynamic_hidden(JNIEnv*, jclass, jint id, jint i) {
    uintptr_t w = game::widget_dynamic(game::widget(id), i);
    return !w || game::widget_hidden(w);
}
jstring n_widget_dynamic_text(JNIEnv* env, jclass, jint id, jint i) {
    uintptr_t w = game::widget_dynamic(game::widget(id), i);
    return w ? env->NewStringUTF(game::widget_text(w).c_str()) : nullptr;
}
// {x, y, width, height, parentId (= id)}, or null.
jintArray n_widget_dynamic_geometry(JNIEnv* env, jclass, jint id, jint i) {
    uintptr_t w = game::widget_dynamic(game::widget(id), i);
    if (!w) return nullptr;
    game::WidgetGeometry g = game::widget_geometry(w);
    jint v[5] = {g.x, g.y, g.width, g.height, id};
    jintArray out = env->NewIntArray(5);
    if (out) env->SetIntArrayRegion(out, 0, 5, v);
    return out;
}

// {int[] packed (index, local, localX, localY, worldX, worldY), String[] names}
jobjectArray n_players(JNIEnv* env, jclass) {
    std::vector<game::PlayerInfo> players = client_thread::players();
    jsize n = static_cast<jsize>(players.size());
    std::vector<jint> packed, app;  // app: kAppearanceStride ints per player (see Native.players())
    jobjectArray names = env->NewObjectArray(n, env->FindClass("java/lang/String"), nullptr);
    jobjectArray overheads = env->NewObjectArray(n, env->FindClass("java/lang/String"), nullptr);
    for (jsize i = 0; i < n; i++) {
        const game::PlayerInfo& p = players[i];
        packed.insert(packed.end(), {p.index, p.local ? 1 : 0, p.fine_x, p.fine_y, p.world_x, p.world_y});
        pack_actor(packed, p.actor);
        jstring name = env->NewStringUTF(p.name.c_str());
        env->SetObjectArrayElement(names, i, name);
        env->DeleteLocalRef(name);
        set_overhead(env, overheads, i, p.actor);
        app.push_back(p.has_appearance ? 1 : 0);
        app.insert(app.end(), p.equipment, p.equipment + 12);
        app.insert(app.end(), p.kits, p.kits + 12);
        app.insert(app.end(), p.colors, p.colors + 5);
        app.insert(app.end(), {p.gender, p.npc_id, p.combat_level, p.skull, p.prayer, p.team});
    }
    jintArray ints = env->NewIntArray(n * kActorStride);
    env->SetIntArrayRegion(ints, 0, n * kActorStride, packed.data());
    jintArray apps = env->NewIntArray(static_cast<jsize>(app.size()));
    env->SetIntArrayRegion(apps, 0, static_cast<jsize>(app.size()), app.data());
    jobjectArray out = env->NewObjectArray(4, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(out, 0, ints);
    env->SetObjectArrayElement(out, 1, names);
    env->SetObjectArrayElement(out, 2, overheads);
    env->SetObjectArrayElement(out, 3, apps);
    return out;
}

// {id0, id1, ...} of an inventory's items followed by quantities (2 * slots ints), or null.
jintArray n_inventory(JNIEnv* env, jclass, jint id) {
    // While an event is posted: the inventory as of that event (packet order: a ChatMessage handler
    // doesn't see an UPDATE_INV that came after it). Otherwise, on the game thread, the live map:
    // scripts that run while the packets are handled (the bank's 505 on the cycle inventory 95
    // arrives) would otherwise see last cycle's snapshot.
    game::Inventory viewed;
    std::vector<game::Inventory> one;
    int view = client_thread::inventory_view(id, viewed);
    if (view == 0) return nullptr;
    bool in_view = view == 1;
    if (in_view) one.push_back(std::move(viewed));
    for (const auto& inv : in_view ? one : client_thread::on_client_thread() ? game::read_inventories() : client_thread::inventories()) {
        if (inv.id != id) continue;
        jsize n = static_cast<jsize>(inv.ids.size());
        jintArray arr = env->NewIntArray(2 * n);
        env->SetIntArrayRegion(arr, 0, n, inv.ids.data());
        env->SetIntArrayRegion(arr, n, n, inv.quantities.data());
        return arr;
    }
    return nullptr;
}

// Ids of every inventory the client holds (as n_inventory sees them outside an event view).
jintArray n_inventory_ids(JNIEnv* env, jclass) {
    std::vector<game::Inventory> all = client_thread::on_client_thread() ? game::read_inventories() : client_thread::inventories();
    std::vector<jint> ids;
    for (const auto& inv : all) ids.push_back(inv.id);
    jintArray arr = env->NewIntArray(static_cast<jsize>(ids.size()));
    if (!ids.empty()) env->SetIntArrayRegion(arr, 0, static_cast<jsize>(ids.size()), ids.data());
    return arr;
}

// {current[25], real[25], xp[25]}
jintArray n_skills(JNIEnv* env, jclass) {
    game::Skills sk = game::skills();
    jintArray arr = env->NewIntArray(3 * game::kSkillCount);
    env->SetIntArrayRegion(arr, 0, game::kSkillCount, sk.current);
    env->SetIntArrayRegion(arr, game::kSkillCount, game::kSkillCount, sk.real);
    env->SetIntArrayRegion(arr, 2 * game::kSkillCount, game::kSkillCount, sk.xp);
    return arr;
}

// ---- cache, index and config lookups (client-api-cache-config) ----
jintArray n_type_param_keys(JNIEnv* env, jclass, jint kind, jint id) {
    if (!require_client_thread(env, "typeParamKeys")) return nullptr;
    std::vector<int32_t> keys;
    return game::type_param_keys(kind, id, keys) ? int_array(env, keys) : nullptr;
}
jobjectArray n_sub_ops(JNIEnv* env, jclass, jint kind, jint id, jint op) {
    if (!require_client_thread(env, "subOps")) return nullptr;
    std::vector<std::string> v;
    bool present = false;
    if (!game::sub_ops(kind, id, op, v, present) || !present) return nullptr;
    jobjectArray r = env->NewObjectArray(static_cast<jsize>(v.size()), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < v.size(); i++) {
        jstring str = v[i].empty() ? nullptr : latin1_string(env, v[i]);
        env->SetObjectArrayElement(r, static_cast<jsize>(i), str);
        if (str) env->DeleteLocalRef(str);
    }
    return r;
}
jintArray n_archive_file_ids(JNIEnv* env, jclass, jint id, jint group) {
    if (!require_client_thread(env, "archiveFileIds")) return nullptr;
    std::vector<int32_t> v;
    return game::archive_file_ids(id, group, v) ? int_array(env, v) : nullptr;
}
jbyteArray n_archive_file(JNIEnv* env, jclass, jint id, jint group, jint file) {
    if (!require_client_thread(env, "archiveFile")) return nullptr;
    std::vector<uint8_t> v;
    if (!game::archive_file(id, group, file, v)) return nullptr;
    jbyteArray r = env->NewByteArray(static_cast<jsize>(v.size()));
    if (r) env->SetByteArrayRegion(r, 0, static_cast<jsize>(v.size()), reinterpret_cast<const jbyte*>(v.data()));
    return r;
}
// {int[] models, short[] recolours, short[] retextures, int[] {use local player}}, or null.
jobjectArray n_npc_overrides(JNIEnv* env, jclass, jint index, jboolean head) {
    if (!require_client_thread(env, "npcOverrides")) return nullptr;
    game::NpcOverrides o;
    if (!game::npc_overrides(index, head, o)) return nullptr;
    auto shorts = [env](const std::vector<int16_t>& v) {
        jshortArray a = env->NewShortArray(static_cast<jsize>(v.size()));
        if (a) env->SetShortArrayRegion(a, 0, static_cast<jsize>(v.size()), v.data());
        return a;
    };
    jobjectArray r = env->NewObjectArray(4, env->FindClass("java/lang/Object"), nullptr);
    env->SetObjectArrayElement(r, 0, int_array(env, o.models));
    env->SetObjectArrayElement(r, 1, shorts(o.recolours));
    env->SetObjectArrayElement(r, 2, shorts(o.retextures));
    env->SetObjectArrayElement(r, 3, int_array(env, {o.use_local_player ? 1 : 0}));
    return r;
}
// archive ids then sprite indices (two halves of equal length), or null.
jintArray n_npc_head_icons(JNIEnv* env, jclass, jint index) {
    if (!require_client_thread(env, "npcHeadIcons")) return nullptr;
    std::vector<int32_t> a, sp;
    if (!game::npc_head_icons(index, a, sp)) return nullptr;
    sp.resize(a.size(), -1);
    a.insert(a.end(), sp.begin(), sp.end());
    return int_array(env, a);
}
jlong n_cache_size(JNIEnv*, jclass, jint which) { return game::cache_size(which); }
jboolean n_cache_reset(JNIEnv* env, jclass, jint which) {
    return require_client_thread(env, "cacheReset") && game::cache_reset(which);
}
// {int[] ids, Object[] values (Integer or String)}, or null.
jobjectArray n_varc_entries(JNIEnv* env, jclass) {
    if (!require_client_thread(env, "varcEntries")) return nullptr;
    std::vector<game::VarcEntry> v;
    if (!game::varc_entries(v)) return nullptr;
    std::vector<int32_t> ids;
    for (auto& e : v) ids.push_back(e.id);
    jclass object = env->FindClass("java/lang/Object");
    jclass integer = env->FindClass("java/lang/Integer");
    jmethodID value_of = env->GetStaticMethodID(integer, "valueOf", "(I)Ljava/lang/Integer;");
    jobjectArray values = env->NewObjectArray(static_cast<jsize>(v.size()), object, nullptr);
    for (size_t i = 0; i < v.size(); i++) {
        jobject o = v[i].type == 2 ? static_cast<jobject>(latin1_string(env, v[i].string_value))
                                   : env->CallStaticObjectMethod(integer, value_of, v[i].int_value);
        env->SetObjectArrayElement(values, static_cast<jsize>(i), o);
        env->DeleteLocalRef(o);
    }
    jobjectArray r = env->NewObjectArray(2, object, nullptr);
    env->SetObjectArrayElement(r, 0, int_array(env, ids));
    env->SetObjectArrayElement(r, 1, values);
    return r;
}
jintArray n_widget_event_ranges(JNIEnv* env, jclass) {
    if (!require_client_thread(env, "widgetEventRanges")) return nullptr;
    std::vector<int32_t> v;
    return game::widget_event_ranges(v) ? int_array(env, v) : nullptr;
}

const JNINativeMethod kNatives[] = {
    {"skills", "()[I", reinterpret_cast<void*>(n_skills)},
    {"inventory", "(I)[I", reinterpret_cast<void*>(n_inventory)},
    {"inventoryIds", "()[I", reinterpret_cast<void*>(n_inventory_ids)},
    {"players", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_players)},
    {"widgetExists", "(I)Z", reinterpret_cast<void*>(n_widget_exists)},
    {"widgetHidden", "(I)Z", reinterpret_cast<void*>(n_widget_hidden)},
    {"widgetText", "(I)Ljava/lang/String;", reinterpret_cast<void*>(n_widget_text)},
    {"rootInterface", "()I", reinterpret_cast<void*>(n_root_interface)},
    {"widgetGeometry", "(I)[I", reinterpret_cast<void*>(n_widget_geometry)},
    {"widgetChildCount", "(I)I", reinterpret_cast<void*>(n_widget_child_count)},
    {"widgetDynamicCount", "(I)I", reinterpret_cast<void*>(n_widget_dynamic_count)},
    {"widgetDynamicHidden", "(II)Z", reinterpret_cast<void*>(n_widget_dynamic_hidden)},
    {"widgetItem", "(II)[I", reinterpret_cast<void*>(n_widget_item)},
    {"widgetColor", "(II)I", reinterpret_cast<void*>(n_widget_color)},
    {"widgetProps", "(II)[I", reinterpret_cast<void*>(n_widget_props)},
    {"widgetSetField", "(IIII)Z", reinterpret_cast<void*>(n_widget_set_field)},
    {"widgetGetField", "(III)I", reinterpret_cast<void*>(n_widget_get_field)},
    {"widgetDragParent", "(II)[I", reinterpret_cast<void*>(n_widget_drag_parent)},
    {"widgetSetDragParent", "(IIII)Z", reinterpret_cast<void*>(n_widget_set_drag_parent)},
    {"widgetRevalidate", "(II)Z", reinterpret_cast<void*>(n_widget_revalidate)},
    {"widgetCreateChild", "(III)I", reinterpret_cast<void*>(n_widget_create_child)},
    {"widgetDeleteChildren", "(I)Z", reinterpret_cast<void*>(n_widget_delete_children)},
    {"widgetTruncateChildren", "(II)Z", reinterpret_cast<void*>(n_widget_truncate_children)},
    {"widgetStrings", "(II)[Ljava/lang/String;", reinterpret_cast<void*>(n_widget_strings)},
    {"widgetSetColor", "(III)V", reinterpret_cast<void*>(n_widget_set_color)},
    {"widgetSetHidden", "(IIZ)V", reinterpret_cast<void*>(n_widget_set_hidden)},
    {"widgetSetText", "(IILjava/lang/String;)V", reinterpret_cast<void*>(n_widget_set_text)},
    {"widgetSetString", "(IIILjava/lang/String;)V", reinterpret_cast<void*>(n_widget_set_string)},
    {"widgetFlags", "(II)I", reinterpret_cast<void*>(n_widget_flags)},
    {"widgetEvents", "(II)[I", reinterpret_cast<void*>(n_widget_events)},
    {"widgetSetFlags", "(III)V", reinterpret_cast<void*>(n_widget_set_flags)},
    {"widgetPtr", "(II)J", reinterpret_cast<void*>(n_widget_ptr)},
    {"widgetDynamicText", "(II)Ljava/lang/String;", reinterpret_cast<void*>(n_widget_dynamic_text)},
    {"widgetDynamicGeometry", "(II)[I", reinterpret_cast<void*>(n_widget_dynamic_geometry)},
    {"property", "(Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void*>(n_property)},
    {"camera", "()[I", reinterpret_cast<void*>(n_camera)},
    {"setCameraTarget", "(II)Z", reinterpret_cast<void*>(n_set_camera_target)},
    {"setCameraFocal", "(II)Z", reinterpret_cast<void*>(n_set_camera_focal)},
    {"viewport", "()[I", reinterpret_cast<void*>(n_viewport)},
    {"playSound", "(III)Z", reinterpret_cast<void*>(n_play_sound)},
    {"screenSize", "()[I", reinterpret_cast<void*>(n_screen_size)},
    {"walkDestination", "()[I", reinterpret_cast<void*>(n_walk_destination)},
    {"friends", "(Z)[Ljava/lang/Object;", reinterpret_cast<void*>(n_friends)},
    {"pendingLogins", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_pending_logins)},
    {"playerOverrides", "(I)[[[S", reinterpret_cast<void*>(n_player_overrides)},
    {"friendListState", "()I", reinterpret_cast<void*>(n_friend_list_state)},
    {"targetState", "(I)[B", reinterpret_cast<void*>(n_target_state)},
    {"targetMode", "()[I", reinterpret_cast<void*>(n_target_mode)},
    {"endTargetMode", "()Z", reinterpret_cast<void*>(n_end_target_mode)},
    {"reloadScene", "()Z", reinterpret_cast<void*>(n_reload_scene)},
    {"removeGroundObject", "(III)Z", reinterpret_cast<void*>(n_remove_ground_object)},
    {"sceneTerrain", "(IIII)[I", reinterpret_cast<void*>(n_scene_terrain)},
    {"sceneSet", "(IIIIJII)Z", reinterpret_cast<void*>(n_scene_set)},
    {"ambientSounds", "()[I", reinterpret_cast<void*>(n_ambient_sounds)},
    {"projectiles", "()[I", reinterpret_cast<void*>(n_projectiles)},
    {"graphicsObjects", "()[I", reinterpret_cast<void*>(n_graphics_objects)},
    {"projectileSetEndCycle", "(JI)Z", reinterpret_cast<void*>(n_projectile_set_end_cycle)},
    {"projectileCreate", "([I)J", reinterpret_cast<void*>(n_projectile_create)},
    {"graphicsObjectFinish", "(J)Z", reinterpret_cast<void*>(n_graphics_object_finish)},
    {"ambientClear", "()Z", reinterpret_cast<void*>(n_ambient_clear)},
    {"ambientRestore", "(J)Z", reinterpret_cast<void*>(n_ambient_restore)},
    {"ambientRemove", "(J)Z", reinterpret_cast<void*>(n_ambient_remove)},
    {"collisionFlags", "(II)[I", reinterpret_cast<void*>(n_collision_flags)},
    {"subInterfaces", "()[I", reinterpret_cast<void*>(n_sub_interfaces)},
    {"drawMinimap", "(I)[I", reinterpret_cast<void*>(n_draw_minimap)},
    {"closeSubInterface", "(IIZ)Z", reinterpret_cast<void*>(n_close_sub_interface)},
    {"openSubInterface", "(III)Z", reinterpret_cast<void*>(n_open_sub_interface)},
    {"setAllOpTargetable", "(Z)Z", reinterpret_cast<void*>(n_set_all_op_targetable)},
    {"widgetTargetFlags", "(II)J", reinterpret_cast<void*>(n_widget_target_flags)},
    {"widgetForcePosition", "(IIII)Z", reinterpret_cast<void*>(n_widget_force_position)},
    {"playerSetHash", "(I[I[II)J", reinterpret_cast<void*>(n_player_set_hash)},
    {"playerHash", "(I)J", reinterpret_cast<void*>(n_player_hash)},
    {"sceneRemoveGameObject", "(IIIJ)Z", reinterpret_cast<void*>(n_scene_remove_game_object)},
    {"sceneRemoveTile", "(IIIZ)Z", reinterpret_cast<void*>(n_scene_remove_tile)},
    {"typeParam", "(III)Ljava/lang/Object;", reinterpret_cast<void*>(n_type_param)},
    {"overlayBuffer", "(II)Ljava/nio/ByteBuffer;", reinterpret_cast<void*>(n_overlay_buffer)},
    {"overlayPublish", "(II)V", reinterpret_cast<void*>(n_overlay_publish)},
    {"message", "(I)[Ljava/lang/String;", reinterpret_cast<void*>(n_message)},
    {"npcs", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_npcs)},
    {"menuEntries", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_menu_entries)},
    {"menuSetText", "(ILjava/lang/String;Ljava/lang/String;)Z", reinterpret_cast<void*>(n_menu_set_text)},
    {"clickedMenuEntry", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_clicked_menu_entry)},
    {"menuOpen", "()Z", reinterpret_cast<void*>(n_menu_open)},
    {"itemSprite", "(IIIIIZ)[I", reinterpret_cast<void*>(n_item_sprite)},
    {"sprite", "(I)[I", reinterpret_cast<void*>(n_sprite)},
    {"spriteWrite", "(I[III)Z", reinterpret_cast<void*>(n_sprite_write)},
    {"spriteInfo", "(I)[I", reinterpret_cast<void*>(n_sprite_info)},
    {"spriteArray", "(I)[I", reinterpret_cast<void*>(n_sprite_array)},
    {"textures", "()[I", reinterpret_cast<void*>(n_textures)},
    {"skyboxColour", "()I", reinterpret_cast<void*>(n_skybox_colour)},
    {"setSkyboxColour", "(I)Z", reinterpret_cast<void*>(n_set_skybox_colour)},
    {"texturePixels", "(IZ)[I", reinterpret_cast<void*>(n_texture_pixels)},
    {"textureBrightness", "()D", reinterpret_cast<void*>(n_texture_brightness)},
    {"setTextureBrightness", "(D)Z", reinterpret_cast<void*>(n_set_texture_brightness)},
    {"menuPoint", "()[I", reinterpret_cast<void*>(n_menu_point)},
    {"sceneObjects", "()[I", reinterpret_cast<void*>(n_scene_objects)},
    {"groundItems", "()[I", reinterpret_cast<void*>(n_ground_items)},
    {"consumeMenuClick", "()V", reinterpret_cast<void*>(n_consume_menu_click)},
    {"consumeSound", "()V", reinterpret_cast<void*>(n_consume_sound)},
    {"areaSound", "()[I", reinterpret_cast<void*>(n_area_sound)},
    {"redirectMenuTap", "(I)V", reinterpret_cast<void*>(n_redirect_menu_tap)},
    {"menuReorder", "([I)Z", reinterpret_cast<void*>(n_menu_reorder)},
    {"menuBuildAt", "(II)Z", reinterpret_cast<void*>(n_menu_build_at)},
    {"menuDiscard", "()V", reinterpret_cast<void*>(n_menu_discard)},
    {"pickAt", "(II)V", reinterpret_cast<void*>(n_pick_at)},
    {"pickReady", "()Z", reinterpret_cast<void*>(n_pick_ready)},
    {"menuSelect", "(I)Z", reinterpret_cast<void*>(n_menu_select)},
    {"requestFrame", "()V", reinterpret_cast<void*>(n_request_frame)},
    {"takeFrame", "()[I", reinterpret_cast<void*>(n_take_frame)},
    {"menuTapping", "()Z", reinterpret_cast<void*>(n_menu_tapping)},
    {"gameState", "()I", reinterpret_cast<void*>(n_game_state)},
    {"gameCycle", "()I", reinterpret_cast<void*>(n_game_cycle)},
    {"serverTick", "()I", reinterpret_cast<void*>(n_server_tick)},
    {"varp", "(I)I", reinterpret_cast<void*>(n_varp)},
    {"varps", "()[I", reinterpret_cast<void*>(n_varps)},
    {"serverVarps", "()[I", reinterpret_cast<void*>(n_server_varps)},
    {"weight", "()I", reinterpret_cast<void*>(n_weight)},
    {"world", "()I", reinterpret_cast<void*>(n_world)},
    {"worldEntry", "(I)[Ljava/lang/Object;", reinterpret_cast<void*>(n_world_entry)},
    {"worldList", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_world_list)},
    {"connection", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_connection)},
    {"setServerTick", "(I)Z", reinterpret_cast<void*>(n_set_server_tick)},
    {"minimapZoom", "()I", reinterpret_cast<void*>(n_minimap_zoom)},
    {"setMinimapZoom", "(I)Z", reinterpret_cast<void*>(n_set_minimap_zoom)},
    {"minimapZoomInput", "()I", reinterpret_cast<void*>(n_minimap_zoom_input)},
    {"setMinimapZoomInput", "(Z)Z", reinterpret_cast<void*>(n_set_minimap_zoom_input)},
    {"menuScroll", "()I", reinterpret_cast<void*>(n_menu_scroll)},
    {"setMenuScroll", "(I)Z", reinterpret_cast<void*>(n_set_menu_scroll)},
    {"beginTargetMode", "()Z", reinterpret_cast<void*>(n_begin_target_mode)},
    {"varpsLong", "(Z)[J", reinterpret_cast<void*>(n_varps_long)},
    {"setVarpLong", "(IJ)Z", reinterpret_cast<void*>(n_set_varp_long)},
    {"crossWorldIds", "()[J", reinterpret_cast<void*>(n_cross_world_ids)},
    {"loginIndex", "()I", reinterpret_cast<void*>(n_login_index)},
    {"loginField", "()I", reinterpret_cast<void*>(n_login_field)},
    {"loginText", "(I)Ljava/lang/String;", reinterpret_cast<void*>(n_login_text)},
    {"setLoginText", "(ILjava/lang/String;)Z", reinterpret_cast<void*>(n_set_login_text)},
    {"setLoginBackground", "([III)V", reinterpret_cast<void*>(n_set_login_background)},
    {"loginBackground", "()[I", reinterpret_cast<void*>(n_login_background)},
    {"setLoginFire", "(Z)V", reinterpret_cast<void*>(n_set_login_fire)},
    {"loginFire", "()Z", reinterpret_cast<void*>(n_login_fire)},
    {"clientInt", "(I)I", reinterpret_cast<void*>(n_client_int)},
    {"setClientInt", "(II)Z", reinterpret_cast<void*>(n_set_client_int)},
    {"musicVolume", "()I", reinterpret_cast<void*>(n_music_volume)},
    {"setMusicVolume", "(I)Z", reinterpret_cast<void*>(n_set_music_volume)},
    {"midiRequests", "()[I", reinterpret_cast<void*>(n_midi_requests)},
    {"setCameraShakeDisabled", "(Z)V", reinterpret_cast<void*>(n_set_camera_shake_disabled)},
    {"cameraShakeDisabled", "()Z", reinterpret_cast<void*>(n_camera_shake_disabled)},
    {"setCameraInvert", "(IZ)V", reinterpret_cast<void*>(n_set_camera_invert)},
    {"setCameraSpeed", "(F)V", reinterpret_cast<void*>(n_set_camera_speed)},
    {"cameraSensitivity", "()F", reinterpret_cast<void*>(n_camera_sensitivity)},
    {"instanceTemplates", "()[I", reinterpret_cast<void*>(n_instance_templates)},
    {"geOffers", "()[J", reinterpret_cast<void*>(n_ge_offers)},
    {"geSlotRaw", "(I)[B", reinterpret_cast<void*>(n_ge_slot_raw)},
    {"dragState", "()[I", reinterpret_cast<void*>(n_drag_state)},
    {"dragClearTarget", "()V", reinterpret_cast<void*>(n_drag_clear_target)},
    {"touchState", "()I", reinterpret_cast<void*>(n_touch_state)},
    {"chatChannel", "(I)[Ljava/lang/Object;", reinterpret_cast<void*>(n_chat_channel)},
    {"worldFlags", "()I", reinterpret_cast<void*>(n_world_flags)},
    {"viewportZoom", "()[I", reinterpret_cast<void*>(n_viewport_zoom)},
    {"viewTransforms", "()[F", reinterpret_cast<void*>(n_view_transforms)},
    {"dbField", "(III)[Ljava/lang/Object;", reinterpret_cast<void*>(n_db_field)},
    {"dbRowTable", "(I)I", reinterpret_cast<void*>(n_db_row_table)},
    {"dbFieldCount", "(II)I", reinterpret_cast<void*>(n_db_field_count)},
    {"runEnergy", "()I", reinterpret_cast<void*>(n_run_energy)},
    {"actorModel", "(ZI)[I", reinterpret_cast<void*>(n_actor_model)},
    {"actorMesh", "(ZI)[I", reinterpret_cast<void*>(n_actor_mesh)},
    {"modelData", "(I)[I", reinterpret_cast<void*>(n_model_data)},
    {"poseModel", "(I[IIIII)[I", reinterpret_cast<void*>(n_pose_model)},
    {"actorHeight", "(ZI)I", reinterpret_cast<void*>(n_actor_height)},
    {"setModIcons", "([I[[B[[I)V", reinterpret_cast<void*>(n_set_mod_icons)},
    {"modIconSizes", "()[I", reinterpret_cast<void*>(n_mod_icon_sizes)},
    {"actorSetOverhead", "(ZILjava/lang/String;I)Z", reinterpret_cast<void*>(n_actor_set_overhead)},
    {"actorSet", "(ZIII)Z", reinterpret_cast<void*>(n_actor_set)},
    {"actorSpotAnims", "(ZI)[I", reinterpret_cast<void*>(n_actor_spot_anims)},
    {"actorSpotAnimSet", "(ZIIII)Z", reinterpret_cast<void*>(n_actor_spot_anim_set)},
    {"actorCreateSpotAnim", "(ZIIIII)Z", reinterpret_cast<void*>(n_actor_create_spot_anim)},
    {"objectModel", "(IIIIJ)[I", reinterpret_cast<void*>(n_object_model)},
    {"dynamicObject", "(IIIIJIZ)[I", reinterpret_cast<void*>(n_dynamic_object)},
    {"healthBarTypes", "()[I", reinterpret_cast<void*>(n_health_bar_types)},
    {"healthBarSetPadding", "(II)Z", reinterpret_cast<void*>(n_health_bar_set_padding)},
    {"varcInt", "(I)I", reinterpret_cast<void*>(n_varc_int)},
    {"varcString", "(I)Ljava/lang/String;", reinterpret_cast<void*>(n_varc_string)},
    {"setVarbit", "(II)Z", reinterpret_cast<void*>(n_set_varbit)},
    {"setVarp", "(II)Z", reinterpret_cast<void*>(n_set_varp)},
    {"setVarcInt", "(II)Z", reinterpret_cast<void*>(n_set_varc_int)},
    {"setVarcString", "(ILjava/lang/String;)Z", reinterpret_cast<void*>(n_set_varc_string)},
    {"varbitDef", "(I)[I", reinterpret_cast<void*>(n_varbit_def)},
    {"addChatMessage", "(ILjava/lang/String;Ljava/lang/String;)Z", reinterpret_cast<void*>(n_add_chat_message)},
    {"messageSetText", "(ILjava/lang/String;)Z", reinterpret_cast<void*>(n_message_set_text)},
    {"chatRemove", "(II)Z", reinterpret_cast<void*>(n_chat_remove)},
    {"chatChanged", "()V", reinterpret_cast<void*>(n_chat_changed)},
    {"nextMessageId", "()I", reinterpret_cast<void*>(n_next_message_id)},
    {"worldMap", "()[I", reinterpret_cast<void*>(n_world_map)},
    {"worldMapContains", "(IIII)I", reinterpret_cast<void*>(n_world_map_contains)},
    {"worldMapJump", "(III)Z", reinterpret_cast<void*>(n_world_map_jump)},
    {"worldMapIcons", "()[I", reinterpret_cast<void*>(n_world_map_icons)},
    {"mapElement", "(I)[I", reinterpret_cast<void*>(n_map_element)},
    {"afkTimeout", "()I", reinterpret_cast<void*>(n_afk_timeout)},
    {"setAfkTimeout", "(I)Z", reinterpret_cast<void*>(n_set_afk_timeout)},
    {"serverVarp", "(I)I", reinterpret_cast<void*>(n_server_varp)},
    {"varbit", "(I)I", reinterpret_cast<void*>(n_varbit)},
    {"playerPosition", "()[I", reinterpret_cast<void*>(n_player_position)},
    {"itemDef", "(I)[Ljava/lang/Object;", reinterpret_cast<void*>(n_item_def)},
    {"npcDef", "(I)[Ljava/lang/Object;", reinterpret_cast<void*>(n_npc_def)},
    {"locDef", "(I)[Ljava/lang/Object;", reinterpret_cast<void*>(n_loc_def)},
    {"sidebarSetModel", "(Ljava/lang/String;)V", reinterpret_cast<void*>(n_sidebar_set_model)},
    {"sidebarPollEvents", "()[I", reinterpret_cast<void*>(n_sidebar_poll_events)},
    {"sidebarOpen", "()Z", reinterpret_cast<void*>(n_sidebar_open)},
    {"sidebarSetOpen", "(Z)V", reinterpret_cast<void*>(n_sidebar_set_open)},
    {"sidebarIcon", "(III[I)V", reinterpret_cast<void*>(n_sidebar_icon)},
    {"shiftHeld", "()Z", reinterpret_cast<void*>(n_shift_held)},
    {"pluginMenu", "([Ljava/lang/String;IIII)V", reinterpret_cast<void*>(n_plugin_menu)},
    {"menuClose", "()Z", reinterpret_cast<void*>(n_menu_close)},
    {"overlayTouch", "(IFF)Z", reinterpret_cast<void*>(n_overlay_touch)},
    {"sceneHeights", "()[I", reinterpret_cast<void*>(n_scene_heights)},
    {"enumDef", "(I)[Ljava/lang/Object;", reinterpret_cast<void*>(n_enum_def)},
    {"runScript", "([Ljava/lang/Object;)[Ljava/lang/Object;", reinterpret_cast<void*>(n_run_script)},
    {"isClientThread", "()Z", reinterpret_cast<void*>(n_is_client_thread)},
    {"setClientDelegate", "(Z)V", reinterpret_cast<void*>(n_set_client_delegate)},
    {"queueChangedSkill", "(I)Z", reinterpret_cast<void*>(n_queue_changed_skill)},
    {"setHiddenActors", "([I[I)V", reinterpret_cast<void*>(n_set_hidden_actors)},
    {"setGeSearch", "([SII)Z", reinterpret_cast<void*>(n_set_ge_search)},
    {"geSearch", "()[I", reinterpret_cast<void*>(n_ge_search)},
    {"minimapDot", "(IIII)[I", reinterpret_cast<void*>(n_minimap_dot)},
    {"setScriptEvents", "(Z)V", reinterpret_cast<void*>(n_set_script_events)},
    {"scriptStacks", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_script_stacks)},
    {"setScriptIntStack", "([II)Z", reinterpret_cast<void*>(n_set_script_int_stack)},
    {"scriptArray", "(ZI)[I", reinterpret_cast<void*>(n_script_array)},
    {"setScriptArray", "(ZI[I)Z", reinterpret_cast<void*>(n_set_script_array)},
    {"scriptIntLocal", "(ZIZI)I", reinterpret_cast<void*>(n_script_int_local)},
    {"currentScript", "()I", reinterpret_cast<void*>(n_current_script)},
    {"scriptIntTop", "(IZI)I", reinterpret_cast<void*>(n_script_int_top)},
    {"scriptStringTop", "()Ljava/lang/String;", reinterpret_cast<void*>(n_script_string_top)},
    {"setOpWatches", "([I[Ljava/lang/String;)V", reinterpret_cast<void*>(n_set_op_watches)},
    {"setScriptString", "(ILjava/lang/String;)Z", reinterpret_cast<void*>(n_set_script_string)},
    {"scriptEventArgs", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_script_event_args)},
    {"scriptEventSource", "()[I", reinterpret_cast<void*>(n_script_event_source)},
    {"scriptActiveWidget", "(Z)[I", reinterpret_cast<void*>(n_script_active_widget)},
    {"textWidth", "(ILjava/lang/String;)I", reinterpret_cast<void*>(n_text_width)},
    {"fontAscent", "(I)I", reinterpret_cast<void*>(n_font_ascent)},
    {"scriptCode", "(I)[Ljava/lang/Object;", reinterpret_cast<void*>(n_script_code)},
    {"widgetListener", "(III)[Ljava/lang/Object;", reinterpret_cast<void*>(n_widget_listener)},
    {"widgetClearListener", "(III)Z", reinterpret_cast<void*>(n_widget_clear_listener)},
    {"widgetTriggers", "(III)[I", reinterpret_cast<void*>(n_widget_triggers)},
    {"scriptStats", "()[J", reinterpret_cast<void*>(n_script_stats)},
    {"typeParamKeys", "(II)[I", reinterpret_cast<void*>(n_type_param_keys)},
    {"archiveFileIds", "(II)[I", reinterpret_cast<void*>(n_archive_file_ids)},
    {"archiveFile", "(III)[B", reinterpret_cast<void*>(n_archive_file)},
    {"npcOverrides", "(IZ)[Ljava/lang/Object;", reinterpret_cast<void*>(n_npc_overrides)},
    {"npcHeadIcons", "(I)[I", reinterpret_cast<void*>(n_npc_head_icons)},
    {"cacheSize", "(I)J", reinterpret_cast<void*>(n_cache_size)},
    {"cacheReset", "(I)Z", reinterpret_cast<void*>(n_cache_reset)},
    {"subOps", "(III)[Ljava/lang/String;", reinterpret_cast<void*>(n_sub_ops)},
    {"varcEntries", "()[Ljava/lang/Object;", reinterpret_cast<void*>(n_varc_entries)},
    {"widgetEventRanges", "()[I", reinterpret_cast<void*>(n_widget_event_ranges)},
};

JNIEnv* game_thread_env() {
    thread_local JNIEnv* env = nullptr;
    if (!env) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "osrsrl-game", nullptr};
        if (g_vm->AttachCurrentThread(&env, &args) != JNI_OK) env = nullptr;
    }
    return env;
}

jclass load_class(JNIEnv* env, jobject loader, const char* name) {
    jclass cl = env->FindClass("java/lang/ClassLoader");
    jmethodID load = env->GetMethodID(cl, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jname = env->NewStringUTF(name);
    auto c = static_cast<jclass>(env->CallObjectMethod(loader, load, jname));
    return check(env, name) ? c : nullptr;
}

bool start_host(JNIEnv* env) {
    std::string pkg = package_name();
    std::string dir = "/data/data/" + pkg + "/code_cache/osrsrl";
    mkdir(("/data/data/" + pkg + "/code_cache").c_str(), 0700);
    mkdir(dir.c_str(), 0700);
    std::string jar = dir + "/host.jar";
    if (!write_file(jar, osrsrl_host_jar, osrsrl_host_jar_end - osrsrl_host_jar)) {
        LOGE("jni: could not write %s", jar.c_str());
        return false;
    }
    std::string plugins = "/sdcard/Android/data/" + pkg + "/files/plugins";

    jclass cl_loader = env->FindClass("java/lang/ClassLoader");
    jobject system = env->CallStaticObjectMethod(
        cl_loader, env->GetStaticMethodID(cl_loader, "getSystemClassLoader", "()Ljava/lang/ClassLoader;"));
    jclass dex_loader = env->FindClass("dalvik/system/DexClassLoader");
    jmethodID ctor = env->GetMethodID(
        dex_loader, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    jobject loader = env->NewObject(dex_loader, ctor, env->NewStringUTF(jar.c_str()),
                                    env->NewStringUTF(dir.c_str()), nullptr, system);
    if (!check(env, "DexClassLoader")) return false;

    jclass native_cls = load_class(env, loader, "net.runelite.host.Native");
    jclass host = load_class(env, loader, "net.runelite.host.Host");
    if (!native_cls || !host) return false;
    if (env->RegisterNatives(native_cls, kNatives, sizeof kNatives / sizeof kNatives[0]) != JNI_OK) {
        check(env, "RegisterNatives");
        return false;
    }
    jmethodID start = env->GetStaticMethodID(host, "start", "(Ljava/lang/String;Ljava/lang/String;)V");
    g_on_event = env->GetStaticMethodID(host, "onEvent", "(III)V");
    g_on_script = env->GetStaticMethodID(host, "onScript", "(II)V");
    if (!check(env, "Host methods") || !start || !g_on_event || !g_on_script) return false;
    env->CallStaticVoidMethod(host, start, env->NewStringUTF(plugins.c_str()), env->NewStringUTF(dir.c_str()));
    if (!check(env, "Host.start")) return false;
    g_host = static_cast<jclass>(env->NewGlobalRef(host));
    return true;
}

void host_thread() {
    JNIEnv* env = nullptr;
    JavaVMAttachArgs args{JNI_VERSION_1_6, "osrsrl-host", nullptr};
    if (g_vm->AttachCurrentThread(&env, &args) != JNI_OK) {
        g_status = "failed";
        return;
    }
    LOGI("jni: starting plugin host (%zu byte jar)", static_cast<size_t>(osrsrl_host_jar_end - osrsrl_host_jar));
    bool ok = start_host(env);
    g_vm->DetachCurrentThread();
    if (!ok) {
        g_status = "failed";
        return;
    }
    client_thread::subscribe([](const client_thread::Event& e) {
        JNIEnv* env = game_thread_env();
        if (!env) return;
        env->CallStaticVoidMethod(g_host, g_on_event, static_cast<jint>(e.type), e.a, e.b);
        check(env, "Host.onEvent");
    });
    script_hooks::set_listener([](int phase, int id) {
        JNIEnv* env = game_thread_env();
        if (!env) return;
        env->CallStaticVoidMethod(g_host, g_on_script, phase, id);
        check(env, "Host.onScript");
    });
    g_status = "ready";
    LOGI("jni: plugin host ready");
}

}  // namespace

void jni_bridge_start(JavaVM* vm) {
    char value[PROP_VALUE_MAX] = "";
    __system_property_get("debug.osrsrl.host", value);
    if (value[0] == '0' || !client_thread::installed()) {
        g_status = "disabled";
        return;
    }
    g_vm = vm;
    std::thread(host_thread).detach();
}

const char* jni_bridge_status() { return g_status.load(); }
