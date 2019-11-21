
#define REDISMODULE_EXPERIMENTAL_API

#include "library.h"
#include "redismodule.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <amqp.h>
#include "utils.h"
#include "amqp_connection.h"

amqp_connection_state_t conn;
amqp_basic_properties_t publishProps;
RedisModuleTimerID heardBeatTimerId = 0;

char* hostname = ""; //hostname for AMQP connection
uint16_t port = DEFAULT_AMQP_PORT; //port for AMQP connection
char* username = ""; //username for AMQP connection
char* password = ""; //password for AMQP connection
uint heartbeat = DEFAULT_AMQP_HEARTBEAT; //seconds
amqp_bytes_t exchange; //exchange name for AMQP publish
amqp_bytes_t routingKey; //routing key for AMQP publish

mstime_t checkFrameInterval = 1000;

bool amqpConnectionEnabled = false;
RedisModuleString *fallbackStorageRedisKeyName;
size_t fallbackStorageMaxSize = DEFAULT_FALLBACK_STORAGE_SIZE;
size_t fallbackStorageLen = 0;

string_array_t* keyMaskArr;

int publishStoredEvents(RedisModuleCtx *ctx) {
    //Send all messages from storage

    if(fallbackStorageMaxSize < 0) {
        return REDISMODULE_OK;
    }

    int res;
    size_t len;
    const char* messageTemp;
    RedisModuleKey *storageList = RedisModule_OpenKey(
            ctx,
            fallbackStorageRedisKeyName,
            REDISMODULE_READ | REDISMODULE_WRITE // NOLINT(hicpp-signed-bitwise)
    );
    RedisModuleString *messageRedis = RedisModule_ListPop(storageList, REDISMODULE_LIST_HEAD);

    while(messageRedis != NULL) {
        messageTemp = RedisModule_StringPtrLen(messageRedis, &len);
        res = amqpPublish(ctx, &conn, exchange, routingKey, &publishProps, messageTemp);
        //Put message back into storage
        if (res != AMQP_STATUS_OK) {
            RedisModule_ListPush(storageList, REDISMODULE_LIST_HEAD, messageRedis);
            RedisModule_FreeString(ctx, messageRedis);
            return REDISMODULE_ERR;
        }
        fallbackStorageLen--;
        RedisModule_FreeString(ctx, messageRedis);
        messageRedis = RedisModule_ListPop(storageList, REDISMODULE_LIST_HEAD);
    }

    RedisModule_DeleteKey(storageList);
    RedisModule_CloseKey(storageList);

    return REDISMODULE_OK;
}

void syncFallbackStorageLen(RedisModuleCtx *ctx) {
    RedisModuleKey *storageList = RedisModule_OpenKey(ctx, fallbackStorageRedisKeyName, REDISMODULE_READ); // NOLINT(hicpp-signed-bitwise)
    fallbackStorageLen = RedisModule_ValueLength(storageList);
    RedisModule_CloseKey(storageList);
}

void pushEventToStorage(RedisModuleCtx *ctx, const char* messageString) {
    if(fallbackStorageMaxSize < 0) {
        return;
    }

    RedisModuleKey *storageList = RedisModule_OpenKey(
            ctx,
            fallbackStorageRedisKeyName,
            REDISMODULE_READ | REDISMODULE_WRITE // NOLINT(hicpp-signed-bitwise)
    );

    if(fallbackStorageMaxSize > 0 && fallbackStorageLen >= fallbackStorageMaxSize) {
        RedisModule_ListPop(storageList, REDISMODULE_LIST_HEAD);
    }

    RedisModule_ListPush(storageList, REDISMODULE_LIST_TAIL, RedisModule_CreateString(ctx, messageString, strlen(messageString)));
    RedisModule_CloseKey(storageList);
}

