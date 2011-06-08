#include <stdlib.h>
#include <stdio.h>
#include "my_stm.h"
#include "rcu.h"
#include "rbnode.h"

static __thread LONG_JMP_BUF Jump_Buff;
static __thread int In_TX = 0;
static int doing_init = 1;
static long long global_init = 0;
static long long thread_init =0;
static long long tx_malloc = 0;
static long long xfree = 0;
static long long get_long_jmp_buf = 0;
static long long start_tx = 0;
static long long commit_tx = 0;
static long long wait_grace_period = 0;
static long long write_word = 0;
static long long write_word_mb = 0;
static long long read_word = 0;
static long long xrp_free = 0;

void stm_init_done()
{
    doing_init = 0;
}

void wlpdstm_global_init()
{
    assert(doing_init);
    global_init++;
}

void wlpdstm_thread_init()
{
    assert(doing_init);
    thread_init++;
}
void wlpdstm_print_stats()
{
    printf(
        "global_init %lld\nthread_init %lld\ntx_malloc %lld\nfree %lld\n"
        "get_long_jmp_buf %lld\n"
        "start_tx %lld\ncommit_tx %lld\nwait_grace_period %lld\nwrite_word %lld\n"
        "write_word_mb %lld\nread_word %lld\nrp_free %lld\n",

        global_init, thread_init, tx_malloc, xfree, get_long_jmp_buf,
        start_tx, commit_tx, wait_grace_period, write_word,
        write_word_mb, read_word, xrp_free);

}
void *wlpdstm_tx_malloc(size_t size)
{
    tx_malloc++;
    assert(In_TX && doing_init);
    return malloc(size);
}
void wlpdstm_free(void *address)
{
    xfree++;
    assert(In_TX && doing_init);
    free(address);
}
LONG_JMP_BUF *wlpdstm_get_long_jmp_buf(tx_desc *tx)
{
    assert(doing_init);
    get_long_jmp_buf++;
    return &Jump_Buff;
}
void wlpdstm_start_tx()
{
    assert(doing_init);
    start_tx++;
    In_TX = 1;
}
void wlpdstm_commit_tx()
{
    commit_tx++;
    assert(In_TX && doing_init);
    In_TX = 0;
}

void wlpdstm_wait_grace_period(void *context)
{
    wait_grace_period++;
    assert(In_TX && doing_init);
    rp_wait_grace_period(context);
}
void wlpdstm_write_word(Word *address, Word value)
{
    write_word++;
    assert(In_TX && doing_init);
    *address = value;
}
void wlpdstm_write_word_mb(Word *address, Word value)
{
    write_word_mb++;
    assert(In_TX && doing_init);
    *address = value;
}
Word wlpdstm_read_word(Word *address)
{
    read_word++;
    assert(In_TX && doing_init);
    return *address;
}
void wlpdstm_rp_free(void *context, void *address)
{
    xrp_free++;
    assert(In_TX && doing_init);
    rp_free(context, rbnode_free, address);
}

