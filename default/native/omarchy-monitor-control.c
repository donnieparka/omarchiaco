#define _POSIX_C_SOURCE 200809L
#include "monitor-model.h"
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FILE_LIMIT (4U * 1024U * 1024U)
#define COMMAND_TIMEOUT 5000
#ifndef MONITOR_CONFIRM_SECONDS
#define MONITOR_CONFIRM_SECONDS 15
#endif

static char config_path[PATH_MAX], state_dir[PATH_MAX];
static char error_text[1024];
static volatile sig_atomic_t stopped;

static json_object *get(json_object *o, const char *key) {
    json_object *v = NULL;
    if (o) (void)json_object_object_get_ex(o, key, &v);
    return v;
}
static const char *str(json_object *o, const char *key) {
    const char *s = json_object_get_string(get(o, key));
    return s ? s : "";
}
static double num(json_object *o, const char *key) {
    return json_object_get_double(get(o, key));
}
static void fail(const char *message) {
    (void)snprintf(error_text, sizeof(error_text), "%s", message);
}
static void on_signal(int sig) { (void)sig; stopped = 1; }
static int64_t millis(void) {
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void nap(long milliseconds) {
    struct timespec ts = {milliseconds / 1000, (milliseconds % 1000) * 1000000};
    while (nanosleep(&ts, &ts) && errno == EINTR && !stopped) {}
}
static bool path_for(char *out, const char *file) {
    int n = snprintf(out, PATH_MAX, "%s/%s", state_dir, file);
    if (n < 0 || n >= PATH_MAX) { fail("Percorso troppo lungo"); return false; }
    return true;
}
static bool mkdirs(const char *path) {
    char copy[PATH_MAX];
    if (strlen(path) >= sizeof(copy)) return false;
    (void)snprintf(copy, sizeof(copy), "%s", path);
    for (char *p = copy + 1; ; p++) {
        if (*p != '/' && *p) continue;
        char saved = *p; *p = 0;
        if (mkdir(copy, 0700) && errno != EEXIST) return false;
        *p = saved;
        if (!saved) break;
    }
    return true;
}
static bool init_paths(void) {
    const char *home = getenv("HOME");
    const char *config = getenv("XDG_CONFIG_HOME"), *state = getenv("XDG_STATE_HOME");
    char c[PATH_MAX], s[PATH_MAX];
    if (!home || !*home) { fail("HOME non disponibile"); return false; }
    int cn = snprintf(c, sizeof(c), "%s/.config", home);
    int sn = snprintf(s, sizeof(s), "%s/.local/state", home);
    if (cn < 0 || cn >= PATH_MAX || sn < 0 || sn >= PATH_MAX) return false;
    cn = snprintf(config_path, sizeof(config_path), "%s/hypr/monitors.lua", config && *config ? config : c);
    sn = snprintf(state_dir, sizeof(state_dir), "%s/omarchy/monitor-control", state && *state ? state : s);
    if (cn < 0 || cn >= PATH_MAX || sn < 0 || sn >= PATH_MAX || !mkdirs(state_dir)) {
        fail("Impossibile preparare la cartella delle impostazioni monitor"); return false;
    }
    return true;
}
static char *read_file(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat sb;
    if (fstat(fd, &sb) || !S_ISREG(sb.st_mode) || sb.st_size < 0 || (uintmax_t)sb.st_size > FILE_LIMIT) {
        close(fd); return NULL;
    }
    size_t cap = (size_t)sb.st_size + 1, used = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fd); return NULL; }
    while (used + 1 < cap) {
        ssize_t n = read(fd, buf + used, cap - used - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { free(buf); close(fd); return NULL; }
        if (!n) break;
        used += (size_t)n;
    }
    close(fd); buf[used] = 0; return buf;
}
static bool write_atomic(const char *path, const char *text) {
    char temp[PATH_MAX];
    int n = snprintf(temp, sizeof(temp), "%s.tmp.XXXXXX", path);
    if (n < 0 || n >= PATH_MAX) return false;
    int fd = mkstemp(temp);
    if (fd < 0) return false;
    struct stat sb;
    mode_t mode = stat(path, &sb) == 0 ? (sb.st_mode & 0777) : 0600;
    bool ok = fchmod(fd, mode) == 0;
    size_t len = strlen(text), used = 0;
    while (ok && used < len) {
        ssize_t amount = write(fd, text + used, len - used);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) { ok = false; break; }
        used += (size_t)amount;
    }
    if (ok) ok = fsync(fd) == 0;
    if (close(fd)) ok = false;
    if (ok) ok = rename(temp, path) == 0;
    if (!ok) (void)unlink(temp);
    if (ok) {
        char *slash = strrchr(temp, '/');
        if (slash) { *slash = 0; int dir = open(temp, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dir >= 0) { (void)fsync(dir); close(dir); }
        }
    }
    return ok;
}
static bool write_state_text(const char *file, const char *text) {
    char path[PATH_MAX];
    return path_for(path, file) && write_atomic(path, text);
}
static bool write_json(const char *file, json_object *o) {
    return write_state_text(file, json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY));
}
static json_object *read_json(const char *file) {
    char path[PATH_MAX];
    if (!path_for(path, file)) return NULL;
    char *text = read_file(path);
    if (!text) return NULL;
    json_object *o = json_tokener_parse(text);
    free(text); return o;
}
static void remove_state(const char *file) {
    char path[PATH_MAX];
    if (path_for(path, file)) (void)unlink(path);
}
static void set_error(const char *message) {
    json_object *o = json_object_new_object();
    json_object_object_add(o, "message", json_object_new_string(message));
    (void)write_json("error.json", o); json_object_put(o);
}
static int lock_file(const char *file) {
    char path[PATH_MAX];
    if (!path_for(path, file)) return -1;
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB)) { close(fd); return -1; }
    return fd;
}