void setupFallbackStorage(RedisModuleCtx *ctx) {
    if(fallbackStorageMaxSize >= 0) {
        syncFallbackStorageLen(ctx);
    } else {
        RedisModuleKey *storageList = RedisModule_OpenKey(ctx, fallbackStorageRedisKeyName, REDISMODULE_READ); // NOLINT(hicpp-signed-bitwise)
        RedisModule_DeleteKey(storageList);
        RedisModule_CloseKey(storageList);
    }
}

/**
 * Publish event to AMQP or store them into (LOCAL NODE!) Redis LIST storage
 * @param ctx
 * @param event
 * @param keyname
 */
void publishEvent(RedisModuleCtx *ctx, const char* event, const char* keyname) {
    int res;

    char messageString[21 + 1 + strlen(event) + 1 + strlen(keyname)];
    sprintf(messageString, "%d\n%s\n%s", RedisModule_GetSelectedDb(ctx), event, keyname);

    if(conn != NULL) {
        res = amqpPublish(ctx, &conn, exchange, routingKey, &publishProps, messageString);
        if (res != AMQP_STATUS_OK) {
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

    if(!amqpConnectionEnabled) {
        return;
    }

    int res;

    if(conn != NULL) {
        if(amqpWaitFrame(&conn) != AMQP_STATUS_OK) {
            amqpDisconnect(ctx, &conn);
            res = amqpConnect(ctx, &conn, hostname, port, username, password, heartbeat);
            if(res == REDIS_AMQP_OK) {
                publishStoredEvents(ctx);
            } else {
                amqpDisconnect(ctx, &conn);
            }
        }
    } else {
        res = amqpConnect(ctx, &conn, hostname, port, username, password, heartbeat);
        if(res == REDIS_AMQP_OK) {
            publishStoredEvents(ctx);
        } else {
            amqpDisconnect(ctx, &conn);
        }
    }

    heardBeatTimerId = RedisModule_CreateTimer(ctx, checkFrameInterval, checkFramesAndHeartbeat, data);
}

bool isRedisCluster(RedisModuleCtx *ctx) {
    int res = RedisModule_GetContextFlags(ctx);
    if(res & REDISMODULE_CTX_FLAGS_CLUSTER) { // NOLINT(hicpp-signed-bitwise)
        return 1;
    }
    return 0;
}

bool isRedisMaster(RedisModuleCtx *ctx) {
    int res = RedisModule_GetContextFlags(ctx);
    if(res & REDISMODULE_CTX_FLAGS_MASTER) { // NOLINT(hicpp-signed-bitwise)
        return 1;
    }
    return 0;
}

int onKeyspaceEvent (RedisModuleCtx *ctx, int type, const char *event, RedisModuleString *key) {
    if(isRedisMaster(ctx) && amqpConnectionEnabled) {
        //If there is a first event, then init AMQP connection and setup timer for connect/reconnect/heartbeat
        if(heardBeatTimerId == 0) {
            amqpConnect(ctx, &conn, hostname, port, username, password, heartbeat);
            char* data = "";
            heardBeatTimerId = RedisModule_CreateTimer(ctx, checkFrameInterval, checkFramesAndHeartbeat, data);
        }

        size_t len;
        const char *keyname = RedisModule_StringPtrLen(key, &len);

        uint klen = stringArrayGetLength(&keyMaskArr);
        for(uint i=0; i < klen; ++i) {
            const char *mask = stringArrayGetElement(&keyMaskArr, i);
            if(strcmp(mask, KEY_MASK_ALL) == 0) {
                RedisModule_Log(ctx, "debug", "Keyspace Event (ALL): %i / %s / %s", type, event, keyname);
                publishEvent(ctx, event, keyname);
                return REDISMODULE_OK;
            } else {
                if(match(mask, keyname) == 1){
                    RedisModule_Log(ctx, "debug", "Keyspace Event (%s): %i / %s / %s", mask, type, event, keyname);
                    publishEvent(ctx, event, keyname);
                    return REDISMODULE_OK;
                }
            }
        }
    }

    return REDISMODULE_OK;
}

void setupDefaultConfig(RedisModuleCtx *ctx) {
    copyStringAllocated(DEFAULT_AMQP_HOST, &hostname, false);
    copyStringAllocated(DEFAULT_AMQP_USER, &username, false);
    copyStringAllocated(DEFAULT_AMQP_PASS, &password, false);

    copyAmqpStringAllocated(DEFAULT_AMQP_EXCHANGE, &exchange, false);
    copyAmqpStringAllocated(DEFAULT_AMQP_ROUTINGKEY, &routingKey, false);

    //Default empty
    //stringArrayAdd(&keyMaskArr, KEY_MASK_ALL);

    fallbackStorageRedisKeyName = RedisModule_CreateString(ctx, DEFAULT_FALLBACK_STORAGE_KEY, strlen(DEFAULT_FALLBACK_STORAGE_KEY));
}

int setupConfig(RedisModuleCtx *ctx, RedisModuleString **argv, int argc, int argOffset) {
    if(argc - argOffset > 0) {
        size_t len;
        char **rest = NULL;
        uint8_t deliveryMode;
        const char* keyMaskArg;
        bool stop = false;

        if(argc - argOffset > 9) {
            stringArrayClean(&keyMaskArr);
        }

        for (int i = 0; i < argc - argOffset && !stop; ++i) {
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
                    deliveryMode = (uint8_t) strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), rest, 10);
                    if(deliveryMode > 2) {
                        RedisModule_Log(ctx, "error", "Wrong value for deliveryMode");
                        return REDISMODULE_ERR;
                    }
                    publishProps.delivery_mode = deliveryMode;
                    break;
                case 8:
                    fallbackStorageMaxSize = (size_t) strtol(RedisModule_StringPtrLen(argv[argOffset + i], &len), rest, 10);
                    break;
                default:
                    keyMaskArg = RedisModule_StringPtrLen(argv[argOffset + i], &len);
                    if(strcmp(keyMaskArg, KEY_MASK_ALL) == 0) {
                        stringArrayClean(&keyMaskArr);
                        stringArrayAdd(&keyMaskArr, KEY_MASK_ALL);
                        stop = true;
                        break;
                    }

                    stringArrayAdd(&keyMaskArr, keyMaskArg);
                    break;
            }
        }

        RedisModule_Log(ctx, "notice", "MODULE CONFIG CHANGED");
    }

    setupFallbackStorage(ctx);

    return REDISMODULE_OK;
}

