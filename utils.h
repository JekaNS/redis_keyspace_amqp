//
// Created by Jeka Kovtun on 16/11/2019.
//

#ifndef REDIS_KEYSPACE_AMQP_UTILS_H
#define REDIS_KEYSPACE_AMQP_UTILS_H

#include "redismodule.h"
#include <amqp.h>
#include <stdbool.h>
#include <string.h>

typedef struct string_array_t_ {
    uint size;
    uint capacity;
    char** data;
} string_array_t;

void copyStringAllocated(const char* in, char** out, bool needFree);
void copyAmqpStringAllocated(const char* in, amqp_bytes_t* out, bool needFree);
void stringArrayInit(string_array_t** arr, int size, int capacity);
void stringArrayAdd(string_array_t** arr, const char* value);
void stringArrayClean(string_array_t** arr);
uint stringArrayGetLength(string_array_t** arr);
const char* stringArrayGetElement(string_array_t** arr, uint index);
char* redisArgsToSerializedString(RedisModuleString **argv, int argc);
RedisModuleString** serializedStringToRedisArgs(RedisModuleCtx *ctx, const char* serializedData, size_t* argc);
int match(const char *regexp, const char *text);

#endif //REDIS_KEYSPACE_AMQP_UTILS_H
