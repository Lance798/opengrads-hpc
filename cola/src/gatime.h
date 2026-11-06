/* Isolated, bounded time evaluation. GPL-2.0-only; see COPYING. */
#ifndef GATIME_H
#define GATIME_H
#include "gatypes.h"
struct gacmn;
struct gastat;
struct gagrid;
void ga_time_init(const char *);
void ga_time_prepare_worker(void);
void ga_time_shutdown(void);
gaint ga_time_active_workers(void);
gaint ga_time_worker(struct gacmn *, int);
gaint ga_time_define(char *, struct gacmn *, gaint, gaint, gaint, gaint,
                     gaint, struct gagrid *, gadouble *, char *, gadouble);
gaint ga_time_display(char *, struct gastat *, struct gacmn *);
#endif
