#!/bin/bash

redis-cli MODULE LOAD ./cmake-build-debug/libredis_keyspace_amqp.dylib
redis-cli keamqp.connect
redis-cli SETEX test_key_name 1 "test_value"

