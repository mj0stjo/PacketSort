#ifndef CODE_UTIL_H
#define CODE_UTIL_H

int pow(int base, int exp){
    int res = 1;
    for (int i = 0; i < exp; i++){
        res *= base;
    }
    return res;
}

#endif //CODE_UTIL_H
