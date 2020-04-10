PROTOS = `ls -1 $(top_builddir)/p4rt/proto/*.proto`

lib_LTLIBRARIES += p4rt/libp4rt.la

p4rt_libp4rt_la_LDFLAGS = \
        $(OVS_LTINFO) \
        -Wl,--version-script=$(top_builddir)/p4rt/libp4rt.sym \
        $(AM_LDFLAGS)

p4rt_libp4rt_la_SOURCES = \
    p4rt/p4rt.c \
    p4rt/p4rt.h \
	p4rt/proto/p4/v1/p4runtime.grpc-c.c \
	p4rt/proto/p4/v1/p4runtime.grpc-c.h \
	p4rt/proto/p4/v1/p4runtime.grpc-c.service.c

p4runtime.grpc-c.c p4runtime.grpc-c.h p4runtime.grpc-c.service.c:
	protoc -I $(top_builddir)/p4rt --grpc-c_out=../../../ \
    		--plugin=protoc-gen-grpc-c=/usr/local/bin/protoc-gen-grpc-c \
    	    $(top_builddir)/p4rt/proto/p4/v1/p4runtime.proto

p4rt_libp4rt_la_CPPFLAGS = $(AM_CPPFLAGS)
p4rt_libp4rt_la_CFLAGS = $(AM_CFLAGS)

gencode:
	protoc -I $(top_builddir)/p4rt/ --grpc-c_out=$(top_builddir)/p4rt/ \
    		--plugin=protoc-gen-grpc-c=/usr/local/bin/protoc-gen-grpc-c \
    		$(top_builddir)/p4rt/proto/p4/v1/p4runtime.proto

pkgconfig_DATA += \
	p4rt/libp4rt.pc

LDADD = \
    -lgrpc \
    -lgpr \
    -lprotobuf-c

