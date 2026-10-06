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
        if (!*str(m, "name")) return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(str(m, "name"), str(json_object_array_get_idx(live, j), "name"))) return false;
    }
    return true;
}
static char *identity(json_object *live, json_object *m) {
    const char *description = str(m, "description");
    size_t matches = 0;
    for (size_t i = 0; i < json_object_array_length(live); ++i)
        if (!strcmp(description, str(json_object_array_get_idx(live, i), "description"))) ++matches;
    bool unique = *description && matches == 1;
    const char *value = unique ? description : str(m, "name");
    size_t size = strlen(value) + 6;
    char *id = malloc(size);
    if (id) (void)snprintf(id, size, "%s%s", unique ? "desc:" : "name:", value);
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
static const char *saved_keys[] = {
    "id", "name", "description", "width", "height", "refreshRate", "scale",
    "transform", "x", "y", "disabled", "mode"
};
static json_object *saved_monitor(json_object *m) {
    json_object *o = json_object_new_object();
    for (size_t k = 0; k < sizeof saved_keys / sizeof saved_keys[0]; ++k) {
        json_object *v = get(m, saved_keys[k]);
        if (v) json_object_object_add(o, saved_keys[k], json_object_get(v));
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
        json_object *m = copy(json_object_array_get_idx(live, i));
        char *id = identity(live, m);
        if (!m || !id) { json_object_put(m); free(id); json_object_put(state); return NULL; }
        putstr(m, "id", id);
        set_live_mode(m);
        json_object_object_add(outputs, id, saved_monitor(m));
        if (active(m)) json_object_array_add(connected, json_object_new_string(id));
        free(id);
        json_object_put(m);
    }
    return state;
}
static bool valid_state(json_object *s) {
    return s && number(s, "version") == 1 &&
        json_object_is_type(get(s, "outputs"), json_type_object) &&
        json_object_is_type(get(s, "relations"), json_type_array);
}
static json_object *prepare(json_object *live, json_object *input_state,
                            bool import_geometry, char *error, size_t size) {
    if (!valid_live(live) || (input_state && !valid_state(input_state))) {
        fail(error, size, "Invalid monitor inventory or saved state"); return NULL;
    }
    json_object *state = input_state ? copy(input_state) : monitor_initial_state(live);
    json_object *monitors = copy(live), *plan = json_object_new_object();
    if (!state || !monitors || !plan) {
        json_object_put(state); json_object_put(monitors); json_object_put(plan);
        fail(error, size, "Out of memory"); return NULL;
    }
    json_object_object_add(plan, "monitors", monitors);
    json_object_object_add(plan, "state", state);
    json_object *outputs = get(state, "outputs"), *connected = json_object_new_array();
    json_object_object_add(state, "connected", connected);
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        char *id = identity(live, m);
        if (!id) { json_object_put(plan); fail(error, size, "Out of memory"); return NULL; }
        putstr(m, "id", id);
        set_live_mode(m);
        json_object *saved = get(outputs, id);
        if (saved && !json_object_is_type(saved, json_type_object)) {
            free(id); json_object_put(plan); fail(error, size, "Invalid saved output"); return NULL;
        }
        if (saved) {
            for (size_t k = 0; k < sizeof saved_keys / sizeof saved_keys[0]; ++k) {
                const char *key = saved_keys[k];
                if (!strcmp(key, "id") || !strcmp(key, "name") || !strcmp(key, "description")) continue;
                if (import_geometry && (!strcmp(key, "width") || !strcmp(key, "height") ||
                    !strcmp(key, "refreshRate") || !strcmp(key, "scale") ||
                    !strcmp(key, "transform") || !strcmp(key, "mode"))) continue;
                json_object *v = get(saved, key);
                if (v) json_object_object_add(m, key, json_object_get(v));
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
static bool edge_valid(json_object *edge, json_object *outputs) {
    const char *a = str(edge, "monitor"), *b = str(edge, "reference");
    const char *d = str(edge, "direction"), *align = str(edge, "alignment");
    return *a && *b && strcmp(a, b) && get(outputs, a) && get(outputs, b) &&
        (!strcmp(d, "left") || !strcmp(d, "right") || !strcmp(d, "up") || !strcmp(d, "down")) &&
        (!strcmp(align, "start") || !strcmp(align, "center") || !strcmp(align, "end"));
}
static bool graph_valid(json_object *state, char *error, size_t size) {
    json_object *relations = get(state, "relations"), *outputs = get(state, "outputs");
    size_t n = json_object_array_length(relations);
    if (n > MAX_OUTPUTS) { fail(error, size, "Too many monitor relations"); return false; }
    for (size_t i = 0; i < n; ++i) {
        json_object *edge = json_object_array_get_idx(relations, i);
        if (!edge_valid(edge, outputs)) { fail(error, size, "Invalid monitor relation"); return false; }
        const char *source = str(edge, "monitor"), *next = str(edge, "reference");
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(source, str(json_object_array_get_idx(relations, j), "monitor"))) {
                fail(error, size, "A monitor has multiple reference monitors"); return false;
            }
        for (size_t depth = 0; depth <= n; ++depth) {
            if (!strcmp(source, next)) { fail(error, size, "Monitor relations contain a cycle"); return false; }
            json_object *parent = find_monitor(relations, "monitor", next);
            if (!parent) break;
            next = str(parent, "reference");
            if (depth == n) { fail(error, size, "Monitor relations contain a cycle"); return false; }
        }
    }
    return true;
}
static bool place(json_object *m, json_object *reference, json_object *edge,
                   char *error, size_t size) {
    int w = 0, h = 0, rw = 0, rh = 0;
    if (!logical_size(m, &w, &h) || !logical_size(reference, &rw, &rh)) {
        fail(error, size, "A monitor has no usable video geometry"); return false;
    }
    int64_t x = number(reference, "x"), y = number(reference, "y");
    const char *d = str(edge, "direction"), *align = str(edge, "alignment");
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
static bool solve_one(json_object *monitors, json_object *relations, json_object *m,
                       unsigned depth, char *error, size_t size) {
    if (depth > MAX_OUTPUTS) { fail(error, size, "Monitor relation cycle"); return false; }
    json_object *edge = find_monitor(relations, "monitor", str(m, "id"));
    if (!edge || !active(m)) return true;
    json_object *reference = find_monitor(monitors, "id", str(edge, "reference"));
    if (!reference || !active(reference)) return true;
    return solve_one(monitors, relations, reference, depth + 1U, error, size) &&
           place(m, reference, edge, error, size);
}
static bool solve(json_object *plan, char *error, size_t size) {
    json_object *state = get(plan, "state"), *monitors = get(plan, "monitors");
    if (!graph_valid(state, error, size)) return false;
    for (size_t i = 0; i < json_object_array_length(monitors); ++i)
        if (!solve_one(monitors, get(state, "relations"), json_object_array_get_idx(monitors, i),
                       0, error, size)) return false;
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
static bool contains_id(json_object *ids, const char *id) {
    if (!ids || !json_object_is_type(ids, json_type_array)) return false;
    for (size_t i = 0; i < json_object_array_length(ids); ++i) {
        const char *candidate = json_object_get_string(json_object_array_get_idx(ids, i));
        if (candidate && !strcmp(candidate, id)) return true;
    }
    return false;
}
static bool topology_fallback(json_object *live, json_object *state) {
    json_object *previous = get(state, "connected");
    if (!previous || !json_object_is_type(previous, json_type_array)) return false;
    bool added = false, removed = false;
    json_object *current = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(live); ++i) {
        json_object *m = json_object_array_get_idx(live, i);
        if (!active(m)) continue;
        char *id = identity(live, m);
        if (!id) continue;
        if (!contains_id(previous, id)) added = true;
        json_object_array_add(current, json_object_new_string(id));
        free(id);
    }
    for (size_t i = 0; i < json_object_array_length(previous); ++i) {
        const char *id = json_object_get_string(json_object_array_get_idx(previous, i));
        if (id && !contains_id(current, id)) removed = true;
    }
    json_object_put(current);
    if (added) return false;
    return removed || flag(state, "fallback");
}
static bool compact(json_object *monitors, char *error, size_t size) {
    json_object *anchor = NULL;
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        if (!active(m)) continue;
        if (!anchor || !strncmp(str(m, "name"), "eDP", 3) || !strncmp(str(m, "name"), "LVDS", 4)) anchor = m;
    }
    if (!anchor) { fail(error, size, "No active monitor remains"); return false; }
    putint(anchor, "x", 0); putint(anchor, "y", 0);
    int aw = 0, ah = 0;
    if (!logical_size(anchor, &aw, &ah)) return false;
    int x = aw;
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        if (m == anchor || !active(m)) continue;
        int w = 0, h = 0;
        if (!logical_size(m, &w, &h) || x > INT_MAX - w) {
            fail(error, size, "Invalid monitor geometry"); return false;
        }
        putint(m, "x", x); putint(m, "y", (int)floor((double)(ah - h) / 2.0));
        x += w;
    }
    return true;
}
static int gcd_int(int a, int b) { while (b) { int t = b; b = a % b; a = t; } return a; }
static bool apply_scale(json_object *m, double requested, char *error, size_t size) {
    int width = number(m, "width"), height = number(m, "height");
    if (!isfinite(requested) || requested < 1 || requested > 4) {
        fail(error, size, "Scale must be between 1 and 4"); return false;
    }
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536) {
        fail(error, size, "Monitor has no active video mode; choose a resolution first"); return false;
    }
    int divisor = gcd_int(width * 120, height * 120);
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
    json_object *plan = prepare(live, state, false, error, size);
    if (!plan) return NULL;
    json_object *monitors = get(plan, "monitors"), *updated = get(plan, "state");
    json_object *m = find_monitor(monitors, "name", name);
    if (!m) { fail(error, size, "Monitor is no longer connected"); goto invalid; }
    bool moved = false;
    if (!strcmp(action, "position")) {
        /* A manual move affects only the selected monitor, even after fallback. */
        for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
            json_object *item = json_object_array_get_idx(monitors, i);
            json_object *actual = find_monitor(live, "name", str(item, "name"));
            putint(item, "x", number(actual, "x"));
            putint(item, "y", number(actual, "y"));
        }
        json_object *ref = find_monitor(monitors, "name", reference);
        json_object *live_target = find_monitor(live, "name", name);
        json_object *live_reference = find_monitor(live, "name", reference);
        if (!active(live_target) || !ref || !active(live_reference) || ref == m) {
            fail(error, size, "Choose a different active reference monitor"); goto invalid;
        }
        json_object *edge = json_object_new_object();
        putstr(edge, "monitor", str(m, "id")); putstr(edge, "reference", str(ref, "id"));
        putstr(edge, "direction", value); putstr(edge, "alignment", alignment);
        if (!edge_valid(edge, get(updated, "outputs"))) {
            json_object_put(edge); fail(error, size, "Invalid direction or alignment"); goto invalid;
        }
        json_object *relations = get(updated, "relations"), *next = json_object_new_array();
        for (size_t i = 0; i < json_object_array_length(relations); ++i) {
            json_object *old = json_object_array_get_idx(relations, i);
            if (strcmp(str(old, "monitor"), str(m, "id")) && strcmp(str(old, "reference"), str(m, "id")))
                json_object_array_add(next, json_object_get(old));
        }
        json_object_array_add(next, edge);
        json_object_object_add(updated, "relations", next);
        if (!graph_valid(updated, error, size) || !place(m, ref, edge, error, size)) goto invalid;
        json_object_object_add(updated, "fallback", json_object_new_boolean(false));
        moved = true;
    } else if (!strcmp(action, "mode")) {
        if (!apply_mode(m, value, error, size)) goto invalid;
    } else if (!strcmp(action, "scale")) {
        char *end = NULL; errno = 0;
        double requested = strtod(value, &end);
        if (errno || end == value || *end) { fail(error, size, "Invalid monitor scale"); goto invalid; }
        if (!apply_scale(m, requested, error, size)) goto invalid;
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
        json_object_object_add(updated, "fallback", json_object_new_boolean(disable));
    } else { fail(error, size, "Unknown monitor action"); goto invalid; }
    if (!moved && !solve(plan, error, size)) goto invalid;
    remember(plan);
    if (flag(updated, "fallback") && !compact(monitors, error, size)) goto invalid;
    if (!no_overlap(monitors, error, size)) goto invalid;
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
    if (topology_changed)
        json_object_object_add(get(plan, "state"), "fallback", json_object_new_boolean(topology_fallback(live, state)));
    if (!solve(plan, error, size)) goto invalid;
    remember(plan);
    if (flag(get(plan, "state"), "fallback") && !compact(get(plan, "monitors"), error, size)) goto invalid;
    if (!no_overlap(get(plan, "monitors"), error, size)) goto invalid;
    return plan;
invalid:
    json_object_put(plan); return NULL;
}

