//
// Created by Jeka Kovtun on 16/11/2019.
//

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
