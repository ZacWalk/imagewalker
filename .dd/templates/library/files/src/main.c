#include "applib.h"

#include <stdio.h>
#include <string.h>

int main(int count, char** arguments)
{
    if (count == 2 && strcmp(arguments[1], "--help") == 0)
    {
        printf("Usage: %s [--help] [TEXT]\n", applib_name());
        return 0;
    }
    if (count > 2)
    {
        fprintf(stderr, "Unknown arguments. Use --help.\n");
        return 2;
    }
    const char* text = count == 2 ? arguments[1] : applib_name();
    printf("%s %08x\n", applib_name(), applib_hash(text, strlen(text)));
    return 0;
}
