#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(){
    char name[256];
    int n;
    n=gethostname(name, sizeof(name));
    printf("Running on: %s\n", name);

    return 0;
}
