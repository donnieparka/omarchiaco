#ifndef ASKPASS_JSON_H
#define ASKPASS_JSON_H

#include "askpass-context.h"
#include <stdbool.h>
#include <stddef.h>

#define ASKPASS_REQUEST_LIMIT 32768
#define ASKPASS_REPLY_LIMIT 8192

/* Valid UTF-8 scalar width, or zero for malformed input. */
size_t askpass_utf8_width(const unsigned char *text, size_t available);
/* Replace invalid UTF-8 and metadata controls; never split a scalar. */
void askpass_clean_metadata(char *output, size_t capacity, const char *input, size_t length);
bool askpass_encode_request(const char *prompt, const struct askpass_context *context,
                            char *output, size_t capacity, size_t *length);
/* Reply length excludes its transport newline. Successful output is NUL terminated. */
bool askpass_decode_reply(const char *reply, size_t length, char *response,
                          size_t capacity, size_t *response_length);
void askpass_clear(void *buffer, size_t length);

#endif
