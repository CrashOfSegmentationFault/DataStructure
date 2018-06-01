#include <stdio.h>
#include <string.h>
#include "pub.h"

void usage()
{
    printf("./s fib numCount\n");
    printf("./s tree\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage();
        return 0;
    }

    if (strcmp(argv[1], "fib") == 0) {
        fib(atoi(argv[2]));
    } else if (strcmp(argv[1], "tree") == 0) {
        tree();
    }

    return 0;
}
