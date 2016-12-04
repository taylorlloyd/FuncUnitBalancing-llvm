#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void func(int a, int *arr)
{
    float sum = 0;
    for (int i = 0; i < a; i++) {
        sum += ((float)arr[i] + (float)i);
    }
    arr[0] = sum;
}

int main(int argc, char *argv[])
{
    int *x = malloc(argc * sizeof(int));
    for (int i = 0; i < argc; i++)
      x[i] = i;
    func(argc, x);
    return 0;
}
