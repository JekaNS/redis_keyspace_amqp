//
// Created by Jeka Kovtun on 16/11/2019.
//

#include "utils.h"
#include "redismodule.h"
#include <amqp.h>
#include <stdbool.h>
#include <string.h>

void copyStringAllocated(const char* in, char** out, bool needFree) {
    if(needFree) {
        RedisModule_Free(*out);
    }
    *out = RedisModule_Alloc(strlen(in) + 1);
    strcpy(*out, in);
}

void copyAmqpStringAllocated(const char* in, amqp_bytes_t* out, bool needFree) {
    if(needFree) {
        RedisModule_Free((*out).bytes);
    }
    (*out).len = strlen(in);
    (*out).bytes = RedisModule_Alloc(strlen(in) + 1);
    strcpy((*out).bytes, in);
}

void stringArrayInit(string_array_t** arr, int size, int capacity) {
    (*arr) = RedisModule_Alloc(sizeof(string_array_t));
    (*arr)->size = size;
    (*arr)->capacity = capacity;
    (*arr)->data = RedisModule_Alloc(capacity * sizeof(char*));
}

void stringArrayAdd(string_array_t** arr, const char* value) {
    if((*arr)->size >= (*arr)->capacity) {
        (*arr)->data = RedisModule_Realloc((*arr)->data, (*arr)->capacity * 2);
        (*arr)->capacity *= 2;
    }
    (*arr)->data[(*arr)->size] = RedisModule_Alloc(strlen(value) + 1);
    strcpy((*arr)->data[(*arr)->size], value);
    (*arr)->size++;
}

void stringArrayClean(string_array_t** arr) {
    int len = (*arr)->size;
    for(int i=0; i < len; ++i) {
        RedisModule_Free((*arr)->data[i]);
    }
    (*arr)->size = 0;
}

uint stringArrayGetLength(string_array_t** arr) {
    return (*arr)->size;
}

const char* stringArrayGetElement(string_array_t** arr, uint index) {
    return (*arr)->data[index];
}

/**
 * Simplified regexp
 * Thanks for Rob Pike https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html
 *
 * Usage:
 *   c    matches any literal character c
 *   .    matches any single character
 *   ^    matches the beginning of the input string
 *   $    matches the end of the input string
 *   *    matches zero or more occurrences of the previous character
 */

/* matchhere: search for regexp at beginning of text */
int matchhere(const char *regexp, const char *text)
{
    if (regexp[0] == '\0')
        return 1;
    if (regexp[1] == '*') {
        int c = regexp[0];
        char *regexp2 = (char*)regexp+2;
        do {    /* a * matches zero or more instances */
            if (matchhere(regexp2, text))
                return 1;
        } while (*text != '\0' && (*text++ == c || c == '.'));
        return 0;
    }

    if (regexp[0] == '$' && regexp[1] == '\0')
        return *text == '\0';
    if (*text!='\0' && (regexp[0]=='.' || regexp[0]==*text))
        return matchhere(regexp+1, text+1);
    return 0;
}

/* match: search for regexp anywhere in text */
int match(const char *regexp, const char *text)
{
    if (regexp[0] == '^')
        return matchhere(regexp+1, text);
    do {    /* must look even if string is empty */
        if (matchhere(regexp, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}




