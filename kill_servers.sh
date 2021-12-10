echo "The servers pids running: "
ps -ef | grep ivshmem-server | grep -v grep | awk '{print $2}'

ps -ef | grep ivshmem-server | grep -v grep | awk '{print "kill -9 "$2}' | sh

rm /tmp/ivshmem_socket-*

echo "Server pids killed: "
ps -ef | grep ivshmem-server | grep -v grep | awk '{print $2}'

rm -r /tmp/shmem