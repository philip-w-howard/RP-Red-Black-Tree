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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef unsigned long long count_t;

typedef struct info_s
{
    count_t reads;
    count_t writes;
    struct info_s *next;
} info_t;

#define MAX_TESTS 30
#define MAX_SETUPS 500

static char *Tests[MAX_TESTS];
static int Num_Tests = 0;
static char *Setups[MAX_SETUPS];
static int Num_Setups = 0;
static info_t *Info[MAX_SETUPS][MAX_TESTS];

char *Input_Filename = NULL;
char *Output_Filename = NULL;
int  Compute_Average = 0;
int  Compute_Std_Dev = 0;
int  Check_Std_Dev = 0;
double Check_Std_Dev_Limit = 0.05;

static void init_data()
{
    int test, setup;

    for (test = 0; test<MAX_TESTS; test++)
    {
        Tests[test] = NULL;
    }

    for (setup=0; setup<MAX_SETUPS; setup++)
    {
        Setups[setup] = NULL;

        for (test = 0; test<MAX_TESTS; test++)
        {
            Info[setup][test] = NULL;
        }
    }
}

static int find(char **list, int list_size, char *key)
{
    int ii;

    for (ii=0; ii<list_size; ii++)
    {
        if (list[ii] == NULL)
        {
            list[ii] = (char *)malloc(strlen(key)+1);
            strcpy(list[ii], key);
            return ii;
        }

        if (strcmp(list[ii], key)==0) return ii;
    }

    fprintf(stderr, "Dataset overflow\n");
    exit(-1);
}

static void insert(char *test_name, int mode, int size, 
        int readers, int writers, int update,
        count_t reads, count_t writes)
{
    int test, setup;
    info_t *node;
    char setup_name[500];

    sprintf(setup_name, "%d\t%d\t%d\t%d\t%d", mode, size, readers, writers, update);

    test = find(Tests, MAX_TESTS, test_name);
    if (test >= Num_Tests) Num_Tests = test+1;
    setup = find(Setups, MAX_SETUPS, setup_name);
    if (setup >= Num_Setups) Num_Setups = setup+1;

    node = (info_t *)malloc(sizeof(info_t));

    node->reads = reads;
    node->writes = writes;
    node->next = Info[setup][test];
    Info[setup][test] = node;
}

static void output(FILE *output)
{
    int ii, jj;

    fprintf(output, "mode\tsize\treaders\twriters\tupdate percent");

    for (ii=0; ii<Num_Tests; ii++)
    {
        if (Compute_Std_Dev)
        {
            fprintf(output, "\t%s reads \t%s read err\t%s writes\t%s write err",
                    Tests[ii], Tests[ii], Tests[ii], Tests[ii]);
        }
        else
        {
            fprintf(output, "\t%s reads\t%s writes", Tests[ii], Tests[ii]);
        }
    }
    fprintf(output, "\n");

    for (ii=0; ii<Num_Setups; ii++)
    {
        // std dev = sqrt( (n*sum_sq - sum*sum)/n*(n-1) )
        if (Compute_Std_Dev || Check_Std_Dev)
        {
            fprintf(output, "%s", Setups[ii]);

            for (jj=0; jj<Num_Tests; jj++)
            {
                int count = 0;
                count_t tot_reads = 0; 
                count_t tot_reads_sq = 0; 
                count_t tot_writes = 0;
                count_t tot_writes_sq = 0;
                double read_std_dev, write_std_dev;

                while (Info[ii][jj] != NULL)
                {
                   count++;
                   tot_reads += Info[ii][jj]->reads;
                   tot_reads_sq += Info[ii][jj]->reads * Info[ii][jj]->reads;
                   tot_writes += Info[ii][jj]->writes;
                   tot_writes_sq += Info[ii][jj]->writes * Info[ii][jj]->writes;
                   Info[ii][jj] = Info[ii][jj]->next;
                }

                if (count!=0)
                {
                    read_std_dev = (double)(count*tot_reads_sq - tot_reads*tot_reads);
                    read_std_dev = sqrt(read_std_dev/(count*(count-1)));
                    tot_reads /= count;
                    write_std_dev = (double)(count*tot_writes_sq - tot_writes*tot_writes);
                    write_std_dev = sqrt(write_std_dev/(count*(count-1)));
                    tot_writes /= count;
                }
                else
                {
                    read_std_dev = 0;
                    write_std_dev = 0;
                    tot_reads = 0;
                    tot_writes = 0;
                }

                if (Check_Std_Dev)
                {
                    if ( (tot_reads != 0 && (read_std_dev/tot_reads) > Check_Std_Dev_Limit)
                            ||
                        (tot_writes != 0 && (write_std_dev/tot_writes) > Check_Std_Dev_Limit)
                       )
                    {
                        fprintf(stderr, "STD DEV LIMIT VIOLATION **********\n");
                    }
                }
                if (Compute_Std_Dev)
                {
                    fprintf(output, "\t%lld\t%f\t%lld\t%f", 
                        tot_reads, read_std_dev, tot_writes, write_std_dev);
                } else {
                    fprintf(output, "\t%lld\t%lld", 
                        tot_reads, tot_writes);
                }
            }
            fprintf(output, "\n");
        }
        else if (Compute_Average)
        {
            fprintf(output, "%s", Setups[ii]);

            for (jj=0; jj<Num_Tests; jj++)
            {
                int count = 0;
                count_t tot_reads = 0; 
                count_t tot_writes = 0;

                while (Info[ii][jj] != NULL)
                {
                    count++;
                    tot_reads += Info[ii][jj]->reads;
                    tot_writes += Info[ii][jj]->writes;
                    Info[ii][jj] = Info[ii][jj]->next;
                }

                if (count!=0)
                {
                    tot_reads /= count;
                    tot_writes /= count;
                }

                fprintf(output, "\t%lld\t%lld", tot_reads, tot_writes);
            }
            fprintf(output, "\n");
        }
        else
        {
            while (Info[ii][0] != NULL)
            {
                fprintf(output, "%s", Setups[ii]);

                for (jj=0; jj<Num_Tests; jj++)
                {
                    if (Info[ii][jj] != NULL)
                    {
                        fprintf(output, "\t%lld\t%lld", 
                               Info[ii][jj]->reads, Info[ii][jj]->writes);
                        Info[ii][jj] = Info[ii][jj]->next;
                    } else {
                        fprintf(output, "\tx\tx");
                    }
                }

                fprintf(output, "\n");
            }
        }
    }
}

