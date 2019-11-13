
#define REDISMODULE_EXPERIMENTAL_API
#include "redismodule.h"
#include <stdlib.h>
#include <string.h>
#include <amqp.h>
#include <amqp_tcp_socket.h>

#define REDIS_AMQP_OK 0
#define REDIS_AMQP_ERR 1
#define REDIS_AMQP_SOCKET_ERR 2
#define REDIS_AMQP_CONNECTION_ERR 3
#define REDIS_AMQP_AUTH_ERR 4
#define REDIS_AMQP_CHANNEL_ERR 5

amqp_connection_state_t conn = NULL;
amqp_socket_t *socket = NULL;
amqp_basic_properties_t publish_props;
int heardBeatTimerId = 0;

char const *hostname = "127.0.0.1";
int port = 5672;
char const *username = "guest";
char const *password = "guest";

amqp_frame_t frame;
struct timeval tval;
struct timeval *tv;


int amqpDisconnect(RedisModuleCtx *ctx) {

    if(conn == NULL) {
        return REDIS_AMQP_ERR;
    }

    void **data;
    if(heardBeatTimerId != 0) {
        RedisModule_StopTimer(ctx, heardBeatTimerId, data);
        heardBeatTimerId = 0;
    }

    amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);

    conn = NULL;
    socket = NULL;

    RedisModule_Log(ctx, "notice", "AMQP DISCONNECTED");
    return REDIS_AMQP_OK;
}

void checkHeartbeat(RedisModuleCtx *ctx, void *data) {
    if(conn == NULL) {
        return;
    }

    int result;
    result = amqp_simple_wait_frame_noblock(conn, &frame, tv);
    if(result < 0 && result != AMQP_STATUS_TIMEOUT) {
        amqpDisconnect(ctx);
        return;
    }
    heardBeatTimerId = RedisModule_CreateTimer(ctx, 200, checkHeartbeat, ctx);

    return;
}

int amqpConnect(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {

    size_t len;


    for (int i = 1; i < argc; ++i) {
        switch(i){
            case 1:
                hostname = RedisModule_StringPtrLen(argv[1], &len);
                break;
            case 2:
                port = atoi(RedisModule_StringPtrLen(argv[2], &len));
                break;
            case 3:
                username = RedisModule_StringPtrLen(argv[3], &len);
                break;
            case 4:
                password = RedisModule_StringPtrLen(argv[4], &len);
                break;
            default:
                break;
        }
    }

    if(conn != NULL) {
        amqpDisconnect(ctx);
    }

    if(conn == NULL) {
        conn = amqp_new_connection();
    }

    publish_props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    publish_props.content_type = amqp_cstring_bytes("text/plain");
    publish_props.delivery_mode = 2; /* persistent delivery mode */

    socket = amqp_tcp_socket_new(conn);
    if (!socket) {
        return REDIS_AMQP_SOCKET_ERR;
    }

    int amqp_status = amqp_socket_open(socket, hostname, port);
    if (amqp_status) {
        amqpDisconnect(ctx);
        return REDIS_AMQP_CONNECTION_ERR;
    }

    amqp_rpc_reply_t amqp_reply;

    amqp_reply = amqp_login(conn, "/", 0, 131072, 5,AMQP_SASL_METHOD_PLAIN, username, password);
    if(amqp_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        amqpDisconnect(ctx);
        return REDIS_AMQP_AUTH_ERR;
    }

    tv = &tval;
    tv->tv_sec = 0;
    tv->tv_usec = 100;
    heardBeatTimerId = RedisModule_CreateTimer(ctx, 200, checkHeartbeat, ctx);

    amqp_channel_open(conn, 1);

    amqp_reply = amqp_get_rpc_reply(conn);
    if(amqp_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        amqpDisconnect(ctx);
        return REDIS_AMQP_CHANNEL_ERR;
    }

    RedisModule_Log(ctx, "notice", "AMQP CONNECTED");
    return REDIS_AMQP_OK;
}

int AmqpConnect_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {

    int amqrp_response = amqpConnect(ctx, argv, argc);
    if(amqrp_response != REDIS_AMQP_OK) {
        char buffer[32];
        sprintf(buffer, "AMQP CONNECT ERROR: %d", amqrp_response);
        const char *err = buffer;
        RedisModule_ReplyWithError(ctx, err);
        return REDISMODULE_ERR;
    }
    RedisModule_ReplyWithSimpleString(ctx,"AMQP CONNECT OK");
    return REDISMODULE_OK;
}

int AmqpDisconnect_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    int amqrp_response = amqpDisconnect(ctx);
    if(amqrp_response != REDIS_AMQP_OK) {
        char buffer[32];
        sprintf(buffer, "AMQP DISCONNECT ERROR: %d", amqrp_response);
        const char *err = buffer;
        RedisModule_ReplyWithError(ctx,err);
        return REDISMODULE_ERR;
    }
    RedisModule_ReplyWithSimpleString(ctx,"AMQP DISCONNECT OK");
    return REDISMODULE_OK;
}

int isRedisMaster(RedisModuleCtx *ctx) {
    unsigned int res = RedisModule_GetContextFlags(ctx);
    if(res & REDISMODULE_CTX_FLAGS_MASTER) {
        return 1;
    }
    return 0;
}

int onKeyspaceEvent (RedisModuleCtx *ctx, int type, const char *event, RedisModuleString *key) {
    if(isRedisMaster(ctx) == 1 && conn != NULL){
        size_t len;
        const char *keyname = RedisModule_StringPtrLen(key, &len);
        RedisModule_Log(ctx, "debug", "Keyspace Event : %i / %s / %s", type, event, keyname);

        amqp_bytes_t exchange = amqp_cstring_bytes("amq.direct");
        amqp_bytes_t routingKey = amqp_cstring_bytes("test_queue");
        amqp_bytes_t message = amqp_cstring_bytes(keyname);
        int res = amqp_basic_publish(conn, 1, exchange, routingKey, 0, 0, &publish_props, message);
        if(res < 0) {
            RedisModule_Log(ctx, "warning", "AMQP Publishing error %s",  amqp_error_string2(res));
        }
    }
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (RedisModule_Init(ctx,"key_evt_amqp",1,REDISMODULE_APIVER_1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, "key_evt_amqp.connect", AmqpConnect_RedisCommand, "readonly", 0, 0, 0) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, "key_evt_amqp.disconnect", AmqpDisconnect_RedisCommand, "readonly", 0, 0, 0) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    int res = RedisModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_EXPIRED, onKeyspaceEvent);

    return REDISMODULE_OK;
}