#define _POSIX_C_SOURCE 200809L
#include "monitor-model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *fixture =
"["
"{\"name\":\"eDP-1\",\"description\":\"Sharp laptop\",\"width\":3840,\"height\":2160,\"refreshRate\":59.997,\"scale\":3,\"transform\":0,\"x\":0,\"y\":0,\"disabled\":false,\"availableModes\":[\"3840x2160@60.00Hz\",\"3840x2160@48.00Hz\"]},"
"{\"name\":\"DP-3\",\"description\":\"Xiaomi Mi Monitor\",\"width\":3440,\"height\":1440,\"refreshRate\":59.999,\"scale\":1,\"transform\":0,\"x\":-3440,\"y\":0,\"disabled\":false,\"availableModes\":[\"3440x1440@60.00Hz\",\"1920x1080@60.00Hz\"]},"
"{\"name\":\"DP-1\",\"description\":\"HP Z23i 12345\",\"width\":1920,\"height\":1080,\"refreshRate\":60,\"scale\":1.6,\"transform\":1,\"x\":0,\"y\":-1200,\"disabled\":false,\"availableModes\":[\"1920x1080@60.00Hz\",\"1280x720@60.00Hz\"]}"
"]";
static char error[512];
static json_object *get(json_object *o, const char *key) { return json_object_object_get(o, key); }
static int number(json_object *o, const char *key) { return json_object_get_int(get(o, key)); }
static const char *string(json_object *o, const char *key) { return json_object_get_string(get(o, key)); }
static json_object *monitor(json_object *plan, const char *name) {
    json_object *monitors = get(plan, "monitors");
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        if (!strcmp(string(m, "name"), name)) return m;
    }
    assert(!"monitor missing"); return NULL;
}
static json_object *plan(json_object *live, json_object *state, const char *name,
                          const char *action, const char *value, const char *ref, const char *align) {
    json_object *p = monitor_plan(live, state, name, action, value, ref, align, error, sizeof error);
    if (!p) fprintf(stderr, "%s %s: %s\n", name, action, error);
    assert(p); return p;
}
static void position(json_object *p, const char *name, int x, int y) {
    json_object *m = monitor(p, name);
    assert(number(m, "x") == x); assert(number(m, "y") == y);
}
static void test_geometry(void) {
    json_object *live = json_tokener_parse(fixture), *state = monitor_initial_state(live);
    char *before = strdup(json_object_to_json_string(live));
    char *state_before = strdup(json_object_to_json_string(state));
    json_object *p = plan(live, state, "DP-1", "position", "left", "DP-3", NULL);
    position(p, "DP-1", -4115, 120);
    assert(!strcmp(before, json_object_to_json_string(live)));
    assert(!strcmp(state_before, json_object_to_json_string(state)));
    json_object *scaled = plan(get(p, "monitors"), get(p, "state"), "DP-1", "scale", "1", NULL, NULL);
    position(scaled, "DP-1", -4520, -240);
    json_object *rotated = plan(get(p, "monitors"), get(p, "state"), "DP-1", "rotation", "0", NULL, NULL);
    position(rotated, "DP-1", -4640, 382);
    json_object *mode = plan(get(p, "monitors"), get(p, "state"), "DP-3", "mode", "1920x1080@60.00Hz", NULL, NULL);
    position(mode, "DP-1", -4115, -60);
    json_object *top = plan(live, state, "DP-1", "position", "left", "DP-3", "start");
    position(top, "DP-1", -4115, 0);
    json_object *bottom = plan(live, state, "DP-1", "position", "left", "DP-3", "end");
    position(bottom, "DP-1", -4115, 240);
    assert(!monitor_plan(live, state, "DP-1", "position", "right", "DP-3", NULL, error, sizeof error));
    assert(strstr(error, "overlap"));
    assert(!monitor_plan(live, state, "DP-1", "position", "left", "DP-1", NULL, error, sizeof error));
    assert(!monitor_plan(live, state, "DP-1", "mode", "777x999@60", NULL, NULL, error, sizeof error));
    assert(!monitor_plan(live, state, "DP-1", "mode", "1920x1080@60.00;evil", NULL, NULL, error, sizeof error));
    assert(!monitor_plan(live, state, "DP-1", "scale", "nan", NULL, NULL, error, sizeof error));
    assert(!monitor_plan(live, state, "DP-1", "scale", "2garbage", NULL, NULL, error, sizeof error));
    assert(!monitor_plan(live, state, "DP-1", "rotation", "4", NULL, NULL, error, sizeof error));
    json_object_put(bottom); json_object_put(top); json_object_put(mode); json_object_put(rotated);
    json_object_put(scaled); json_object_put(p); json_object_put(state); json_object_put(live);
    free(before); free(state_before);
}
static void test_relations(void) {
    json_object *live = json_tokener_parse(fixture);
    json_object *root = plan(live, NULL, "DP-3", "position", "left", "eDP-1", "start");
    json_object *chain = plan(get(root, "monitors"), get(root, "state"), "DP-1", "position", "left", "DP-3", NULL);
    assert(json_object_array_length(get(get(chain, "state"), "relations")) == 2);
    json_object *scaled = plan(get(chain, "monitors"), get(chain, "state"), "eDP-1", "scale", "2", NULL, NULL);
    position(scaled, "DP-3", -3440, 0); position(scaled, "DP-1", -4115, 120);
    json_object *moved = plan(get(chain, "monitors"), get(chain, "state"), "DP-3", "position", "up", "eDP-1", NULL);
    position(moved, "DP-3", -1080, -1440); position(moved, "DP-1", -4115, 120);
    assert(json_object_array_length(get(get(moved, "state"), "relations")) == 1);
    json_object *bad = NULL;
    assert(json_object_deep_copy(get(chain, "state"), &bad, NULL) == 0);
    json_object *relations = get(bad, "relations");
    json_object *edge = json_object_array_get_idx(relations, 0);
    json_object_object_add(edge, "reference", json_object_new_string("desc:HP Z23i 12345"));
    assert(!monitor_reconcile(get(chain, "monitors"), bad, true, error, sizeof error));
    assert(strstr(error, "cycle"));
    json_object_put(bad); json_object_put(moved); json_object_put(scaled);
    json_object_put(chain); json_object_put(root); json_object_put(live);
}
static void test_topology(void) {
    json_object *live = json_tokener_parse(fixture);
    json_object *p = plan(live, NULL, "DP-1", "position", "left", "DP-3", NULL);
    json_object *without = json_object_new_array();
    json_object_array_add(without, json_object_get(monitor(p, "eDP-1")));
    json_object_array_add(without, json_object_get(monitor(p, "DP-1")));
    json_object *compact = monitor_reconcile(without, get(p, "state"), true, error, sizeof error);
    assert(compact); position(compact, "eDP-1", 0, 0); position(compact, "DP-1", 1280, -240);
    json_object *restored = monitor_reconcile(get(p, "monitors"), get(compact, "state"), true, error, sizeof error);
    assert(restored); position(restored, "DP-1", -4115, 120);
    json_object *disabled = plan(get(p, "monitors"), get(p, "state"), "DP-3", "disable", NULL, NULL, NULL);
    position(disabled, "DP-1", 1280, -240);
    assert(json_object_array_length(get(get(disabled, "state"), "relations")) == 1);
    json_object *enabled = plan(get(disabled, "monitors"), get(disabled, "state"), "DP-3", "enable", NULL, NULL, NULL);
    position(enabled, "DP-1", -4115, 120); position(enabled, "DP-3", -3440, 0);
    json_object *hp_off = plan(get(p, "monitors"), get(p, "state"), "DP-1", "disable", NULL, NULL, NULL);
    position(hp_off, "DP-3", 1280, -360);
    json_object *manual = plan(get(hp_off, "monitors"), get(hp_off, "state"), "eDP-1", "position", "down", "DP-3", NULL);
    position(manual, "DP-3", 1280, -360);
    position(manual, "eDP-1", 2360, 1080);
    json_object *fallback_mode = plan(get(hp_off, "monitors"), get(hp_off, "state"), "DP-3", "mode", "1920x1080@60.00", NULL, NULL);
    position(fallback_mode, "DP-3", 1280, -180);
    json_object *old = json_tokener_parse("{\"id\":\"desc:Travel monitor\",\"name\":\"DP-2\",\"x\":5000,\"y\":0,\"width\":1920,\"height\":1080,\"scale\":1}");
    json_object_object_add(get(get(compact, "state"), "outputs"), "desc:Travel monitor", old);
    json_object *historical = monitor_reconcile(get(p, "monitors"), get(compact, "state"), true, error, sizeof error);
    assert(historical); position(historical, "DP-1", -4115, 120);
    json_object *mode = plan(get(hp_off, "monitors"), get(hp_off, "state"), "DP-1", "mode", "1280x720@60.00", NULL, NULL);
    assert(number(monitor(mode, "DP-1"), "width") == 1280);
    assert(json_object_get_boolean(get(monitor(mode, "DP-1"), "disabled")));
    json_object *one = json_object_new_array();
    json_object_array_add(one, json_object_get(monitor(p, "eDP-1")));
    assert(!monitor_plan(one, NULL, "eDP-1", "disable", NULL, NULL, NULL, error, sizeof error));
    /* Saved dimensions must not turn a connected but black output into a
     * usable fallback when protecting the only genuinely active display. */
    json_object *black = NULL;
    assert(json_object_deep_copy(get(p, "monitors"), &black, NULL) == 0);
    for (size_t i = 1; i < json_object_array_length(black); ++i) {
        json_object *item = json_object_array_get_idx(black, i);
        json_object_object_add(item, "width", json_object_new_int(0));
        json_object_object_add(item, "height", json_object_new_int(0));
        json_object_object_add(item, "disabled", json_object_new_boolean(false));
    }
    assert(!monitor_plan(black, get(p, "state"), "eDP-1", "disable", NULL, NULL, NULL, error, sizeof error));
    assert(strstr(error, "only active"));
    assert(!monitor_plan(black, get(p, "state"), "eDP-1", "position", "left", "DP-3", NULL, error, sizeof error));
    assert(strstr(error, "active reference"));
    assert(!monitor_plan(black, get(p, "state"), "DP-1", "position", "left", "eDP-1", NULL, error, sizeof error));
    json_object_put(black);
    json_object *renamed = NULL;
    assert(json_object_deep_copy(get(p, "monitors"), &renamed, NULL) == 0);
    json_object_object_add(json_object_array_get_idx(renamed, 2), "name", json_object_new_string("DP-9"));
    json_object *same = monitor_reconcile(renamed, get(p, "state"), true, error, sizeof error);
    assert(same); position(same, "DP-9", -4115, 120);
    json_object *changed = NULL;
    assert(json_object_deep_copy(get(p, "monitors"), &changed, NULL) == 0);
    json_object_object_add(json_object_array_get_idx(changed, 2), "transform", json_object_new_int(0));
    json_object *external = monitor_reconcile(changed, get(p, "state"), false, error, sizeof error);
    assert(external); position(external, "DP-1", -4640, 382);
    json_object_put(external); json_object_put(changed); json_object_put(same); json_object_put(renamed);
    json_object_put(historical); json_object_put(fallback_mode); json_object_put(manual);
    json_object_put(one); json_object_put(mode); json_object_put(hp_off); json_object_put(enabled);
    json_object_put(disabled); json_object_put(restored); json_object_put(compact);
    json_object_put(without); json_object_put(p); json_object_put(live);
}
static void test_configuration(void) {
    const char *original =
        "-- User settings\nlocal variable = 42\n"
        "-- BEGIN monitor-layout selector (managed automatically)\n"
        "hl.monitor({ output = \"DP-1\", mode = \"preferred\", position = \"auto-up\", scale = 1.6, transform = 1, bitdepth = 10, extra = {a=1,b=\"a,b\"} })\n"
        "  hl.monitor({ output = \"DP-2\", mode = \"preferred\", scale = 1.25})\n"
        "-- END monitor-layout selector\n"
        "-- trailing custom code\nhl.env(\"CUSTOM\", \"hello\")\n";
    json_object *live = json_tokener_parse(fixture);
    json_object *p = plan(live, NULL, "DP-1", "position", "left", "DP-3", NULL);
    char *updated = monitor_configuration(original, get(p, "monitors"), error, sizeof error);
    if (!updated) fprintf(stderr, "configuration: %s\n", error);
    assert(updated);
    assert(!strncmp(updated, "-- User settings\nlocal variable = 42\n", 36));
    assert(strstr(updated, "bitdepth = 10, extra = {a=1,b=\"a,b\"}"));
    assert(strstr(updated, "position = \"-4115x120\""));
    assert(strstr(updated, "  hl.monitor({ output = \"DP-2\", mode = \"preferred\", scale = 1.25})\n"));
    assert(strstr(updated, "-- trailing custom code\nhl.env(\"CUSTOM\", \"hello\")\n"));
    char *again = monitor_configuration(updated, get(p, "monitors"), error, sizeof error);
    assert(again && !strcmp(updated, again)); free(again); free(updated);
    updated = monitor_configuration("-- custom only", get(p, "monitors"), error, sizeof error);
    assert(updated && strstr(updated, "-- custom only\n\n-- BEGIN")); free(updated);
    assert(!monitor_configuration("-- BEGIN monitor-layout selector (managed automatically)\n", get(p, "monitors"), error, sizeof error));
    assert(!monitor_configuration("-- BEGIN monitor-layout selector (managed automatically)\nunexpected()\n-- END monitor-layout selector\n", get(p, "monitors"), error, sizeof error));
    const char *duplicate = "-- BEGIN monitor-layout selector (managed automatically)\nhl.monitor({output=\"DP-1\"})\nhl.monitor({output=\"DP-1\"})\n-- END monitor-layout selector\n";
    assert(!monitor_configuration(duplicate, get(p, "monitors"), error, sizeof error));
    const char *trailing = "-- BEGIN monitor-layout selector (managed automatically)\nhl.monitor({output=\"DP-1\", scale=1, })\n-- END monitor-layout selector\n";
    updated = monitor_configuration(trailing, get(p, "monitors"), error, sizeof error);
    assert(updated && !strstr(updated, ", ,")); free(updated);
    json_object_put(p); json_object_put(live);
}
int main(void) {
    test_geometry(); test_relations(); test_topology(); test_configuration();
    puts("monitor-model: geometry, relations, hotplug, validation and Lua preservation passed");
    return 0;
}
