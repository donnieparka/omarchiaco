#define _POSIX_C_SOURCE 200809L
#include "monitor-model.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static json_object *get(json_object *o, const char *key) { return json_object_object_get(o, key); }
static json_object *at(json_object *o, size_t index) { return json_object_array_get_idx(o, index); }
static void integer(json_object *o, const char *key, int value) {
    json_object_object_add(o, key, json_object_new_int(value));
}
static void add(json_object *live, const char *name, int w, int h, double scale,
                int transform, int x, int y, bool disabled) {
    json_object *m = json_object_new_object();
    json_object_object_add(m, "name", json_object_new_string(name));
    json_object_object_add(m, "model", json_object_new_string(name));
    integer(m, "width", w); integer(m, "height", h); integer(m, "transform", transform);
    integer(m, "x", x); integer(m, "y", y);
    json_object_object_add(m, "scale", json_object_new_double(scale));
    json_object_object_add(m, "refreshRate", json_object_new_double(60));
    json_object_object_add(m, "disabled", json_object_new_boolean(disabled));
    json_object *modes = json_object_new_array();
    char mode[80];
    (void)snprintf(mode, sizeof mode, "%dx%d@60.00Hz", w, h);
    json_object_array_add(modes, json_object_new_string(mode));
    json_object_array_add(modes, json_object_new_string("1280x720@60.00Hz"));
    json_object_object_add(m, "availableModes", modes);
    json_object_array_add(live, m);
}
static void require(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
static void expect_position(json_object *m, int x, int y) {
    int actual_x = json_object_get_int(get(m, "x"));
    int actual_y = json_object_get_int(get(m, "y"));
    if (actual_x != x || actual_y != y) {
        fprintf(stderr, "%s: expected (%d,%d), actual (%d,%d)\n",
                json_object_get_string(get(m, "name")), x, y, actual_x, actual_y);
        exit(1);
    }
}
static void position(json_object *p, size_t index, int x, int y) {
    json_object *m = at(get(p, "monitors"), index);
    expect_position(m, x, y);
    expect_position(get(get(get(p, "state"), "outputs"), json_object_get_string(get(m, "id"))), x, y);
}
static json_object *request(json_object *live, json_object *state, const char *name,
                            const char *action, const char *value, bool success) {
    char *before_live = strdup(json_object_to_json_string(live));
    char *before_state = strdup(json_object_to_json_string(state));
    char error[512] = "";
    json_object *p = monitor_plan(live, state, name, action, value, "", "center", error, sizeof error);
    if (success && !p) { fprintf(stderr, "%s plan: %s\n", action, error); exit(1); }
    require(success == (p != NULL), "unexpected plan success/failure");
    require(!strcmp(before_live, json_object_to_json_string(live)), "plan mutated its live input");
    require(!strcmp(before_state, json_object_to_json_string(state)), "plan mutated saved preferences");
    if (!success) require(*error, "rejected layout needs an error");
    if (p) require(json_object_array_length(get(get(p, "state"), "relations")) == 0,
                   "reflow must not introduce persistent dependencies");
    free(before_live); free(before_state);
    return p;
}
static json_object *change(json_object *live, const char *name, const char *action, const char *value) {
    json_object *state = monitor_initial_state(live);
    json_object *p = request(live, state, name, action, value, true);
    json_object_put(state);
    return p;
}
static void test_effective_scale_and_roundtrip(void) {
    json_object *live = json_object_new_array();
    add(live, "eDP-1", 3840, 2160, 2, 0, 0, 0, false);
    add(live, "DP-1", 3440, 1440, 1.6, 0, -115, -900, false);
    json_object *p = change(live, "eDP-1", "scale", "1.7");
    require(json_object_get_double(get(at(get(p, "monitors"), 0), "scale")) == 1.875,
            "reflow must use the effective whole-pixel scale");
    position(p, 0, 0, 0); position(p, 1, -51, -900);
    json_object *back = request(get(p, "monitors"), get(p, "state"), "eDP-1", "scale", "2", true);
    position(back, 0, 0, 0); position(back, 1, -115, -900);
    json_object_put(back); json_object_put(p); json_object_put(live);
}
static void test_sides_and_alignments(void) {
    struct example { int x, y, result_x, result_y; const char *target; } examples[] = {
        {0, -600, 0, -300, "DP-1"},
        {100, -600, 300, -300, "DP-1"},
        {200, -600, 600, -300, "DP-1"},
        {100, 800, 300, 800, "DP-1"},
        {-800, 100, -400, 250, "DP-1"},
        {1000, 100, 1000, 250, "DP-1"},
        {100, -600, -150, -600, "eDP-1"},
        {100, 800, -150, 400, "eDP-1"},
        {-800, 100, -800, -100, "eDP-1"},
        {1000, 100, 500, -100, "eDP-1"},
        {1100, 130, 1100, 280, "DP-1"},
        {1100, 30, 1100, 30, "DP-1"},
        {200, -600, -300, -600, "eDP-1"},
        {0, -600, 0, -600, "eDP-1"}
    };
    for (size_t i = 0; i < sizeof examples / sizeof examples[0]; ++i) {
        json_object *live = json_object_new_array();
        add(live, "eDP-1", 1000, 800, 1, 0, 0, 0, false);
        add(live, "DP-1", 800, 600, 1, 0, examples[i].x, examples[i].y, false);
        json_object *p = change(live, examples[i].target, "scale", "2");
        position(p, 0, 0, 0); position(p, 1, examples[i].result_x, examples[i].result_y);
        json_object_put(p); json_object_put(live);
    }
}
static void test_chain_and_inventory_order(void) {
    for (int reversed = 0; reversed < 2; ++reversed) {
        json_object *live = json_object_new_array();
        if (!reversed) add(live, "eDP-1", 1000, 800, 1, 0, 0, 0, false);
        add(live, "DP-1", 800, 600, 1, 0, 1000, 100, false);
        add(live, "DP-2", 600, 400, 1, 0, 1800, 200, false);
        if (reversed) add(live, "eDP-1", 1000, 800, 1, 0, 0, 0, false);
        json_object *p = change(live, "DP-1", "scale", "2");
        position(p, reversed ? 2U : 0U, 0, 0);
        position(p, reversed ? 0U : 1U, 1000, 250);
        position(p, reversed ? 1U : 2U, 1400, 200);
        json_object_put(p); json_object_put(live);
    }
}
static void test_mode_rotation_and_unchanged_size(void) {
    json_object *live = json_object_new_array();
    add(live, "eDP-1", 1000, 800, 1, 0, 0, 0, false);
    add(live, "DP-1", 800, 600, 1, 0, 1000, 100, false);
    json_object *p = change(live, "eDP-1", "rotation", "1");
    position(p, 0, 0, 0); position(p, 1, 800, 200);
    json_object_put(p);
    p = change(live, "eDP-1", "rotation", "2");
    position(p, 0, 0, 0); position(p, 1, 1000, 100);
    json_object_put(p);
    p = change(live, "eDP-1", "mode", "1280x720@60.00");
    position(p, 0, 0, 0); position(p, 1, 1280, 60);
    json_object_put(p);
    p = change(live, "eDP-1", "mode", "1000x800@60.00");
    position(p, 0, 0, 0); position(p, 1, 1000, 100);
    json_object_put(p); json_object_put(live);
}
static void test_laptop_anchor_and_external_fallback(void) {
    json_object *live = json_object_new_array();
    add(live, "DP-1", 800, 600, 1, 0, 0, 0, false);
    add(live, "eDP-1", 1000, 800, 1, 0, -1000, -100, false);
    json_object *p = change(live, "eDP-1", "scale", "2");
    position(p, 0, -500, -200); position(p, 1, -1000, -100);
    json_object_put(p); json_object_put(live);
    live = json_object_new_array();
    add(live, "DP-1", 1000, 800, 1, 0, 20, 20, false);
    add(live, "DP-2", 800, 600, 1, 0, 1020, 120, false);
    add(live, "eDP-1", 3840, 2160, 2, 0, -3000, 0, true);
    json_object *state = monitor_initial_state(live);
    json_object *disabled = json_tokener_parse(json_object_to_json_string(at(live, 2)));
    json_object_object_add(disabled, "desiredDisabled", json_object_new_boolean(true));
    json_object_object_add(get(state, "outputs"), "model:eDP-1", disabled);
    p = request(live, state, "DP-1", "scale", "2", true);
    position(p, 0, 20, 20); position(p, 1, 520, -80); position(p, 2, -3000, 0);
    json_object_put(state); json_object_put(p); json_object_put(live);
    live = json_object_new_array();
    add(live, "DP-1", 1000, 800, 1, 0, 10, 20, false);
    add(live, "DP-2", 800, 600, 1, 0, 1010, 120, false);
    p = change(live, "DP-1", "scale", "2");
    position(p, 0, 10, 20); position(p, 1, 510, -80);
    json_object_put(p); json_object_put(live);
}
static void test_live_layout_wins_over_stale_state(void) {
    json_object *live = json_object_new_array();
    add(live, "eDP-1", 1000, 800, 1, 0, 0, 0, false);
    add(live, "DP-1", 800, 600, 1, 0, 1000, 100, false);
    json_object *state = monitor_initial_state(live);
    integer(at(live, 0), "x", 50); integer(at(live, 0), "y", 20);
    integer(at(live, 1), "x", 1050); integer(at(live, 1), "y", 120);
    json_object *p = request(live, state, "eDP-1", "scale", "2", true);
    position(p, 0, 50, 20); position(p, 1, 550, -80);
    json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_non_geometry_actions_and_disconnected_profiles(void) {
    json_object *live = json_object_new_array();
    add(live, "eDP-1", 1000, 800, 1, 0, 0, 0, false);
    add(live, "DP-1", 800, 600, 1, 0, 1000, 100, false);
    add(live, "DP-2", 600, 400, 1, 0, 1800, 200, false);
    json_object *p = change(live, "DP-1", "disable", "");
    position(p, 0, 0, 0); position(p, 2, 1800, 200);
    json_object_put(p);
    json_object *state = monitor_initial_state(live);
    json_object_array_del_idx(live, 2, 1);
    p = request(live, state, "eDP-1", "scale", "2", true);
    json_object *disconnected = get(get(get(p, "state"), "outputs"), "model:DP-2");
    expect_position(disconnected, 1800, 200);
    require(json_object_array_length(get(get(p, "state"), "connected")) == 2,
            "reflow must not revive a disconnected output");
    json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_reject_overlap_and_coordinate_overflow(void) {
    json_object *live = json_object_new_array();
    add(live, "eDP-1", 1000, 800, 1, 0, 0, 0, false);
    add(live, "DP-1", 800, 600, 1, 0, -800, 100, false);
    add(live, "DP-2", 800, 600, 1, 0, 100, -600, false);
    json_object *state = monitor_initial_state(live);
    request(live, state, "eDP-1", "scale", "2", false);
    json_object_put(state); json_object_put(live);
    live = json_object_new_array();
    add(live, "eDP-1", 1000, 800, 2, 0, INT_MAX - 200, 0, false);
    add(live, "DP-1", 800, 600, 2, 0, INT_MAX - 150, -300, false);
    state = monitor_initial_state(live);
    request(live, state, "eDP-1", "scale", "1", false);
    json_object_put(state); json_object_put(live);
}
int main(void) {
    test_effective_scale_and_roundtrip();
    test_sides_and_alignments();
    test_chain_and_inventory_order();
    test_mode_rotation_and_unchanged_size();
    test_laptop_anchor_and_external_fallback();
    test_live_layout_wins_over_stale_state();
    test_non_geometry_actions_and_disconnected_profiles();
    test_reject_overlap_and_coordinate_overflow();
    puts("monitor-reflow: effective scale, sides, alignment, chains, anchors, mode/rotation, state and rejection passed");
    return 0;
}
