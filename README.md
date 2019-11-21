# Redis module for forward keyspace events to RabbitMQ

## Description

Module name: key_evt_amqp

This is a redis module that can catch keyspace events, [like this](https://redis.io/topics/notifications), and send it to RabbitMQ.  
Redis Cluster supported.

After loading, this module subscribes to internal keyspace events, connection to RabbitMQ is lazy. When a keyspace event occurs, the module serializes it and sends an AMQP message to the broker.  
  
Format of AMQP message body is simple multiline string  with line endings "\n" and 3 values: Redis DB id, event name, key name.  
Example:

        0
        expire
        redis_key_name

If for some reason the AMQP message cannot be delivered, the module saves the events in to native RedisList structure as a fallback behavior. When the connection returns, all events from the fallback storage will be sent to the broker.  

There is a support heartbeat for RabbitMQ connection, and module always trying to reconnect if its lost.

Module configuration allow you select event types that will be listen. Also you can use a filters for events by key mask like ^prefix.*$. Or you can catch events for ALL keys.    

In Cluster mode, module must be loaded to all nodes. But only master nodes will be send AMQP messages to broker. When node change self state from master to slave or back, module automaticaly decide  wich node must forward keyspace events in to RabbitMQ. In this way  excluded duplicates of events.   

Also in cluster mode all module commands will be broadcast to all nodes. So you don't need to execute every command on each node.  

## Getting started

### Building

## Prereqs:
- [CMake v3.7 or better](http://www.cmake.org/)
- A C compiler (GCC, clang. Other compilers may also
  work)
- [pkg-config](http://pkg-config.freedesktop.org)
- [RabbitMQ C client](https://github.com/alanxz/rabbitmq-c) (librabbitmq-dev) 


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
    
    MODULE LOAD <PATH_TO_BUILD_DIR>/libredis_keyspace_amqp.so xe 127.0.0.1 5672 user pass 20 exchange_name routing_key 2 10000000 ALL

Or in redis.conf:

    loadmodule <PATH_TO_BUILD_DIR>/libredis_keyspace_amqp.so xe 127.0.0.1 5672 user pass 20 exchange_name routing_key 2 10000000 ALL

Configuration params description:

- "xe" String flags that mean witch type of redis keyspace events will be listen. 
    See [Redis Keyspace Notifications](https://redis.io/topics/notifications#configuration) for for more information.  
    Warning! Setting up event type is possible only at module load. Be cause internal event subscription can be called only once. So if you need to change event types, you must unload whole module using "MODULE UNLOAD key_evt_amqp" then load it again with new params.  
    
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
    
    Default: "xe" - all types of events
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
- fallback_storage_size Size of RedisList structure for store events that happens when no connection to RabbitMQ.  
  positive integer (>0) - max number of stored events in RedisList  
  0 - unlimited fallback storage  
  negative integer (<0) - turnoff fallback behavior  
  Default: 10 000 000
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
      
    Default: [empty] that means that no events will be forwarded, until you manualy added any keymasks or "ALL"


## Module commands


### key_evt_amqp.config_set


Command will setup new module configuration. Accept same parameters as MODULE LOAD except module path and keyspace event types (g$lshzxe).  
Requires at least one parameter.
Notice: if you provide params for keymask filters, that will be clear previously configured filters.

        key_evt_amqp.config_set 127.0.0.1 5672 user pass 20 exchange_name routing_key 2 10000000 ALL


### key_evt_amqp.connect


Command will setup new module configuration and then connect to RabbitMQ. Accept same parameters as MODULE LOAD except module path and keyspace event types (g$lshzxe).  
If no parameters will provided, command will use configuration previously stated.  
Notice: if you provide params for keymask filters, that will be clear previously configured filters.

        key_evt_amqp.connect 127.0.0.1 5672 user pass 20 exchange_name routing_key 2 10000000 ALL


### key_evt_amqp.disconnect


Will disconnect from RabbitMQ. Connection will not be restored automaticaly.  
Has no parameters.

        key_evt_amqp.disconnect


### key_evt_amqp.fallback_storage_size_set


At run time you can setup fallback storage size. Size means events qty. Or you can use unlimited storage (0). And of course you can switch off fallback storage at all (-1).

        key_evt_amqp.fallback_storage_size_set 10000000
        key_evt_amqp.fallback_storage_size_set 0
        key_evt_amqp.fallback_storage_size_set -1


### key_evt_amqp.keymask_clean


This command will clean keymask filters. See default value for key_mask param of MODULE LOAD command above.  
After this, module will not be forward any events.

        key_evt_amqp.keymask_clean


### key_evt_amqp.keymask_add


Adds keymasks to filters existing in configuration. See description for key_mask param of MODULE LOAD command above.  
You can pass any numbers of keymasks as separate parameters.

        key_evt_amqp.keymask_add ^prefix.*$ .*substring.* other_key.*postfix$
        key_evt_amqp.keymask_add ALL


### key_evt_amqp.keymask_set


Same thing as doing sequentially "key_evt_amqp.keymask_clean" and then "key_evt_amqp.keymask_add".   
You can pass any numbers of keymasks as separate parameters.

        key_evt_amqp.keymask_set ^prefix.*$ .*substring.* other_key.*postfix$
        key_evt_amqp.keymask_set ALL



## Built with


- [RabbitMQ C AMQP client library](https://github.com/alanxz/rabbitmq-c)
- [Redis modules API](https://redis.io/topics/modules-api-ref)
- Thanks Rob Pike for code [https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html](https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html)