/* No shell is involved; bound both command duration and captured output. */
static char *command(char *const argv[]) {
    int p[2];
    if (pipe(p)) { fail("Impossibile avviare il comando"); return NULL; }
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return NULL; }
    if (!pid) {
        (void)setpgid(0, 0);
        close(p[0]); (void)dup2(p[1], STDOUT_FILENO); close(p[1]);
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd >= 0) { (void)dup2(nullfd, STDIN_FILENO); (void)dup2(nullfd, STDERR_FILENO); if (nullfd > 2) close(nullfd); }
        execvp(argv[0], argv); _exit(127);
    }
    close(p[1]); (void)setpgid(pid, pid);
    (void)fcntl(p[0], F_SETFL, O_NONBLOCK);
    char *buf = malloc(FILE_LIMIT + 1);
    if (!buf) { (void)kill(-pid, SIGKILL); close(p[0]); (void)waitpid(pid, NULL, 0); return NULL; }
    size_t used = 0; bool eof = false, done = false, bad = false; int status = 0;
    int64_t deadline = millis() + COMMAND_TIMEOUT;
    while ((!eof || !done) && millis() < deadline) {
        ssize_t n = read(p[0], buf + used, FILE_LIMIT - used);
        if (n > 0) { used += (size_t)n; if (used == FILE_LIMIT) { bad = true; break; } }
        else if (n == 0) eof = true;
        else if (errno != EINTR && errno != EAGAIN) { bad = true; break; }
        if (!done) { pid_t w = waitpid(pid, &status, WNOHANG); if (w == pid) done = true; }
        if (n <= 0 && (!done || !eof)) { struct pollfd pollfd = {p[0], POLLIN, 0}; (void)poll(&pollfd, 1, 20); }
    }
    close(p[0]);
    if (!done || !eof || bad) { (void)kill(-pid, SIGKILL); if (!done) (void)waitpid(pid, &status, 0); free(buf); fail("Il comando non risponde o restituisce troppi dati"); return NULL; }
    if (!WIFEXITED(status) || WEXITSTATUS(status)) { free(buf); fail("Il comando Hyprland non è riuscito"); return NULL; }
    buf[used] = 0; return buf;
}
static json_object *live_monitors(void) {
    char *args[] = {"hyprctl", "-j", "monitors", "all", NULL};
    char *text = command(args);
    if (!text) return NULL;
    json_object *o = json_tokener_parse(text); free(text);
    if (!o || !json_object_is_type(o, json_type_array) || json_object_array_length(o) > 64) {
        json_object_put(o); fail("Risposta monitor di Hyprland non valida"); return NULL;
    }
    return o;
}
static bool reload_config(void) {
    char *a[] = {"hyprctl", "reload", NULL};
    char *b[] = {"hyprctl", "configerrors", NULL};
    char *text = command(a); if (!text) return false; free(text);
    text = command(b); if (!text) return false;
    text[strcspn(text, "\r\n")] = 0;
    bool ok = !*text || !strcmp(text, "ok");
    if (!ok) (void)snprintf(error_text, sizeof(error_text), "Hyprland ha rifiutato la configurazione: %.850s", text);
    free(text); return ok;
}
static json_object *find_monitor(json_object *live, const char *name) {
    for (size_t i = 0; i < json_object_array_length(live); i++) {
        json_object *m = json_object_array_get_idx(live, i);
        if (!strcmp(str(m, "name"), name)) return m;
    }
    return NULL;
}
static bool active(json_object *m) {
    return m && !json_object_get_boolean(get(m, "disabled")) && num(m, "width") > 0 && num(m, "height") > 0;
}
static bool same_output(json_object *actual, json_object *expected) {
    if (!actual) return false;
    bool disabled = json_object_get_boolean(get(expected, "disabled"));
    if (disabled) return !active(actual);
    if (!active(actual)) return false;
    const char *keys[] = {"width", "height", "transform", "x", "y", NULL};
    for (size_t i = 0; keys[i]; i++) if (num(actual, keys[i]) != num(expected, keys[i])) return false;
    return fabs(num(actual, "scale") - num(expected, "scale")) < 0.005 && fabs(num(actual, "refreshRate") - num(expected, "refreshRate")) < 0.08;
}
static bool verify_plan(json_object *planned) {
    int64_t deadline = millis() + 2500;
    do {
        json_object *live = live_monitors(); bool matches = live != NULL;
        if (live) {
            for (size_t i = 0; i < json_object_array_length(planned); i++) {
                json_object *m = json_object_array_get_idx(planned, i);
                if (!same_output(find_monitor(live, str(m, "name")), m)) matches = false;
            }
            json_object_put(live);
        }
        if (matches) return true;
        nap(100);
    } while (!stopped && millis() < deadline);
    fail("Hyprland non ha applicato la disposizione richiesta. Impostazioni precedenti ripristinate.");
    return false;
}
static json_object *load_state(json_object *live) {
    char path[PATH_MAX];
    if (!path_for(path, "layout.json")) return NULL;
    if (access(path, F_OK) && errno == ENOENT) return monitor_initial_state(live);
    json_object *state = read_json("layout.json");
    if (!state || json_object_get_int(get(state, "version")) != 1 || !json_object_is_type(get(state, "outputs"), json_type_object) || !json_object_is_type(get(state, "relations"), json_type_array)) {
        json_object_put(state); fail("Lo stato salvato dei monitor non è valido; configurazione non modificata"); return NULL;
    }
    return state;
}
static bool dialog_paths(json_object *pending, char *selection, char *done) {
    const char *token = str(pending, "token");
    if (strlen(token) != 32 || strspn(token, "0123456789abcdef") != 32) return false;
    char file[80];
    (void)snprintf(file, sizeof(file), "confirm-%s.selection", token);
    if (!path_for(selection, file)) return false;
    (void)snprintf(file, sizeof(file), "confirm-%s.done", token);
    return path_for(done, file);
}
static void hide_confirmation(json_object *pending) {
    char selection[PATH_MAX], done[PATH_MAX];
    if (!json_object_get_boolean(get(pending, "dialogShown")) || !dialog_paths(pending, selection, done) || !access(done, F_OK)) return;
    char saved[sizeof(error_text)]; (void)snprintf(saved, sizeof(saved), "%s", error_text);
    char *args[] = {"omarchy-shell", "shell", "hide", "omarchy.menu", NULL};
    char *out = command(args); free(out);
    /* The menu writes its completion file asynchronously when it closes. */
    for (int i = 0; i < 10 && access(done, F_OK); i++) nap(20);
    (void)snprintf(error_text, sizeof(error_text), "%s", saved);
}
static bool show_confirmation(json_object *pending) {
    char selection[PATH_MAX], done[PATH_MAX], prompt[512];
    if (!dialog_paths(pending, selection, done)) return false;
    (void)unlink(selection); (void)unlink(done);
    /* A freshly mapped central menu remains visible when a monitor has moved. */
    char *hide[] = {"omarchy-shell", "shell", "hide", "omarchy.monitor", NULL};
    char *out = command(hide); free(out);
    json_object *payload = json_object_new_object(), *options = json_object_new_array();
    (void)snprintf(prompt, sizeof(prompt), "%s. Mantenere? Ripristino automatico dopo %d secondi", str(pending, "summary"), MONITOR_CONFIRM_SECONDS);
    json_object_object_add(payload, "mode", json_object_new_string("select"));
    json_object_object_add(payload, "prompt", json_object_new_string(prompt));
    json_object_array_add(options, json_object_new_string("Ripristina"));
    json_object_array_add(options, json_object_new_string("Mantieni"));
    json_object_object_add(payload, "options", options);
    json_object_object_add(payload, "width", json_object_new_int(720));
    json_object_object_add(payload, "selectionFile", json_object_new_string(selection));
    json_object_object_add(payload, "doneFile", json_object_new_string(done));
    char *args[] = {"omarchy-shell", "shell", "summon", "omarchy.menu", (char *)json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN), NULL};
    out = command(args);
    bool ok = out && !strncmp(out, "ok", 2);
    free(out); json_object_put(payload);
    if (!ok) fail("Impossibile aprire la conferma a schermo. Impostazioni precedenti ripristinate.");
    return ok;
}
static void clear_transaction(void) {
    json_object *pending = read_json("pending.json");
    char selection[PATH_MAX], done[PATH_MAX];
    if (pending && dialog_paths(pending, selection, done)) { (void)unlink(selection); (void)unlink(done); }
    json_object_put(pending);
    remove_state("pending.json"); remove_state("response.json");
    remove_state("next.json"); remove_state("before.json"); remove_state("before.lua");
}
static bool restore_transaction(void) {
    json_object *pending = read_json("pending.json");
    if (pending) hide_confirmation(pending);
    json_object_put(pending);
    char path[PATH_MAX];
    if (!path_for(path, "before.lua")) return false;
    char *previous = read_file(path);
    json_object *state = read_json("before.json");
    if (!previous || !state) { free(previous); json_object_put(state); fail("Backup monitor mancante: impossibile ripristinare"); return false; }
    bool ok = write_atomic(config_path, previous) && write_json("layout.json", state);
    free(previous); json_object_put(state);
    if (ok) ok = reload_config();
    if (ok) clear_transaction();
    return ok;
}
static void notify(const char *message) {
    char *args[] = {"omarchy-notification-send", "Monitor", (char *)message, NULL};
    char saved[sizeof(error_text)]; (void)snprintf(saved, sizeof(saved), "%s", error_text);
    char *out = command(args); free(out);
    (void)snprintf(error_text, sizeof(error_text), "%s", saved);
}
static void detached_stdio(void) {
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) { for (int i = 0; i <= 2; i++) (void)dup2(fd, i); if (fd > 2) close(fd); }
}
static void watchdog(int lockfd, json_object *pending) {
    (void)setsid(); detached_stdio();
    const char *token = str(pending, "token");
    int64_t deadline = millis() + MONITOR_CONFIRM_SECONDS * 1000;
    bool keep = false;
    while (!stopped && millis() < deadline) {
        json_object *response = read_json("response.json");
        bool answered = response && !strcmp(str(response, "token"), token);
        if (answered) keep = !strcmp(str(response, "action"), "confirm");
        json_object_put(response);
        if (answered) break;
        char selection[PATH_MAX], done[PATH_MAX];
        if (dialog_paths(pending, selection, done) && !access(done, F_OK)) {
            char *answer = read_file(selection);
            if (answer) { answer[strcspn(answer, "\r\n")] = 0; keep = !strcmp(answer, "Mantieni"); }
            free(answer); break;
        }
        nap(100);
    }
    if (keep) {
        json_object *next = read_json("next.json");
        keep = next && write_json("layout.json", next);
        json_object_put(next);
    }
    if (keep) { hide_confirmation(pending); clear_transaction(); remove_state("error.json"); }
    else if (restore_transaction()) { notify("Impostazioni precedenti ripristinate"); }
    else { set_error(error_text); notify("Ripristino non riuscito: controlla le impostazioni monitor"); }
    json_object_put(pending); close(lockfd); _exit(0);
}
static int emit(json_object *o) {
    puts(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN)); json_object_put(o); return 0;
}
static int emit_error(void) {
    if (!*error_text) fail("Operazione monitor non riuscita");
    json_object *o = json_object_new_object();
    json_object_object_add(o, "error", json_object_new_string(error_text));
    (void)emit(o); fprintf(stderr, "%s\n", error_text); return 1;
}
static int status_command(void) {
    json_object *live = live_monitors();
    if (!live) return emit_error();
    json_object *state = load_state(live);
    if (!state) { json_object_put(live); return emit_error(); }
    json_object *o = json_object_new_object();
    json_object *inventory = monitor_initial_state(live);
    json_object *outputs = get(inventory, "outputs");
    if (outputs) {
        json_object_object_foreach(outputs, id, saved) {
            json_object *m = find_monitor(live, str(saved, "name"));
            if (!m) continue;
            json_object_object_add(m, "id", json_object_new_string(id));
            json_object_object_add(m, "relation", NULL);
            json_object *relations = get(state, "relations");
            for (size_t i = 0; i < json_object_array_length(relations); i++) {
                json_object *r = json_object_array_get_idx(relations, i);
                if (strcmp(str(r, "monitor"), id)) continue;
                json_object *ref = get(outputs, str(r, "reference"));
                if (!ref) continue;
                json_object *relation = json_object_new_object();
                json_object_object_add(relation, "reference", json_object_new_string(str(ref, "name")));
                json_object_object_add(relation, "direction", json_object_get(get(r, "direction")));
                json_object_object_add(relation, "alignment", json_object_get(get(r, "alignment")));
                json_object_object_add(m, "relation", relation);
            }
        }
    }
    json_object_put(inventory);
    json_object_object_add(o, "monitors", live);
    json_object_object_add(o, "pending", read_json("pending.json"));
    json_object *e = read_json("error.json");
    json_object_object_add(o, "error", e ? json_object_get(get(e, "message")) : NULL);
    json_object_put(e);
    json_object_object_add(o, "relations", json_object_get(get(state, "relations")));
    json_object_put(state); return emit(o);
}
static int response_command(const char *action, const char *token) {
    json_object *p = read_json("pending.json");
    if (!p || strcmp(str(p, "token"), token) || num(p, "deadline") <= (double)time(NULL)) {
        json_object_put(p); fail("La modifica non è più in attesa di conferma"); return emit_error();
    }
    json_object_put(p);
    json_object *r = json_object_new_object();
    json_object_object_add(r, "token", json_object_new_string(token));
    json_object_object_add(r, "action", json_object_new_string(action));
    bool ok = write_json("response.json", r); json_object_put(r);
    if (!ok) return emit_error();
    r = json_object_new_object(); json_object_object_add(r, "ok", json_object_new_boolean(true)); return emit(r);
}
static bool create_token(char *out, size_t length) {
    unsigned char bytes[16]; int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, bytes, sizeof(bytes)); close(fd);
    if (n != (ssize_t)sizeof(bytes) || length < 33) return false;
    for (size_t i = 0; i < sizeof(bytes); i++) (void)snprintf(out + i * 2, length - i * 2, "%02x", (unsigned int)bytes[i]);
    return true;
}
static int apply_command(int argc, char **argv, bool preview) {
    if (argc < 2 || argc > 5) { fail("Uso: apply MONITOR AZIONE [VALORE] [RIFERIMENTO] [ALLINEAMENTO]"); return emit_error(); }
    int lockfd = lock_file("lock");
    int64_t lock_deadline = millis() + 4000;
    while (lockfd < 0 && !stopped && millis() < lock_deadline) {
        json_object *busy = read_json("pending.json");
        if (busy) { json_object_put(busy); fail("Un'altra modifica è in attesa di conferma"); return emit_error(); }
        /* The connection watcher briefly owns the same lock during polling.
         * Let an ordinary click wait for it instead of reporting a false pending change. */
        nap(40); lockfd = lock_file("lock");
    }
    if (lockfd < 0) { fail("Sincronizzazione dei monitor in corso; riprova tra un momento"); return emit_error(); }
    json_object *old_pending = read_json("pending.json");
    if (old_pending) {
        json_object_put(old_pending);
        if (!restore_transaction()) { close(lockfd); return emit_error(); }
    }
    json_object *live = live_monitors(), *state = live ? load_state(live) : NULL;
    json_object *plan = state ? monitor_plan(live, state, argv[0], argv[1], argc > 2 ? argv[2] : "", argc > 3 ? argv[3] : "", argc > 4 ? argv[4] : "center", error_text, sizeof(error_text)) : NULL;
    char *original = plan ? read_file(config_path) : NULL;
    char *updated = original ? monitor_configuration(original, get(plan, "monitors"), error_text, sizeof(error_text)) : NULL;
    int result = 1;
    if (!plan || !original || !updated) goto out;
    if (preview) { result = emit(json_object_get(plan)); goto out; }
    char token[33];
    if (!create_token(token, sizeof(token))) goto out;
    json_object *p = json_object_new_object();
    json_object_object_add(p, "token", json_object_new_string(token));
    json_object_object_add(p, "deadline", json_object_new_int64(0));
    const char *label = !strcmp(argv[1], "position") ? "Posizione" : !strcmp(argv[1], "mode") ? "Risoluzione" :
        !strcmp(argv[1], "rotation") ? "Rotazione" : !strcmp(argv[1], "scale") ? "Scala" :
        !strcmp(argv[1], "disable") ? "Disattivazione" : "Attivazione";
    char summary[256]; (void)snprintf(summary, sizeof(summary), "%s · %s", argv[0], label);
    json_object_object_add(p, "summary", json_object_new_string(summary));
    json_object_object_add(p, "phase", json_object_new_string("applying"));
    remove_state("response.json"); remove_state("error.json");
    bool prepared = write_state_text("before.lua", original) && write_json("before.json", state) && write_json("next.json", get(plan, "state")) && write_json("pending.json", p);
    if (!prepared) { json_object_put(p); fail("Impossibile salvare il backup monitor"); goto out; }
    if (!write_atomic(config_path, updated) || !reload_config() || !verify_plan(get(plan, "monitors")) || stopped) {
        char reason[sizeof(error_text)]; (void)snprintf(reason, sizeof(reason), "%s", error_text);
        if (restore_transaction()) fail(*reason ? reason : "Modifica annullata");
        set_error(error_text); json_object_put(p); goto out;
    }
    json_object_object_add(p, "dialogShown", json_object_new_boolean(true));
    if (!write_json("pending.json", p) || !show_confirmation(p)) {
        char reason[sizeof(error_text)]; (void)snprintf(reason, sizeof(reason), "%s", error_text);
        if (restore_transaction()) fail(reason);
        set_error(error_text); json_object_put(p); goto out;
    }
    json_object_object_add(p, "deadline", json_object_new_int64((int64_t)time(NULL) + MONITOR_CONFIRM_SECONDS));
    json_object_object_add(p, "phase", json_object_new_string("confirming"));
    if (!write_json("pending.json", p)) { (void)restore_transaction(); json_object_put(p); goto out; }
    pid_t pid = fork();
    if (pid < 0) { (void)restore_transaction(); json_object_put(p); goto out; }
    if (!pid) watchdog(lockfd, p);
    json_object *response = json_object_new_object();
    json_object_object_add(response, "pending", p);
    result = emit(response);
out:
    free(original); free(updated); json_object_put(plan); json_object_put(state); json_object_put(live); close(lockfd);
    return result ? emit_error() : result;
}

