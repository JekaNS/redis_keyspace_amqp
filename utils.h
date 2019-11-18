//
// Created by Jeka Kovtun on 16/11/2019.
//

#ifndef REDIS_KEYSPACE_AMQP_UTILS_H
#define REDIS_KEYSPACE_AMQP_UTILS_H

#include "redismodule.h"
#include <amqp.h>
#include <stdbool.h>
#include <string.h>

void copyStringAllocated(const char* in, char** out, bool needFree);
void copyAmqpStringAllocated(const char* in, amqp_bytes_t* out, bool needFree);

#endif //REDIS_KEYSPACE_AMQP_UTILS_H
