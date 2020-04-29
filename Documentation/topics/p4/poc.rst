

----------
Demo steps
----------

Make sure you have all required software (OvS.p4, p4c-ubpf, clang-6.0) installed. If not, please refer to :ref:`ovs_p4_install`.

All the files needed to run this demo are located under /demo directory. Run all below commands from this directory.

* The demo.p4 P4 program is used to drive this demo. It does very simple, dumb forwarding between ports 1 and 2.

* Compile the program with ``p4c-ubpf``::

    p4c-ubpf demo.p4 -o demo.c

* Compile from C to BPF::

    clang-6.0 -O2 -target bpf -I../p4c/backends/ubpf/runtime -c demo.c -o demo.o

* Add OVS bridge of type "p4"::

    sudo ovs-vsctl add-br br0 -- set bridge br0 datapath_type=ubpf p4=true

* Setup network namespaces and create ``veth`` interfaces. The ``test.sh`` script automates this step::

    sudo ./test.sh

* Attach ports to P4 bridge::

    sudo ovs-vsctl add-port br0 ovsp4-p0
    sudo ovs-vsctl add-port br0 ovsp4-p1

* Verify ports are added and no error occurred::

    sudo ovs-vsctl show

* Run ``ping`` between namespaces, traffic should be forwarded via P4 bridge::

    sudo ip netns exec ns0 ping -i .2 10.1.1.2