int init(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    publishProps._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG; // NOLINT(hicpp-signed-bitwise)
    publishProps.content_type = amqp_cstring_bytes("text/plain");
    publishProps.delivery_mode = DEFAULT_AMQP_DELIVERY_MODE; //AMQP delivery mode NON persistent

    stringArrayInit(&keyMaskArr, 0, 8);

    setupDefaultConfig(ctx);

    /**
     * On MODULE LOAD command, argv starts from first argument after module path, command and module path are not included.
     * argOffset = 1 be cause on module load first argument must be an mask for keyspace events to subscribe.
     *      There is no API to change subscription at runtime, so events mask is must be constant after init.
     */
    return setupConfig(ctx, argv, argc, 1);
}

void broadcastRedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(isRedisCluster(ctx)) {
        const char* serializedData =  redisArgsToSerializedString(argv, argc);
        RedisModule_SendClusterMessage(ctx, NULL, CLUSTER_MESSAGE_TYPE_CMD, (unsigned char*) serializedData, (uint32_t) strlen(serializedData));
        RedisModule_Free((char*)serializedData);
    }
}

int connectAmqpCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    /**
     * On simple command argv starts from first literal? including command name.
     * So i need to skip first argv.
     */
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

    amqpConnectionEnabled = true;

    RedisModule_ReplyWithSimpleString(ctx,"AMQP CONNECT OK");
    return REDISMODULE_OK;
}

int disconnectAmqpCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if(argc > 1) {
        RedisModule_WrongArity(ctx);
    }

    amqpConnectionEnabled = false;

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

int setupFallbackStorageSizeCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(argc != 2) {
        return REDISMODULE_ERR;
    }

    size_t len;
    char **rest = NULL;
    fallbackStorageMaxSize = (size_t) strtol(RedisModule_StringPtrLen(argv[1], &len), rest, 10);
    setupFallbackStorage(ctx);

    return REDISMODULE_OK;
}

int keymaskCleanCommand(RedisModuleCtx *ctx) {
    REDISMODULE_NOT_USED(ctx);
    stringArrayClean(&keyMaskArr);
    return REDISMODULE_OK;
}

int keymaskAddCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(ctx);
    size_t len;
    const char* keyMaskArg;
    for (int i = 0; i < argc; ++i) {
        keyMaskArg = RedisModule_StringPtrLen(argv[i], &len);
        if (strcmp(keyMaskArg, KEY_MASK_ALL) == 0) {
            stringArrayClean(&keyMaskArr);
            stringArrayAdd(&keyMaskArr, KEY_MASK_ALL);
            break;
        }
        stringArrayAdd(&keyMaskArr, keyMaskArg);
    }
    return REDISMODULE_OK;
}

int amqpConnect_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    broadcastRedisCommand(ctx, argv, argc);
    return connectAmqpCommand(ctx, argv, argc);
}

int amqpDisconnect_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    broadcastRedisCommand(ctx, argv, argc);
    return disconnectAmqpCommand(ctx, argv, argc);
}

int setupConfig_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(argc < 2) {
        RedisModule_WrongArity(ctx);
    }
    broadcastRedisCommand(ctx, argv, argc);
    return setupConfig(ctx, argv, argc, 1);
}

int setupFallbackStorageSize_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(argc != 2) {
        RedisModule_WrongArity(ctx);
    }
    broadcastRedisCommand(ctx, argv, argc);
    return setupFallbackStorageSizeCommand(ctx, argv, argc);
}

int keymaskClean_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(argc > 1) {
        RedisModule_WrongArity(ctx);
    }
    broadcastRedisCommand(ctx, argv, argc);
    return keymaskCleanCommand(ctx);
}

int keymaskAdd_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(argc < 2) {
        RedisModule_WrongArity(ctx);
    }
    broadcastRedisCommand(ctx, argv, argc);
    return keymaskAddCommand(ctx, argv, argc);
}

int keymaskSet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if(argc < 2) {
        RedisModule_WrongArity(ctx);
    }
    broadcastRedisCommand(ctx, argv, argc);
    keymaskCleanCommand(ctx);
    keymaskAddCommand(ctx, argv, argc);

    return REDISMODULE_OK;
}

