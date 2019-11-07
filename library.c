#include "redismodule.h"
#include <stdlib.h>

#include <amqp.h>
#include <amqp_tcp_socket.h>

#define AMQP_CONNECT_OK 0
#define AMQP_CONNECT_ERR 1

static const char AMQP_DEFAULT_HOST[] = "localhost";
static const int AMQP_DEFAULT_PORT = 5672;
static const char AMQP_DEFAULT_USERNAME[] = "guest";
static const char AMQP_DEFAULT_PASSWORD[] = "guest";

amqp_socket_t *socket = NULL;
amqp_connection_state_t conn;

int InitAmqpConnection(RedisModuleString **argv, int argc) {

    size_t *len = 0;
    char const *hostname;
    int port;
    char const *username;
    char const *password;

    switch (argc) {
        case 4:
            hostname = RedisModule_StringPtrLen(argv[1], len);
            port = atoi(RedisModule_StringPtrLen(argv[2], len));
            username = RedisModule_StringPtrLen(argv[3], len);
            password = AMQP_DEFAULT_PASSWORD;
            break;
        case 3:
            hostname = RedisModule_StringPtrLen(argv[1], len);
            port = atoi(RedisModule_StringPtrLen(argv[2], len));
            username = AMQP_DEFAULT_USERNAME;
            password = AMQP_DEFAULT_PASSWORD;
            break;
        case 2:
            hostname = RedisModule_StringPtrLen(argv[1], len);
            port = 5672;
            username = AMQP_DEFAULT_USERNAME;
            password = AMQP_DEFAULT_PASSWORD;
            break;
        case 1:
        default:
            hostname = AMQP_DEFAULT_HOST;
            port = AMQP_DEFAULT_PORT;
            username = AMQP_DEFAULT_USERNAME;
            password = AMQP_DEFAULT_PASSWORD;
            break;
    }

    conn = amqp_new_connection();
    socket = amqp_tcp_socket_new(conn);
    if (!socket) {
        return AMQP_CONNECT_ERR;
    }

    if (amqp_socket_open(socket, hostname, port)) {
        return AMQP_CONNECT_ERR;
    }

    amqp_rpc_reply_t login_status = amqp_login(conn, "/", 0, 131072, 0,AMQP_SASL_METHOD_PLAIN, username, password);
    if(login_status.reply_type != AMQP_RESPONSE_NORMAL) {
        return AMQP_CONNECT_ERR;
    }

    return AMQP_CONNECT_OK;
}

int HelloworldRand_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_ReplyWithLongLong(ctx,rand());
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (RedisModule_Init(ctx,"key_evt_amqp",1,REDISMODULE_APIVER_1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    if(InitAmqpConnection(argv, argc) != AMQP_CONNECT_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, "keamqp.rand", HelloworldRand_RedisCommand, "readonly", 0, 0, 0) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    return REDISMODULE_OK;
}