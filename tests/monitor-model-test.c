#define _POSIX_C_SOURCE 200809L
#include "monitor-model.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *fixture =
"["
"{\"name\":\"eDP-1\",\"model\":\"Sharp\",\"description\":\"Sharp laptop\",\"width\":3840,\"height\":2160,\"refreshRate\":60,\"scale\":3,\"transform\":0,\"x\":0,\"y\":0,\"disabled\":false,\"availableModes\":[\"3840x2160@60.00Hz\"]},"
"{\"name\":\"DP-3\",\"model\":\"Mi\",\"description\":\"Xiaomi Mi Monitor\",\"width\":3440,\"height\":1440,\"refreshRate\":60,\"scale\":1,\"transform\":0,\"x\":-3440,\"y\":0,\"disabled\":false,\"availableModes\":[\"3440x1440@60.00Hz\",\"1920x1080@60.00Hz\"]},"
"{\"name\":\"DP-1\",\"model\":\"Z23i\",\"description\":\"HP Z23i 12345\",\"width\":1920,\"height\":1080,\"refreshRate\":60,\"scale\":1.6,\"transform\":1,\"x\":0,\"y\":-1200,\"disabled\":false,\"availableModes\":[\"1920x1080@60.00Hz\",\"1280x720@60.00Hz\"]}"
"]";
static char error[512];
static json_object *get(json_object *o, const char *key) { return json_object_object_get(o, key); }
static int number(json_object *o, const char *key) { return json_object_get_int(get(o, key)); }
static const char *string(json_object *o, const char *key) { return json_object_get_string(get(o, key)); }
static void integer(json_object *o, const char *key, int value) { json_object_object_add(o, key, json_object_new_int(value)); }
static void text(json_object *o, const char *key, const char *value) { json_object_object_add(o, key, json_object_new_string(value)); }
static void boolean(json_object *o, const char *key, bool value) { json_object_object_add(o, key, json_object_new_boolean(value)); }
static json_object *monitor(json_object *p, size_t index) { return json_object_array_get_idx(get(p, "monitors"), index); }
static json_object *profile(json_object *state, const char *id) { return get(get(state, "outputs"), id); }
static json_object *reconcile(json_object *live, json_object *state, bool topology) {
    json_object *p = monitor_reconcile(live, state, topology, error, sizeof error);
    if (!p) fprintf(stderr, "reconcile: %s\n", error);
    assert(p); return p;
}
static json_object *plan(json_object *live, json_object *state, const char *name,
                          const char *action, const char *value, const char *reference) {
    json_object *p = monitor_plan(live, state, name, action, value, reference, NULL, error, sizeof error);
    if (!p) fprintf(stderr, "%s: %s\n", action, error);
    assert(p); return p;
}
static void position(json_object *m, int x, int y) {
    assert(number(m, "x") == x); assert(number(m, "y") == y);
}
static json_object *legacy_state(void) {
    return json_tokener_parse("{\"version\":1,\"relations\":[],\"connected\":[],\"outputs\":{"
        "\"desc:HP old\":{\"name\":\"DP-1\",\"description\":\"HP old\",\"width\":1920,\"height\":1080,\"refreshRate\":60,\"mode\":\"1920x1080@60.00\",\"scale\":1.6,\"transform\":1,\"x\":0,\"y\":-1200,\"disabled\":false},"
        "\"desc:Beihai old\":{\"name\":\"DP-1\",\"description\":\"Beihai old\",\"width\":3440,\"height\":1440,\"refreshRate\":60,\"scale\":1,\"transform\":0,\"x\":-3440,\"y\":0,\"disabled\":false}}}");
}
static void test_new_model_recovers_without_stale_rules(void) {
    json_object *live = json_tokener_parse(fixture), *state = legacy_state();
    json_object *acer = json_object_array_get_idx(live, 2);
    text(acer, "model", "X34"); text(acer, "name", "DP-2"); text(acer, "description", "Acer X34 new");
    integer(acer, "width", 0); integer(acer, "height", 0); boolean(acer, "disabled", true);
    integer(acer, "x", 0); integer(acer, "y", 0);
    json_object_object_add(acer, "availableModes", json_tokener_parse("[\"3440x1440@60.00Hz\"]"));
    char *before = strdup(json_object_to_json_string(state));
    json_object *p = reconcile(live, state, true);
    assert(number(monitor(p, 2), "width") == 3440);
    assert(number(monitor(p, 2), "transform") == 0);
    assert(json_object_get_double(get(monitor(p, 2), "scale")) == 1);
    assert(!json_object_get_boolean(get(monitor(p, 2), "disabled")));
    assert(profile(get(p, "state"), "model:X34"));
    assert(get(profile(get(p, "state"), "model:X34"), "desiredDisabled"));
    assert(!json_object_get_boolean(get(profile(get(p, "state"), "model:X34"), "desiredDisabled")));
    assert(profile(get(p, "state"), "desc:HP old") && profile(get(p, "state"), "desc:Beihai old"));
    position(monitor(p, 0), 0, 0); position(monitor(p, 1), -3440, 0); position(monitor(p, 2), 1280, 0);
    const char *original = "-- custom\n-- BEGIN monitor-layout selector (managed automatically)\n"
        "hl.monitor({output=\"DP-1\",transform=1,bitdepth=10})\n"
        "hl.monitor({output=\"DP-99\",scale=2})\n-- END monitor-layout selector\n-- after\n";
    char *generated = monitor_configuration(original, get(p, "monitors"), state, error, sizeof error);
    assert(generated && !strstr(generated, "\"DP-1\"") && !strstr(generated, "DP-99"));
    assert(strstr(generated, "output = \"DP-2\"") && strstr(generated, "-- after"));
    assert(!strstr(generated, "desc:") && !strstr(generated, "bitdepth"));
    assert(!strcmp(before, json_object_to_json_string(state)));
    json_object *again = reconcile(get(p, "monitors"), get(p, "state"), false);
    position(monitor(again, 2), 1280, 0);
    free(before); free(generated); json_object_put(again); json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_ambiguous_connector_configuration_is_idempotent(void) {
    json_object *live = json_tokener_parse(fixture), *state = legacy_state();
    json_object *p = reconcile(live, state, true);
    const char *original = "-- BEGIN monitor-layout selector (managed automatically)\n\n"
        "hl.monitor({output=\"DP-1\",bitdepth=10})\n-- END monitor-layout selector\n";
    char *generated = monitor_configuration(original, get(p, "monitors"), get(p, "state"), error, sizeof error);
    assert(generated);
    char *again = monitor_configuration(generated, get(p, "monitors"), get(p, "state"), error, sizeof error);
    assert(again && !strcmp(generated, again));
    assert(strstr(generated, "automatically)\n\n"));
    free(again); free(generated); json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_first_inventory_does_not_save_accidental_off_state(void) {
    json_object *live = json_tokener_parse(fixture), *hp = json_object_array_get_idx(live, 2);
    integer(hp, "width", 0); integer(hp, "height", 0); boolean(hp, "disabled", true);
    integer(hp, "x", 0); integer(hp, "y", 0);
    json_object *state = monitor_initial_state(live);
    json_object *p = reconcile(live, state, true);
    assert(!json_object_get_boolean(get(monitor(p, 2), "disabled")));
    assert(number(monitor(p, 2), "width") == 1920 && number(monitor(p, 2), "transform") == 0);
    position(monitor(p, 2), 1280, 0);
    json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_model_survives_connector_description_changes(void) {
    json_object *live = json_tokener_parse(fixture), *state = monitor_initial_state(live);
    json_object *hp = json_object_array_get_idx(live, 2);
    text(hp, "name", "DP-9"); text(hp, "description", "HP new serial");
    integer(hp, "transform", 0); integer(hp, "width", 0); integer(hp, "height", 0); boolean(hp, "disabled", true);
    json_object *p = reconcile(live, state, true);
    assert(!strcmp(string(monitor(p, 2), "id"), "model:Z23i"));
    assert(number(monitor(p, 2), "transform") == 1 && number(monitor(p, 2), "width") == 1920);
    assert(!json_object_get_boolean(get(monitor(p, 2), "disabled")));
    position(monitor(p, 2), 0, -1200);
    const char *original = "-- BEGIN monitor-layout selector (managed automatically)\n"
        "hl.monitor({output=\"DP-1\",bitdepth=10})\n-- END monitor-layout selector\n";
    char *generated = monitor_configuration(original, get(p, "monitors"), state, error, sizeof error);
    assert(generated && strstr(generated, "output=\"DP-9\",bitdepth=10"));
    free(generated); json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_legacy_migration_uses_description_not_connector(void) {
    json_object *live = json_tokener_parse(fixture), *state = legacy_state();
    json_object *hp = json_object_array_get_idx(live, 2);
    text(hp, "description", "HP old"); text(hp, "name", "DP-9"); integer(hp, "transform", 0);
    json_object *p = reconcile(live, state, true);
    assert(number(monitor(p, 2), "transform") == 1);
    assert(profile(get(p, "state"), "model:Z23i"));
    assert(!profile(get(p, "state"), "desc:HP old"));
    assert(profile(get(p, "state"), "desc:Beihai old"));
    json_object *saved = profile(get(p, "state"), "model:Z23i");
    integer(saved, "transform", 3);
    json_object_object_add(get(get(p, "state"), "outputs"), "desc:HP old", json_object_get(profile(state, "desc:HP old")));
    json_object *preferred = reconcile(live, get(p, "state"), true);
    assert(number(monitor(preferred, 2), "transform") == 3);
    json_object_put(preferred); json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_duplicate_models_and_malformed_profiles(void) {
    json_object *live = json_tokener_parse(fixture), *state = monitor_initial_state(live);
    text(json_object_array_get_idx(live, 2), "model", "Mi");
    assert(!monitor_reconcile(live, state, true, error, sizeof error));
    assert(strstr(error, "model"));
    assert(!monitor_configuration("", live, state, error, sizeof error));
    text(json_object_array_get_idx(live, 2), "model", "Z23i");
    json_object_object_add(get(state, "outputs"), "desc:Bad", json_object_new_int(17));
    assert(!monitor_configuration("", live, state, error, sizeof error));
    assert(!monitor_reconcile(live, state, false, error, sizeof error));
    json_object_put(state); json_object_put(live);
}
static void test_geometry_reflow_and_independent_hotplug(void) {
    json_object *live = json_tokener_parse(fixture), *state = monitor_initial_state(live);
    json_object_object_add(state, "relations", json_tokener_parse("[{\"monitor\":\"old\",\"reference\":\"old\"}]"));
    boolean(state, "fallback", true);
    json_object *p = plan(live, state, "DP-1", "position", "left", "DP-3");
    position(monitor(p, 2), -4115, 120);
    assert(json_object_array_length(get(get(p, "state"), "relations")) == 0);
    assert(!json_object_get_boolean(get(get(p, "state"), "fallback")));
    json_object *scaled = plan(get(p, "monitors"), get(p, "state"), "DP-1", "scale-preset", "2", NULL);
    position(monitor(scaled, 2), -3980, 240);
    position(monitor(scaled, 0), 0, 0); position(monitor(scaled, 1), -3440, 0);
    json_object *rotated = plan(get(p, "monitors"), get(p, "state"), "DP-1", "rotation", "0", NULL);
    position(monitor(rotated, 2), -4640, 382);
    json_object_put(rotated);
    json_object *off = plan(get(p, "monitors"), get(p, "state"), "DP-3", "disable", NULL, NULL);
    position(monitor(off, 2), -4115, 120);
    json_object *without = json_object_new_array();
    json_object_array_add(without, json_object_get(monitor(p, 0)));
    json_object_array_add(without, json_object_get(monitor(p, 2)));
    json_object *remaining = reconcile(without, get(p, "state"), true);
    position(monitor(remaining, 1), -4115, 120);
    json_object_put(remaining); json_object_put(without); json_object_put(off); json_object_put(scaled);
    json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_intent_and_last_active_protection(void) {
    json_object *live = json_tokener_parse(fixture), *state = monitor_initial_state(live);
    json_object *hp = json_object_array_get_idx(live, 2), *saved = profile(state, "model:Z23i");
    json_object_object_del(saved, "desiredDisabled"); boolean(saved, "disabled", true);
    boolean(hp, "disabled", true); integer(hp, "width", 0); integer(hp, "height", 0);
    json_object *p = reconcile(live, state, true);
    assert(json_object_get_boolean(get(monitor(p, 2), "disabled")));
    json_object *enabled = plan(get(p, "monitors"), get(p, "state"), "DP-1", "enable", NULL, NULL);
    assert(!json_object_get_boolean(get(monitor(enabled, 2), "desiredDisabled")));
    json_object *off = plan(get(enabled, "monitors"), get(enabled, "state"), "DP-1", "disable", NULL, NULL);
    json_object *kept = reconcile(get(enabled, "monitors"), get(off, "state"), true);
    assert(json_object_get_boolean(get(monitor(kept, 2), "disabled")));
    json_object *active = json_tokener_parse(fixture);
    json_object *observed = reconcile(active, state, true);
    assert(!json_object_get_boolean(get(monitor(observed, 2), "disabled")));
    for (size_t i = 1; i < json_object_array_length(active); ++i) {
        integer(json_object_array_get_idx(active, i), "width", 0);
        integer(json_object_array_get_idx(active, i), "height", 0);
    }
    assert(!monitor_plan(active, state, "eDP-1", "disable", NULL, NULL, NULL, error, sizeof error));
    assert(strstr(error, "only active"));
    json_object_put(observed); json_object_put(active); json_object_put(kept); json_object_put(off);
    json_object_put(enabled); json_object_put(p); json_object_put(state); json_object_put(live);
}
static void test_mode_fallback_scale_and_recovery(void) {
    json_object *live = json_tokener_parse(fixture), *hp = json_object_array_get_idx(live, 2);
    integer(hp, "y", -3000);
    json_object *state = monitor_initial_state(live);
    json_object_object_add(hp, "availableModes", json_tokener_parse("[\"1366x768@60.00Hz\"]"));
    json_object *p = reconcile(live, state, true);
    assert(number(monitor(p, 2), "width") == 1366);
    assert(json_object_get_double(get(monitor(p, 2), "scale")) == 2);
    json_object *saved = profile(get(p, "state"), "model:Z23i");
    assert(number(saved, "width") == 1920 && json_object_get_boolean(get(saved, "modeFallback")));
    assert(json_object_get_double(get(saved, "scale")) == 1.6);
    json_object *scaled = plan(get(p, "monitors"), get(p, "state"), "DP-1", "scale", "1", NULL);
    assert(json_object_get_double(get(profile(get(scaled, "state"), "model:Z23i"), "scale")) == 1);
    json_object *explicit = plan(get(p, "monitors"), get(p, "state"), "DP-1", "mode", "1366x768@60.00", NULL);
    assert(!get(profile(get(explicit, "state"), "model:Z23i"), "modeFallback"));
    json_object_object_add(monitor(scaled, 2), "availableModes", json_tokener_parse("[\"1920x1080@60.00Hz\"]"));
    json_object *restored = reconcile(get(scaled, "monitors"), get(scaled, "state"), false);
    assert(number(monitor(restored, 2), "width") == 1920);
    assert(json_object_get_double(get(monitor(restored, 2), "scale")) == 1);
    json_object_put(restored); json_object_put(explicit); json_object_put(scaled); json_object_put(p);
    json_object_put(state); json_object_put(live);
}
static void test_healthy_position_import_and_configuration(void) {
    json_object *live = json_tokener_parse(fixture), *state = monitor_initial_state(live);
    json_object *hp = json_object_array_get_idx(live, 2);
    integer(hp, "x", 4000); integer(hp, "y", 1000);
    text(hp, "description", "Quoted \"description\" \\ with spaces");
    json_object *p = reconcile(live, state, false);
    position(monitor(p, 2), 4000, 1000);
    integer(hp, "width", 0); integer(hp, "height", 0); integer(hp, "x", 0); integer(hp, "y", 0);
    json_object *failed = reconcile(live, get(p, "state"), false);
    position(monitor(failed, 2), 4000, 1000);
    char *generated = monitor_configuration("-- custom only", get(p, "monitors"), state, error, sizeof error);
    assert(generated && strstr(generated, "-- custom only\n") && !strstr(generated, "desc:"));
    char *again = monitor_configuration(generated, get(p, "monitors"), get(p, "state"), error, sizeof error);
    assert(again && !strcmp(again, generated));
    free(again); free(generated); json_object_put(failed); json_object_put(p); json_object_put(state); json_object_put(live);
}
static void check_scale_options(int width, int height, const char *expected_json) {
    json_object *expected = json_tokener_parse(expected_json);
    for (int orientation = 0; orientation < 2; ++orientation) {
        int w = orientation ? height : width, h = orientation ? width : height;
        (void)snprintf(error, sizeof error, "previous error");
        json_object *options = monitor_scale_options(w, h, error, sizeof error);
        assert(options && !*error && json_object_is_type(options, json_type_array));
        assert(json_object_array_length(options) == json_object_array_length(expected));
        char fixture_text[512];
        (void)snprintf(fixture_text, sizeof fixture_text,
            "[{\"name\":\"DP-1\",\"model\":\"Test\",\"width\":%d,\"height\":%d,"
            "\"refreshRate\":60,\"scale\":1,\"transform\":0,\"x\":0,\"y\":0,\"disabled\":false,"
            "\"availableModes\":[\"%dx%d@60.00Hz\"]}]", w, h, w, h);
        json_object *live = json_tokener_parse(fixture_text);
        for (size_t i = 0; i < json_object_array_length(options); ++i) {
            json_object *value = json_object_array_get_idx(options, i);
            assert(json_object_is_type(value, json_type_double) || json_object_is_type(value, json_type_int));
            double scale = json_object_get_double(value);
            assert(scale == json_object_get_double(json_object_array_get_idx(expected, i)));
            char requested[32];
            (void)snprintf(requested, sizeof requested, "%.12g", scale);
            for (int transform = 0; transform < 4; ++transform) {
                integer(json_object_array_get_idx(live, 0), "transform", transform);
                json_object *state = monitor_initial_state(live);
                json_object *p = plan(live, state, "DP-1", "scale-preset", requested, NULL);
                assert(json_object_get_double(get(monitor(p, 0), "scale")) == scale);
                assert(number(monitor(p, 0), "transform") == transform);
                json_object_put(p); json_object_put(state);
            }
        }
        json_object_put(live); json_object_put(options);
    }
    json_object_put(expected);
}
static void test_scale_options(void) {
    check_scale_options(3840, 2160, "[1,1.25,1.5,1.6,2,2.5,3,4]");
    check_scale_options(6016, 3384, "[1,1.6,2,4]");
    check_scale_options(3440, 1440, "[1,1.25,1.6,2,2.5,4]");
    check_scale_options(1280, 800, "[1,1.25,1.6,2,2.5,4]");
    check_scale_options(4200, 4200, "[1,1.25,1.5,1.6,1.75,2,2.5,3,3.5,4]");
    check_scale_options(1, 1, "[1]");
    check_scale_options(65536, 65536, "[1,1.6,2,4]");
    check_scale_options(1, 65536, "[1]");
    const int invalid[] = {0, -1, INT_MIN, 65537, INT_MAX};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(!monitor_scale_options(invalid[i], 2160, error, sizeof error) && *error);
        assert(!monitor_scale_options(3840, invalid[i], error, sizeof error) && *error);
        assert(!monitor_scale_options(invalid[i], 2160, NULL, 0));
    }
    char tiny[1] = {'x'};
    assert(!monitor_scale_options(0, 0, tiny, sizeof tiny) && tiny[0] == '\0');
    json_object *options = monitor_scale_options(3840, 2160, NULL, 0);
    assert(options); json_object_put(options);
}
static void test_stale_scale_preset_is_rejected(void) {
    json_object *live = json_tokener_parse(fixture);
    assert(json_object_array_del_idx(live, 1, 2) == 0);
    json_object *state = monitor_initial_state(live);
    json_object *p = plan(live, state, "eDP-1", "scale-preset", "1.5", NULL);
    assert(json_object_get_double(get(monitor(p, 0), "scale")) == 1.5);
    json_object_put(p);
    /* Status offered 1.5 at 4K, but the live mode changed before the click. */
    json_object *m = json_object_array_get_idx(live, 0);
    integer(m, "width", 1280); integer(m, "height", 800);
    json_object_object_add(m, "scale", json_object_new_double(1));
    json_object_object_add(m, "availableModes", json_tokener_parse("[\"1280x800@60.00Hz\"]"));
    char *before_live = strdup(json_object_to_json_string(live));
    char *before_state = strdup(json_object_to_json_string(state));
    assert(!monitor_plan(live, state, "eDP-1", "scale-preset", "1.5", NULL, NULL, error, sizeof error));
    assert(strstr(error, "not supported"));
    assert(!strcmp(before_live, json_object_to_json_string(live)));
    assert(!strcmp(before_state, json_object_to_json_string(state)));
    p = plan(live, state, "eDP-1", "scale-preset", "1.6", NULL);
    assert(json_object_get_double(get(monitor(p, 0), "scale")) == 1.6);
    json_object_put(p);
    p = plan(live, state, "eDP-1", "scale", "1.5", NULL);
    assert(json_object_get_double(get(monitor(p, 0), "scale")) == 1.6);
    json_object_put(p); free(before_state); free(before_live);
    json_object_put(state); json_object_put(live);
}
static void test_custom_scales_still_normalize(void) {
    json_object *live = json_tokener_parse(fixture);
    assert(json_object_array_del_idx(live, 1, 2) == 0);
    json_object *state = monitor_initial_state(live);
    json_object *p = plan(live, state, "eDP-1", "scale", "1.7", NULL);
    assert(json_object_get_double(get(monitor(p, 0), "scale")) == 1.875);
    json_object_put(p);
    p = plan(live, state, "eDP-1", "scale", "1.3333333333333333", NULL);
    assert(json_object_get_double(get(monitor(p, 0), "scale")) == 160.0 / 120.0);
    json_object_put(p); json_object_put(state); json_object_put(live);
}
int main(void) {
    test_scale_options();
    test_stale_scale_preset_is_rejected();
    test_custom_scales_still_normalize();
    test_new_model_recovers_without_stale_rules();
    test_ambiguous_connector_configuration_is_idempotent();
    test_first_inventory_does_not_save_accidental_off_state();
    test_model_survives_connector_description_changes();
    test_legacy_migration_uses_description_not_connector();
    test_duplicate_models_and_malformed_profiles();
    test_geometry_reflow_and_independent_hotplug();
    test_intent_and_last_active_protection();
    test_mode_fallback_scale_and_recovery();
    test_healthy_position_import_and_configuration();
    puts("monitor-model: model profiles, legacy migration, geometry reflow, independent hotplug, intent and mode recovery passed");
    return 0;
}
