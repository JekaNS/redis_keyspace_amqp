# Redis keyspace event to AMQP

## Introduction

This ia a redis module that can catch keyspace events and send it to RabbitMQ.  
Redis Cluster supported.

## Getting started

### Building and installing

#### Prereqs:
- [CMake v3.7 or better](http://www.cmake.org/)
- A C compiler (GCC, clang. Other compilers may also
  work)
- [pkg-config](http://pkg-config.freedesktop.org)
- [RabbitMQ C client](https://github.com/alanxz/rabbitmq-c) (librabbitmq-dev) 
- *Optionally* [OpenSSL](http://www.openssl.org/) v0.9.8+ to enable support for
  connecting to RabbitMQ over SSL/TLS

After downloading and extracting the source from a tarball to a directory
, the commands to build libredis_keyspace_amqp on most
systems are:

    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    cmake --build . --target redis_keyspace_amqp
    
### Usage

Load module into redis using MODULE LOAD command or at startup using "loadmodule" directive in redis.conf.
Also on load, you can pass parameters for redis_keyspace_amqp module configuration.

Example:
    
    MODULE LOAD <PATH_TO_BUILD_DIR>/libredis_keyspace_amqp.so xe 127.0.0.1 5672 user pass 20 exchange_name routing_key 2 ALL
Or in redis.conf:

    loadmodule <PATH_TO_BUILD_DIR>/libredis_keyspace_amqp.so xe 127.0.0.1 5672 user pass 20 exchange_name routing_key 2 ALL

Configuration params description:

- "xe" String flag that mean witch type of redis keyspace events will be listen. 
    See [Redis Keyspace Notifications](https://redis.io/topics/notifications#configuration) for for more information.   
    Possible values is: 
    - g     Generic commands (non-type specific) like DEL, EXPIRE, RENAME, ...
    - $     String commands
    - l     List commands
    - s     Set commands
    - h     Hash commands
    - z     Sorted set commands
    - x     Expired events (events generated every time a key expires)
    - e     Evicted events (events generated when a key is evicted for maxmemory)
    - A     Alias for g$lshzxe
    
    Default: "A" - all types of events
- hostname for RabbitMQ connection.  
    Default: 127.0.0.1
- port for RabbitMQ connection.  
    Default: 5672
- username for RabbitMQ connection.  
    Default: guest
- password for RabbitMQ connection.  
    Default: guest
- heartbeat in seconds for RabbitMQ connection.  
    Default: 30
- exchange_name for publish AMQP message.  
    Default: amq.direct
- routing_key for publish AMQP message.  
    Default: redis_keyspace_events
- delivery_mode for publish AMQP message. 1 (nonpersistent) or 2 (persistent).  
    Default: 1
- key_mask for filter keys. If you need to catch events for all keys, just pass there "ALL". Otherwise you can use simple mask like "^prefix.*$". There is no regular expressions, just simple checks.
    Thanks [Rob Pike for the code](https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html)
      
    You can use following constructs:
    - "c" matches any literal character c
    - "." matches any single character
    - "^" matches the beginning of the input string
    - "$" matches the end of the input string
    - "*" matches zero or more occurrences of the previous character
    
    There can be multiple masks at same time. Just use every mask as next parameter.  
    In case if one of masks equals "ALL", other masks will be ignored.
      
    Default: ALL
    
