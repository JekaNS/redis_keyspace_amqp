//
// Created by Jeka Kovtun on 16/11/2019.
//

#define REDISMODULE_EXPERIMENTAL_API

#include "amqp_connection.h"
#include "redismodule.h"
#include <stdlib.h>
#include <amqp.h>
#include <amqp_tcp_socket.h>

struct timeval waitFrameTimeout = { .tv_sec = 0, .tv_usec = 0 };

int amqpWaitFrame(amqp_connection_state_t* conn) {
    int res;
    amqp_frame_t frame;
    do {
        res = amqp_simple_wait_frame_noblock(*conn, &frame, &waitFrameTimeout);
        if (AMQP_FRAME_METHOD == frame.frame_type && frame.payload.method.id == AMQP_CONNECTION_CLOSE_METHOD) {
            amqp_send_method(*conn, frame.channel, AMQP_CONNECTION_CLOSE_OK_METHOD, NULL);
            return AMQP_STATUS_CONNECTION_CLOSED;
        }
        if (AMQP_FRAME_METHOD == frame.frame_type && frame.payload.method.id == AMQP_CHANNEL_CLOSE_METHOD) {
            amqp_send_method(*conn, frame.channel, AMQP_CHANNEL_CLOSE_OK_METHOD, NULL);
            return AMQP_STATUS_CONNECTION_CLOSED;
        }
    } while(res != AMQP_STATUS_CONNECTION_CLOSED && res != AMQP_STATUS_TIMEOUT && frame.frame_type != 0 && frame.frame_type != AMQP_FRAME_HEARTBEAT);

    return res != AMQP_STATUS_TIMEOUT ? res : AMQP_STATUS_OK;
}

int amqpDisconnect(RedisModuleCtx *ctx, amqp_connection_state_t* conn) {
    if(*conn == NULL) {
        return REDIS_AMQP_ERR;
    }

    amqp_channel_close(*conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(*conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(*conn);

    *conn = NULL;

    RedisModule_Log(ctx, "notice", "AMQP DISCONNECTED");
    return REDIS_AMQP_OK;
}

int amqpConnect(RedisModuleCtx *ctx, amqp_connection_state_t* conn, char* hostname, uint16_t port, char* username, char* password, uint heartbeat) {

    if(*conn != NULL) {
        return REDIS_AMQP_ALREADY_CONNECTED;
    }

    *conn = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(*conn);
    if (!socket) {
        return REDIS_AMQP_SOCKET_ERR;
    }

    int amqp_status = amqp_socket_open(socket, hostname, port);
    if (amqp_status) {
        amqpDisconnect(ctx, conn);
        return REDIS_AMQP_CONNECTION_ERR;
    }

    amqp_rpc_reply_t amqp_reply;

    amqp_reply = amqp_login(*conn, "/", 0, 131072, heartbeat,AMQP_SASL_METHOD_PLAIN, username, password);
    if(amqp_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        amqpDisconnect(ctx, conn);
        return REDIS_AMQP_AUTH_ERR;
    }

    amqp_channel_open(*conn, 1);

    amqp_reply = amqp_get_rpc_reply(*conn);
    if(amqp_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        amqpDisconnect(ctx, conn);
        return REDIS_AMQP_CHANNEL_ERR;
    }

    RedisModule_Log(ctx, "notice", "AMQP CONNECTED");
    return REDIS_AMQP_OK;
}

int amqpPublish(RedisModuleCtx *ctx, amqp_connection_state_t* conn, amqp_bytes_t exchange, amqp_bytes_t routing_key, struct amqp_basic_properties_t_ const *properties,
                const char* body) {
    if(*conn != NULL) {
        if(amqpWaitFrame(conn) != AMQP_STATUS_OK) {
            amqpDisconnect(ctx, conn);
            return AMQP_STATUS_CONNECTION_CLOSED;
        }
        amqp_bytes_t amqpBody = amqp_cstring_bytes(body);
        return amqp_basic_publish(*conn, 1, exchange, routing_key, 0, 0, properties, amqpBody);
    }
    return AMQP_STATUS_CONNECTION_CLOSED;
}