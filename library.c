
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

char* hostname;
int port = 5672;
char* username;
char* password;
int heartbeat = 0;
char* exchange;
char* routing_key;
uint8_t delivery_mode = 2;

amqp_frame_t frame;
struct timeval tval;
struct timeval* wait_frame_timeout;


int amqpDisconnect(RedisModuleCtx *ctx) {

    if(conn == NULL) {
        return REDIS_AMQP_ERR;
    }

    void **data = NULL;
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

void checkFramesAndHeartbeat(RedisModuleCtx *ctx, void *data) {
    heardBeatTimerId = 0;

    if(conn == NULL) {
        return;
    }

    int result;
    result = amqp_simple_wait_frame_noblock(conn, &frame, wait_frame_timeout);
    if(result < 0 && result != AMQP_STATUS_TIMEOUT) {
        //TODO manage reconnect
        amqpDisconnect(ctx);
        return;
    }
    heardBeatTimerId = RedisModule_CreateTimer(ctx, 200, checkFramesAndHeartbeat, ctx);
}

void initDefaultConfig() {
    const char* h = "127.0.0.1";
    const char* u = "guest";
    const char* p = "guest";
    const char* e = "amq.direct";
    const char* r = "redis_keyspace_events";

    hostname = RedisModule_Alloc(strlen(h) + 1);
    strcpy(hostname,h);

    username = RedisModule_Alloc(strlen(u) + 1);
    strcpy(username,u);

    password = RedisModule_Alloc(strlen(p) + 1);
    strcpy(password,p);

    exchange = RedisModule_Alloc(strlen(e) + 1);
    strcpy(exchange,e);

    routing_key = RedisModule_Alloc(strlen(r) + 1);
    strcpy(routing_key,r);
}

void initConfig(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, int argOffset) {
    if(argc - argOffset > 0) {
        size_t len;
        char **res = NULL;
        char const *arg;
        for (int i = 0; i < argc - argOffset; ++i) {
            switch (i) {
                case 0:
                    RedisModule_Free(hostname);
                    arg = RedisModule_StringPtrLen(argv[argOffset + i], &len);
                    hostname = RedisModule_Alloc(len + 1);
                    strcpy(hostname,arg);
                    break;
                case 1:
                    port = (int)strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), res, 10);
                    break;
                case 2:
                    RedisModule_Free(username);
                    arg = RedisModule_StringPtrLen(argv[argOffset + i], &len);
                    username = RedisModule_Alloc(len + 1);
                    strcpy(username,arg);
                    break;
                case 3:
                    RedisModule_Free(password);
                    arg = RedisModule_StringPtrLen(argv[argOffset + i], &len);
                    password = RedisModule_Alloc(len + 1);
                    strcpy(password,arg);
                    break;
                case 4:
                    heartbeat = (int)strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), res, 10);
                    break;
                case 5:
                    RedisModule_Free(exchange);
                    arg = RedisModule_StringPtrLen(argv[argOffset + i], &len);
                    exchange = RedisModule_Alloc(len + 1);
                    strcpy(exchange,arg);
                    break;
                case 6:
                    RedisModule_Free(routing_key);
                    arg = RedisModule_StringPtrLen(argv[argOffset + i], &len);
                    routing_key = RedisModule_Alloc(len + 1);
                    strcpy(routing_key,arg);
                    break;
                case 7:
                    delivery_mode = (int)strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), res, 10);
                    break;
                default:
                    break;
            }
        }

        publish_props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG; // NOLINT(hicpp-signed-bitwise)
        publish_props.content_type = amqp_cstring_bytes("text/plain");
        publish_props.delivery_mode = delivery_mode; /* persistent delivery mode */

        wait_frame_timeout = &tval;
        wait_frame_timeout->tv_sec = 0;
        wait_frame_timeout->tv_usec = 5;

        RedisModule_Log(ctx, "notice", "MODULE CONFIG CHANGED");
    }
}

int amqpConnect(RedisModuleCtx *ctx) {

    if(conn != NULL) {
        amqpDisconnect(ctx);
    }

    if(conn == NULL) {
        conn = amqp_new_connection();
    }

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

    amqp_reply = amqp_login(conn, "/", 0, 131072, heartbeat,AMQP_SASL_METHOD_PLAIN, username, password);
    if(amqp_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        amqpDisconnect(ctx);
        return REDIS_AMQP_AUTH_ERR;
    }

    amqp_channel_open(conn, 1);

    amqp_reply = amqp_get_rpc_reply(conn);
    if(amqp_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        amqpDisconnect(ctx);
        return REDIS_AMQP_CHANNEL_ERR;
    }

    heardBeatTimerId = RedisModule_CreateTimer(ctx, 500, checkFramesAndHeartbeat, ctx);

    RedisModule_Log(ctx, "notice", "AMQP CONNECTED");
    return REDIS_AMQP_OK;
}

int AmqpConnect_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    initConfig(ctx, argv, argc, 1);
    int amqrp_response = amqpConnect(ctx);
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
    if(argc > 1) {
        RedisModule_WrongArity(ctx);
    }

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
    if(res & REDISMODULE_CTX_FLAGS_MASTER) { // NOLINT(hicpp-signed-bitwise)
        return 1;
    }
    return 0;
}

int onKeyspaceEvent (RedisModuleCtx *ctx, int type, const char *event, RedisModuleString *key) {
    //TODO save events when no connection or publish error
    if(isRedisMaster(ctx) == 1 && conn != NULL){
        size_t len;
        const char *keyname = RedisModule_StringPtrLen(key, &len);
        RedisModule_Log(ctx, "debug", "Keyspace Event : %i / %s / %s", type, event, keyname);

        amqp_bytes_t exchangeName = amqp_cstring_bytes(exchange);
        amqp_bytes_t routingKey = amqp_cstring_bytes(routing_key);
        amqp_bytes_t message = amqp_cstring_bytes(keyname);
        int res = amqp_basic_publish(conn, 1, exchangeName, routingKey, 0, 0, &publish_props, message);
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
    initDefaultConfig();
    initConfig(ctx, argv, argc, 0);

    RedisModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_EXPIRED, onKeyspaceEvent); // NOLINT(hicpp-signed-bitwise)

    return REDISMODULE_OK;
}
