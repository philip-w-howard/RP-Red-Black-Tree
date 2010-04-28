#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long long count_t;

typedef struct info_s
{
    char *test;
    int  size;
    int readers;
    int writers;
    count_t reads;
    count_t writes;
    struct info_s *next;
} info_t;

#define MAX_TESTS 30

static void insert(info_t *dataset, char *test, int size, int readers, int writers,
        count_t reads, count_t writes)
{
    int ii;
    info_t *node;

    printf("insert %s %d %d %d\n", test, size, readers, writers);
    for (ii=0; ii<MAX_TESTS; ii++)
    {
        if (dataset[ii].test == NULL)
        {
            dataset[ii].test = (char *)malloc(strlen(test)+1);
            strcpy( dataset[ii].test, test);
            dataset[ii].size = size;
            dataset[ii].readers = readers;
            dataset[ii].writers = writers;
            dataset[ii].reads = reads;
            dataset[ii].writes = writes;
            dataset[ii].next = NULL;

            return;
        } 
        else if (strcmp( dataset[ii].test, test) == 0) 
        {
            node = (info_t *)malloc(sizeof(info_t));

            node->test = NULL;
            node->size = size;
            node->readers = readers;
            node->writers = writers;
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
    int size;
    int readers;
    int writers;
    info_t *temp;

    printf("threads\treaders\twriters");

    n_tests = 0;
    while (n_tests < MAX_TESTS && dataset[n_tests].test != NULL)
    {
        printf("\t%s_reads\t%s_writes", dataset[n_tests].test, dataset[n_tests].test);
        n_tests++;
    }
    printf("\n");

    while (dataset[0].test != NULL)
    {
        size = dataset[0].size;
        readers = dataset[0].readers;
        writers = dataset[0].writers;
        printf("%d\t%d\t%d", size, readers, writers);

        for (ii=0; ii<n_tests; ii++)
        {
            if (dataset[ii].writers != writers ||
                dataset[ii].readers != readers ||
                dataset[ii].size    != size)
            {
                fprintf(stderr, "dataset mismatch\n");
                exit(-1);
            }
            printf("\t%lld\t%lld", dataset[ii].reads, dataset[ii].writes);
            if (dataset[ii].next == NULL)
            {
                dataset[0].test = NULL;
            }
            else
            {
                temp = dataset[ii].next;
                dataset[ii].size = dataset[ii].next->size;
                dataset[ii].readers = dataset[ii].next->readers;
                dataset[ii].writers = dataset[ii].next->writers;
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
    char *name, *c_size, *c_reads, *c_writes;
    char *c_readers, *c_writers;
    int line_count = 0;
    int ii;
    FILE *input;

    if (argc > 1)
    {
        input = fopen(argv[1], "r");
        if (input == NULL)
        {
            fprintf(stderr, "unable to open %s\n", argv[1]);
            exit(-1);
        }
    } else {
        input = stdin;
    }

    info_t dataset[MAX_TESTS];
    for (ii=0; ii<MAX_TESTS; ii++)
    {
        dataset[ii].test = NULL;
    }

    while (fgets(buff, sizeof(buff), input))
    {
        line_count++;
        name = strtok(buff, " ");
        c_size = strtok(NULL, " ");
        c_readers = strtok(NULL, " ");
        c_writers = strtok(NULL, " ");
        c_reads = strtok(NULL, " ");
        c_writes = strtok(NULL, " ");

        if (c_writes == NULL)
        {
            fprintf(stderr, "Invalid line at %d\n"
                    "%s %s %s %s %s %s\n", line_count,
                    name, c_size, c_readers, c_writers, c_reads, c_writes);
            exit(-1);
        }

        insert(dataset, name, atoi(c_size), atoi(c_readers), atoi(c_writers),
                atoll(c_reads), atoll(c_writes));
    }

    output(dataset);

    fprintf(stderr, "processed input file with %d lines\n", line_count);

    return 0;
}

