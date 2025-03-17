#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdio.h>

char *find_file(char *pattern, char *path)
{
    static struct dirent file;
    DIR *dir;
    struct dirent *entry;
    int i, found, len1, len2;

    dir = opendir(path);
    if (dir == NULL) {
        return NULL;
    }

    found = 0;
    len1 = strlen(pattern);

    while (!found && (entry = readdir(dir)) != NULL)
    {
        len2 = strlen(entry->d_name);

        for (i = 0; i < len2; i++)
        {
            if (strncasecmp(&entry->d_name[i], pattern, len1) == 0)
            {
                strcpy(file.d_name, entry->d_name);
                found = 1;
                break;
            }
        }
    }

    closedir(dir);

    return found ? file.d_name : NULL;
}