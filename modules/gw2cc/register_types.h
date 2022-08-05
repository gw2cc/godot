// Fake module so godot thinks there is a gw2cc module.

#ifdef WIN32
#define USED
#else
#define USED __attribute__((used))
#endif

void register_gw2cc_types() USED;
void unregister_gw2cc_types() USED;
