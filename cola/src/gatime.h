/* Isolated, bounded time evaluation. GPL-2.0-only; see COPYING. */
#ifndef GATIME_H
#define GATIME_H
#include "gatypes.h"
struct gacmn;
struct gastat;
struct gagrid;
struct gafile;
void ga_time_init(const char *);
void ga_time_prepare_worker(void);
void ga_time_shutdown(void);
gaint ga_time_active_workers(void);
gaint ga_time_is_worker(void);
gaint ga_time_worker(struct gacmn *, int);
gaint ga_time_define(char *, struct gacmn *, gaint, gaint, gaint, gaint,
                     gaint, struct gagrid *, gadouble *, char *, gadouble);
gaint ga_time_display(char *, struct gastat *, struct gacmn *);
gaint ga_time_can_evaluate(char *, struct gastat *);
gaint ga_time_evaluatesteps(char *, struct gastat *, struct gagrid *, gaint, gaint,
                            gaint, gadouble *, char *);
gaint ga_time_can_read(struct gafile *);
gadouble ga_time_batch_bytes(struct gafile *);
gaint ga_time_readsteps(struct gagrid *, gaint, gaint, gaint,
                        gadouble *, char *, long);
#endif
