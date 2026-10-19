#define _POSIX_C_SOURCE 200809L
#include "monitor-model.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BEGIN "-- BEGIN monitor-layout selector (managed automatically)"
#define END "-- END monitor-layout selector"
#define MAX_OUTPUTS 128U

struct text { char *data; size_t length, capacity; };

static void fail(char *error, size_t size, const char *format, ...) {
    if (!error || !size) return;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(error, size, format, args);
    va_end(args);
}
static json_object *get(json_object *o, const char *key) {
    json_object *v = NULL;
    if (o && json_object_is_type(o, json_type_object))
        (void)json_object_object_get_ex(o, key, &v);
    return v;
}
static const char *str(json_object *o, const char *key) {
    json_object *v = get(o, key);
    return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : "";
}
static int number(json_object *o, const char *key) { return json_object_get_int(get(o, key)); }
static double real(json_object *o, const char *key) { return json_object_get_double(get(o, key)); }
static bool flag(json_object *o, const char *key) { return json_object_get_boolean(get(o, key)) != 0; }
static void putstr(json_object *o, const char *key, const char *v) {
    json_object_object_add(o, key, json_object_new_string(v));
}
static void putint(json_object *o, const char *key, int v) {
    json_object_object_add(o, key, json_object_new_int(v));
}
static void putreal(json_object *o, const char *key, double v) {
    json_object_object_add(o, key, json_object_new_double(v));
}
static json_object *copy(json_object *o) {
    json_object *r = NULL;
    if (json_object_deep_copy(o, &r, NULL) != 0) return NULL;
    return r;
}
static bool append(struct text *b, const char *s, size_t n) {
    if (n > SIZE_MAX - b->length - 1) return false;
    size_t wanted = b->length + n + 1;
    if (wanted > b->capacity) {
        size_t capacity = wanted > SIZE_MAX / 2 ? wanted : wanted * 2;
        char *next = realloc(b->data, capacity);
        if (!next) return false;
        b->data = next;
        b->capacity = capacity;
    }
    memcpy(b->data + b->length, s, n);
    b->length += n;
    b->data[b->length] = '\0';
    return true;
}
static bool adds(struct text *b, const char *s) { return append(b, s, strlen(s)); }
static bool active(json_object *m) {
    return !flag(m, "disabled") && number(m, "width") > 0 && number(m, "height") > 0;
}
static bool valid_live(json_object *live) {
    if (!live || !json_object_is_type(live, json_type_array) ||
        json_object_array_length(live) > MAX_OUTPUTS) return false;
    for (size_t i = 0; i < json_object_array_length(live); ++i) {
        json_object *m = json_object_array_get_idx(live, i);
        if (!*str(m, "name") || !*str(m, "model")) return false;
        for (size_t j = 0; j < i; ++j) {
            json_object *other = json_object_array_get_idx(live, j);
            if (!strcmp(str(m, "name"), str(other, "name")) ||
                !strcmp(str(m, "model"), str(other, "model"))) return false;
        }
    }
    return true;
}
static char *identity(json_object *live, json_object *m) {
    (void)live;
    size_t size = strlen(str(m, "model")) + 7;
    char *id = malloc(size);
    if (id) (void)snprintf(id, size, "model:%s", str(m, "model"));
    return id;
}
static json_object *find_monitor(json_object *monitors, const char *key, const char *value) {
    if (!value) return NULL;
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        if (!strcmp(str(m, key), value)) return m;
    }
    return NULL;
}
static char *normalize_mode(const char *mode) {
    size_t n = strlen(mode);
    if (n >= 2 && !strcmp(mode + n - 2, "Hz")) n -= 2;
    return strndup(mode, n);
}
static bool parse_mode(const char *mode, int *w, int *h, double *hz) {
    int end = 0;
    if (sscanf(mode, "%dx%d@%lf%n", w, h, hz, &end) != 3 || end <= 0 ||
        mode[end] != '\0' || *w <= 0 || *h <= 0 || *w > 65536 || *h > 65536 ||
        !isfinite(*hz) || *hz <= 0 || *hz > 10000) return false;
    return true;
}
static void set_live_mode(json_object *m) {
    char mode[128];
    int w = number(m, "width"), h = number(m, "height");
    double hz = real(m, "refreshRate");
    if (w <= 0 || h <= 0 || !isfinite(hz) || hz <= 0) {
        putstr(m, "mode", "preferred");
        return;
    }
    json_object *modes = get(m, "availableModes");
    if (modes && json_object_is_type(modes, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(modes); ++i) {
            const char *raw = json_object_get_string(json_object_array_get_idx(modes, i));
            char *candidate = normalize_mode(raw ? raw : "");
            int cw = 0, ch = 0;
            double rate = 0;
            bool match = candidate && parse_mode(candidate, &cw, &ch, &rate) &&
                         cw == w && ch == h && fabs(rate - hz) < 0.02;
            if (match) putstr(m, "mode", candidate);
            free(candidate);
            if (match) return;
        }
    }
    (void)snprintf(mode, sizeof mode, "%dx%d@%.5f", w, h, hz);
    putstr(m, "mode", mode);
}
static bool preferred_mode(json_object *m) {
    json_object *modes = get(m, "availableModes");
    if (!json_object_is_type(modes, json_type_array)) return false;
    for (size_t i = 0; i < json_object_array_length(modes); ++i) {
        const char *raw = json_object_get_string(json_object_array_get_idx(modes, i));
        char *candidate = normalize_mode(raw ? raw : "");
        int w = 0, h = 0; double hz = 0;
        bool usable = candidate && parse_mode(candidate, &w, &h, &hz);
        if (usable) {
            putstr(m, "mode", candidate);
            putint(m, "width", w); putint(m, "height", h); putreal(m, "refreshRate", hz);
        }
        free(candidate);
        if (usable) return true;
    }
    return false;
}
static bool mode_available(json_object *m, const char *wanted) {
    if (!*wanted || !strcmp(wanted, "preferred")) return true;
    json_object *modes = get(m, "availableModes");
    if (!json_object_is_type(modes, json_type_array)) return true;
    int ww = 0, wh = 0; double wr = 0;
    if (!parse_mode(wanted, &ww, &wh, &wr)) return false;
    for (size_t i = 0; i < json_object_array_length(modes); ++i) {
        const char *raw = json_object_get_string(json_object_array_get_idx(modes, i));
        char *candidate = normalize_mode(raw ? raw : "");
        int w = 0, h = 0; double hz = 0;
        bool match = candidate && parse_mode(candidate, &w, &h, &hz) &&
                     w == ww && h == wh && fabs(hz - wr) < 0.02;
        free(candidate);
        if (match) return true;
    }
    return false;
}
static const char *saved_keys[] = {
    "id", "name", "model", "description", "width", "height", "refreshRate", "scale",
    "transform", "x", "y", "disabled", "mode", "desiredDisabled"
};
static json_object *saved_monitor(json_object *m) {
    json_object *o = json_object_new_object();
    for (size_t k = 0; k < sizeof saved_keys / sizeof saved_keys[0]; ++k) {
        json_object *v = get(m, saved_keys[k]);
        if (v) json_object_object_add(o, saved_keys[k], json_object_get(v));
    }
    json_object *desired = get(m, "_desiredGeometry");
    if (desired) {
        const char *keys[] = {"mode", "width", "height", "refreshRate", "scale"};
        for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
            json_object *v = get(desired, keys[k]);
            if (v) json_object_object_add(o, keys[k], json_object_get(v));
        }
        json_object_object_add(o, "modeFallback", json_object_new_boolean(true));
    }
    return o;
}
json_object *monitor_initial_state(json_object *live) {
    if (!valid_live(live)) return NULL;
    json_object *state = json_object_new_object(), *outputs = json_object_new_object();
    json_object *connected = json_object_new_array();
    putint(state, "version", 1);
    json_object_object_add(state, "fallback", json_object_new_boolean(false));
    json_object_object_add(state, "outputs", outputs);
    json_object_object_add(state, "relations", json_object_new_array());
    json_object_object_add(state, "connected", connected);
    for (size_t i = 0; i < json_object_array_length(live); ++i) {
        /* A first inventory's inactive observation is not a saved preference.
         * Reconciliation will initialize it from advertised modes instead. */
        if (!active(json_object_array_get_idx(live, i))) continue;
        json_object *m = copy(json_object_array_get_idx(live, i));
        char *id = identity(live, m);
        if (!m || !id) { json_object_put(m); free(id); json_object_put(state); return NULL; }
        putstr(m, "id", id);
        set_live_mode(m);
        if (active(m)) json_object_object_add(m, "desiredDisabled", json_object_new_boolean(false));
        json_object_object_add(outputs, id, saved_monitor(m));
        if (active(m)) json_object_array_add(connected, json_object_new_string(id));
        free(id);
        json_object_put(m);
    }
    return state;
}
static bool valid_state(json_object *s) {
    if (!s || number(s, "version") != 1 ||
        !json_object_is_type(get(s, "outputs"), json_type_object) ||
        !json_object_is_type(get(s, "relations"), json_type_array)) return false;
    json_object_object_foreach(get(s, "outputs"), id, saved) {
        (void)id;
        if (!json_object_is_type(saved, json_type_object)) return false;
    }
    return true;
}
static bool apply_scale(json_object *m, double requested, char *error, size_t size);
static json_object *prepare(json_object *live, json_object *input_state,
                            bool import_geometry, char *error, size_t size) {
    if (!valid_live(live) || (input_state && !valid_state(input_state))) {
        fail(error, size, "Invalid saved state or monitor inventory: connected models must be nonempty and unique"); return NULL;
    }
    json_object *state = input_state ? copy(input_state) : monitor_initial_state(live);
    json_object *monitors = copy(live), *plan = json_object_new_object();
    if (!state || !monitors || !plan) {
        json_object_put(state); json_object_put(monitors); json_object_put(plan);
        fail(error, size, "Out of memory"); return NULL;
    }
    json_object_object_add(plan, "monitors", monitors);
    json_object_object_add(plan, "state", state);
    /* Legacy dependencies are discarded; each profile owns absolute coordinates. */
    json_object_object_add(state, "relations", json_object_new_array());
    json_object_object_add(state, "fallback", json_object_new_boolean(false));
    json_object *outputs = get(state, "outputs"), *connected = json_object_new_array();
    json_object_object_add(state, "connected", connected);
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        char *id = identity(live, m);
        if (!id) { json_object_put(plan); fail(error, size, "Out of memory"); return NULL; }
        putstr(m, "id", id);
        json_object_object_del(m, "_desiredGeometry");
        json_object_object_del(m, "_newPlacement");
        set_live_mode(m);
        json_object *saved = get(outputs, id);
        if (!saved && *str(m, "description")) {
            const char *legacy_id = NULL;
            json_object_object_foreach(outputs, old_id, old) {
                if (!strncmp(old_id, "model:", 6) || strcmp(str(old, "description"), str(m, "description"))) continue;
                if (legacy_id) {
                    fail(error, size, "Multiple legacy profiles match model %s", str(m, "model"));
                    free(id); json_object_put(plan); return NULL;
                }
                legacy_id = old_id;
                saved = old;
            }
            if (legacy_id) {
                json_object_object_add(outputs, id, json_object_get(saved));
                json_object_object_del(outputs, legacy_id);
            }
        }
        bool unknown = !input_state || !saved;
        bool reused_connector = false;
        json_object_object_foreach(outputs, prior_id, prior) {
            if (strcmp(prior_id, id) && !strcmp(str(prior, "name"), str(m, "name"))) reused_connector = true;
        }
        if (unknown) json_object_object_add(m, "_newPlacement", json_object_new_boolean(true));
        if (unknown && (!active(m) || reused_connector)) {
            putreal(m, "scale", 1); putint(m, "transform", 0);
            putint(m, "x", 0); putint(m, "y", 0);
            bool usable = preferred_mode(m);
            if (!usable) {
                putint(m, "width", 0); putint(m, "height", 0); putstr(m, "mode", "preferred");
            }
            json_object_object_add(m, "disabled", json_object_new_boolean(!usable));
        }
        if (unknown && active(m))
            json_object_object_add(m, "desiredDisabled", json_object_new_boolean(false));
        bool was_active = active(m);
        json_object *desired = get(saved, "desiredDisabled");
        bool restore_enabled = saved && (json_object_is_type(desired, json_type_boolean) ?
                               !json_object_get_boolean(desired) : !flag(saved, "disabled"));
        if (saved) {
            bool saved_geometry = number(saved, "width") > 0 && number(saved, "height") > 0;
            for (size_t k = 0; k < sizeof saved_keys / sizeof saved_keys[0]; ++k) {
                const char *key = saved_keys[k];
                if (!strcmp(key, "id") || !strcmp(key, "name") || !strcmp(key, "model") || !strcmp(key, "description") ||
                    !strcmp(key, "disabled")) continue;
                if (import_geometry && was_active && (!strcmp(key, "x") || !strcmp(key, "y"))) continue;
                if ((!saved_geometry || (import_geometry && was_active && !flag(saved, "modeFallback"))) &&
                    (!strcmp(key, "width") || !strcmp(key, "height") ||
                     !strcmp(key, "refreshRate") || !strcmp(key, "scale") ||
                     !strcmp(key, "transform") || !strcmp(key, "mode"))) continue;
                json_object *v = get(saved, key);
                if (v) json_object_object_add(m, key, json_object_get(v));
            }
            if (json_object_is_type(desired, json_type_boolean))
                json_object_object_add(m, "disabled", json_object_get(desired));
            else if (!was_active && saved_geometry && restore_enabled)
                json_object_object_add(m, "disabled", json_object_new_boolean(false));
        }
        if (!was_active && restore_enabled &&
            (number(m, "width") <= 0 || number(m, "height") <= 0 || flag(m, "disabled"))) {
            if (preferred_mode(m)) {
                putreal(m, "scale", 1); putint(m, "transform", 0);
                json_object_object_add(m, "disabled", json_object_new_boolean(false));
            }
        }
        if (saved && number(saved, "width") > 0 && number(saved, "height") > 0 &&
            !mode_available(m, str(saved, "mode"))) {
            json_object_object_add(m, "_desiredGeometry", copy(saved));
            if (!preferred_mode(m)) {
                putstr(m, "mode", "preferred");
                putint(m, "width", 0); putint(m, "height", 0);
                json_object_object_add(m, "disabled", json_object_new_boolean(true));
            } else if (!apply_scale(m, real(m, "scale"), error, size)) {
                free(id); json_object_put(plan); return NULL;
            }
        }
        json_object_object_add(outputs, id, saved_monitor(m));
        if (active(m)) json_object_array_add(connected, json_object_new_string(id));
        free(id);
    }
    return plan;
}
static bool logical_size(json_object *m, int *w, int *h) {
    int width = number(m, "width"), height = number(m, "height");
    double scale = real(m, "scale");
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536 ||
        !isfinite(scale) || scale < 1 || scale > 4) return false;
    if (number(m, "transform") & 1) { int temp = width; width = height; height = temp; }
    *w = (int)llround((double)width / scale);
    *h = (int)llround((double)height / scale);
    return *w > 0 && *h > 0;
}
static bool place(json_object *m, json_object *reference, const char *d, const char *align,
                   char *error, size_t size) {
    if ((strcmp(d, "left") && strcmp(d, "right") && strcmp(d, "up") && strcmp(d, "down")) ||
        (strcmp(align, "start") && strcmp(align, "center") && strcmp(align, "end"))) {
        fail(error, size, "Invalid direction or alignment"); return false;
    }
    int w = 0, h = 0, rw = 0, rh = 0;
    if (!logical_size(m, &w, &h) || !logical_size(reference, &rw, &rh)) {
        fail(error, size, "A monitor has no usable video geometry"); return false;
    }
    int64_t x = number(reference, "x"), y = number(reference, "y");
    bool horizontal = !strcmp(d, "left") || !strcmp(d, "right");
    int delta = horizontal ? rh - h : rw - w;
    int offset = !strcmp(align, "center") ? (int)floor((double)delta / 2.0) :
                 !strcmp(align, "end") ? delta : 0;
    if (horizontal) { x += !strcmp(d, "left") ? -w : rw; y += offset; }
    else { y += !strcmp(d, "up") ? -h : rh; x += offset; }
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
        fail(error, size, "Monitor position is out of range"); return false;
    }
    putint(m, "x", (int)x); putint(m, "y", (int)y);
    return true;
}
static bool no_overlap(json_object *monitors, char *error, size_t size) {
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *a = json_object_array_get_idx(monitors, i);
        if (!active(a)) continue;
        int aw = 0, ah = 0;
        if (!logical_size(a, &aw, &ah)) { fail(error, size, "Invalid monitor geometry"); return false; }
        int64_t ax = number(a, "x"), ay = number(a, "y");
        for (size_t j = 0; j < i; ++j) {
            json_object *b = json_object_array_get_idx(monitors, j);
            if (!active(b)) continue;
            int bw = 0, bh = 0;
            if (!logical_size(b, &bw, &bh)) return false;
            int64_t bx = number(b, "x"), by = number(b, "y");
            if (ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah) {
                fail(error, size, "%s would overlap %s", str(a, "name"), str(b, "name")); return false;
            }
        }
    }
    return true;
}
struct rectangle { int64_t x, y; int w, h; };
struct attachment {
    size_t child, parent;
    const char *direction, *alignment;
    int64_t gap, offset;
    long double distance;
    int overlap;
};
static int half_floor(int value) {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}
static struct attachment attachment(struct rectangle child, struct rectangle parent) {
    int64_t dx = child.x >= parent.x + parent.w ? child.x - parent.x - parent.w :
                 parent.x >= child.x + child.w ? parent.x - child.x - child.w : 0;
    int64_t dy = child.y >= parent.y + parent.h ? child.y - parent.y - parent.h :
                 parent.y >= child.y + child.h ? parent.y - child.y - child.h : 0;
    bool separated_x = child.x >= parent.x + parent.w || parent.x >= child.x + child.w;
    bool separated_y = child.y >= parent.y + parent.h || parent.y >= child.y + child.h;
    bool horizontal = separated_x && (!separated_y || dx >= dy);
    struct attachment result = {0};
    result.distance = (long double)dx * dx + (long double)dy * dy;
    result.gap = horizontal ? dx : dy;
    result.direction = horizontal ? (child.x < parent.x ? "left" : "right") :
                                   (child.y < parent.y ? "up" : "down");
    int64_t child_start = horizontal ? child.y : child.x;
    int64_t parent_start = horizontal ? parent.y : parent.x;
    int child_extent = horizontal ? child.h : child.w;
    int parent_extent = horizontal ? parent.h : parent.w;
    int64_t start = child_start > parent_start ? child_start : parent_start;
    int64_t end = child_start + child_extent < parent_start + parent_extent ?
                  child_start + child_extent : parent_start + parent_extent;
    result.overlap = end > start ? (int)(end - start) : 0;
    int offsets[] = {half_floor(parent_extent - child_extent), 0, parent_extent - child_extent};
    const char *alignments[] = {"center", "start", "end"};
    int64_t actual = child_start - parent_start;
    result.alignment = alignments[0];
    result.offset = actual - offsets[0];
    for (size_t i = 1; i < sizeof offsets / sizeof offsets[0]; ++i) {
        int64_t candidate = actual - offsets[i];
        if (llabs(candidate) < llabs(result.offset)) {
            result.alignment = alignments[i]; result.offset = candidate;
        }
    }
    return result;
}
static bool embedded_output(json_object *m) {
    const char *name = str(m, "name");
    return !strncmp(name, "eDP-", 4) || !strncmp(name, "LVDS-", 5) || !strncmp(name, "DSI-", 4);
}
static long double origin_distance(struct rectangle r) {
    return (long double)r.x * r.x + (long double)r.y * r.y;
}
/* Infer a temporary nearest-neighbour tree from the old rectangles, not saved
 * dependencies. Keep its root fixed and replay attachments with the new sizes.
 * Nothing about hotplug, enable or disable invokes this geometry-only operation. */
