#include "askpass-json.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void askpass_clear(void *buffer, size_t length)
{
    volatile unsigned char *p = buffer;
    while (length--) *p++ = 0;
}

size_t askpass_utf8_width(const unsigned char *s, size_t n)
{
    size_t width, i;
    if (!n) return 0;
    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xc2 && s[0] <= 0xdf) width = 2;
    else if (s[0] >= 0xe0 && s[0] <= 0xef) width = 3;
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) width = 4;
    else return 0;
    if (n < width) return 0;
    for (i = 1; i < width; ++i) if ((s[i] & 0xc0) != 0x80) return 0;
    if ((s[0] == 0xe0 && s[1] < 0xa0) || (s[0] == 0xed && s[1] >= 0xa0) ||
        (s[0] == 0xf0 && s[1] < 0x90) || (s[0] == 0xf4 && s[1] >= 0x90)) return 0;
    return width;
}

void askpass_clean_metadata(char *out, size_t capacity, const char *input, size_t length)
{
    size_t pos = 0, used = 0;
    if (!capacity) return;
    while (pos < length) {
        const unsigned char *s = (const unsigned char *)input + pos;
        size_t width = askpass_utf8_width(s, length - pos);
        const char *value = input + pos;
        size_t count = width;
        if (!width) { value = "\xef\xbf\xbd"; count = 3; width = 1; }
        else if (s[0] < 0x20 || s[0] == 0x7f || (width == 2 && s[0] == 0xc2 && s[1] < 0xa0)) {
            value = " "; count = 1;
        }
        if (count >= capacity - used) break;
        memcpy(out + used, value, count);
        used += count;
        pos += width;
    }
    out[used] = '\0';
}

struct writer { char *out; size_t capacity, used; bool valid; };

static void append(struct writer *w, const char *s, size_t n)
{
    if (!w->valid || n >= w->capacity - w->used) { w->valid = false; return; }
    memcpy(w->out + w->used, s, n);
    w->used += n;
    w->out[w->used] = '\0';
}

static void text(struct writer *w, const char *s) { append(w, s, strlen(s)); }

static void string(struct writer *w, const char *s)
{
    size_t left = strlen(s);
    text(w, "\"");
    while (left && w->valid) {
        unsigned char c = (unsigned char)*s;
        size_t width = askpass_utf8_width((const unsigned char *)s, left);
        if (!width) { text(w, "\xef\xbf\xbd"); width = 1; }
        else if (c < 0x20) {
            char escape[7];
            (void)snprintf(escape, sizeof escape, "\\u%04x", (unsigned)c);
            text(w, escape);
        } else {
            if (c == '"' || c == '\\') text(w, "\\");
            append(w, s, width);
        }
        s += width;
        left -= width;
    }
    text(w, "\"");
}

bool askpass_encode_request(const char *prompt, const struct askpass_context *context,
                            char *output, size_t capacity, size_t *length)
{
    struct writer w = { output, capacity, 0, capacity > 0 };
    size_t base, i;
    if (capacity) output[0] = '\0';
    text(&w, "{\"prompt\":"); string(&w, prompt);
    if (!w.valid) return false;
    base = w.used;
    if (context && context->count <= ASKPASS_PROCESS_LIMIT) {
        text(&w, ",\"context\":{\"processes\":[");
        for (i = 0; i < context->count; ++i) {
            char number[32];
            if (i) text(&w, ",");
            (void)snprintf(number, sizeof number, "%ld", (long)context->processes[i].pid);
            text(&w, "{\"pid\":"); text(&w, number);
            text(&w, ",\"executable\":"); string(&w, context->processes[i].executable); text(&w, "}");
        }
        text(&w, "]");
        if (context->cwd[0]) { text(&w, ",\"cwd\":"); string(&w, context->cwd); }
        if (context->operation[0]) { text(&w, ",\"operation\":"); string(&w, context->operation); }
        if (context->destination[0]) { text(&w, ",\"destination\":"); string(&w, context->destination); }
        text(&w, "}");
    }
    text(&w, "}\n");
    if (!w.valid) {
        w.valid = true; w.used = base;
        text(&w, "}\n");
    }
    if (!w.valid) return false;
    *length = w.used;
    return true;
}

struct parser { const unsigned char *s; size_t left; };

static void whitespace(struct parser *p)
{
    while (p->left && (*p->s == ' ' || *p->s == '\t' || *p->s == '\r' || *p->s == '\n')) {
        ++p->s; --p->left;
    }
}

static bool consume(struct parser *p, unsigned char c)
{
    whitespace(p);
    if (!p->left || *p->s != c) return false;
    ++p->s; --p->left;
    return true;
}

static bool literal(struct parser *p, const char *s)
{
    size_t n = strlen(s);
    if (p->left < n || memcmp(p->s, s, n) != 0) return false;
    p->s += n; p->left -= n;
    return true;
}