struct field { char key[64]; const char *start, *end; };
struct rule { struct field fields[64]; size_t count; const char *closing; char *output; };
static const char *skipspace(const char *p) { while (*p && isspace((unsigned char)*p)) ++p; return p; }
static bool safe_name(const char *name) {
    if (!*name) return false;
    for (const char *p = name; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') return false;
    return true;
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
static json_object *matching_output(json_object *monitors, const char *output) {
    json_object *m = find_monitor(monitors, "name", output);
    if (m || strncmp(output, "desc:", 5)) return m;
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *candidate = json_object_array_get_idx(monitors, i);
        if (!strcmp(str(candidate, "description"), output + 5)) return candidate;
    }
    return NULL;
}
static char *lua_value(json_object *m, const char *key) {
    char text[160];
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
static const char *config_keys[] = {"mode", "position", "scale", "transform", "disabled"};
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
    const char *name = str(m, "name");
    if (!safe_name(name) || !adds(out, "hl.monitor({ output = \"") || !adds(out, name) || !adds(out, "\"")) return false;
    for (size_t k = 0; k < sizeof config_keys / sizeof config_keys[0]; ++k) {
        char *value = lua_value(m, config_keys[k]);
        if (!value) return false;
        bool ok = adds(out, ", ") && adds(out, config_keys[k]) && adds(out, " = ") && adds(out, value);
        free(value);
        if (!ok) return false;
    }
    return adds(out, " })\n");
}
char *monitor_configuration(const char *original, json_object *monitors, char *error, size_t size) {
    if (error && size) error[0] = '\0';
    if (!original || !valid_live(monitors)) { fail(error, size, "Invalid monitor configuration input"); return NULL; }
    const char *begin = strstr(original, BEGIN), *end = strstr(original, END);
    if ((begin == NULL) != (end == NULL) ||
        (begin && (strstr(begin + strlen(BEGIN), BEGIN) || strstr(end + strlen(END), END) || end < begin ||
                   (begin > original && begin[-1] != '\n') || begin[strlen(BEGIN)] != '\n' ||
                   (end > original && end[-1] != '\n') || (end[strlen(END)] && end[strlen(END)] != '\n')))) {
        fail(error, size, "Monitor configuration has invalid managed markers"); return NULL;
    }
    struct text result = {0};
    json_object *seen = json_object_new_object(), *updated = json_object_new_object();
    if (!seen || !updated) goto invalid;
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
            if (get(seen, rule.output)) {
                free(rule.output); free(line); fail(error, size, "Duplicate managed monitor rule"); goto invalid;
            }
            json_object_object_add(seen, rule.output, json_object_new_boolean(true));
            json_object *m = matching_output(monitors, rule.output);
            bool ok = true;
            if (m) {
                if (get(updated, str(m, "name"))) {
                    fail(error, size, "Multiple managed rules match the same monitor"); ok = false;
                } else {
                    ok = patch_rule(&result, line, &rule, m);
                    json_object_object_add(updated, str(m, "name"), json_object_new_boolean(true));
                }
            } else ok = adds(&result, line);
            free(rule.output); free(line);
            if (!ok || !adds(&result, "\n")) goto invalid;
            cursor = newline + 1;
        }
    } else {
        if (!adds(&result, original)) goto invalid;
        if (result.length && result.data[result.length - 1] != '\n' && !adds(&result, "\n")) goto invalid;
        if (!adds(&result, "\n" BEGIN "\n")) goto invalid;
    }
    for (size_t i = 0; i < json_object_array_length(monitors); ++i) {
        json_object *m = json_object_array_get_idx(monitors, i);
        if (!get(updated, str(m, "name")) && !new_rule(&result, m)) goto invalid;
    }
    if (!adds(&result, END) || !adds(&result, end ? end + strlen(END) : "\n")) goto invalid;
    json_object_put(seen); json_object_put(updated);
    return result.data;
invalid:
    json_object_put(seen); json_object_put(updated); free(result.data);
    if (error && size && !*error) fail(error, size, "Invalid monitor rule or out of memory");
    return NULL;
}