static bool reflow(json_object *before, json_object *monitors, char *error, size_t size) {
    struct rectangle old[MAX_OUTPUTS] = {{0}};
    bool included[MAX_OUTPUTS] = {false}, placed[MAX_OUTPUTS] = {false};
    size_t count = json_object_array_length(monitors), remaining = 0, root = count;
    bool resized = false;
    for (size_t i = 0; i < count; ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        json_object *prior = json_object_array_get_idx(before, i);
        if (!active(m) || !active(prior)) continue;
        int w = 0, h = 0;
        if (!logical_size(prior, &old[i].w, &old[i].h) || !logical_size(m, &w, &h)) {
            fail(error, size, "Invalid monitor geometry"); return false;
        }
        old[i].x = number(prior, "x"); old[i].y = number(prior, "y");
        included[i] = true; ++remaining;
        resized = resized || w != old[i].w || h != old[i].h;
        /* A healthy live placement wins over stale saved coordinates. */
        putint(m, "x", (int)old[i].x); putint(m, "y", (int)old[i].y);
        json_object *anchor = root < count ? json_object_array_get_idx(before, root) : NULL;
        if (root == count || (embedded_output(prior) && !embedded_output(anchor)) ||
            (embedded_output(prior) == embedded_output(anchor) &&
             (origin_distance(old[i]) < origin_distance(old[root]) ||
              (origin_distance(old[i]) == origin_distance(old[root]) &&
               strcmp(str(prior, "name"), str(anchor, "name")) < 0)))) root = i;
    }
    if (!resized || remaining < 2) return true;
    if (!no_overlap(before, error, size)) return false;
    placed[root] = true; --remaining;
    while (remaining) {
        struct attachment best = {0};
        bool found = false;
        for (size_t i = 0; i < count; ++i) {
            if (!included[i] || placed[i]) continue;
            for (size_t j = 0; j < count; ++j) {
                if (!placed[j]) continue;
                struct attachment candidate = attachment(old[i], old[j]);
                candidate.child = i; candidate.parent = j;
                int child_order = found ? strcmp(str(json_object_array_get_idx(before, i), "name"),
                                                 str(json_object_array_get_idx(before, best.child), "name")) : 0;
                int parent_order = found ? strcmp(str(json_object_array_get_idx(before, j), "name"),
                                                  str(json_object_array_get_idx(before, best.parent), "name")) : 0;
                if (!found || candidate.distance < best.distance ||
                    (candidate.distance == best.distance &&
                     (candidate.overlap > best.overlap ||
                      (candidate.overlap == best.overlap &&
                       (child_order < 0 || (!child_order && parent_order < 0)))))) {
                    best = candidate; found = true;
                }
            }
        }
        if (!found) { fail(error, size, "Cannot reconstruct monitor layout"); return false; }
        json_object *child = json_object_array_get_idx(monitors, best.child);
        json_object *parent = json_object_array_get_idx(monitors, best.parent);
        if (!place(child, parent, best.direction, best.alignment, error, size)) return false;
        int64_t x = number(child, "x"), y = number(child, "y");
        if (!strcmp(best.direction, "left")) { x -= best.gap; y += best.offset; }
        else if (!strcmp(best.direction, "right")) { x += best.gap; y += best.offset; }
        else if (!strcmp(best.direction, "up")) { y -= best.gap; x += best.offset; }
        else { y += best.gap; x += best.offset; }
        if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
            fail(error, size, "Monitor position is out of range"); return false;
        }
        putint(child, "x", (int)x); putint(child, "y", (int)y);
        placed[best.child] = true; --remaining;
    }
    return true;
}
static void remember(json_object *plan) {
    json_object *monitors = get(plan, "monitors"), *outputs = get(get(plan, "state"), "outputs");
    json_object *connected = json_object_new_array();
    json_object_object_add(get(plan, "state"), "connected", connected);
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        json_object_object_add(outputs, str(m, "id"), saved_monitor(m));
        if (active(m)) json_object_array_add(connected, json_object_new_string(str(m, "id")));
    }
}
static bool place_new_outputs(json_object *monitors, char *error, size_t size) {
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        if (!flag(m, "_newPlacement") || !active(m)) continue;
        int w = 0, h = 0;
        if (!logical_size(m, &w, &h)) { fail(error, size, "Invalid monitor geometry"); return false; }
        int64_t x = number(m, "x"), y = number(m, "y"), right = INT_MIN;
        bool collision = false;
        for (size_t j = 0; j < json_object_array_length(monitors); ++j) {
            json_object *other = json_object_array_get_idx(monitors, j);
            if (other == m || !active(other) || (j > i && flag(other, "_newPlacement"))) continue;
            int ow = 0, oh = 0;
            if (!logical_size(other, &ow, &oh)) { fail(error, size, "Invalid monitor geometry"); return false; }
            int64_t ox = number(other, "x"), oy = number(other, "y");
            if (ox + ow > right) right = ox + ow;
            if (x < ox + ow && ox < x + w && y < oy + oh && oy < y + h) collision = true;
        }
        if (collision) {
            if (right > INT_MAX) { fail(error, size, "Monitor position is out of range"); return false; }
            putint(m, "x", (int)right); putint(m, "y", 0);
        }
    }
    return true;
}
static int gcd_int(int a, int b) { while (b) { int t = b; b = a % b; a = t; } return a; }
/* A scale in 1/120 units produces whole logical pixels iff it divides this. */
static int scale_divisor(int width, int height, char *error, size_t size) {
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536) {
        fail(error, size, "Monitor has no active video mode; choose a resolution first"); return 0;
    }
    return gcd_int(width * 120, height * 120);
}
json_object *monitor_scale_options(int width, int height, char *error, size_t size) {
    if (error && size) error[0] = '\0';
    int divisor = scale_divisor(width, height, error, size);
    if (!divisor) return NULL;
    const int presets[] = {120, 150, 180, 192, 210, 240, 300, 360, 420, 480};
    json_object *options = json_object_new_array();
    if (!options) { fail(error, size, "Out of memory"); return NULL; }
    for (size_t i = 0; i < sizeof presets / sizeof presets[0]; ++i) {
        if (divisor % presets[i]) continue;
        json_object *value = json_object_new_double((double)presets[i] / 120.0);
        if (!value || json_object_array_add(options, value) != 0) {
            json_object_put(value); json_object_put(options);
            fail(error, size, "Out of memory"); return NULL;
        }
    }
    return options;
}
static bool apply_scale(json_object *m, double requested, char *error, size_t size) {
    if (!isfinite(requested) || requested < 1 || requested > 4) {
        fail(error, size, "Scale must be between 1 and 4"); return false;
    }
    int divisor = scale_divisor(number(m, "width"), number(m, "height"), error, size);
    if (!divisor) return false;
    int units = (int)floor(requested * 120 + 0.5);
    if (units > divisor) units = divisor;
    while (divisor % units) ++units;
    if (units > 480) { fail(error, size, "No valid scale at or above the requested value"); return false; }
    putreal(m, "scale", (double)units / 120.0);
    return true;
}
static bool apply_mode(json_object *m, const char *value, char *error, size_t size) {
    char *wanted = normalize_mode(value);
    bool found = false;
    json_object *modes = get(m, "availableModes");
    if (wanted && modes && json_object_is_type(modes, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(modes); ++i) {
            const char *raw = json_object_get_string(json_object_array_get_idx(modes, i));
            char *candidate = normalize_mode(raw ? raw : "");
            if (candidate && !strcmp(wanted, candidate)) found = true;
            free(candidate);
        }
    }
    int w = 0, h = 0;
    double hz = 0;
    if (!found || !parse_mode(wanted, &w, &h, &hz)) {
        free(wanted); fail(error, size, "This mode is not available for the selected monitor"); return false;
    }
    putstr(m, "mode", wanted); free(wanted);
    putint(m, "width", w); putint(m, "height", h); putreal(m, "refreshRate", hz);
    return apply_scale(m, real(m, "scale"), error, size);
}
json_object *monitor_plan(json_object *live, json_object *state, const char *name,
                          const char *action, const char *value, const char *reference,
                          const char *alignment, char *error, size_t size) {
    if (error && size) error[0] = '\0';
    if (!name || !action) { fail(error, size, "Monitor and action are required"); return NULL; }
    if (!value) value = "";
    if (!alignment || !*alignment) alignment = "center";
    bool strict_scale = !strcmp(action, "scale-preset");
    bool scale_action = strict_scale || !strcmp(action, "scale");
    bool geometry_action = scale_action || !strcmp(action, "mode") || !strcmp(action, "rotation");
    json_object *plan = prepare(live, state, geometry_action, error, size);
    if (!plan) return NULL;
    json_object *monitors = get(plan, "monitors");
    json_object *m = find_monitor(monitors, "name", name);
    if (!m) { fail(error, size, "Monitor is no longer connected"); goto invalid; }
    if (!strcmp(action, "position")) {
        json_object *live_target = find_monitor(live, "name", name);
        json_object *live_reference = find_monitor(live, "name", reference);
        if (!active(live_target) || !active(live_reference) || live_target == live_reference) {
            fail(error, size, "Choose a different active reference monitor"); goto invalid;
        }
        if (!place(m, live_reference, value, alignment, error, size)) goto invalid;
        json_object_object_del(m, "_newPlacement");
    } else if (!strcmp(action, "mode")) {
        if (!apply_mode(m, value, error, size)) goto invalid;
        json_object_object_del(m, "_desiredGeometry");
    } else if (scale_action) {
        char *end = NULL; errno = 0;
        double requested = strtod(value, &end);
        if (errno || end == value || *end) { fail(error, size, "Invalid monitor scale"); goto invalid; }
        if (!apply_scale(m, requested, error, size)) goto invalid;
        if (strict_scale && fabs(real(m, "scale") - requested) > 1e-12) {
            fail(error, size, "Scale preset is not supported by the current video mode"); goto invalid;
        }
        json_object *desired = get(m, "_desiredGeometry");
        if (desired && !apply_scale(desired, real(m, "scale"), error, size)) goto invalid;
    } else if (!strcmp(action, "rotation")) {
        if (strlen(value) != 1 || value[0] < '0' || value[0] > '3') {
            fail(error, size, "Rotation must be 0, 1, 2 or 3"); goto invalid;
        }
        putint(m, "transform", value[0] - '0');
    } else if (!strcmp(action, "disable") || !strcmp(action, "enable")) {
        bool disable = !strcmp(action, "disable");
        size_t count = 0;
        for (size_t i = 0; i < json_object_array_length(live); ++i)
            if (active(json_object_array_get_idx(live, i))) ++count;
        if (disable && active(find_monitor(live, "name", name)) && count <= 1) {
            fail(error, size, "Cannot disable the only active monitor"); goto invalid;
        }
        if (!disable && (number(m, "width") <= 0 || number(m, "height") <= 0)) {
            json_object *modes = get(m, "availableModes");
            if (!modes || !json_object_is_type(modes, json_type_array) || !json_object_array_length(modes) ||
                !apply_mode(m, json_object_get_string(json_object_array_get_idx(modes, 0)), error, size)) goto invalid;
        }
        json_object_object_add(m, "disabled", json_object_new_boolean(disable));
        json_object_object_add(m, "desiredDisabled", json_object_new_boolean(disable));
    } else { fail(error, size, "Unknown monitor action"); goto invalid; }
    if (geometry_action && !reflow(live, monitors, error, size)) goto invalid;
    if (!place_new_outputs(monitors, error, size) || !no_overlap(monitors, error, size)) goto invalid;
    remember(plan);
    return plan;
invalid:
    json_object_put(plan);
    return NULL;
}
json_object *monitor_reconcile(json_object *live, json_object *state, bool topology_changed,
                               char *error, size_t size) {
    if (error && size) error[0] = '\0';
    json_object *plan = prepare(live, state, !topology_changed, error, size);
    if (!plan) return NULL;
    if (!place_new_outputs(get(plan, "monitors"), error, size) ||
        !no_overlap(get(plan, "monitors"), error, size)) goto invalid;
    remember(plan);
    return plan;
invalid:
    json_object_put(plan); return NULL;
}

