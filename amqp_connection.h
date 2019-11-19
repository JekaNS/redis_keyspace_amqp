//
// Created by Jeka Kovtun on 16/11/2019.
//

#ifndef REDIS_KEYSPACE_AMQP_AMQP_CONNECTION_H
#define REDIS_KEYSPACE_AMQP_AMQP_CONNECTION_H

#define REDISMODULE_EXPERIMENTAL_API
#include "redismodule.h"
#include <stdbool.h>
#include <amqp.h>

#define REDIS_AMQP_OK 0
#define REDIS_AMQP_ERR 1
#define REDIS_AMQP_SOCKET_ERR 2
#define REDIS_AMQP_CONNECTION_ERR 3
#define REDIS_AMQP_AUTH_ERR 4
#define REDIS_AMQP_CHANNEL_ERR 5
#define REDIS_AMQP_ALREADY_CONNECTED 6

int amqpWaitFrame(amqp_connection_state_t* conn);
int amqpDisconnect(RedisModuleCtx *ctx, amqp_connection_state_t* conn);
int amqpConnect(RedisModuleCtx *ctx, amqp_connection_state_t* conn, char* hostname, uint16_t port, char* username, char* password, uint heartbeat);
int amqpPublish(RedisModuleCtx *ctx, amqp_connection_state_t* conn, amqp_bytes_t exchange, amqp_bytes_t routing_key, struct amqp_basic_properties_t_ const *properties,
                const char *body);

#endif //REDIS_KEYSPACE_AMQP_AMQP_CONNECTION_H
