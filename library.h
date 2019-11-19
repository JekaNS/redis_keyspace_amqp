#ifndef REDIS_KEYSPACE_AMQP_LIBRARY_H
#define REDIS_KEYSPACE_AMQP_LIBRARY_H

#include "redismodule.h"

#define DEFAULT_FALLBACK_STORAGE_KEY "REDIS_MODULE-key_evt_amqp-fallback_storage"
#define DEFAULT_AMQP_HOST "127.0.0.1"
#define DEFAULT_AMQP_PORT 5627
#define DEFAULT_AMQP_USER "guest"
#define DEFAULT_AMQP_PASS "guest"
#define DEFAULT_AMQP_HEARTBEAT 30
#define DEFAULT_AMQP_EXCHANGE "amq.direct"
#define DEFAULT_AMQP_ROUTINGKEY "redis_keyspace_events"

#define KEY_MASK_ALL "ALL"

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc);

#endif //REDIS_KEYSPACE_AMQP_LIBRARY_H