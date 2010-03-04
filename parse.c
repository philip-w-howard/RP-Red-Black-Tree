#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long long count_t;

typedef struct info_s
{
    char *test;
    int  nthreads;
    count_t reads;
    count_t writes;
    struct info_s *next;
} info_t;

#define MAX_TESTS 30

static void insert(info_t *dataset, char *test, int nthreads, count_t reads, count_t writes)
{
    int ii;
    info_t *node;

    for (ii=0; ii<MAX_TESTS; ii++)
    {
        if (dataset[ii].test == NULL)
        {
            dataset[ii].test = (char *)malloc(strlen(test)+1);
            strcpy( dataset[ii].test, test);
            dataset[ii].nthreads = nthreads;
            dataset[ii].reads = reads;
            dataset[ii].writes = writes;
            dataset[ii].next = NULL;

            return;
        } 
        else if (strcmp( dataset[ii].test, test) == 0) 
        {
            node = (info_t *)malloc(sizeof(info_t));

            node->test = NULL;
            node->nthreads = nthreads;
            node->reads = reads;
            node->writes = writes;
            node->next = dataset[ii].next;
            dataset[ii].next = node;
            
            return;
        }
    }

    fprintf(stderr, "Too many test ID's\n");
    exit(-1);
}

static void output(info_t *dataset)
{
    int n_tests, ii;
    int threads;
    info_t *temp;

    printf("threads");

    n_tests = 0;
    while (n_tests < MAX_TESTS && dataset[n_tests].test != NULL)
    {
        printf(" %s_reads %s_writes", dataset[n_tests].test, dataset[n_tests].test);
        n_tests++;
    }
    printf("\n");

    while (dataset[0].test != NULL)
    {
        threads = dataset[0].nthreads;
        printf("%d", threads);

        for (ii=0; ii<n_tests; ii++)
        {
            if (dataset[ii].nthreads != threads)
            {
                fprintf(stderr, "dataset mismatch\n");
                exit(-1);
            }
            printf(" %lld %lld", dataset[ii].reads, dataset[ii].writes);
            if (dataset[ii].next == NULL)
            {
                dataset[0].test = NULL;
            }
            else
            {
                temp = dataset[ii].next;
                dataset[ii].nthreads = dataset[ii].next->nthreads;
                dataset[ii].reads = dataset[ii].next->reads;
                dataset[ii].writes = dataset[ii].next->writes;
                dataset[ii].next = dataset[ii].next->next;
                free(temp);
            }
        }
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    char buff[1000];
    char *name, *c_threads, *c_reads, *c_writes;
    int line_count = 0;
    int ii;
    FILE *input = fopen("redblack.txt", "r");

    info_t dataset[MAX_TESTS];
    for (ii=0; ii<MAX_TESTS; ii++)
    {
        dataset[ii].test = NULL;
    }

    while (fgets(buff, sizeof(buff), input))
    {
        line_count++;

        name = strtok(buff, " ");
        c_threads = strtok(NULL, " ");
        c_reads = strtok(NULL, " ");
        c_writes = strtok(NULL, " ");

        if (c_writes == NULL)
        {
            fprintf(stderr, "Invalid line at %d\n", line_count);
            exit(-1);
        }

        insert(dataset, name, atoi(c_threads), atoll(c_reads), atoll(c_writes));
    }

    output(dataset);

    fprintf(stderr, "processed input file with %d lines\n", line_count);

    return 0;
}

