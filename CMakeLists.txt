cmake_minimum_required(VERSION 3.7)
project(redis_keyspace_amqp C)

find_package(PkgConfig REQUIRED)
find_package(openssl REQUIRED)

pkg_search_module(RABBITMQ REQUIRED librabbitmq)

include_directories(${RABBITMQ_INCLUDE_DIRS})

add_library(redis_keyspace_amqp SHARED library.c library.h)

find_library(AMQP_LIB rabbitmq)
target_link_libraries(redis_keyspace_amqp ${AMQP_LIB})
