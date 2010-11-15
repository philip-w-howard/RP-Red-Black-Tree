#include <stdio.h>

//Copyright (c) 2010 Philip W. Howard
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in
//all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

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

