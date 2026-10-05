#include "text.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

char *host_trim(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text))
        ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    return text;
}

int host_parse_u32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || *text == '\0' || *text == '-')
        return 0;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || *end != '\0' || parsed > 0xfffffffful)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

int host_parse_size(const char *text, size_t *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || *text == '\0' || *text == '-')
        return 0;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || *end != '\0' || (uintmax_t)parsed > (uintmax_t)(size_t)-1)
        return 0;
    *value = (size_t)parsed;
    return 1;
}

int host_ascii_identifier(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!(isalpha(*p) || *p == '_'))
        return 0;
    for (++p; *p != '\0'; ++p) {
        if (!(isalnum(*p) || *p == '_'))
            return 0;
    }
    return 1;
}

int host_ascii_game_name(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (*p == '\0')
        return 0;
    for (; *p != '\0'; ++p) {
        if (!(islower(*p) || isdigit(*p) || *p == '_'))
            return 0;
    }
    return 1;
}

char *host_strdup(const char *text)
{
    return host_strndup(text, strlen(text));
}

char *host_strndup(const char *text, size_t size)
{
    char *copy;

    if (size == (size_t)-1)
        return NULL;
    copy = (char *)malloc(size + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, text, size);
    copy[size] = '\0';
    return copy;
}
