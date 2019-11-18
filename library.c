
#define REDISMODULE_EXPERIMENTAL_API

#include "redismodule.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <amqp.h>
#include "utils.h"
#include "amqp_connection.h"

amqp_connection_state_t conn;
amqp_basic_properties_t publishProps;
RedisModuleTimerID heardBeatTimerId;

char* hostname = ""; //hostname for AMQP connection
uint16_t port = 5672; //port for AMQP connection
char* username = ""; //username for AMQP connection
char* password = ""; //password for AMQP connection
uint heartbeat = 30; //seconds
amqp_bytes_t exchange; //exchange name for AMQP publish
amqp_bytes_t routingKey; //routing key for AMQP publish

mstime_t checkFrameInterval = 1000;

bool operationalMode = false;
RedisModuleString *storageRedisKeyName;

void publishStoredEvents(RedisModuleCtx *ctx) {
    //Send all messages from storage
    int res;
    size_t len;
    const char* messageTemp;
    RedisModuleKey *storageList = RedisModule_OpenKey(
            ctx,
            storageRedisKeyName,
            REDISMODULE_READ | REDISMODULE_WRITE // NOLINT(hicpp-signed-bitwise)
    );
    RedisModuleString *messageRedis = RedisModule_ListPop(storageList, REDISMODULE_LIST_HEAD);

    while(messageRedis != NULL) {
        messageTemp = RedisModule_StringPtrLen(messageRedis, &len);
        res = amqpPublish(&conn, exchange, routingKey, &publishProps, messageTemp);
        //Put message back into storage
        if (res < AMQP_STATUS_OK) {
            RedisModule_ListPush(storageList, REDISMODULE_LIST_HEAD, messageRedis);
            break;
        }
        RedisModule_FreeString(ctx, messageRedis);
        messageRedis = RedisModule_ListPop(storageList, REDISMODULE_LIST_HEAD);
    }
    RedisModule_CloseKey(storageList);
}

void pushEventToStorage(RedisModuleCtx *ctx, const char* messageString) {
    RedisModuleKey *storageList = RedisModule_OpenKey(
            ctx,
            storageRedisKeyName,
            REDISMODULE_READ | REDISMODULE_WRITE // NOLINT(hicpp-signed-bitwise)
    );
    RedisModule_ListPush(storageList, REDISMODULE_LIST_TAIL, RedisModule_CreateString(ctx, messageString, strlen(messageString)));
    RedisModule_CloseKey(storageList);
}

/**
 * Publish event to AMQP or store them into Redis LIST storage
 * @param ctx
 * @param event
 * @param keyname
 */
void publishEvent(RedisModuleCtx *ctx, const char* event, const char* keyname) {
    int res;

    char messageString[strlen(event) + 1 + strlen(keyname)];
    strcpy(messageString, event);
    strcat(messageString, "\n");
    strcat(messageString, keyname);

    if(conn != NULL) {
        res = amqpPublish(&conn, exchange, routingKey, &publishProps, messageString);
        if (res < AMQP_STATUS_OK) {
            RedisModule_Log(ctx, "warning", "AMQP Publishing error: %s", amqp_error_string2(res));
            pushEventToStorage(ctx, messageString);
        } else {
            publishStoredEvents(ctx);
        }
    } else {
        RedisModule_Log(ctx, "warning", "AMQP Publishing error: no connection");
        pushEventToStorage(ctx, messageString);
    }

}

void checkFramesAndHeartbeat(RedisModuleCtx *ctx, void* data) {
    heardBeatTimerId = 0;

    if(!operationalMode) {
        return;
    }

    int res;

    if(conn != NULL) {
        res = amqpWaitFrame(&conn);
        if(res < 0 && res != AMQP_STATUS_TIMEOUT) {
            amqpDisconnect(ctx, &conn);
            res = amqpConnect(ctx, &conn, hostname, port, username, password, heartbeat);
            if(res == REDIS_AMQP_OK) {
                publishStoredEvents(ctx);
            }
        }
    } else {
        res = amqpConnect(ctx, &conn, hostname, port, username, password, heartbeat);
        if(res == REDIS_AMQP_OK) {
            publishStoredEvents(ctx);
        }
    }

    heardBeatTimerId = RedisModule_CreateTimer(ctx, checkFrameInterval, checkFramesAndHeartbeat, data);
}


bool isRedisMaster(RedisModuleCtx *ctx) {
    unsigned int res = RedisModule_GetContextFlags(ctx);
    if(res & REDISMODULE_CTX_FLAGS_MASTER) { // NOLINT(hicpp-signed-bitwise)
        return 1;
    }
    return 0;
}

