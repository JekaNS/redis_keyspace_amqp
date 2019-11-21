#ifndef REDIS_KEYSPACE_AMQP_LIBRARY_H
#define REDIS_KEYSPACE_AMQP_LIBRARY_H

#include "redismodule.h"

#define REDIS_MODULE_NAME "key_evt_amqp"
#define REDIS_COMMAND_CONNECT "key_evt_amqp.connect"
#define REDIS_COMMAND_DISCONNECT "key_evt_amqp.disconnect"
#define REDIS_COMMAND_CONFIG "key_evt_amqp.config_set"
#define REDIS_COMMAND_FALLBACK_STORAGE_SIZE "key_evt_amqp.fallback_storage_size_set"
#define REDIS_COMMAND_KEYMASK_CLEAN "key_evt_amqp.keymask_clean"
#define REDIS_COMMAND_KEYMASK_ADD "key_evt_amqp.keymask_add"
#define REDIS_COMMAND_KEYMASK_SET "key_evt_amqp.keymask_set"

#define DEFAULT_FALLBACK_STORAGE_KEY "REDIS_MODULE-key_evt_amqp-fallback_storage"
#define DEFAULT_FALLBACK_STORAGE_SIZE 10000000

#define DEFAULT_AMQP_HOST "127.0.0.1"
#define DEFAULT_AMQP_PORT 5627
#define DEFAULT_AMQP_USER "guest"
#define DEFAULT_AMQP_PASS "guest"
#define DEFAULT_AMQP_HEARTBEAT 30
#define DEFAULT_AMQP_EXCHANGE "amq.direct"
#define DEFAULT_AMQP_ROUTINGKEY "redis_keyspace_events"
#define DEFAULT_AMQP_DELIVERY_MODE 1


#define KEY_MASK_ALL "ALL"

#define CLUSTER_MESSAGE_TYPE_CMD 191

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc);

#endif //REDIS_KEYSPACE_AMQP_LIBRARY_H