static bool topology_equal(json_object *a, json_object *b) {
    if (!a || !b || json_object_array_length(a) != json_object_array_length(b)) return false;
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        json_object *m = json_object_array_get_idx(a, i), *n = find_monitor(b, str(m, "name"));
        if (!n || strcmp(str(m, "description"), str(n, "description")) || json_object_get_boolean(get(m, "disabled")) != json_object_get_boolean(get(n, "disabled"))) return false;
    }
    return true;
}
static bool geometry_equal(json_object *a, json_object *b) {
    if (!topology_equal(a, b)) return false;
    const char *keys[] = {"width", "height", "scale", "transform", NULL};
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        json_object *m = json_object_array_get_idx(a, i), *n = find_monitor(b, str(m, "name"));
        for (size_t j = 0; keys[j]; j++) if (num(m, keys[j]) != num(n, keys[j])) return false;
    }
    return true;
}
static bool reconcile_once(json_object *live, bool topology) {
    json_object *state = load_state(live);
    json_object *plan = state ? monitor_reconcile(live, state, topology, error_text, sizeof(error_text)) : NULL;
    char *original = plan ? read_file(config_path) : NULL;
    char *updated = original ? monitor_configuration(original, get(plan, "monitors"), error_text, sizeof(error_text)) : NULL;
    bool ok = updated != NULL;
    if (ok && strcmp(original, updated)) {
        ok = write_state_text("automatic.before.lua", original) && write_atomic(config_path, updated) && reload_config() && verify_plan(get(plan, "monitors"));
        if (!ok) {
            char reason[sizeof(error_text)]; (void)snprintf(reason, sizeof(reason), "%s", error_text);
            (void)write_atomic(config_path, original); (void)reload_config(); fail(reason);
        }
    }
    if (ok) ok = write_json("layout.json", get(plan, "state"));
    free(original); free(updated); json_object_put(plan); json_object_put(state);
    return ok;
}
static int daemon_command(void) {
    int daemonfd = lock_file("daemon.lock");
    if (daemonfd < 0) { fail("Il servizio monitor è già attivo"); return emit_error(); }
    json_object *previous = NULL;
    bool was_busy = false;
    while (!stopped) {
        int lockfd = lock_file("lock");
        if (lockfd < 0) { was_busy = true; nap(500); continue; }
        json_object *pending = read_json("pending.json");
        if (pending) {
            if (!restore_transaction()) set_error(error_text);
            json_object_put(pending); was_busy = true;
        }
        json_object *live = live_monitors();
        if (live && json_object_array_length(live)) {
            bool topology = !topology_equal(previous, live);
            bool geometry = !geometry_equal(previous, live);
            if (was_busy) { geometry = false; was_busy = false; }
            /* Coalesce link training/MST events before touching output rules. */
            if (topology) {
                nap(700);
                json_object *settled = live_monitors();
                if (!settled || !topology_equal(live, settled)) {
                    json_object_put(settled); json_object_put(live); close(lockfd); nap(500); continue;
                }
                json_object_put(live); live = settled;
            }
            if (topology || geometry) {
                if (!reconcile_once(live, topology)) { set_error(error_text); fprintf(stderr, "%s\n", error_text); }
                json_object *after = live_monitors();
                /* A connector can change while reload/verification is running.
                 * Do not consume that new event before reconciling its layout. */
                if (after && topology_equal(live, after)) { json_object_put(live); live = after; }
                else json_object_put(after);
            }
            json_object_put(previous); previous = live;
        } else json_object_put(live);
        close(lockfd); nap(1000);
    }
    json_object_put(previous); close(daemonfd); return 0;
}
static int open_panel(void) {
    char *args[] = {"omarchy-shell", "shell", "summon", "omarchy.monitor", "{}", NULL};
    char *out = command(args); if (!out) return emit_error(); free(out); return 0;
}
static int isolated_apply(int argc, char **argv) {
    /* Disabling the host output can destroy the QML Process that launched us.
     * Finish the transaction and show its confirmation from an independent child. */
    pid_t pid = fork();
    if (pid < 0) return emit_error();
    if (!pid) { (void)setsid(); exit(apply_command(argc, argv, false)); }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) { if (errno != EINTR) return 1; }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
