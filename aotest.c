#include <stdio.h>

#include "atomic_ops.h"

int main(int argc, char **argv)
{
    AO_t value = 0;
    AO_t a,b,c;

    a = AO_fetch_and_add_full(&value, 1);
    b = AO_fetch_and_add_full(&value, 1);
    c = AO_fetch_and_add_full(&value, 1);

    printf("%ld %ld %ld %ld\n", value, a,b,c);

    a = AO_fetch_and_add_full(&value, -1);
    b = AO_fetch_and_add_full(&value, -1);
    c = AO_fetch_and_add_full(&value, -1);

    printf("%ld %ld %ld %ld\n", value, a,b,c);

    a = AO_compare_and_swap_full(&value, 0, 5);
    b = AO_compare_and_swap_full(&value, 0, 5);
    c = AO_compare_and_swap_full(&value, 5, 10);

    printf("%ld %ld %ld %ld\n", value, a,b,c);
    return 0;
}

