#include "applib.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* what)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main(void)
{
    /* Published FNV-1a vectors, so this pins the algorithm rather than only its
       self-consistency. A rewrite that is merely stable would still fail here. */
    check(applib_hash("", 0) == 2166136261u, "empty input is the FNV offset basis");
    check(applib_hash("a", 1) == 0xe40c292cu, "single byte matches the FNV-1a vector");
    check(applib_hash("foobar", 6) == 0xbf9cf968u, "six bytes match the FNV-1a vector");
    check(applib_hash(NULL, 0) == 2166136261u, "NULL with zero length is accepted");
    check(applib_name() != NULL && strlen(applib_name()) > 0, "library reports a name");
    if (failures == 0) { printf("PASS: applib\n"); }
    return failures == 0 ? 0 : 1;
}