static bool hex4(struct parser *p, uint32_t *value)
{
    size_t i;
    *value = 0;
    if (p->left < 4) return false;
    for (i = 0; i < 4; ++i) {
        unsigned char c = p->s[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a') + 10U;
        else if (c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A') + 10U;
        else return false;
        *value = *value * 16U + digit;
    }
    p->s += 4; p->left -= 4;
    return true;
}

static size_t scalar_bytes(uint32_t cp, unsigned char *out)
{
    if (cp < 0x80) { out[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xc0U | cp >> 6);
        out[1] = (unsigned char)(0x80U | (cp & 63U)); return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xe0U | cp >> 12);
        out[1] = (unsigned char)(0x80U | ((cp >> 6) & 63U));
        out[2] = (unsigned char)(0x80U | (cp & 63U)); return 3;
    }
    out[0] = (unsigned char)(0xf0U | cp >> 18);
    out[1] = (unsigned char)(0x80U | ((cp >> 12) & 63U));
    out[2] = (unsigned char)(0x80U | ((cp >> 6) & 63U));
    out[3] = (unsigned char)(0x80U | (cp & 63U)); return 4;
}

/* NULL output validates/skips a string. Decoded strings may contain NUL. */
static bool parse_string(struct parser *p, char *out, size_t capacity, size_t *length)
{
    size_t used = 0;
    if (!consume(p, '"')) return false;
    while (p->left) {
        unsigned char bytes[4], c = *p->s;
        const unsigned char *value = p->s;
        size_t width;
        if (c == '"') {
            ++p->s; --p->left;
            if (out) out[used] = '\0';
            *length = used;
            return true;
        }
        if (c < 0x20) return false;
        if (c == '\\') {
            ++p->s; --p->left;
            if (!p->left) return false;
            c = *p->s++; --p->left;
            value = bytes; width = 1;
            switch (c) {
            case '"': case '\\': case '/': bytes[0] = c; break;
            case 'b': bytes[0] = '\b'; break;
            case 'f': bytes[0] = '\f'; break;
            case 'n': bytes[0] = '\n'; break;
            case 'r': bytes[0] = '\r'; break;
            case 't': bytes[0] = '\t'; break;
            case 'u': {
                uint32_t cp, low;
                if (!hex4(p, &cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (!literal(p, "\\u") || !hex4(p, &low) || low < 0xdc00 || low > 0xdfff) return false;
                    cp = 0x10000U + ((cp - 0xd800U) << 10) + low - 0xdc00U;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                width = scalar_bytes(cp, bytes); break;
            }
            default: return false;
            }
        } else {
            width = askpass_utf8_width(p->s, p->left);
            if (!width) return false;
            p->s += width; p->left -= width;
        }
        if (out) {
            if (width >= capacity - used) return false;
            memcpy(out + used, value, width);
        }
        used += width;
    }
    return false;
}

static bool skip_value(struct parser *p, unsigned depth)
{
    size_t ignored;
    unsigned char close;
    if (depth > 16) return false;
    whitespace(p);
    if (!p->left) return false;
    if (*p->s == '"') return parse_string(p, NULL, 0, &ignored);
    if (*p->s == 't') return literal(p, "true");
    if (*p->s == 'f') return literal(p, "false");
    if (*p->s == 'n') return literal(p, "null");
    if (*p->s == '[' || *p->s == '{') {
        bool object = *p->s == '{';
        close = object ? '}' : ']';
        ++p->s; --p->left;
        whitespace(p);
        if (p->left && *p->s == close) { ++p->s; --p->left; return true; }
        for (;;) {
            if (object && (!parse_string(p, NULL, 0, &ignored) || !consume(p, ':'))) return false;
            if (!skip_value(p, depth + 1)) return false;
            whitespace(p);
            if (p->left && *p->s == close) { ++p->s; --p->left; return true; }
            if (!consume(p, ',')) return false;
        }
    }
    /* JSON number grammar, with no conversion or overflow-sensitive arithmetic. */
    if (*p->s == '-') { ++p->s; --p->left; }
    if (!p->left) return false;
    if (*p->s == '0') { ++p->s; --p->left; }
    else {
        if (*p->s < '1' || *p->s > '9') return false;
        do { ++p->s; --p->left; } while (p->left && *p->s >= '0' && *p->s <= '9');
    }
    if (p->left && *p->s == '.') {
        ++p->s; --p->left;
        if (!p->left || *p->s < '0' || *p->s > '9') return false;
        do { ++p->s; --p->left; } while (p->left && *p->s >= '0' && *p->s <= '9');
    }
    if (p->left && (*p->s == 'e' || *p->s == 'E')) {
        ++p->s; --p->left;
        if (p->left && (*p->s == '+' || *p->s == '-')) { ++p->s; --p->left; }
        if (!p->left || *p->s < '0' || *p->s > '9') return false;
        do { ++p->s; --p->left; } while (p->left && *p->s >= '0' && *p->s <= '9');
    }
    return true;
}

bool askpass_decode_reply(const char *reply, size_t length, char *response,
                          size_t capacity, size_t *response_length)
{
    struct parser p = { (const unsigned char *)reply, length };
    char key[ASKPASS_REPLY_LIMIT + 1];
    bool seen_ok = false, seen_response = false, ok = false;
    size_t used = 0, i;
    if (!capacity || length > ASKPASS_REPLY_LIMIT || !consume(&p, '{')) goto fail;
    whitespace(&p);
    if (p.left && *p.s == '}') goto fail;
    for (;;) {
        size_t key_length;
        if (!parse_string(&p, key, sizeof key, &key_length) || !consume(&p, ':')) goto fail;
        whitespace(&p);
        if (key_length == 2 && memcmp(key, "ok", 2) == 0) {
            if (seen_ok) goto fail;
            seen_ok = true;
            if (literal(&p, "true")) ok = true;
            else if (!literal(&p, "false")) goto fail;
        } else if (key_length == 8 && memcmp(key, "response", 8) == 0) {
            if (seen_response || !parse_string(&p, response, capacity, &used)) goto fail;
            seen_response = true;
        } else if (!skip_value(&p, 0)) goto fail;
        whitespace(&p);
        if (p.left && *p.s == '}') { ++p.s; --p.left; break; }
        if (!consume(&p, ',')) goto fail;
    }
    whitespace(&p);
    if (p.left || !seen_ok || !seen_response || !ok) goto fail;
    for (i = 0; i < used; ++i)
        if (response[i] == '\0' || response[i] == '\r' || response[i] == '\n') goto fail;
    *response_length = used;
    return true;
fail:
    if (capacity) askpass_clear(response, capacity);
    *response_length = 0;
    return false;
}
