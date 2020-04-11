#ifndef P4RT_PROVIDER_H
#define P4RT_PROVIDER_H 1

#include "openvswitch/hmap.h"

struct p4rt {
    struct hmap_node hmap_node; /* In global 'all_p4rts' hmap. */
    const struct p4rt_class *p4rt_class;

    char *type;                 /* Datapath type. */
    char *name;                 /* Datapath name. */
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

};

extern const struct p4rt_class p4rt_dpif_class;


#endif //P4RT_PROVIDER_H
