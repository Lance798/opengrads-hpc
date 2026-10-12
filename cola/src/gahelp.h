/* Added in 2026: usage hints at the prompt (gahelp.c). GPLv2; see COPYING. */

#define GAHELP_CMD   0     /* commands */
#define GAHELP_SET   1     /* set options */
#define GAHELP_QUERY 2     /* query options */
#define GAHELP_FUNC  3     /* functions */
#define GAHELP_DRAW  4     /* draw ... */

void gahelpclear (void);
void gahelpnote (gaint, const char *);
void gahelpafter (char *, struct gacmn *);
gaint gahelpfailed (void);
const char *gahelpname (gaint, gaint);
const char *gahelpwords (gaint, const char *);
