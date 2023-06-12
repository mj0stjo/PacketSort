#ifndef CODE_UTIL_H
#define CODE_UTIL_H

int powx(int base, int exp){
    int res = 1;
    int i;
    for (i = 0; i < exp; i++){
        res *= base;
    }
    return res;
}

#endif //CODE_UTIL_H
