cmake_minimum_required(VERSION 3.7)
project(redis_keyspace_amqp C)

set(CMAKE_C_STANDARD 99)

add_library(redis_keyspace_amqp SHARED library.c library.h)

find_library(AMQP_LIB rabbitmq)
target_link_libraries(redis_keyspace_amqp "${AMQP_LIB}")