void clusterMessageReceiver(RedisModuleCtx *ctx, const char* sender_id, uint8_t type, const unsigned char* payload, uint32_t len) {
    RedisModule_Log(ctx,"notice","Cluster message received (type %d) from %.*s: '%.*s'",
                    type,REDISMODULE_NODE_ID_LEN,sender_id,(int)len, payload);

    if(type != CLUSTER_MESSAGE_TYPE_CMD) {
        return;
    }

    size_t argc;
    RedisModuleString** argv = serializedStringToRedisArgs(ctx, (const char*) payload, &argc);

    if(argc > 0) {
        size_t tmp;
        const char* command = RedisModule_StringPtrLen(argv[0], &tmp);

        if(strcmp(command, REDIS_COMMAND_CONNECT) == 0) {
            connectAmqpCommand(ctx, argv, (int) argc);
        }
        else if(strcmp(command, REDIS_COMMAND_DISCONNECT) == 0) {
            disconnectAmqpCommand(ctx, argv, (int) argc);
        }
        else if(strcmp(command, REDIS_COMMAND_CONFIG) == 0) {
            setupConfig(ctx, argv, (int) argc, 1);
        }
        else if(strcmp(command, REDIS_COMMAND_FALLBACK_STORAGE_SIZE) == 0) {
            setupFallbackStorageSizeCommand(ctx, argv, (int) argc);
        }
        else if(strcmp(command, REDIS_COMMAND_KEYMASK_CLEAN) == 0) {
            keymaskCleanCommand(ctx);
        }
        else if(strcmp(command, REDIS_COMMAND_KEYMASK_ADD) == 0) {
            keymaskAddCommand(ctx, argv, (int) argc);
        }
        else if(strcmp(command, REDIS_COMMAND_KEYMASK_ADD) == 0) {
            keymaskCleanCommand(ctx);
            keymaskAddCommand(ctx, argv, (int) argc);
        }

        for(size_t i=0; i < argc; ++i) {
            RedisModule_FreeString(ctx, argv[i]);
        }
        RedisModule_Free(argv);
    }
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (RedisModule_Init(ctx,REDIS_MODULE_NAME,1, REDISMODULE_APIVER_1) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, REDIS_COMMAND_CONNECT, amqpConnect_RedisCommand, "write", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, REDIS_COMMAND_DISCONNECT, amqpDisconnect_RedisCommand, "readonly", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, REDIS_COMMAND_CONFIG, setupConfig_RedisCommand, "write", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, REDIS_COMMAND_FALLBACK_STORAGE_SIZE, setupFallbackStorageSize_RedisCommand, "write", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, REDIS_COMMAND_KEYMASK_CLEAN, keymaskClean_RedisCommand, "readonly", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, REDIS_COMMAND_KEYMASK_ADD, keymaskAdd_RedisCommand, "readonly", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if (RedisModule_CreateCommand(ctx, REDIS_COMMAND_KEYMASK_SET, keymaskSet_RedisCommand, "readonly", 0, 0, 0) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    if(init(ctx, argv, argc) != REDISMODULE_OK) {
        return REDISMODULE_ERR;
    }

    uint keySpaceSubscriptionMode = 0;

    if(argc > 0) {
        size_t len;
        const char * eventsMask = RedisModule_StringPtrLen(argv[0], &len);
        const char* err = "Wrong events mask mode. Use 'g$lshzxeA' like 'notify-keyspace-events' for CONFIG SET";
        for(u_long i=0; i < strlen(eventsMask); ++i) {
            switch(eventsMask[i]) {
                case 'g':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_GENERIC; // NOLINT(hicpp-signed-bitwise)
                    break;
                case '$':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_STRING; // NOLINT(hicpp-signed-bitwise)
                    break;
                case 'l':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_LIST; // NOLINT(hicpp-signed-bitwise)
                    break;
                case 's':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_SET; // NOLINT(hicpp-signed-bitwise)
                    break;
                case 'h':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_HASH; // NOLINT(hicpp-signed-bitwise)
                    break;
                case 'z':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_ZSET; // NOLINT(hicpp-signed-bitwise)
                    break;
                case 'x':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_EXPIRED; // NOLINT(hicpp-signed-bitwise)
                    break;
                case 'e':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_EVICTED; // NOLINT(hicpp-signed-bitwise)
                    break;
                case 'A':
                    keySpaceSubscriptionMode |= REDISMODULE_NOTIFY_ALL; // NOLINT(hicpp-signed-bitwise)
                    break;
                default:
                    RedisModule_Log(ctx, "error", err);
                    return REDISMODULE_ERR;
            }
        }
    } else {
        keySpaceSubscriptionMode = REDISMODULE_NOTIFY_EXPIRED | REDISMODULE_NOTIFY_EVICTED; // NOLINT(hicpp-signed-bitwise)
    }

    //No need to connect if nothing to do
    if(stringArrayGetLength(&keyMaskArr) > 0) {
        amqpConnectionEnabled = true;
    }

    RedisModule_SubscribeToKeyspaceEvents(ctx, keySpaceSubscriptionMode, onKeyspaceEvent); // NOLINT(hicpp-signed-bitwise)

    if(isRedisCluster(ctx)) {
        RedisModule_RegisterClusterMessageReceiver(ctx, CLUSTER_MESSAGE_TYPE_CMD, clusterMessageReceiver);
    }

    return REDISMODULE_OK;
}

