#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Turn a Git-generated, tab-separated short-hash / subject stream into notes.
 * The Git range is determined by the caller so this tool never guesses tags. */
static void markdown_text(FILE *output, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        if (strchr("\\`*_{}[]<>|", *cursor) != NULL)
            fputc('\\', output);
        if (*cursor >= 32)
            fputc(*cursor, output);
        ++cursor;
    }
}

static int tag_is_safe(const char *tag)
{
    size_t i;
    if (!tag || !tag[0])
        return 0;
    for (i = 0; tag[i]; ++i) {
        const char c = tag[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

int command_release_notes(int argc, char **argv)
{
    FILE *input = NULL, *output = NULL;
    char line[16384];
    size_t entries = 0;
    int result = 1;

    if (argc != 7 || !tag_is_safe(argv[2]) || !tag_is_safe(argv[3]) ||
        !(strcmp(argv[4], "stable") == 0 || strcmp(argv[4], "development") == 0) ||
        !tag_is_safe(argv[5])) {
        fprintf(stderr, "usage: njemu-tool release-notes LOG PREVIOUS-TAG CURRENT-TAG stable|development SHA OUTPUT\n");
        return 2;
    }
    input = fopen(argv[1], "rb");
    if (!input) {
        perror(argv[1]);
        return 1;
    }
    output = fopen(argv[6], "wb");
    if (!output) {
        perror(argv[6]);
        goto done;
    }

    if (strcmp(argv[4], "development") == 0) {
        fputs("## Development build\n\nBuilt from `master` at [`", output);
        markdown_text(output, argv[5]);
        fprintf(output, "`](https://github.com/fjtrujy/NJEMU/commit/%s). ", argv[5]);
        fputs("This rolling prerelease may change on every successful build.\n\n", output);
    } else {
        fputs("## NJEMU ", output);
        markdown_text(output, argv[3]);
        fputs("\n\n", output);
    }

    if (strcmp(argv[2], "initial") == 0) {
        fputs("### Initial tagged release\n\n", output);
    } else {
        fputs("### Changes since ", output);
        markdown_text(output, argv[2]);
        fputs("\n\n", output);
    }
    while (fgets(line, sizeof(line), input)) {
        char *tab = strchr(line, '\t');
        char *newline;
        if (!tab) {
            fprintf(stderr, "release-notes: malformed Git log input\n");
            goto done;
        }
        *tab = 0;
        newline = strpbrk(tab + 1, "\r\n");
        if (!newline && !feof(input)) {
            fprintf(stderr, "release-notes: commit subject too long\n");
            goto done;
        }
        if (newline)
            *newline = 0;
        if (!tag_is_safe(line) || !tab[1]) {
            fprintf(stderr, "release-notes: invalid commit record\n");
            goto done;
        }
        fputs("- ", output);
        markdown_text(output, tab + 1);
        fputs(" ([`", output);
        fputs(line, output);
        fprintf(output, "`](https://github.com/fjtrujy/NJEMU/commit/%s))\n", line);
        ++entries;
    }
    if (ferror(input))
        goto done;
    if (!entries)
        fputs("No changes since the preceding stable release.\n", output);

    if (strcmp(argv[2], "initial") == 0) {
        fprintf(output, "\n**Full history:** https://github.com/fjtrujy/NJEMU/commits/%s\n", argv[3]);
    } else {
        fputs("\n**Full changelog:** [", output);
        markdown_text(output, argv[2]);
        fputs("...", output);
        markdown_text(output, argv[3]);
        fprintf(output, "](https://github.com/fjtrujy/NJEMU/compare/%s...%s)\n",
                argv[2], argv[3]);
    }
    if (fflush(output) != 0 || ferror(output))
        goto done;
    result = 0;
done:
    if (input)
        fclose(input);
    if (output) {
        if (fclose(output) != 0)
            result = 1;
        if (result)
            remove(argv[6]);
    }
    return result;
}
