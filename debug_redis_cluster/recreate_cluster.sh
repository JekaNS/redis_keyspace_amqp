killall redis-server
sleep 2
killall redis-server
sleep 2
rm 700?/*.aof
rm 700?/*.rdb
rm 700?/nodes*
./start_cluster.sh
./redis-trib.rb create --replicas 1 127.0.0.1:7001 127.0.0.1:7002 127.0.0.1:7003 127.0.0.1:7004 127.0.0.1:7005 127.0.0.1:7006

