#!/bin/bash

redis-cli MODULE LOAD ./cmake-build-debug/libredis_keyspace_amqp.dylib 127.0.0.1 5672 guest guest 20 redis redis_keyspace_events 2 ALL
redis-cli SETEX test_key_name 1 "test_value"

