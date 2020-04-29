# Introduction

This repository implements the proof of concept of P4-capable Open vSwitch with P4Runtime interface.

In order to fill the gap in the P4 ecosystem (lack of performant P4 software switch) the ambition of this project is to
build a high-performance P4 software switch. I believe that P4 needs the same, what Open vSwitch was for OpenFlow.

The idea behind this PoC is to get feedback from the community (developers, users, architects, etc.) and
gather volunteers willing to help with further development of this project. All kinds of contributions are more than welcome!
The [Design](./Documentation/topics/p4/design.rst) page presents the target solution and most of the features described there are not implemented.  
Feel free to throw your ideas in!

**Note!** The current version of OvS.p4 is a proof of concept and it almost certainly contains serious bugs.

# Demo

The current version of the project allows to:

* create single P4 bridge with initial P4 program
* add ports to the bridge, 
* receive traffic from this ports and handle it in the P4 datapath.


# Project status

OvS.p4 is very young, work-in-progress project. In fact, the current version is just a proof of concept, far from being ready to 
be used operationally. 

# Contributing

The OvS.p4 is not ready to welcome code contributions at this stage. However, I'm waiting for your contributions 
to the design process!

* Check out the [Design proposal](./Documentation/topics/p4/design.rst). This project follows a documentation-driven approach. Your feedback is more than welcome!
* Join the Slack channel to discuss the current design, propose a new feature or discuss the future of the project.

You can also reach me out on [Twitter](https://twitter.com/tomek_osinski) or via [email](mailto:osinstom@gmail.com).

# Roadmap

This project is just a proof of concept. I've ambitious goal to develop this PoC into a high-performance P4 software switch.
There is a long list of exciting features to be implemented on the [Roadmap]() page.



