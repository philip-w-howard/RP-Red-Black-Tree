#include <stdio.h>

#include "atomic_ops.h"

int main(int argc, char **argv)
{
    AO_t value = 0;
    AO_t a,b,c;

    a = AO_fetch_and_add_full(&value, 2);
    b = AO_fetch_and_add_full(&value, 2);
    c = AO_fetch_and_add_full(&value, 2);

    printf("%d %d %d %d\n", value, a,b,c);

    a = AO_fetch_and_add_full(&value, -2);
    b = AO_fetch_and_add_full(&value, -2);
    c = AO_fetch_and_add_full(&value, -2);

    printf("%d %d %d %d\n", value, a,b,c);

    a = AO_compare_and_swap_full(&value, 0, 5);
    b = AO_compare_and_swap_full(&value, 0, 5);
    c = AO_compare_and_swap_full(&value, 5, 10);

    printf("%d %d %d %d\n", value, a,b,c);

    return 0;
}