int main(int argc, char **argv) {
    struct sigaction sa; memset(&sa, 0, sizeof(sa)); sa.sa_handler = on_signal; sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGTERM, &sa, NULL); (void)sigaction(SIGINT, &sa, NULL);
    (void)signal(SIGPIPE, SIG_IGN);
    if (!init_paths()) return emit_error();
    const char *base = strrchr(argv[0], '/'); base = base ? base + 1 : argv[0];
    if (!strcmp(base, "omarchy-monitor-layout")) return open_panel();
    if (!strcmp(base, "omarchy-monitor-apply")) return isolated_apply(argc - 1, argv + 1);
    if (argc == 1 || !strcmp(argv[1], "--help")) {
        puts("omarchy-monitor-control status|open|daemon\n"
             "omarchy-monitor-control apply|preview MONITOR mode|scale|rotation VALUE\n"
             "omarchy-monitor-control apply|preview MONITOR position left|right|up|down REFERENCE start|center|end\n"
             "omarchy-monitor-control apply|preview MONITOR enable|disable\n"
             "omarchy-monitor-control confirm|revert TOKEN"); return 0;
    }
    if (!strcmp(argv[1], "status") && argc == 2) return status_command();
    if (!strcmp(argv[1], "open") && argc == 2) return open_panel();
    if (!strcmp(argv[1], "daemon") && argc == 2) return daemon_command();
    if ((!strcmp(argv[1], "confirm") || !strcmp(argv[1], "revert")) && argc == 3) return response_command(argv[1], argv[2]);
    if (!strcmp(argv[1], "apply")) return isolated_apply(argc - 2, argv + 2);
    if (!strcmp(argv[1], "preview")) return apply_command(argc - 2, argv + 2, true);
    fail("Comando monitor sconosciuto"); return emit_error();
}
