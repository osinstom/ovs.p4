#ifndef P4RT_PROVIDER_H
#define P4RT_PROVIDER_H 1

#include "openvswitch/hmap.h"

struct p4rt {
    struct hmap_node hmap_node; /* In global 'all_p4rts' hmap. */
    const struct p4rt_class *p4rt_class;

    char *type;                 /* Datapath type. */
    char *name;                 /* Datapath name. */

    /* Datapath. */
    struct hmap ports;          /* Contains "struct p4port"s. */
};

struct p4port {
    struct hmap_node hmap_node; /* In struct p4rt's "ports" hmap. */
    struct p4rt *p4rt;          /* The p4rt that contains this port. */
    struct netdev *netdev;
    ofp_port_t port_no;         /* P4Runtime port number. */
    long long int created;      /* Time created, in msec. */
};

struct p4rt_class {

    void (*init)();

    /* Returns the type to pass to netdev_open() when a datapath of type
     * 'datapath_type' has a port of type 'port_type', for a few special
     * cases when a netdev type differs from a port type.  For example,
     * when using the userspace datapath, a port of type "internal"
     * needs to be opened as "tap".
     *
     * Returns either 'type' itself or a string literal, which must not
     * be freed. */
    const char *(*port_open_type)(const char *datapath_type,
                                  const char *port_type);

    /* Enumerates the types of all supported p4rt types into 'types'.  The
     * caller has already initialized 'types'.  The implementation should add
     * its own types to 'types' but not remove any existing ones, because other
     * p4rt classes might already have added names to it. */
    void (*enumerate_types)(struct sset *types);

    /* Performs any periodic activity required on p4rts of type
     * 'type'.
     *
     * An p4rt provider may implement it or not, depending on whether
     * it needs type-level maintenance.
     *
     * Returns 0 if successful, otherwise a positive errno value. */
    int (*type_run)(const char *type);

    /*
     * CONSTRUCTION.
     */
    struct p4rt *(*alloc)(void);
    int (*construct)(struct p4rt *p4rt);
    void (*destruct)(struct p4rt *p4rt, bool del);
    void (*dealloc)(struct p4rt *p4rt);

    int (*run)(struct p4rt *p4rt);

/* ## ---------------- ## */
/* ## p4port Functions ## */
/* ## ---------------- ## */

    struct p4port *(*port_alloc)(void);
    int (*port_construct)(struct p4port *p4port);
    void (*port_destruct)(struct p4port *p4port, bool del);
    void (*port_dealloc)(struct p4port *p4port);

    /* Looks up a port named 'devname' in 'p4rt'.  On success, returns 0 and
     * initializes '*port' appropriately. Otherwise, returns a positive errno
     * value.
     *
     * The caller owns the data in 'port' and must free it with
     * p4rt_port_destroy() when it is no longer needed. */
    int (*port_query_by_name)(const struct p4rt *p4rt,
                              const char *devname, struct p4rt_port *port);

    /* Attempts to add 'netdev' as a port on 'p4rt'.  Returns 0 if
     * successful, otherwise a positive errno value.  The caller should
     * inform the implementation of the OpenFlow port through the
     * ->port_construct() method.
     *
     * It doesn't matter whether the new port will be returned by a later call
     * to ->port_poll(); the implementation may do whatever is more
     * convenient. */
    int (*port_add)(struct p4rt *p, struct netdev *netdev);




};

extern const struct p4rt_class p4rt_dpif_class;


#endif //P4RT_PROVIDER_H
