#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//************************************************************
void *mygetline(char *buff, int size, FILE *file)
{
    void *result;

    result = fgets(buff, size, file);
    if (result)
    {
        if (buff[strlen(buff)-1] != '\n')
        {
            printf("buffer overflow\n");
            exit(-4);
        }
        buff[strlen(buff)-1] = 0;
    }
    return result;
}
//************************************************************
void get2items(char *buff, int curr_col, char **item1, char **item2)
{
    char *item;
    item = strtok(buff, ",");
    *item1 = item;
    while (--curr_col)
    {
        item = strtok(NULL, ",");
        *item2 = item;
    }
}
//************************************************************
int main(int argc, char **argv)
{
    int num_cols;
    int curr_col;
    char *filename;
    FILE *infile, *outfile;
    char buff[2048];
    char *item1, *item2;
    double scale = 1.0;
    double value;

    if (argc < 3)
    {
        printf("csvparse <input> <output>\n");
        exit(-1);
    }

    infile = fopen(argv[1], "r");
    outfile = fopen(argv[2], "w");

    if (argc > 3)
    {
        scale = atof(argv[3]);
        if (scale == 0.0)
        {
            printf("Invalid scale\n");
            exit(-3);
        }
    }

    if (infile == NULL || outfile == NULL)
    {
        printf("Unable to open files %s %s\n", argv[1], argv[2]);
        exit(-2);
    }

    mygetline(buff, sizeof(buff), infile);
    num_cols = atoi(buff);
    curr_col = 1;

    while (curr_col < num_cols)
    {
        curr_col++;
        mygetline(buff, sizeof(buff), infile);
        get2items(buff, curr_col, &item1, &item2);
        fprintf(outfile, "\\addplot coordinates{ %% %s\n", item2);

        while (mygetline(buff, sizeof(buff), infile))
        {
            get2items(buff, curr_col, &item1, &item2);
            if (scale != 1)
            {
                value = atof(item2);
                value /= scale;
                fprintf(outfile, "(%s,%f)\n", item1, value);
            }
            else
            {
                fprintf(outfile, "(%s,%s)\n", item1, item2);
            }
        }
        fprintf(outfile, "};\n\n");

        rewind(infile);
        mygetline(buff, sizeof(buff), infile);
    }

    fclose(infile);
    fclose(outfile);

    return 0;
}
