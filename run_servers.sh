echo "The servers pids running: "
ps -ef | grep ivshmem-server | grep -v grep | awk '{print $2}'

ps -ef | grep ivshmem-server | grep -v grep | awk '{print "kill -9 "$2}' | sh

rm /tmp/ivshmem_socket-*

echo "Server pids killed: "
ps -ef | grep ivshmem-server | grep -v grep | awk '{print $2}'

rm -r /tmp/shmem


mkdir /tmp/shmem

nohup ivshmem-server \
        -l 4M -n 4 -F -v \
        -M fg-doorbell-01 \
        -m /tmp/shmem \
        -S /tmp/ivshmem_socket-01 \
        &

nohup ivshmem-server \
        -l 4M -n 4 -F -v \
        -M fg-doorbell-02 \
        -m /tmp/shmem \
        -S /tmp/ivshmem_socket-02 \
        &

nohup ivshmem-server \
        -l 4M -n 4 -F -v \
        -M fg-doorbell-10 \
        -m /tmp/shmem \
        -S /tmp/ivshmem_socket-10 \
        &

nohup ivshmem-server \
        -l 4M -n 4 -F -v \
        -M fg-doorbell-12 \
        -m /tmp/shmem \
        -S /tmp/ivshmem_socket-12 \
        &

nohup ivshmem-server \
        -l 4M -n 4 -F -v \
        -M fg-doorbell-20 \
        -m /tmp/shmem \
        -S /tmp/ivshmem_socket-20 \
        &

nohup ivshmem-server \
        -l 4M -n 4 -F -v \
        -M fg-doorbell-21 \
        -m /tmp/shmem \
        -S /tmp/ivshmem_socket-21 \
        &

echo "The servers pids running: "
ps -ef | grep ivshmem-server | grep -v grep | awk '{print $2}'