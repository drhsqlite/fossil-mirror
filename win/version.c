#include <stdio.h>

int main(int argc, char *argv[]){
    FILE *m,*u;
    char b[10240];
    u = fopen(argv[1],"r");
    fgets(b, sizeof(b)-1,u);
    b[strlen(b)-1] =0;
    printf("#define MANIFEST_UUID \"%s\"\n",b);
    printf("#define MANIFEST_VERSION \"[%10.10s]\"\n",b);
    m = fopen(argv[2],"r");
    while(b ==  fgets(b, sizeof(b)-1,m)){
        if(0 == strncmp("D ",b,2)){
            printf("#define MANIFEST_DATE \"%.10s %.8s\"\n",b+2,b+13);
            printf("#define MANIFEST_YEAR \"%.4s\"\n",b+2);
            return 0;
        }
    }
    return 1;
}
