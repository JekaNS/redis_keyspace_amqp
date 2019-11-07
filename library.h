#ifndef REDIS_KEYSPACE_AMQP_LIBRARY_H
#define REDIS_KEYSPACE_AMQP_LIBRARY_H
#include "redismodule.h"

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc);

#endif //REDIS_KEYSPACE_AMQP_LIBRARY_H