int onKeyspaceEvent (RedisModuleCtx *ctx, int type, const char *event, RedisModuleString *key) {
    if(isRedisMaster(ctx) && operationalMode) {
        if(heardBeatTimerId == 0) {
            amqpConnect(ctx, &conn, hostname, port, username, password, heartbeat);
            char* data = "";
            heardBeatTimerId = RedisModule_CreateTimer(ctx, checkFrameInterval, checkFramesAndHeartbeat, data);
        }

        size_t len;
        const char *keyname = RedisModule_StringPtrLen(key, &len);
        RedisModule_Log(ctx, "debug", "Keyspace Event : %i / %s / %s", type, event, keyname);

        publishEvent(ctx, event, keyname);
    }
    return REDISMODULE_OK;
}

void setupDefaultConfig(RedisModuleCtx *ctx) {
    copyStringAllocated("127.0.0.1", &hostname, false);
    copyStringAllocated("guest", &username, false);
    copyStringAllocated("guest", &password, false);

    copyAmqpStringAllocated("amq.direct", &exchange, false);
    copyAmqpStringAllocated("redis_keyspace_events", &routingKey, false);

    storageRedisKeyName = RedisModule_CreateString(ctx, "key_evt_amqp", 12);
}

int setupConfig(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, int argOffset) {
    if(argc - argOffset > 0) {
        size_t len;
        char **rest = NULL;
        uint8_t delivery_mode;
        for (int i = 0; i < argc - argOffset; ++i) {
            switch (i) {
                case 0:
                    copyStringAllocated(RedisModule_StringPtrLen(argv[argOffset + i], &len), &hostname, true);
                    break;
                case 1:
                    port = (uint16_t) strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), rest, 10);
                    break;
                case 2:
                    copyStringAllocated(RedisModule_StringPtrLen(argv[argOffset + i], &len), &username, true);
                    break;
                case 3:
                    copyStringAllocated(RedisModule_StringPtrLen(argv[argOffset + i], &len), &password, true);
                    break;
                case 4:
                    heartbeat = (uint) strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), rest, 10);
                    break;
                case 5:
                    copyAmqpStringAllocated(RedisModule_StringPtrLen(argv[argOffset + i], &len), &exchange, true);
                    break;
                case 6:
                    copyAmqpStringAllocated(RedisModule_StringPtrLen(argv[argOffset + i], &len), &routingKey, true);
                    break;
                case 7:
                    delivery_mode = (uint8_t) strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), rest, 10);
                    if(delivery_mode > 2) {
                        RedisModule_Log(ctx, "error", "Wrong value for delivery_mode");
                        return REDISMODULE_ERR;
                    }
                    publishProps.delivery_mode = delivery_mode;
                    break;
                default:
                    break;
            }
        }

        RedisModule_Log(ctx, "notice", "MODULE CONFIG CHANGED");
    }
    return REDISMODULE_OK;
}

int init(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    publishProps._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG; // NOLINT(hicpp-signed-bitwise)
    publishProps.content_type = amqp_cstring_bytes("text/plain");
    publishProps.delivery_mode = 1; //AMQP delivery mode NON persistent

    setupDefaultConfig(ctx);
    return setupConfig(ctx, argv, argc, 0);
}

int AmqpConnect_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(setupConfig(ctx, argv, argc, 1) != REDISMODULE_OK) {
        RedisModule_WrongArity(ctx);
        return REDISMODULE_ERR;
    }
    int amqrp_response = amqpConnect(ctx, &conn, hostname, port, username, password, heartbeat);
    if(amqrp_response != REDIS_AMQP_OK) {
        char buffer[32];
        sprintf(buffer, "AMQP CONNECT ERROR: %d", amqrp_response);
        const char *err = buffer;
        RedisModule_ReplyWithError(ctx, err);
        return REDISMODULE_ERR;
    }

    char* data = "";
    heardBeatTimerId = RedisModule_CreateTimer(ctx, checkFrameInterval, checkFramesAndHeartbeat, data);

    operationalMode = true;

    RedisModule_ReplyWithSimpleString(ctx,"AMQP CONNECT OK");
    return REDISMODULE_OK;
}

int AmqpDisconnect_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if(argc > 1) {
        RedisModule_WrongArity(ctx);
    }

    operationalMode = false;

    if( heardBeatTimerId != 0) {
        void** data = NULL;
        RedisModule_StopTimer(ctx, heardBeatTimerId, data);
        heardBeatTimerId = 0;
    }

    int amqrp_response = amqpDisconnect(ctx, &conn);
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

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (RedisModule_Init(ctx,"key_evt_amqp",1,REDISMODULE_APIVER_1) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, "key_evt_amqp.connect", AmqpConnect_RedisCommand, "readonly", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, "key_evt_amqp.disconnect", AmqpDisconnect_RedisCommand, "readonly", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if(init(ctx, argv, argc) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    operationalMode = true;

    RedisModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_EXPIRED, onKeyspaceEvent); // NOLINT(hicpp-signed-bitwise)

    return REDISMODULE_OK;
}