void parse_args(int argc, char **argv)
{
    int ii;

    for (ii=1; ii<argc; ii++)
    {
        if (argv[ii][0] == '-')
        {
            if (strcmp(argv[ii], "-avg") == 0) {
                Compute_Average = 1;
            } else if (strcmp(argv[ii], "-stdev") == 0) {
                Compute_Std_Dev = 1;
            } else if (strcmp(argv[ii], "-check") == 0) {
                Check_Std_Dev = 1;
            } else {
                fprintf(stderr, "Unrecognized command %s\n", argv[ii]);
                exit(-1);
            }

        } else if (Input_Filename == NULL) {
            Input_Filename = argv[ii];
        } else if (Output_Filename == NULL) {
            Output_Filename = argv[ii];
        } else {
            fprintf(stderr, "Unrecognized command %s\n", argv[ii]);
            exit(-1);
        }
    }
}

int main(int argc, char **argv)
{
    char buff[1000];
    char *name, *c_mode, *c_size, *c_reads, *c_writes;
    char *c_readers, *c_writers, *c_updatep;
    int line_count = 0;
    FILE *infile, *outfile;

    parse_args(argc, argv);
    if (Input_Filename != NULL)
    {
        infile = fopen(Input_Filename, "r");
        if (infile == NULL)
        {
            fprintf(stderr, "unable to open %s\n", Input_Filename);
            exit(-1);
        }
    } else {
        infile = stdin;
    }

    if (Output_Filename != NULL)
    {
        outfile = fopen(Output_Filename, "w");
        if (outfile == NULL)
        {
            fprintf(stderr, "Unable to open %s\n", Output_Filename);
            exit(-1);
        }
    } else {
        outfile = stdout;
    }


    init_data();

    while (fgets(buff, sizeof(buff), infile))
    {
        line_count++;
        name = strtok(buff, " ");
        c_mode = strtok(NULL, " ");
        c_size = strtok(NULL, " ");
        c_readers = strtok(NULL, " ");
        c_writers = strtok(NULL, " ");
        c_updatep = strtok(NULL, " ");
        c_reads = strtok(NULL, " ");
        c_writes = strtok(NULL, " ");

        if (c_writes == NULL)
        {
            fprintf(stderr, "Invalid line at %d\n"
                    "%s %s %s %s %s %s %s %s\n", line_count,
                    name, c_mode, c_size, 
                    c_readers, c_writers, c_updatep, c_reads, c_writes);
            exit(-1);
        }

        insert(name, atoi(c_mode), atoi(c_size), 
                atoi(c_readers), atoi(c_writers), atoi(c_updatep),
                atoll(c_reads), atoll(c_writes));
    }

    output(outfile);

    fprintf(stderr, "processed input file with %d lines\n", line_count);

    return 0;
}

