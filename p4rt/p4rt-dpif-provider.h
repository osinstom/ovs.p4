#ifndef P4RT_DPIF_PROVIDER_H
#define P4RT_DPIF_PROVIDER_H

struct p4rt_dpif_class {
    /* Type of dpif in this class, e.g. "system", "netdev", etc.
     *
     * One of the providers should supply a "system" type, since this is
     * the type assumed if no type is specified when opening a dpif. */
    const char *type;

    /* Called when the dpif provider is registered, typically at program
     * startup.  Returning an error from this function will prevent any
     * datapath with this class from being created.
     *
     * This function may be set to null if a datapath class needs no
     * initialization at registration time. */
    int (*init)(void);

    int (*open)(const struct p4rt_dpif_class *dpif_class,
                const char *name, bool create, struct p4rt_dpif **dpifp);

};

extern const struct p4rt_dpif_class dpif_ubpf_class;

#endif //P4RT_DPIF_PROVIDER_H
