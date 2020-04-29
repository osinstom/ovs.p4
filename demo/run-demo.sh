
ovs-appctl -t ovsdb-server exit
ovs-appctl -t ovs-vswitchd exit

ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock --remote=db:Open_vSwitch,Open_vSwitch,manager_options --pidfile --detach
ovs-vswitchd unix:/usr/local/var/run/openvswitch/db.sock --pidfile --detach --verbose --log-file=ovs.log

p4c-ubpf demo.p4 -o demo.c

clang-6.0 -O2 -target bpf -I../p4c/backends/ubpf/runtime -c demo.c -o demo.o

ovs-vsctl del-br br0

ovs-vsctl add-br br0 -- set bridge br0 datapath_type=ubpf p4=true other_config:program="$(pwd)/demo.o"

./test.sh

ovs-vsctl add-port br0 ovsp4-p0
ovs-vsctl add-port br0 ovsp4-p1

ovs-vsctl show

ip netns exec ns0 ping -i .2 10.1.1.2