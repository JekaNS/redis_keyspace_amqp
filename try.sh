#!/bin/bash

redis-cli -p 7007 MODULE UNLOAD key_evt_amqp
redis-cli -p 7007 MODULE LOAD /home/build_release/libredis_keyspace_amqp.so
redis-cli -p 7007 keamqp.rand
redis-cli -p 7007 keamqp.rand

