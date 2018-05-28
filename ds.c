#include "stdio.h"
#include "pub.h"

int rfib(int n)
{
    if (n == 0)
        return 0;
    if (n == 1 || n == 2)
        return 1;
    return rfib(n - 1) + rfib(n - 2);
}

int lfib(int n)
{
    int f1 = 1;
    int f2 = 1;
    int fs = 0;
    int i = 0;

    if (n <= 2) {
        return 1;
    }

    for (i = 3; i <= n; i++) {
        fs = f1 + f2;
        f1 = f2;
        f2 = fs;
    }

    return fs;
}

int fib(int nCount)
{
    int op = 0;

    printf("1 recursion\n");
    printf("2 loop\n");
    printf("Enter==>");
    scanf("%d", &op);

    if (op == 1) {
        printf("%d Count Number is:%d\n", nCount, rfib(nCount));
    } else if (op == 2) {
        lfib(nCount);
        printf("%d Count Number is:%d\n", nCount, lfib(nCount));
    } else {
        printf("Enter error.\n");
        return -1;
    }

    return 0;
}