struct field { char key[64]; const char *start, *end; };
struct rule { struct field fields[64]; size_t count; const char *closing; char *output; };
static const char *skipspace(const char *p) { while (*p && isspace((unsigned char)*p)) ++p; return p; }
static char *lua_string(const char *value) {
    /* Decimal escapes work in Lua as well as JSON for ASCII controls only
     * when avoided: use Lua's named escapes and reject other control bytes. */
    struct text out = {0};
    if (!adds(&out, "\"")) return NULL;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        const char *escape = NULL;
        switch (*p) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        default: break;
        }
        if ((*p < 32 && !escape) ||
            !(escape ? adds(&out, escape) : append(&out, (const char *)p, 1))) {
            free(out.data); return NULL;
        }
    }
    if (!adds(&out, "\"")) { free(out.data); return NULL; }
    return out.data;
}
static const char *selector(json_object *m) {
    return str(m, "name");
}
static bool parse_rule(const char *line, struct rule *rule) {
    memset(rule, 0, sizeof *rule);
    const char *p = skipspace(line);
    if (strncmp(p, "hl.monitor", 10)) return false;
    p = skipspace(p + 10);
    if (*p++ != '(') return false;
    p = skipspace(p);
    if (*p++ != '{') return false;
    for (;;) {
        p = skipspace(p);
        if (*p == '}') { rule->closing = p++; break; }
        if (rule->count >= sizeof rule->fields / sizeof rule->fields[0]) return false;
        struct field *f = &rule->fields[rule->count];
        const char *key = p;
        if (!isalpha((unsigned char)*p) && *p != '_') return false;
        while (isalnum((unsigned char)*p) || *p == '_') ++p;
        size_t keylen = (size_t)(p - key);
        if (keylen >= sizeof f->key) return false;
        memcpy(f->key, key, keylen); f->key[keylen] = '\0';
        for (size_t i = 0; i < rule->count; ++i)
            if (!strcmp(f->key, rule->fields[i].key)) return false;
        p = skipspace(p);
        if (*p++ != '=') return false;
        p = skipspace(p); f->start = p;
        char quote = '\0', stack[64];
        size_t depth = 0;
        while (*p) {
            if (quote) {
                if (*p == '\\') { if (!p[1]) return false; p += 2; continue; }
                if (*p == quote) quote = '\0';
                ++p; continue;
            }
            if (*p == '"' || *p == '\'') { quote = *p++; continue; }
            if (*p == '-' && p[1] == '-') return false;
            if (*p == ',' && !depth) break;
            if (*p == '}' && !depth) break;
            if (*p == '(' || *p == '{' || *p == '[') {
                if (depth >= sizeof stack) return false;
                stack[depth++] = *p;
            } else if (*p == ')' || *p == '}' || *p == ']') {
                if (!depth) return false;
                char opening = stack[--depth];
                if ((*p == ')' && opening != '(') || (*p == '}' && opening != '{') ||
                    (*p == ']' && opening != '[')) return false;
            }
            ++p;
        }
        if (quote || depth || !*p) return false;
        f->end = p;
        while (f->end > f->start && isspace((unsigned char)f->end[-1])) --f->end;
        if (f->end == f->start) return false;
        ++rule->count;
        if (*p == ',') ++p;
    }
    p = skipspace(p);
    if (*p++ != ')' || *skipspace(p)) return false;
    for (size_t i = 0; i < rule->count; ++i) {
        struct field *f = &rule->fields[i];
        if (strcmp(f->key, "output")) continue;
        char *value = strndup(f->start, (size_t)(f->end - f->start));
        json_tokener *tok = json_tokener_new();
        if (!value || !tok) { free(value); json_tokener_free(tok); return false; }
        size_t len = strlen(value);
        json_object *output = len <= INT_MAX ? json_tokener_parse_ex(tok, value, (int)len) : NULL;
        bool valid = json_tokener_get_error(tok) == json_tokener_success && output &&
                     json_object_is_type(output, json_type_string) &&
                     json_tokener_get_parse_end(tok) == len && *json_object_get_string(output);
        if (valid) rule->output = strdup(json_object_get_string(output));
        json_object_put(output); json_tokener_free(tok); free(value);
        return valid && rule->output;
    }
    return false;
}
static json_object *matching_output(json_object *state, json_object *monitors, const char *output) {
    json_object *owner = NULL, *outputs = get(state, "outputs");
    const char *owner_id = NULL;
    if (outputs) {
        json_object_object_foreach(outputs, id, saved) {
            bool match = !strncmp(output, "desc:", 5) ?
                         !strcmp(str(saved, "description"), output + 5) : !strcmp(str(saved, "name"), output);
            if (!match) continue;
            if (owner) return NULL;
            owner = saved; owner_id = id;
        }
    }
    if (!owner) return state ? NULL : find_monitor(monitors, "name", output);
    if (!strncmp(owner_id, "model:", 6)) return find_monitor(monitors, "model", owner_id + 6);
    json_object *match = NULL;
    if (!*str(owner, "description")) return NULL;
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        if (strcmp(str(owner, "description"), str(m, "description"))) continue;
        if (match) return NULL;
        match = m;
    }
    return match;
}
static char *lua_value(json_object *m, const char *key) {
    char text[160];
    if (!strcmp(key, "output")) return lua_string(selector(m));
    if (!strcmp(key, "position")) {
        (void)snprintf(text, sizeof text, "\"%dx%d\"", number(m, "x"), number(m, "y"));
        return strdup(text);
    }
    if (!strcmp(key, "scale")) {
        double scale = real(m, "scale");
        if (!isfinite(scale) || scale < 1 || scale > 4) return NULL;
        (void)snprintf(text, sizeof text, "%.12g", scale);
        return strdup(text);
    }
    if (!strcmp(key, "transform")) {
        int transform = number(m, "transform");
        if (transform < 0 || transform > 7) return NULL;
        (void)snprintf(text, sizeof text, "%d", transform);
        return strdup(text);
    }
    if (!strcmp(key, "disabled")) return strdup(flag(m, "disabled") ? "true" : "false");
    if (!strcmp(key, "mode")) {
        const char *mode = str(m, "mode");
        if (!*mode) {
            int w = number(m, "width"), h = number(m, "height");
            if (w > 0 && h > 0) (void)snprintf(text, sizeof text, "%dx%d@%.5f", w, h, real(m, "refreshRate"));
            else (void)snprintf(text, sizeof text, "preferred");
            mode = text;
        }
        if (!strcmp(mode, "preferred")) return strdup("\"preferred\"");
        int w = 0, h = 0; double hz = 0;
        if (!parse_mode(mode, &w, &h, &hz)) return NULL;
        json_object *j = json_object_new_string(mode);
        char *result = strdup(json_object_to_json_string_ext(j, JSON_C_TO_STRING_PLAIN));
        json_object_put(j); return result;
    }
    return NULL;
}
static const char *config_keys[] = {"output", "mode", "position", "scale", "transform", "disabled"};
static bool patch_rule(struct text *out, const char *line, struct rule *rule, json_object *m) {
    bool seen[sizeof config_keys / sizeof config_keys[0]] = {false};
    const char *cursor = line;
    for (size_t i = 0; i < rule->count; ++i) {
        struct field *f = &rule->fields[i];
        for (size_t k = 0; k < sizeof config_keys / sizeof config_keys[0]; ++k) {
            if (strcmp(f->key, config_keys[k])) continue;
            char *value = lua_value(m, config_keys[k]);
            if (!value) return false;
            bool ok = append(out, cursor, (size_t)(f->start - cursor)) && adds(out, value);
            free(value);
            if (!ok) return false;
            cursor = f->end; seen[k] = true;
        }
    }
    /* Insert before trailing whitespace; reuse an existing trailing comma. */
    const char *insert = rule->closing;
    while (insert > cursor && isspace((unsigned char)insert[-1])) --insert;
    if (!append(out, cursor, (size_t)(insert - cursor))) return false;
    bool comma = insert > line && insert[-1] == ',';
    for (size_t k = 0; k < sizeof config_keys / sizeof config_keys[0]; ++k) {
        if (seen[k]) continue;
        char *value = lua_value(m, config_keys[k]);
        if (!value) return false;
        bool ok = adds(out, comma ? " " : ", ") && adds(out, config_keys[k]) && adds(out, " = ") && adds(out, value);
        free(value);
        if (!ok) return false;
        comma = false;
    }
    return adds(out, insert);
}
static bool new_rule(struct text *out, json_object *m) {
    if (!adds(out, "hl.monitor({ ")) return false;
    for (size_t k = 0; k < sizeof config_keys / sizeof config_keys[0]; ++k) {
        char *value = lua_value(m, config_keys[k]);
        if (!value) return false;
        bool ok = (k == 0 || adds(out, ", ")) && adds(out, config_keys[k]) && adds(out, " = ") && adds(out, value);
        free(value);
        if (!ok) return false;
    }
    return adds(out, " })\n");
}
char *monitor_configuration(const char *original, json_object *monitors, json_object *state,
                            char *error, size_t size) {
    if (error && size) error[0] = '\0';
    if (!original || !valid_live(monitors) || (state && !valid_state(state))) {
        fail(error, size, "Invalid monitor configuration input"); return NULL;
    }
    const char *begin = strstr(original, BEGIN), *end = strstr(original, END);
    if ((begin == NULL) != (end == NULL) ||
        (begin && (strstr(begin + strlen(BEGIN), BEGIN) || strstr(end + strlen(END), END) || end < begin ||
                   (begin > original && begin[-1] != '\n') || begin[strlen(BEGIN)] != '\n' ||
                   (end > original && end[-1] != '\n') || (end[strlen(END)] && end[strlen(END)] != '\n')))) {
        fail(error, size, "Monitor configuration has invalid managed markers"); return NULL;
    }
    struct text result = {0};
    json_object *seen = json_object_new_object(), *updated = json_object_new_object();
    json_object *profiles = json_object_new_object();
    if (!seen || !updated || !profiles) goto invalid;
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = copy(json_object_array_get_idx(monitors, i));
        char *id = m ? identity(monitors, m) : NULL;
        if (!id) { json_object_put(m); goto invalid; }
        putstr(m, "id", id);
        json_object_object_add(profiles, id, m);
        free(id);
    }
    if (begin) {
        if (!append(&result, original, (size_t)(begin - original)) || !adds(&result, BEGIN "\n")) goto invalid;
        const char *cursor = begin + strlen(BEGIN) + 1;
        while (cursor < end) {
            const char *newline = strchr(cursor, '\n');
            if (!newline || newline > end) goto invalid;
            char *line = strndup(cursor, (size_t)(newline - cursor));
            if (!line) goto invalid;
            if (!*skipspace(line)) {
                bool ok = adds(&result, line) && adds(&result, "\n");
                free(line); if (!ok) goto invalid;
                cursor = newline + 1; continue;
            }
            struct rule rule;
            if (!parse_rule(line, &rule)) { free(line); fail(error, size, "Unrecognized managed monitor rule"); goto invalid; }
            json_object *m = get(seen, rule.output) ? NULL : matching_output(state, monitors, rule.output);
            json_object_object_add(seen, rule.output, json_object_new_boolean(true));
            bool ok = true;
            if (m && !get(updated, str(m, "model"))) {
                ok = patch_rule(&result, line, &rule, m) && adds(&result, "\n");
                json_object_object_add(updated, str(m, "model"), json_object_new_boolean(true));
            }
            /* Stale or ambiguously owned managed rules are deliberately dropped. */
            free(rule.output); free(line);
            if (!ok) goto invalid;
            cursor = newline + 1;
        }
    } else {
        if (!adds(&result, original)) goto invalid;
        if (result.length && result.data[result.length - 1] != '\n' && !adds(&result, "\n")) goto invalid;
        if (!adds(&result, "\n" BEGIN "\n")) goto invalid;
    }
    json_object_object_foreach(profiles, profile_id, m) {
        (void)profile_id;
        if (!get(updated, str(m, "model")) && !new_rule(&result, m)) goto invalid;
    }
    if (!adds(&result, END) || !adds(&result, end ? end + strlen(END) : "\n")) goto invalid;
    json_object_put(seen); json_object_put(updated); json_object_put(profiles);
    return result.data;
invalid:
    json_object_put(seen); json_object_put(updated); json_object_put(profiles); free(result.data);
    if (error && size && !*error) fail(error, size, "Invalid monitor rule or out of memory");
    return NULL;
}
