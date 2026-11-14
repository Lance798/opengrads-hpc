/* Copyright (C) 1988-2018 by George Mason University. See file COPYRIGHT for more information. */
/* Modified in 2026 for OpenMP, bounded spatial reductions, vertical functions,
   and saturation diagnostics; see COPYING. */

/*  Originally authored by B. Doty.
    Some functions provided by others. */

#ifdef HAVE_CONFIG_H
#include "config.h"
/* If autoconfed, only include malloc.h when it's presen */
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#else /* undef HAVE_CONFIG_H */
#include <malloc.h>
#endif /* HAVE_CONFIG_H */

#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include "grads.h"
#include "gahelp.h"
#include "gaomp.h"
#include "gatime.h"

#if OPENGRADS == 1
#include "gaudx.h"
#endif


extern struct gamfcmn mfcmn;     /* for global warning level settings */
static struct gaupb *upba=NULL;  /* Anchor for user defined plug-in */
static char pout[1256];          /* Build error msgs here */

/* Function routine names.  Add a new function by putting the
   prototype here and adding to the if tests below.  */
gaint ffsqrt   (struct gafunc *, struct gastat *);
gaint ffsin    (struct gafunc *, struct gastat *);
gaint ffcos    (struct gafunc *, struct gastat *);
gaint fftan    (struct gafunc *, struct gastat *);
gaint ffasin   (struct gafunc *, struct gastat *);
gaint ffacos   (struct gafunc *, struct gastat *);
gaint ffexp    (struct gafunc *, struct gastat *);
gaint fflog    (struct gafunc *, struct gastat *);
gaint fflog10  (struct gafunc *, struct gastat *);
gaint ffabs    (struct gafunc *, struct gastat *);
gaint ffif     (struct gafunc *, struct gastat *);
gaint ffpow    (struct gafunc *, struct gastat *);
gaint ffmag    (struct gafunc *, struct gastat *);
gaint ffatan   (struct gafunc *, struct gastat *);
gaint ffave    (struct gafunc *, struct gastat *);
gaint ffgint   (struct gafunc *, struct gastat *);
gaint ffhdiv   (struct gafunc *, struct gastat *);
gaint ffhcrl   (struct gafunc *, struct gastat *);
gaint ffvint   (struct gafunc *, struct gastat *);
gaint ffzslice (struct gafunc *, struct gastat *);
gaint ffzint   (struct gafunc *, struct gastat *);
gaint ffesat   (struct gafunc *, struct gastat *);
gaint ffqvsat  (struct gafunc *, struct gastat *);
gaint ffelp    (struct gafunc *, struct gastat *);
gaint fftlp    (struct gafunc *, struct gastat *);
gaint ffaav    (struct gafunc *, struct gastat *);
gaint fflterp  (struct gafunc *, struct gastat *);
gaint ffscor   (struct gafunc *, struct gastat *);
gaint fftcor   (struct gafunc *, struct gastat *);
gaint fftmav   (struct gafunc *, struct gastat *);
gaint ffmask   (struct gafunc *, struct gastat *);
gaint ffg2s    (struct gafunc *, struct gastat *);
gaint ffg2s2   (struct gafunc *, struct gastat *);
gaint fftv2t   (struct gafunc *, struct gastat *);
gaint fftv2q   (struct gafunc *, struct gastat *);
gaint ffoacr   (struct gafunc *, struct gastat *);
gaint ffoabn   (struct gafunc *, struct gastat *);
gaint ffsmth   (struct gafunc *, struct gastat *);
gaint ffsave   (struct gafunc *, struct gastat *);
gaint ffsmin   (struct gafunc *, struct gastat *);
gaint ffsmax   (struct gafunc *, struct gastat *);
gaint ffskip   (struct gafunc *, struct gastat *);
gaint ffcnst   (struct gafunc *, struct gastat *);
gaint ffcdif   (struct gafunc *, struct gastat *);
gaint ffroll   (struct gafunc *, struct gastat *);
gaint ffmn     (struct gafunc *, struct gastat *);
gaint ffamn    (struct gafunc *, struct gastat *);
gaint ffsum    (struct gafunc *, struct gastat *);
gaint ffsumg   (struct gafunc *, struct gastat *);
gaint ffasum   (struct gafunc *, struct gastat *);
gaint ffasumg  (struct gafunc *, struct gastat *);
gaint ffatot   (struct gafunc *, struct gastat *);
gaint ffamin   (struct gafunc *, struct gastat *);
gaint ffamax   (struct gafunc *, struct gastat *);
gaint ffaminlocx (struct gafunc *, struct gastat *);
gaint ffaminlocy (struct gafunc *, struct gastat *);
gaint ffamaxlocx (struct gafunc *, struct gastat *);
gaint ffamaxlocy (struct gafunc *, struct gastat *);
gaint ffgrarea (struct gafunc *, struct gastat *);
gaint ffclgr   (struct gafunc *, struct gastat *);
gaint ffmin    (struct gafunc *, struct gastat *);
gaint ffmax    (struct gafunc *, struct gastat *);
gaint ffminl   (struct gafunc *, struct gastat *);
gaint ffmaxl   (struct gafunc *, struct gastat *);
gaint ffflvl   (struct gafunc *, struct gastat *);
gaint ffsreg   (struct gafunc *, struct gastat *);
gaint fftreg   (struct gafunc *, struct gastat *);
gaint ffs2g1d  (struct gafunc *, struct gastat *);
gaint fftv2    (struct gafunc *, struct gastat *, gaint);
gaint ffsmnx   (struct gafunc *, struct gastat *, gaint);
gaint tmaskf   (struct gafunc *, struct gastat *, gaint);
gaint aave     (struct gafunc *, struct gastat *, gaint);
gaint ave      (struct gafunc *, struct gastat *, gaint);
gaint scorr    (struct gafunc *, struct gastat *, gaint);
gaint tvrh2q   (gadouble, gadouble, gadouble, gadouble *, gadouble *);
gaint fndarg   (char *, gaint *);
void cosadj    (struct gagrid *);
char doaave    (struct gagrid *, gadouble, gadouble, gadouble, gadouble, gaint, gadouble *);
gaint mnmx     (struct gafunc *, struct gastat *, int);


/* the pointer to the anchor of user-defined plug-ins is set from grads.c */
void setupba (struct gaupb *upb) {
  upba = upb;
}


/* Handles function calls from gaexpr.c  */
char *rtnprs (char *ch, char *name, struct gastat *pst) {
struct gafunc *pfc;
struct gastat *pst2;
struct gaupb *upb=NULL;
char *pos;
gaint pdeep,rc;
size_t sz;
gaint (*fpntr)(struct gafunc *, struct gastat *);

  /* Find the function name and get the function pointer. */
  fpntr=NULL;

  /* New built-ins also need a funcs[] entry in gahelp.c.
     Checklist: docs/ADDING_FUNCTIONS.md; check: pytests/check_function_catalog.py. */
  /* start with the list of built-in functions */
  if (cmpwrd("sqrt",name)) fpntr = ffsqrt;
  if (cmpwrd("sin",name)) fpntr = ffsin;
  if (cmpwrd("cos",name)) fpntr = ffcos;
  if (cmpwrd("tan",name)) fpntr = fftan;
  if (cmpwrd("asin",name)) fpntr = ffasin;
  if (cmpwrd("acos",name)) fpntr = ffacos;
  if (cmpwrd("exp",name)) fpntr = ffexp;
  if (cmpwrd("log",name)) fpntr = fflog;
  if (cmpwrd("log10",name)) fpntr = fflog10;
  if (cmpwrd("abs",name)) fpntr = ffabs;
  if (cmpwrd("if",name)) fpntr = ffif;
  if (cmpwrd("pow",name)) fpntr = ffpow;
  if (cmpwrd("ave",name)) fpntr = ffave;
  if (cmpwrd("mag",name)) fpntr = ffmag;
  if (cmpwrd("atan2",name)) fpntr = ffatan;
  if (cmpwrd("hdivg",name)) fpntr = ffhdiv;
  if (cmpwrd("hcurl",name)) fpntr = ffhcrl;
  if (cmpwrd("vint",name)) fpntr = ffvint;
  if (cmpwrd("zslice",name)) fpntr = ffzslice;
  if (cmpwrd("zint",name)) fpntr = ffzint;
  if (cmpwrd("esat",name)) fpntr = ffesat;
  if (cmpwrd("qvsat",name)) fpntr = ffqvsat;
  if (cmpwrd("tloop",name)) fpntr = fftlp;
  if (cmpwrd("eloop",name)) fpntr = ffelp;
  if (cmpwrd("aave",name)) fpntr = ffaav;
  if (cmpwrd("scorr",name)) fpntr = ffscor;
  if (cmpwrd("tcorr",name)) fpntr = fftcor;
  if (cmpwrd("tmave",name)) fpntr = fftmav;
  if (cmpwrd("maskout",name)) fpntr = ffmask;
  if (cmpwrd("gr2stn",name)) fpntr = ffg2s;
  if (cmpwrd("tvrh2q",name)) fpntr = fftv2q;
  if (cmpwrd("tvrh2t",name)) fpntr = fftv2t;
  if (cmpwrd("gint",name)) fpntr = ffgint;
  if (cmpwrd("oacres",name)) fpntr = ffoacr;
  if (cmpwrd("oabin",name)) fpntr = ffoabn;
  if (cmpwrd("smth9",name)) fpntr = ffsmth;
  if (cmpwrd("stnave",name)) fpntr = ffsave;
  if (cmpwrd("stnmin",name)) fpntr = ffsmin;
  if (cmpwrd("stnmax",name)) fpntr = ffsmax;
  if (cmpwrd("skip",name)) fpntr = ffskip;
  if (cmpwrd("const",name)) fpntr = ffcnst;
  if (cmpwrd("cdiff",name)) fpntr = ffcdif;
  if (cmpwrd("roll",name)) fpntr = ffroll;
  if (cmpwrd("mean",name)) fpntr = ffmn;
  if (cmpwrd("amean",name)) fpntr = ffamn;
  if (cmpwrd("sum",name)) fpntr = ffsum;
  if (cmpwrd("sumg",name)) fpntr = ffsumg;
  if (cmpwrd("asum",name)) fpntr = ffasum;
  if (cmpwrd("asumg",name)) fpntr = ffasumg;
  if (cmpwrd("atot",name)) fpntr = ffatot;
  if (cmpwrd("grarea",name)) fpntr = ffgrarea;
  if (cmpwrd("coll2gr",name)) fpntr = ffclgr;
  if (cmpwrd("min",name)) fpntr = ffmin;
  if (cmpwrd("max",name)) fpntr = ffmax;
  if (cmpwrd("minloc",name)) fpntr = ffminl;
  if (cmpwrd("maxloc",name)) fpntr = ffmaxl;
  if (cmpwrd("fndlvl",name)) fpntr = ffflvl;
  if (cmpwrd("sregr",name)) fpntr = ffsreg;
  if (cmpwrd("tregr",name)) fpntr = fftreg;
  if (cmpwrd("s2g1d",name)) fpntr = ffs2g1d;
  if (cmpwrd("lterp",name)) fpntr = fflterp;
  if (cmpwrd("amin",name)) fpntr = ffamin;
  if (cmpwrd("amax",name)) fpntr = ffamax;
  if (cmpwrd("aminlocx",name)) fpntr = ffaminlocx;
  if (cmpwrd("aminlocy",name)) fpntr = ffaminlocy;
  if (cmpwrd("amaxlocx",name)) fpntr = ffamaxlocx;
  if (cmpwrd("amaxlocy",name)) fpntr = ffamaxlocy;

#if OPENGRADS == 1
  /* OpenGrADS User Defined Extensions */
  if (fpntr==NULL) *(void **) &fpntr = (void *) gaudf(name);
#endif

  /* check the list of plug-ins functions/defops */
  if (fpntr==NULL) {
    upb = upba;
    while (upb) {
      if (cmpwrd(upb->name,name) && upb->type<=2) break;
      upb = upb->upb;
    }
  }

  if (upb==NULL && fpntr==NULL) {           /* Didn't find it....      */
    gaprnt (0,"Syntax Error:  Invalid Operand \n");
    snprintf(pout,1255,"  '%s' not a variable or function name\n",name);
    gaprnt (0,pout);
    gahelpnote(1,name);
    return (NULL);
  }

  /* Allocate storage for gastat and gafunc structures                */
  sz = sizeof(struct gafunc);
  pfc = (struct gafunc *)galloc(sz,"funccall");
  sz = sizeof(struct gastat);
  pst2 = (struct gastat *)galloc(sz,"funcpst");

  *pst2 = *pst;                            /* Provide copy of gastat  */

  /* Parse the argument list                                          */
  pfc->argnum = 0;                         /* Initial arg count       */
  pfc->buff[0] = '\0';                     /* With no args, as in     */
  pfc->argpnt[0] = pfc->buff;              /*  coll2gr(), a function  */
                                           /*  that reads the 1st one */
                                           /*  gets "", not garbage   */
  if (*ch=='(') {                          /* If no leading paren..   */
    ch++;                                  /* Past the '('            */
    if (*ch==')') {                        /* Was it '()'???          */
      ch++;                                /*  Point past this func   */
    } else {                               /* We have something       */
      pos = pfc->buff;                     /*  Point to output buffer */
      pdeep = 0;                           /*  No parens nested yet   */
      pfc->argpnt[0] = pos;                /*  Start of 1st arg       */
      while (pdeep!=0 || *ch!=')') {       /*  Until end of args...   */
        if (*ch=='\0' || *ch=='\n') {      /*   End of string?        */
          gaprnt (0,"Syntax Error:  Unmatched parens\n");
          gahelpnote(2,name);
          goto err;                        /*    And return           */
        }                                  /*   Is ok                 */
        *pos = *ch;                        /*   Move to output buffer */
        if (*ch=='(') pdeep++;             /*   Track paren nesting   */
        else if (*ch==')') pdeep--;        /*   ditto                 */
        else if (pdeep==0 && *ch==',') {   /*   End of an arg?        */
          *pos = '\0';                     /*    Terminate string     */
          pfc->argnum++;                   /*    Bump arg counter     */
          pfc->argpnt[pfc->argnum] = pos+1; /*   Start of next arg    */
          *(pos+1) = '\0';                 /*    If trailing comma    */
        }                                  /*   endif                 */
        pos++; ch++;                       /*   Bump pointers         */
      }                                    /*  Continue               */
      *pos = '\0';                         /*  terminate final arg    */
      pfc->argnum++;                       /*  Count instead of subscr*/
      ch++;                                /*  Pnt past func call     */
    }                                      /* X                       */
  }                                        /* We have args parsed     */

  /* Everything is all set.  Call the function routine.               */

  if (upb) rc = ffudpi(pfc,pst2,upb);      /* Call the plug-in        */
  else rc = (*fpntr)(pfc, pst2);           /* Call the function       */

  if (rc==-1) {
    snprintf(pout,1255,"Error in %s : Arg was stn data type\n",name);
    gaprnt (0,pout);
  }

  if (rc) {                                /* If an error occurred... */
    snprintf(pout,1255,"Operation Error:  Error from %s function\n",name);
    gaprnt (0,pout);
    gahelpnote(2,name);
    goto err;
  }
  pst->type = pst2->type;
  pst->result = pst2->result;              /* Return result grid      */
  gree(pst2,"f404");
  gree(pfc,"f405");
  return (ch);                             /* And return new pointer  */

err:
  gree(pst2,"f406");
  gree(pfc,"f407");
  return (NULL);

}


/**********************************************************************\
*                                                                      *
*  Function routines follow.  To add a funcion routine, add code here, *
*  and update the gafunc.h file.                                       *
*                                                                      *
\**********************************************************************/

gaint ffsqrt (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt,ecnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from SQRT: Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  ecnt=0;
  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val  = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) reduction(+:ecnt) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) {
        if (val[i]<0.0) {
          valu[i] = 0;
          ecnt++;
        } else {
	  val[i] = sqrt(val[i]);
	  valu[i] = 1;
	}
      }
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask != 0) {
        if (rpt->val < 0.0) {
          rpt->umask = 0;
          ecnt++;
        } else rpt->val = sqrt(rpt->val);
      }
      rpt=rpt->rpt;
    }
  }
  if (ecnt>0) {
    snprintf(pout,1255,"Warning from SQRT:  Data has %i values < zero \n",ecnt);
    gaprnt (1,pout);
    gaprnt (1,"                    These were set to the undefined value \n");
  }
  return (0);
}

gaint ffsin  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from SIN:  Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) val[i] = sin(val[i]);
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) rpt->val = sin(rpt->val);
      rpt=rpt->rpt;
    }
  }

  return (0);
}

gaint ffcos  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from COS:  Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) val[i] = cos(val[i]);
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) rpt->val = cos(rpt->val);
      rpt=rpt->rpt;
    }
  }

  return (0);
}

gaint fftan  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from TAN:  Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) val[i] = tan(val[i]);
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) rpt->val = tan(rpt->val);
      rpt=rpt->rpt;
    }
  }
  return (0);
}

gaint ffasin  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from ASIN:  Too many or too few args \n");
    gaprnt (0,"                  One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) {
        if (val[i]>1.0 || val[i]<-1.0) {
	  valu[i] = 0;
	}
        else {
	  val[i] = asin(val[i]);
	}
      }
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) {
        if (rpt->val>1.0 || rpt->val<-1.0) rpt->umask = 0;
        else rpt->val = asin(rpt->val);
      }
      rpt=rpt->rpt;
    }
  }
  return (0);
}

gaint ffacos  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from ACOS:  Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) {
        if (val[i]>1.0 || val[i]<-1.0) {
	  valu[i] = 0;
	}
        else {
	  val[i] = acos(val[i]);
	}
      }
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) {
        if (rpt->val>1.0 || rpt->val<-1.0) rpt->umask = 0;
        else rpt->val = acos(rpt->val);
      }
      rpt=rpt->rpt;
    }
  }
  return (0);
}

gaint ffabs (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from ABS:  Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) val[i] = fabs(val[i]);
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) rpt->val = fabs(rpt->val);
      rpt=rpt->rpt;
    }
  }

  return (0);
}

gaint ffexp  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from EXP:  Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) val[i] = exp(val[i]);
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) rpt->val = exp(rpt->val);
      rpt=rpt->rpt;
    }
  }

  return (0);
}

gaint fflog  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt,ecnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from LOG:  Too many or too few args \n");
    gaprnt (0,"                 One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  ecnt=0;
  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) reduction(+:ecnt) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) {
        if (val[i]<=0.0) {
          valu[i] = 0;
          ecnt++;
        }
	else val[i] = log(val[i]);
      }
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) {
        if (rpt->val<=0.0) {
          rpt->umask = 0;
          ecnt++;
        } else rpt->val = log(rpt->val);
      }
      rpt=rpt->rpt;
    }
  }
  if (ecnt>0) {
    snprintf(pout,1255,"Warning from LOG:  Data has %i values <= zero \n",ecnt);
    gaprnt (1,pout);
    gaprnt (1,"                   These were set to the undefined value \n");
  }

  return (0);
}

gaint fflog10 (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt,ecnt;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val;
char *valu;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from LOG10:  Too many or too few args \n");
    gaprnt (0,"                   One argument expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  ecnt=0;
  if (pst->type==1) {
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    valu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) reduction(+:ecnt) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (valu[i]!=0) {
        if (val[i]<=0.0) {
          valu[i] = 0;
          ecnt++;
        } else val[i] = log10(val[i]);
      }
    }
  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (rpt->umask!=0) {
        if (rpt->val<=0.0) {
          rpt->umask = 0;
          ecnt++;
        } else rpt->val = log10(rpt->val);
      }
      rpt=rpt->rpt;
    }
  }
  if (ecnt>0) {
    snprintf(pout,1255,"Warning from LOG10:  Data has %i values <= zero \n",ecnt);
    gaprnt (1,pout);
    gaprnt (1,"                     These were set to the undefined value \n");
  }

  return (0);
}

/* Implement the IF function.  Three expressions for the operation.
   The first arg is assumed to be the result of a logical expression. */

gaint ffif (struct gafunc *pfc, struct gastat *pst) {
struct gastat pst2,pst3;
struct gagrid *mypgr=NULL,*pgr1,*pgrres1,*pgrres2,*pgrres3;
gaint rc,i,savaloc;

  if (pfc->argnum!=3) {
    gaprnt (0,"Error from IF:  Too many or too few args \n");
    gaprnt (0,"                Three arguments expected \n");
    return (1);
  }

  pst2 = *pst;
  pst3 = *pst;

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) {
    return (rc);
  }

  rc = gaexpr(pfc->argpnt[1],&pst2);
  if (rc) {
    gafree (pst);
    return (rc);
  }

  rc = gaexpr(pfc->argpnt[2],&pst3);
  if (rc) {
    gafree (pst);
    gafree (&pst2);
    return (rc);
  }

  if (pst->type!=1 || pst2.type!=1 || pst3.type!=1) {
    gaprnt (0,"Error from IF:  Arguments must be grids \n");
    goto err1;
  }

  /* We need a copy of the result of the logical expression (arg 1)
     since we need to use that result twice, and gagrop
     may put its result grid there for our first
     step.  We will re-use the scaling info, but we have to be
     careful with the alocf issue. */

  mypgr = galloc(sizeof(struct gagrid),"ffif3");
  if (mypgr==NULL) goto err1;

  pgr1 = pst->result.pgr;
  savaloc = pgr1->alocf;   /* preserve original alocf value */
  *mypgr = *pgr1;
  mypgr->alocf = 0;
  pgr1->alocf = 0;   /* don't let scaling info get freed until we are ready */

  /* Both branch operations modify their inputs. A one-dimensional condition
     needs its own values/masks too; scalar pointers must refer to the copy's
     embedded storage rather than the original condition. */
  if (mypgr->idim != -1) {
    mypgr->grid = NULL;
    mypgr->umask = NULL;
    mypgr->grid = galloc(sizeof(gadouble)*mypgr->isiz*mypgr->jsiz,"ffif1");
    if (mypgr->grid==NULL) goto err1;
    mypgr->umask = galloc(sizeof(char)*mypgr->isiz*mypgr->jsiz,"ffif2");
    if (mypgr->umask==NULL) goto err1;
    for (i=0; i<mypgr->isiz*mypgr->jsiz; i++) {
      *(mypgr->grid+i) = *(pgr1->grid+i);
      *(mypgr->umask+i) = *(pgr1->umask+i);
    }
  } else {
    mypgr->grid = &mypgr->rmin;
    mypgr->umask = &mypgr->umin;
  }

  /* Don't let gagrop free anything  */

  rc = 0;
  pgrres1=NULL; pgrres2=NULL; pgrres3=NULL;

  pgrres1 = gagrop(pgr1, pst2.result.pgr, 14, 0);
  if (pgrres1==NULL) rc = 1;
  if (rc==0) pgrres2 = gagrop(mypgr, pst3.result.pgr, 15, 0);
  if (pgrres2==NULL) rc = 1;
  if (rc==0) pgrres3 = gagrop(pgrres2, pgrres1, 2, 0);
  if (pgrres3==NULL) rc = 1;

  /* We don't really know what original pgr ended up being our final result.
     We sure don't want to free that one! We'll do something kloodgy
     and compare pointers.  On error, pgrres3 ends up NULL, so everything
     will get free'd  */

  /* printf ("xxxx %p %p %p %p %p\n",pgrres3,pst->result.pgr,
                   pst2.result.pgr,pst3.result.pgr,mypgr);
  printf ("xxxx before gafree pst\n",rc); */

  if (pgrres3 == mypgr) mypgr->alocf = savaloc; /* propagate alocf... */
  else pgr1->alocf = savaloc;      /*  or restore it */

  if (pgrres3 != pst->result.pgr) gafree(pst);
  if (pgrres3 != pst2.result.pgr) gafree(&pst2);
  if (pgrres3 != pst3.result.pgr) gafree(&pst3);
  if (pgrres3 != mypgr) gagfre(mypgr);

  if (rc==0) {
    pst->type = 1;
    pst->result.pgr = pgrres3;
  }

  return (rc);

  err1:
    if (mypgr) {
      pgr1->alocf = savaloc;
      if (mypgr->idim>=0) {
        if (mypgr->grid) gree(mypgr->grid,"ffif1");
        if (mypgr->umask) gree(mypgr->umask,"ffif2");
      }
      gree(mypgr,"ffif3");
    }
    gafree(pst);
    gafree(&pst2);
    gafree(&pst3);
    return(1);
}


gaint ffpow (struct gafunc *pfc, struct gastat *pst) {
struct gastat pst2;
gaint rc;

  if (pfc->argnum!=2) {
    gaprnt (0,"Error from POW:  Too many or too few args \n");
    gaprnt (0,"                 Two arguments expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  pst2 = *pst;
  rc = gaexpr(pfc->argpnt[1],&pst2);
  if (rc) {
    gafree (pst);
    return (rc);
  }

  rc = gafopr (pst, &pst2, 10);
  if (rc) {
    gafree (pst);
    gafree (&pst2);
  }

  return (rc);
}

gaint ffmag (struct gafunc *pfc, struct gastat *pst) {
struct gastat pst2;
gaint rc;

  if (pfc->argnum!=2) {
    gaprnt (0,"Error from MAG:  Too many or too few args \n");
    gaprnt (0,"                 Two arguments expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  pst2 = *pst;
  rc = gaexpr(pfc->argpnt[1],&pst2);
  if (rc) {
    gafree (pst);
    return (rc);
  }

  rc = gafopr (pst, &pst2, 11);
  if (rc) {
    gafree (pst);
    gafree (&pst2);
  }
  return (rc);
}

/* Perform atan2 function */

gaint ffatan (struct gafunc *pfc, struct gastat *pst) {
struct gastat pst2;
gaint rc;

  if (pfc->argnum!=2) {
    gaprnt (0,"Error from ATAN2:  Too many or too few args \n");
    gaprnt (0,"                   Two arguments expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  pst2 = *pst;
  rc = gaexpr(pfc->argpnt[1],&pst2);
  if (rc) {
    gafree (pst);
    return (rc);
  }

  rc = gafopr (pst, &pst2, 12);
  if (rc) {
    gafree (pst);
    gafree (&pst2);
  }
  return (rc);
}

/* Performs a bi-linear interpolation between two grids,
   adapted from the external UDF lterp.
*/
gaint fflterp (struct gafunc *pfc, struct gastat *pst) {
struct gastat pst2;
struct gagrid *pgr1,*pgr2;
gadouble (*i1conv) (gadouble *, gadouble);
gadouble (*i2conv) (gadouble *, gadouble);
gadouble *x1=NULL,*x2=NULL,*y1=NULL,*y2=NULL;
gadouble *x1b=NULL,*x2b=NULL,*y1b=NULL,*y2b=NULL;
gadouble *gr1,*gr2,*gxout=NULL,*gyout=NULL,*area1=NULL,*area2=NULL;
gadouble rad,w1,w2=0,alo,ahi,pct,minpct,tscl;
gaint rc,i,ii,j,jj,i2,ij,ij1,ij2,j2,error=0,opt;
gaint pxgbflg,pygbflg;
gaint ib,ie,jb,je,maxgrid,icnt,indx,flag;
gadouble *area_box=NULL,*fld_box=NULL,tot_fld,tot_area;
char *gr1u,*gr2u;
struct gafile *pfi1,*pfi2;
gaint ic,jc,k,jcm1,jcp1,jcp2,bessel;
gadouble r,s,r1,r2,r3,s1,s2,s3,fijm1,fij,fijp1,fijp2,u,fr[4],del,del2,del3;
char uijm1,uij,uijp1,uijp2;
struct dt t1g1;
gadouble tval;

  rad = M_PI/180.0;

  if (pfc->argnum<2 || pfc->argnum>4) {
    gaprnt (0,"Error from LTERP:  Too many or too few args \n");
    gaprnt (0,"                    2 to 4 arguments expected \n");
    return (1);
  }

  if (pfc->argnum>=3) {
    if (strcmp("bilin",pfc->argpnt[2])==0)  {opt = 0; bessel=0;}  /* bilinear interpolation */
    if (strcmp("bessel",pfc->argpnt[2])==0) {opt = 0; bessel=1;}  /* bilinear interpolation with bessel */
    if (strcmp("aave" ,pfc->argpnt[2])==0)   opt = 1;             /* area average */
    if (strcmp("amean",pfc->argpnt[2])==0)   opt = 2;             /* area mean */

    if (pfc->argnum==4) {
      if (getdbl(pfc->argpnt[3],&minpct)==NULL) {
	gaprnt (0,"Error from LTERP:  4th argument must be a real number between 0 and 100 \n");
	return(1);
      }
      if (minpct<0 || minpct>100) {
	gaprnt (0,"Error from LTERP:  4th argument must be between 0 and 100 \n");
	return(1);
      }
    }
    else minpct=50.0;
  }
  else {
    opt=0;         /* bilin without bessel is the default */
    bessel=0;
  }


   /* Evaluate the 2nd expression, the destination grid */
  pst2=*pst;
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) {
    gaprnt (0,"Error from LTERP: Failed to evaluate 2nd expression \n");
    error=1; goto err;
  }
  if (pst->type!=1) {
    gaprnt (0,"Error from LTERP: The 2nd argument is not a grid expression \n");
    error=1; goto err;
  }

  /* Check environment */
  if (pst->idim==-1) {
    gaprnt (0,"Error from LTERP: The destination grid is a constant \n");
    error=1; goto err;
  }
  if (pst->idim==2 || pst->idim==4 || pst->jdim==2 || pst->jdim==4) {
    gaprnt (0,"Error from LTERP: The grids cannot vary in Z or E \n");
    error=1; goto err;
  }
  if (opt==1 || opt==2) {
    if (pst->idim!=0 || pst->jdim!=1) {
    gaprnt (0,"Error from LTERP: The grids must vary in X and Y when using aave or amean\n");
    error=1; goto err;
    }
  }

  /* Evaluate the 1st expression, the data values to be interpolated */
  rc = gaexpr(pfc->argpnt[0],&pst2);
  if (rc) {
    gaprnt (0,"Error from LTERP: Failed to evaluate 1st expression \n");
    error=1; goto err;
  }
  if (pst2.type!=1) {
    gaprnt (0,"Error from LTERP: The 1st argument is not a grid expression \n");
    error=1; goto err;
  }

  /* Verify that the varying dimensions are equivalent */
  if (pst->idim!=pst2.idim || pst->jdim!=pst2.jdim) {
    gaprnt (0,"Error from LTERP: Grids have different varying dimensions \n");
    error=1; goto err;
  }

  /* Get the grids */
  pgr1 = pst2.result.pgr;     /* data source grid (input)  */
  pgr2 = pst->result.pgr;     /* destination grid (result) */
  gr1  = pgr1->grid;
  gr1u = pgr1->umask;
  gr2  = pgr2->grid;
  gr2u = pgr2->umask;
  pfi1 = pgr1->pfile;
  pfi2 = pgr2->pfile;

  /* If the time dimension varies, the time increment must be equivalent
     (we won't interpolate from months to minutes) */
  if (pst->idim==3) {
    if ((*(pgr1->ivals+6)<0.5 && *(pgr2->ivals+6)>0.8) ||
	(*(pgr1->ivals+6)>0.8 && *(pgr2->ivals+6)<0.5)) {
      gaprnt (0,"Error from LTERP: Grids do not have matching time increments \n");
      error=1; goto err;
    }
  }
  if (pst->jdim==3) {
    if ((*(pgr1->jvals+6)<0.5 && *(pgr2->jvals+6)>0.8) ||
	(*(pgr1->jvals+6)>0.8 && *(pgr2->jvals+6)<0.5)) {
      gaprnt (0,"Error from LTERP: Grids do not have matching time increments \n");
      error=1; goto err;
    }
  }

  /* get coordinate information (grid box centers and edges) for both grids,
     source input grid 1 and destination output grid 2 */

  if (pgr1->idim>-1) {
    /* x1  is for the longitude of source grid box centers */
    /* x1b is for the longitude of source grid box boundaries */
    x1 = (gadouble *)galloc(sizeof(gadouble)*pgr1->isiz,"x1");
    if (x1==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for x1\n");
      error=1; goto err;
    }
    if (pgr1->idim==3) {
      /* don't need to get the boundaries for time interpolation */
      j=0;
      for (i=pgr1->dimmin[pgr1->idim];i<=pgr1->dimmax[pgr1->idim];i++) {
	*(x1+j) = (gadouble)i;
	j++;
      }
    } else {
      /* one extra point in the boundary array */
      x1b = (gadouble *)galloc(sizeof(gadouble)*(pgr1->isiz+1),"x1b");
      if (x1b==NULL) {
	gaprnt (0,"Error from LTERP: Unable to allocate memory for x1b\n");
	error=1; goto err;
      }
      i1conv = pgr1->igrab;
      j=0;
      for (i=pgr1->dimmin[pgr1->idim];i<=pgr1->dimmax[pgr1->idim];i++) {
	*(x1+j)  = i1conv(pgr1->ivals,(gadouble)i);         /* grid box centers */
	*(x1b+j) = i1conv(pgr1->ivals,(gadouble)(i-0.5));   /* grid box edges */
	j++;
      }
      *(x1b+j) = i1conv(pgr1->ivals,(gadouble)(i-0.5));  /* final edge */
    }
  }

  if (pgr2->idim>-1) {
    /* x2  is for the longitude of destination grid box centers */
    /* x2b is for the longitude of destination grid box boundaries */
    x2 = (gadouble *)galloc(sizeof(gadouble)*pgr2->isiz,"x2");
    if (x2==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for x2\n");
      error=1; goto err;
    }
    if (pgr2->idim==3) {
      /* don't need to get the boundaries for time interpolation */
      j=0;
      for (i=pgr2->dimmin[pgr2->idim];i<=pgr2->dimmax[pgr2->idim];i++) {
	*(x2+j) = (gadouble)i;
	j++;
      }
    } else {
      x2b = (gadouble *)galloc(sizeof(gadouble)*(pgr2->isiz+1),"x2b");
      if (x2b==NULL) {
	gaprnt (0,"Error from LTERP: Unable to allocate memory for x2b\n");
	error=1; goto err;
      }
      i2conv = pgr2->igrab;
      j=0;
      for (i=pgr2->dimmin[pgr2->idim];i<=pgr2->dimmax[pgr2->idim];i++) {
	*(x2+j)  = i2conv(pgr2->ivals,(gadouble)i);        /* grid box centers */
	*(x2b+j) = i2conv(pgr2->ivals,(gadouble)(i-0.5));  /* grid box edges */
	j++;
      }
      *(x2b+j) = i2conv(pgr2->ivals,(gadouble)(i-0.5));  /* final edge */
    }
  }

  if (pgr1->jdim>-1) {
    /* y1  is for the latitude of source grid box centers */
    /* y1b is for the latitude of source grid box boundaries */
    y1 = (gadouble *)galloc(sizeof(gadouble)*pgr1->jsiz,"y1");
    if (y1==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for y1\n");
      error=1; goto err;
    }
    if (pgr1->jdim==3) {
      /* don't need to get the boundaries for time interpolation */
      j=0;
      for (i=pgr1->dimmin[pgr1->jdim];i<=pgr1->dimmax[pgr1->jdim];i++) {
	*(y1+j) = (gadouble)i;
	j++;
      }
    } else {
      y1b = (gadouble *)galloc(sizeof(gadouble)*(pgr1->jsiz+1),"y1b");
      if (y1b==NULL) {
	gaprnt (0,"Error from LTERP: Unable to allocate memory for y1b\n");
	error=1; goto err;
      }
      i1conv = pgr1->jgrab;
      j=0;
      for (i=pgr1->dimmin[pgr1->jdim];i<=pgr1->dimmax[pgr1->jdim];i++) {
	*(y1+j)  = i1conv(pgr1->jvals,(gadouble)i);         /* grid box centers */
	*(y1b+j) = i1conv(pgr1->jvals,(gadouble)(i-0.5));   /* grid box edges */
	j++;
       }
      *(y1b+j) = i1conv(pgr1->jvals,(gadouble)(i-0.5));  /* final edge */
    }
  }

  if (pgr2->jdim>-1) {
    /* y2  is for the latitude of destination grid box centers */
    /* y2b is for the latitude of destination grid box boundaries */
    y2 = (gadouble *)galloc(sizeof(gadouble)*pgr2->jsiz,"y2");
    if (y2==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for y2\n");
      error=1; goto err;
    }
    if (pgr2->jdim==3) {
      /* don't need to get the boundaries for time interpolation */
      j=0;
      for (i=pgr2->dimmin[pgr2->jdim];i<=pgr2->dimmax[pgr2->jdim];i++) {
	*(y2+j) = (gadouble)i;
	j++;
      }
    } else {
      y2b = (gadouble *)galloc(sizeof(gadouble)*(pgr2->jsiz+1),"y2b");
      if (y2==NULL) {
	gaprnt (0,"Error from LTERP: Unable to allocate memory for y2b\n");
	error=1; goto err;
      }
      i2conv = pgr2->jgrab;
      j=0;
      for (i=pgr2->dimmin[pgr2->jdim];i<=pgr2->dimmax[pgr2->jdim];i++) {
	*(y2+j)  = i2conv(pgr2->jvals,(gadouble)i);         /* grid box centers */
	*(y2b+j) = i2conv(pgr2->jvals,(gadouble)(i-0.5));   /* grid box edges */
	j++;
      }
      *(y2b+j) = i2conv(pgr2->jvals,(gadouble)(i-0.5));   /* final edge */
    }
  }

  /* Normalize the time coordinates.  We want the grid time values to
     be the same coordinate system for each grid.  */
  if (pst->idim==3 || pst->jdim==3) {

    /* get world coordinate value of initial t for grid 2 (destination grid) */
    gr2t (pfi2->grvals[3],pgr2->dimmin[3],&t1g1);
    /* find the t value for this time in the axis of grid 1 (src grid) */
    tval = t2gr(pfi1->abvals[3], &t1g1);

    if (pst->idim==3) {
      /* the i dimension is T-varying */
      /* determine scaling factor */
      if (pgr1->ivals[6]>0.8)
	tscl = pgr2->ivals[6] / pgr1->ivals[6] ;
      else
	tscl = pgr2->ivals[5] / pgr1->ivals[5] ;
      /* normalize */
      for (i=0; i<pgr2->isiz; i++) *(x2+i) = tval + tscl*(gadouble)i;
    }
    else {
      /* the j dimension is T-varying */
      /* determine scaling factor */
      if (pgr1->jvals[6]>0.8)
	tscl = pgr2->jvals[6] / pgr1->jvals[6] ;
      else
	tscl = pgr2->jvals[5] / pgr1->jvals[5] ;
      /* normalize */
      for (i=0; i<pgr2->jsiz; i++) *(y2+i) = tval + tscl*(gadouble)i;
    }
  }

  if (opt==1 || opt==2) {
    /* for aave and amean,
       gxout/gyout contain real-valued locations of output grid box EDGES in i/j-axis of input grid,
       and whole integer values of gxout/gyout correspond to input grid point boundaries */

    /* allocate memory with one extra point in array size for final boundary value */
    gxout = (gadouble *)galloc((pgr2->isiz+1)*sizeof(gadouble),"gxout");
    if (gxout==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for gxout\n"); error=1; goto err;
    }
    gyout = (gadouble *)galloc((pgr2->jsiz+1)*sizeof(gadouble),"gyout");
    if (gyout==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for gyout\n"); error=1; goto err;
    }
    /* loop over i dimension for output grid boundaries */
    for (i2=0; i2<=pgr2->isiz; i2++) {
      /* check if this output grid point lies inside the longitude range of input grid */
      if ((*(x2b+i2) >= *(x1b+0)) && (*(x2b+i2) <= *(x1b+pgr1->isiz))){
	/* loop over i dimension for input grid boundaries */
	for (i=0; i<pgr1->isiz; i++) {
	  if (*(x2b+i2) >= *(x1b+i) && *(x2b+i2) <= *(x1b+i+1)) {
	    *(gxout+i2) = (gadouble)i + (*(x2b+i2)-*(x1b+i))/(*(x1b+i+1)-*(x1b+i));
	    break;
	  }
	}
      }
      else {
	/* output grid point is outside the longitude range of input grid */
	if (*(x2b+i2) < *(x1b+0))
	  *(gxout+i2) = 0.0;
	else
	  *(gxout+i2) = (gadouble)pgr1->isiz;
      }
    }
    /* loop over j dimension for output grid boundaries */
    for (j2=0; j2<=pgr2->jsiz; j2++) {
      /* check if this output grid point lies inside the latitude range of input grid */
      if (*(y2b+j2) >= *(y1b+0) && *(y2b+j2) <= *(y1b+pgr1->jsiz)) {
	/* loop over j dimension for input grid boundaries */
	for (j=0; j<pgr1->jsiz; j++) {
	  if (*(y2b+j2) >= *(y1b+j) && *(y2b+j2) <= *(y1b+j+1)) {
	    *(gyout+j2) = (gadouble)j + (*(y2b+j2)-*(y1b+j))/(*(y1b+j+1)-*(y1b+j));
	    break;
	  }
	}
      }
      else {
	/* output grid point is outside the longitude range of input grid */
	if (*(y2b+j2) < *(y1b+0))
	  *(gyout+j2) = 0.0;
	else
	  *(gyout+j2) = (gadouble)pgr1->jsiz;
      }
    }
  }
  else {
    /* for bilin (opt==0),
       gxout/gyout contain real-valued locations of output grid box CENTERS in i/j-axis of input grid,
       and whole integer values of gxout/gyout correspond to input grid point centers */

    /* allocate memory */
    gxout = (gadouble *)galloc((pgr2->isiz)*sizeof(gadouble),"gxout");
    if (gxout==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for gxout\n"); error=1; goto err;
    }
    /* loop over i dimension for output grid centers */
    for (i2=0; i2<pgr2->isiz; i2++) {
      /* initialize -- destination grid points outside range of input grid will have gxout=-999 */
      *(gxout+i2)=-999;
      /* check if this output grid point lies inside the longitude range of input grid box centers */
      if ((*(x2+i2) >= *(x1+0)) && (*(x2+i2) <= *(x1+pgr1->isiz-1))){
	/* loop over i dimension for input grid box centers */
	for (i=0; i<pgr1->isiz-1; i++) {
	  if (*(x2+i2) >= *(x1+i) && *(x2+i2) <= *(x1+i+1)) {
	    *(gxout+i2) = (gadouble)i + (*(x2+i2)-*(x1+i))/(*(x1+i+1)-*(x1+i));
	    break;
	  }
	}
      }
    }
    if (pgr1->jdim>0) {
      /* allocate memory */
      gyout = (gadouble *)galloc((pgr2->jsiz)*sizeof(gadouble),"gyout");
      if (gyout==NULL) {
	gaprnt (0,"Error from LTERP: Unable to allocate memory for gyout\n"); error=1; goto err;
      }
      /* loop over j dimension for output grid boundaries */
      for (j2=0; j2<pgr2->jsiz; j2++) {
	/* initialize -- destination grid points outside range of input grid will have gyout=-999 */
	*(gyout+j2)=-999;
	/* check if this output grid point lies inside the latitude range of input grid */
	if (*(y2+j2) >= *(y1+0) && *(y2+j2) <= *(y1+pgr1->jsiz-1)) {
	  /* loop over j dimension for input grid boundaries */
	  for (j=0; j<pgr1->jsiz-1; j++) {
	    if (*(y2+j2) >= *(y1+j) && *(y2+j2) <= *(y1+j+1)) {
	      *(gyout+j2) = (gadouble)j + (*(y2+j2)-*(y1+j))/(*(y1+j+1)-*(y1+j));
	      break;
	    }
	  }
	}
      }
    }
  }

  /* calculate the area of each grid box for the input and output grids */
  /* areas are weighted by latitude if using aave, but not if using amean */
  /* don't bother to calculate areas if using bilin */
  if (opt==1 || opt==2) {
    area1 = (gadouble *)galloc(pgr1->isiz*pgr1->jsiz*sizeof(gadouble),"area1");
    if (area1==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for area1\n"); error=1; goto err;
    }
    for (j=0; j<pgr1->jsiz; j++) {
      alo = *(y1b+j);
      ahi = *(y1b+j+1);
      if (alo < -90.0) alo = -90.0; if (alo > 90.0) alo = 90.0;
      if (ahi < -90.0) ahi = -90.0; if (ahi > 90.0) ahi = 90.0;
      if (opt==1)
	w1 = fabs(sin(ahi*rad)-sin(alo*rad));  /* length is weighted by latitude (aave) */
      else
	w1 = fabs((ahi-alo)*rad);              /* length is not weighted by latitude (amean) */
      for (i=0; i<pgr1->isiz; i++) {
	ij = j*pgr1->isiz+i;
	alo = *(x1b+i);
	ahi = *(x1b+i+1);
	w2 = fabs((ahi-alo)*rad);              /* w2 is the width of grid box  */
	*(area1+ij) =  w1 * w2;
      }
    }
    area2 = (gadouble *)galloc(pgr2->isiz*pgr2->jsiz*sizeof(gadouble),"area2");
    if (area2==NULL) {
      gaprnt (0,"Error from LTERP: Unable to allocate memory for area2\n"); error=1; goto err;
    }
    for (j=0; j<pgr2->jsiz; j++) {
      alo = *(y2b+j);
      ahi = *(y2b+j+1);
      if (alo < -90.0) alo = -90.0; if (alo > 90.0) alo = 90.0;
      if (ahi < -90.0) ahi = -90.0; if (ahi > 90.0) ahi = 90.0;
      if (opt==1)
	w1 = fabs(sin(ahi*rad)-sin(alo*rad));  /* length is weighted by latitude (aave) */
      else
	w1 = fabs((ahi-alo)*rad);              /* length is not weighted by latitude (amean) */
      for (i=0; i<pgr2->isiz; i++) {
	ij = j*pgr2->isiz+i;
	alo = *(x2b+i);
	ahi = *(x2b+i+1);
	w2 = fabs((ahi-alo)*rad);              /* w2 is the width of grid box  */
	*(area2+ij) =  w1 * w2;
      }
    }
  }

  /* Finally, we're ready to perform the interpolation */
  if (opt==1 || opt==2) {
    /* box averaging, based on doaave, regrid2.f and re.c */

    /* loop over output grid points */
    for (j=0; j<pgr2->jsiz; j++) {
      for (i=0; i<pgr2->isiz; i++) {
	ij = j*pgr2->isiz+i;
	if (*(area2+ij) > 0) {
	  /* whole integer values of gxout and gyout correspond to the boundaries of input grid boxes */
	  ib = (int)*(gxout+i);
	  ie = (int)*(gxout+i+1);
	  jb = (int)*(gyout+j);
	  je = (int)*(gyout+j+1);
	  /* allocate memory for group of input grid boxes within a single output grid box */
	  maxgrid = (ie-ib+1)*(je-jb+1);
	  area_box = (gadouble *)galloc(sizeof(gadouble)*maxgrid,"area_box");
	  fld_box  = (gadouble *)galloc(sizeof(gadouble)*maxgrid,"fld_box");
	  if (area_box==NULL || fld_box==NULL) {
	    gaprnt (0,"Error from LTERP: memory allocation error for box_averaging \n");
	    error=1; goto err;
	  }
	  /* Now loop over all input grid boxes that lie within the output grid box.
	     Figure out area of of each, whether whole or partial.
	     Start with the length along the latitude axis.
	     If input grid box is partially inside output grid box, we need to recalculate w1 */
	  icnt=0;   /* initialize the counter for input grid boxes that are within output grid box */
	  for (jj=jb; jj<=je; jj++) {
	    pygbflg=0;                 /* not a partial grid box in y */
	    if (jb!=je && jj==jb) {
	      /* more than one in y range, and this grid box is at bottom of y range */
	      /* re-calculate length of partial grid box */
	      pygbflg=1;
	      alo = *(y2b+j);
	      ahi = *(y1b+jb+1);
	      if (alo < -90.0) alo = -90.0; if (alo > 90.0) alo = 90.0;
	      if (ahi < -90.0) ahi = -90.0; if (ahi > 90.0) ahi = 90.0;
	      /* don't let alo be less than min lat of source grid */
	      if (jb==0 && alo<*(y1b)) alo = *(y1b);
	      if (opt==1)
		w1 = fabs(sin(ahi*rad)-sin(alo*rad));
	      else
		w1 = fabs((ahi-alo)*rad);
	    }
	    else if (je!=jb && jj==je) {
	      /* more than one in y range, and this grid box is at top of y range */
	      /* re-calculate length of partial grid box */
	      pygbflg=1;
	      alo = *(y1b+je);
	      ahi = *(y2b+j+1);
	      if (alo < -90.0) alo = -90.0; if (alo > 90.0) alo = 90.0;
	      if (ahi < -90.0) ahi = -90.0; if (ahi > 90.0) ahi = 90.0;
	      /* don't let ahi be more than max lat of source grid */
	      if (je==pgr1->jsiz && ahi>*(y1b+je)) ahi=*(y1b+je);
	      if (opt==1)
		w1 = fabs(sin(ahi*rad)-sin(alo*rad));
	      else
		w1 = fabs((ahi-alo)*rad);
	    }
	    else {
	      /* only one box in y range, or this box is in the middle of the y range.
		 either way, we will use entire length of output grid box (pygblfg is still 0) */
	      /* get length anyway in case we have a partial overlap in the x dimension */
	      alo = *(y1b+jj);
	      ahi = *(y1b+jj+1);
	      if (alo < -90.0) alo = -90.0; if (alo > 90.0) alo = 90.0;
	      if (ahi < -90.0) ahi = -90.0; if (ahi > 90.0) ahi = 90.0;
	      if (opt==1)
		w1 = fabs(sin(ahi*rad)-sin(alo*rad));
	      else
		w1 = fabs((ahi-alo)*rad);
	    }
	    /* Now get the width along the longitude axis.
	       If input grid box is partially inside the output grid box, we need to recalculate w2 */
	    for (ii=ib; ii<=ie; ii++) {
	      pxgbflg=0;                 /* not a partial grid box in x */
	      if (ib!=ie && ii==ib) {
		/* more than one in x range, and this grid box is at left edge of range */
		pxgbflg=1;
		alo = *(x2b+i);
		ahi = *(x1b+ib+1);
		/* don't let alo be less than min lon of source grid */
		if (ib==0 && alo<*(x1b)) alo = *(x1b);
		w2 = fabs((ahi-alo)*rad);
	      }
	      else if (ie!=ib && ii==ie) {
		/* more than one in x range, and this grid box is at right edge of range */
		pxgbflg=1;
		alo = *(x1b+ie);
		ahi = *(x2b+i+1);
		/* don't let ahi be more than max lon of source grid */
		if (ie==pgr1->isiz && ahi>*(x1b+ie)) ahi=*(x1b+ie);
		w2 = fabs((ahi-alo)*rad);
	      }
	      else {
		/* only one box in x range, or this box is in the middle of the x range.
		   either way, we'll use entire width of output grid box (pxgbflg is 0) */
		if (pygbflg) {
		  /* get width because we have a partial overlap in the y dimension */
		  alo = *(x1b+ii);
		  ahi = *(x1b+ii+1);
		  w2 = fabs((ahi-alo)*rad);
		}
	      }
	      /* Set the area and data value for each non-missing input grid box in range */
	      indx = jj*pgr1->isiz+ii;
	      if ((*(gr1u+indx)==1) && (*(area1+indx)>0)) {
		if (pygbflg || pxgbflg) {
		  /* recalculate area of partial grid box, as long as length and width are non-zero */
		  if (w1>0 && w2>0) {
		    *(area_box+icnt) = w1 * w2;
		    *(fld_box+icnt)  = *(gr1+indx);
		    icnt++;
		  }
		}
		else {
		  /* the entire input grid box is within output grid, use area1 (already calculated) */
		  *(area_box+icnt) = *(area1+indx);
		  *(fld_box+icnt)  = *(gr1+indx);
		  icnt++;
		}
	      }
	      if (icnt > maxgrid) {
		gaprnt (0,"Error from LTERP: icnt>maxgrid \n");
		error=1; goto err;
	      }
	    }
	  }
	  /* Now integrate over all the input boxes within the output box */
	  tot_fld = 0.0;
	  tot_area = 0.0;
	  for (ii=0; ii<icnt; ii++) {
	    tot_fld  = tot_fld + (*(fld_box+ii) * *(area_box+ii));
	    tot_area = tot_area + *(area_box+ii);
	  }
	  /* Calculate the percentage of the output grid box area that contains valid data */
	  pct = 100*(tot_area/(*(area2+ij)));
	  if (pct>minpct) {
	    *(gr2u+ij) = 1;
	    *(gr2+ij) = tot_fld/tot_area;
	  }
	  else {
	    *(gr2u+ij) = 0;
	  }
	  /* release memory */
	    if (fld_box)  gree(fld_box,"399a");
	    if (area_box) gree(area_box,"399b");
	    fld_box = area_box = NULL;
	} /* matches if *(area2+ij)>0 */
      }
    }
  } /* matches if (opt==1 || opt==2) (for box averaging) */

  else {
    /* bilinear interpolation based on regrid2.f and re.c */
    /* bilinear/bessel interpolation based on the FNOC routine bssl5 by D. Hensen, FNOC */

    /* 1D */
    if (pgr1->jdim<0) {
      /* loop over all points in destination output grid */
      for (i=0; i<pgr2->isiz; i++) {
	/* check if this destination grid point is outside the source grid */
	if (gxout[i]<0) {
	  *(gr2u+i) = 0;
	}
	else {
	  /* ic is index values for nearest input source grid point */
	  ic = (int) gxout[i];
	  r = gxout[i] - ic;
	  /* if we're at the edge of the grid, and endpoints of both grids have the same value,
	     we'll use the endpoints in the result grid -- tweak ic, and r to enable this. */
	  if ((ic==pgr1->isiz-1) && x1[ic]==x2[i]) {
	    ic--;
	    r=1;
	  }
	  /* make sure surrounding points are valid */
	  if (*(gr1u+ic) == 1 && *(gr1u+ic+1) == 1) {
	    *(gr2u+i) = 1;
	    *(gr2+i) = *(gr1+ic)*(1-r) + *(gr1+ic+1)*r;
	  }
	  else {
	    *(gr2u+i) = 0;
	  }
	}
      }
    }
    /* 2D */
    else {
      /* loop over all points in destination output grid */
      for (j=0; j<pgr2->jsiz; j++) {
	for (i=0; i<pgr2->isiz; i++) {
	  ij2=i+j*pgr2->isiz;
	  /* check if this destination grid point is outside the source grid */
	  if (gxout[i]<0 || gyout[j]<0) {
	    *(gr2u+ij2) = 0;
	  }
	  else {
	    /* ic and jc are index values for input source grid point at bottom left of 4 nearest neighbors */
	    ic = (int) gxout[i];
	    jc = (int) gyout[j];
	    r = gxout[i] - ic;
	    s = gyout[j] - jc;
	    /* if we're at the top/right edge of the grid, and endpoints of both grids have the same value,
	       we'll use the endpoints in the result grid -- tweak ic, jc, r, and s to enable this. */
	    if ((ic==pgr1->isiz-1) && x1[ic]==x2[i]) {
	      ic--;
	      r=1;
	    }
	    if ((jc==pgr1->jsiz-1) && y1[jc]==y2[j]) {
	      jc--;
	      s=1;
	    }
	    ij1=ic+jc*pgr1->isiz;
	    /* make sure surrounding points are valid */
	    if (*(gr1u+ij1) == 1 &&
		*(gr1u+ij1+1) == 1 &&
		*(gr1u+ij1+pgr1->isiz) == 1 &&
		*(gr1u+ij1+pgr1->isiz+1) == 1) {
	      *(gr2u+ij2) = 1;
	      *(gr2+ij2) = (1-s)*(*(gr1+ij1)*(1-r) + *(gr1+ij1+1)*r) +
		s*(*(gr1+ij1+pgr1->isiz)*(1-r) + *(gr1+ij1+pgr1->isiz+1)*r);
	    }
	    else {
	      *(gr2u+ij2) = 0;
	    }

	    if (bessel==1) {
	      /* refine interpolation with bessel algorithm */
	      /* interpolate 4 columns (i-1,i,i+1,i+2) to j+s and store in fr[0] through fr[3]*/
	      r1 = r - 0.5;
	      r2 = r * (r-1) * 0.5;
	      r3 = r1 * r2 * (1/3);
	      s1 = s - 0.5;
	      s2 = s * (s-1) * 0.5;
	      s3 = s1 * s2 * (1/3);
	      k = 0;
	      flag=1;
	      for (ii=ic-1; ii<=ic+2; ii++) {
		/* make sure we're far enough away from the grid edges */
		if (ic==0 || jc==0) { flag=0; break; }
		if (ic>pgr1->isiz-3 || jc>pgr1->jsiz-3) { flag=0; break; }

		/* set up indices and values for secondary ring -- the nearest 16 grid points */
		jcp1 = jc+1;
		jcp2 = jc+2;
		jcm1 = jc-1;

		fijm1 = *(gr1+ii + jcm1 * pgr1->isiz);
		fij   = *(gr1+ii + jc   * pgr1->isiz);
		fijp1 = *(gr1+ii + jcp1 * pgr1->isiz);
		fijp2 = *(gr1+ii + jcp2 * pgr1->isiz);

		uijm1 = *(gr1u+ii + jcm1 * pgr1->isiz);
		uij   = *(gr1u+ii + jc   * pgr1->isiz);
		uijp1 = *(gr1u+ii + jcp1 * pgr1->isiz);
		uijp2 = *(gr1u+ii + jcp2 * pgr1->isiz);

		/* exit if any value undefined */
		if (uijm1 == 0 || uij == 0 || uijp1 == 0 || uijp2 == 0) { flag=0; break; }
		u = (fij + fijp1) * 0.5;
		del = fijp1 - fij;
		del2 = (fijp2 - fijp1 + fijm1 - fij) * 0.5;
		del3 = fijp2 - fijp1 - del * 2 + fij - fijm1;
		fr[k] = u + s1*del + s2*del2 + s3*del3;
		k++;
	      }

	      /* interpolate the fr row to ii+r */
	      if (flag) {
		u = (fr[1] + fr[2]) * 0.5;
		del = fr[2] - fr[1];
		del2 = (fr[3] - fr[2] + fr[0] - fr[1]) * 0.5;
		del3 = fr[3] - fr[2] - del*2 + fr[1] - fr[0];
		*(gr2+ij2) = u + r1*del + r2*del2 + r3*del3;
	      }
	    }
	  }
	}
      }
    }
  }

/* Alternaive bilinear interpolation from Brian Doty's lterp code */
/*     /\* see which way x y vary with i*\/ */
/*     if (pgr2->ilinr == 0) { */
/*       idir2 = -1; */
/*       if (*(x2+1) > *x2) idir2 = 1; */
/*     } else { */
/*       idir2 = 1; */
/*     } */
/*     if (pgr2->jlinr == 0) { */
/*       jdir2 = -1; */
/*       if (*(y2+1) > *y2) jdir2 = 1; */
/*     } else { */
/*       jdir2 = 1; */
/*     } */
/*     if (pgr1->jdim<0) { */
/*       /\* 1D *\/ */
/*       i1 = -1; */
/*       for (i2=0; i2<pgr2->isiz; i2++) { */
/* 	if (idir2 == 1) { */
/* 	  while (i1<pgr1->isiz-1 && *(x1+i1+1)<=*(x2+i2)) i1++; */
/* 	} else { */
/* 	  while (i1<pgr1->isiz-1 && *(x1+i1+1)>=*(x2+i2)) i1++; */
/* 	}  */
/* 	if (i1==pgr1->isiz-1 && *(x1+i1)==*(x2+i2)) i1--; */
/* 	if (i1<0 || i1>pgr1->isiz-2 || *(gr1u+i1)==0 || *(gr1u+i1+1)==0) { */
/* 	  *(gr2u+i2) = 0; */
/* 	} */
/* 	else { */
/* 	  xd = *(x1+i1+1) - *(x1+i1); */
/* 	  rd = *(gr1+i1+1) - *(gr1+i1); */
/* 	  *(gr2+i2) = *(gr1+i1)+(*(x2+i2)-*(x1+i1))*rd/xd; */
/* 	  *(gr2u+i2) = 1; */
/* 	} */
/*       } */
/*     } else { */
/*       /\* 2D *\/ */
/*       j1 = -1; */
/*       for (j2=0; j2<pgr2->jsiz; j2++) { */
/* 	if (jdir2 == 1) { */
/* 	  while (j1<pgr1->jsiz-1 && *(y1+j1+1)<=*(y2+j2)) j1++; */
/* 	} else { */
/* 	  while (j1<pgr1->jsiz-1 && *(y1+j1+1)>=*(y2+j2)) j1++; */
/* 	} */
/* 	if (j1==pgr1->jsiz-1 && *(y1+j1)==*(y2+j2)) j1--; */
/* 	i1 = -1; */
/* 	for (i2=0; i2<pgr2->isiz; i2++) { */
/* 	  if(idir2 == 1) { */
/* 	    while (i1<pgr1->isiz-1 && *(x1+i1+1)<=*(x2+i2)) i1++; */
/* 	  } else { */
/* 	    while (i1<pgr1->isiz-1 && *(x1+i1+1)>=*(x2+i2)) i1++; */
/* 	  }  */
/* 	  if (i1==pgr1->isiz-1 && *(x1+i1)==*(x2+i2)) i1--; */
/* 	  ij1 = j1*pgr1->isiz+i1; */
/* 	  ij2 = j2*pgr2->isiz+i2; */
/* 	  if ( i1<0 || i1>pgr1->isiz-2 ||  */
/* 	       j1<0 || j1>pgr1->jsiz-2 || */
/* 	       *(gr1u+ij1)==0 ||           */
/* 	       *(gr1u+ij1+1)==0 || */
/* 	       *(gr1u+ij1+pgr1->isiz)==0 ||  */
/* 	       *(gr1u+ij1+pgr1->isiz+1)==0 ) { */
/* 	    *(gr2u+ij2) = 0; */
/* 	  } */
/* 	  else { */
/* 	    yd = *(y1+j1+1) - *(y1+j1); */
/* 	    rd = *(gr1+ij1+pgr1->isiz) - *(gr1+ij1); */
/* 	    t1 = *(gr1+ij1) + (*(y2+j2)-*(y1+j1))*rd/yd; */
/* 	    rd = *(gr1+ij1+pgr1->isiz+1) - *(gr1+ij1+1); */
/* 	    t2 = *(gr1+ij1+1) + (*(y2+j2)-*(y1+j1))*rd/yd; */
/* 	    xd = *(x1+i1+1) - *(x1+i1); */
/* 	    *(gr2+ij2) = t1 + (*(x2+i2)-*(x1+i1))*(t2-t1)/xd; */
/* 	    *(gr2u+ij2) = 1; */
/* 	  } */
/* 	} */
/*       } */
/*     } */

err:

  /* release memory */
  gafree (&pst2);
  if (error) if (pst!=NULL) gafree (pst);
  if (x1!=NULL) gree(x1,"f400");
  if (x2!=NULL) gree(x2,"f401");
  if (y1!=NULL) gree(y1,"f402");
  if (y2!=NULL) gree(y2,"f403");
  if (x1b!=NULL) gree(x1b,"f400b");
  if (x2b!=NULL) gree(x2b,"f401b");
  if (y1b!=NULL) gree(y1b,"f402b");
  if (y2b!=NULL) gree(y2b,"f403b");
  if (gxout!=NULL) gree(gxout,"f400c");
  if (gyout!=NULL) gree(gyout,"f401c");
  if (area1!=NULL) gree(area1,"f402b");
  if (area2!=NULL) gree(area2,"f403b");
  if (fld_box!=NULL) gree(fld_box,"399a");
  if (area_box!=NULL) gree(area_box,"399b");
  if (error)
    return 1;
  else
    return 0;
}

/* Perform a two-operand operation which may have both grid or
   stn data types involved.  */

gaint gafopr (struct gastat *pst1, struct gastat *pst2, gaint op) {
struct gagrid *pgr;
struct gastn *stn;

  /* Check for grid-grid operation */
  pgr=NULL;
  if (pst1->type == 1 && pst2->type==1) {
    pgr = gagrop(pst1->result.pgr, pst2->result.pgr, op, 1);
    if (pgr==NULL) return (1);
    pst1->type = 1;
    pst1->result.pgr = pgr;
    return (0);
  }

  /* If both stns, do stn-stn operation */
  if (pst1->type==0 && pst2->type==0 ) {
    stn = gastop(pst1->result.stn, pst2->result.stn, op, 1);
    if (stn==NULL) return (1);
    pst1->type = 0;
    pst1->result.stn = stn;
    return (0);
  }

  /* Operation between grid and stn is invalid -- unless the grid
     is really a constant.  Check for this.  */
  if (pst1->type == 1) pgr=pst1->result.pgr;
  if (pst2->type == 1) pgr=pst2->result.pgr;
  if (pgr->idim == -1 && pgr->jdim == -1) {
    if (pst1->type == 0) {
      stn = gascop (pst1->result.stn, pgr->rmin, op, 0);
    } else {
      stn = gascop (pst2->result.stn, pgr->rmin, op, 1);
    }
    if (stn==NULL) return (1);
    gagfre (pgr);
    pst1->type = 0;
    pst1->result.stn = stn;
  } else {
    gaprnt (0,"Operation Error: Incompatable Data Types\n");
    gaprnt (0,"  One operand was stn data, other was grid\n");
    return (1);
  }
  return (0);
}

char *aavenam[5] = {"AAVE","AMEAN","ASUM","ASUMG","ATOT"};

gaint ffaav (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = aave (pfc, pst, 1);
   return (rc);
}

gaint ffamn (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = aave (pfc, pst, 2);
   return (rc);
}

gaint ffasum (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = aave (pfc, pst, 3);
   return (rc);
}

gaint ffasumg (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = aave (pfc, pst, 4);
   return (rc);
}

gaint ffatot (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = aave (pfc, pst, 5);
   return (rc);
}

/* Bare defined variables can be reduced through a private read-only view.
   The view is never returned to the evaluator, whose operators mutate grids. */
static struct gafile *reduction_defined(char *expression, struct gastat *pst) {
  char name[17],*p=expression;
  gaint n=0;
  while (*p==' ') p++;
  while ((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='_') {
    if (n==16) return NULL;
    name[n++]=*p++;
  }
  name[n]=0; while (*p==' ') p++;
  return n && !*p ? getdfn(name,pst) : NULL;
}

static char doaave_stride(struct gagrid *, gadouble, gadouble, gadouble, gadouble,
                           gaint, size_t, gadouble *);
static gaint aave_defined(char *,struct gastat *,gaint,gadouble *,char *);

gaint aave (struct gafunc *pfc, struct gastat *pst, int sel) {
gadouble (*iconv) (gadouble *, gadouble);
gadouble (*jconv) (gadouble *, gadouble);
struct gagrid *pgr;
struct gafile *pfi;
gadouble res=-999,x1,x2,y1,y2;
gaint dim,wflag,rc,gflag=0;
char *ch,*fnam,resu;

  fnam = aavenam[sel-1];

  /* Check for valid number of args       */
  if (pfc->argnum==2 && !strncmp(pfc->argpnt[1],"global",1)) gflag=1;
  if (pfc->argnum!=5 && !gflag) {
    snprintf(pout,1255,"Error from %s:  Too many or too few args\n",fnam);
    gaprnt(0,pout);
    gaprnt (0,"                  5 arguments expected \n");
    return (1);
  }

  /* Check environment.  Z or T or E can't vary.  */
  if (pst->idim>1 || pst->jdim>1) {
    snprintf(pout,1255,"Error from %s  Invalid environment.  ",fnam);
    gaprnt(0,pout);
    gaprnt (0,"Z, T, or E can't vary.\n");
    return (1);
  }

  /* Parse the dimension expressions */
  pfi = pst->pfid;
  if (gflag) {
    ch = dimprs ("lon=0", pst, pfi, &dim, &x1, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x1 = x1 - 0.5;
    ch = dimprs ("lon=360", pst, pfi, &dim, &x2, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x2 = x2 + 0.5;
    ch = dimprs ("lat=-90", pst, pfi, &dim, &y1, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y1 = y1 - 0.5;
    ch = dimprs ("lat=90", pst, pfi, &dim, &y2, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y2 = y2 + 0.5;
  } else {
    pfi = pst->pfid;
    ch = dimprs (pfc->argpnt[1], pst, pfi, &dim, &x1, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x1 = x1 - 0.5;
    ch = dimprs (pfc->argpnt[2], pst, pfi, &dim, &x2, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x2 = x2 + 0.5;
    ch = dimprs (pfc->argpnt[3], pst, pfi, &dim, &y1, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y1 = y1 - 0.5;
    ch = dimprs (pfc->argpnt[4], pst, pfi, &dim, &y2, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y2 = y2 + 0.5;
  }

  /* Set up pst block properly to get the grid */
  iconv = pfi->gr2ab[0];
  pst->dmin[0] = iconv(pfi->grvals[0],x1);
  pst->dmax[0] = iconv(pfi->grvals[0],x2);
  jconv = pfi->gr2ab[1];
  pst->dmin[1] = jconv(pfi->grvals[1],y1);
  pst->dmax[1] = jconv(pfi->grvals[1],y2);

  pst->idim = 0;
  pst->jdim = 1;

  /* Defined arrays stay read-only and do not need an intermediate XY copy. */
  if (!aave_defined(pfc->argpnt[0],pst,sel,&res,&resu)) goto area_result;

  /* Get the grid */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gafree (pst);
    return (-1);
  }
  pgr = pst->result.pgr;

  /* Average over the grid  */
  resu = doaave(pgr,pst->dmin[0],pst->dmax[0],pst->dmin[1],pst->dmax[1],sel,&res);
  gafree (pst);
area_result:
  pgr = gagrvl(res);
  pgr->umin = resu;
  pst->type = 1;
  pst->result.pgr = pgr;
  return (0);

err1:
  snprintf(pout,1255,"Error from %s:  Invalid dimension expression \n",fnam);
  gaprnt(0,pout);
  return (1);
}

char *mnmxnam[6] = {"AMIN","AMAX","AMINLOCX","AMINLOCY","AMAXLOCX", "AMAXLOCY"};

gaint ffamin (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = mnmx (pfc, pst, 1);
   return (rc);
}

gaint ffamax (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = mnmx (pfc, pst, 2);
   return (rc);
}

gaint ffaminlocx (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = mnmx (pfc, pst, 3);
   return (rc);
}

gaint ffaminlocy (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = mnmx (pfc, pst, 4);
   return (rc);
}

gaint ffamaxlocx (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = mnmx (pfc, pst, 5);
   return (rc);
}

gaint ffamaxlocy (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = mnmx (pfc, pst, 6);
   return (rc);
}

gaint mnmx (struct gafunc *pfc, struct gastat *pst, int sel) {
  gadouble (*iconv) (gadouble *, gadouble);
  gadouble (*jconv) (gadouble *, gadouble);
  struct gagrid *pgr;
  struct gafile *pfi;
  gadouble res=-999,x1,x2,y1,y2,min,max,minx,maxx,miny,maxy;
  gaint dim,wflag,rc,gflag=0,i,j,x,y,cnt;
  char *ch,*fnam,resu;
  gadouble *gr;
  char *gru;

  fnam = mnmxnam[sel-1];

  /* Check for valid number of args       */
  if (pfc->argnum==2 && !strncmp(pfc->argpnt[1],"global",1)) gflag=1;
  if (pfc->argnum!=5 && !gflag) {
    snprintf(pout,1255,"Error from %s:  Too many or too few args\n",fnam);
    gaprnt(0,pout);
    gaprnt (0,"                  5 arguments expected \n");
    return (1);
  }

  /* Check environment.  Z or T or E can't vary.  */
  if (pst->idim>1 || pst->jdim>1) {
    snprintf(pout,1255,"Error from %s  Invalid environment.  ",fnam);
    gaprnt(0,pout);
    gaprnt (0,"Z, T, or E can't vary.\n");
    return (1);
  }

  /* Parse the dimension expressions */
  pfi = pst->pfid;
  if (gflag) {
    ch = dimprs ("lon=0", pst, pfi, &dim, &x1, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x1 = x1 - 0.5;
    ch = dimprs ("lon=360", pst, pfi, &dim, &x2, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x2 = x2 + 0.5;
    ch = dimprs ("lat=-90", pst, pfi, &dim, &y1, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y1 = y1 - 0.5;
    ch = dimprs ("lat=90", pst, pfi, &dim, &y2, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y2 = y2 + 0.5;
  } else {
    pfi = pst->pfid;
    ch = dimprs (pfc->argpnt[1], pst, pfi, &dim, &x1, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x1 = x1 - 0.5;
    ch = dimprs (pfc->argpnt[2], pst, pfi, &dim, &x2, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag && sel!=4) x2 = x2 + 0.5;
    ch = dimprs (pfc->argpnt[3], pst, pfi, &dim, &y1, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y1 = y1 - 0.5;
    ch = dimprs (pfc->argpnt[4], pst, pfi, &dim, &y2, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag && sel!=4) y2 = y2 + 0.5;
  }

  /* Set up pst block properly to get the grid */
  iconv = pfi->gr2ab[0];
  pst->dmin[0] = iconv(pfi->grvals[0],x1);
  pst->dmax[0] = iconv(pfi->grvals[0],x2);
  jconv = pfi->gr2ab[1];
  pst->dmin[1] = jconv(pfi->grvals[1],y1);
  pst->dmax[1] = jconv(pfi->grvals[1],y2);

  pst->idim = 0;
  pst->jdim = 1;

  /* Get the grid */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gafree (pst);
    return (-1);
  }
  pgr = pst->result.pgr;

  /* Get the area min/max and its location */
  min =  9.99e35;
  max = -9.99e35;
  minx = maxx = miny = maxy = -1;
  gr  = pgr->grid;
  gru = pgr->umask;
  cnt = 0;
  for (j=0; j<pgr->jsiz; j++) {
    y = (gadouble)(j+pgr->dimmin[1]);
    for (i=0; i<pgr->isiz; i++) {
      x = (gadouble)(i+pgr->dimmin[0]);
      if (*gru == 1) {
	cnt++;
	if (cnt==1 || min>*gr) { min = *gr; minx = x; miny = y; }   /* any size */
	if (cnt==1 || max<*gr) { max = *gr; maxx = x; maxy = y; }
      }
      gr++; gru++;
    }
  }
  if (cnt==0) {
    resu = 0;
    res = pgr->undef;
  }
  else {
    resu = 1;
    if      (sel==1) res = min;
    else if (sel==2) res = max;
    else if (sel==3) res = minx;
    else if (sel==4) res = miny;
    else if (sel==5) res = maxx;
    else if (sel==6) res = maxy;
  }

  gafree (pst);
  pgr = gagrvl(res);
  pgr->umin = resu;
  pst->type = 1;
  pst->result.pgr = pgr;
  return (0);

err1:
  snprintf(pout,1255,"Error from %s:  Invalid dimension expression \n",fnam);
  gaprnt(0,pout);
  return (1);
}



gaint ffscor (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = scorr (pfc, pst, 1);
   return (rc);
}

gaint ffsreg (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
  rc = scorr (pfc,pst,2);
  return (rc);
}

gaint scorr (struct gafunc *pfc, struct gastat *pst, gaint sel) {
gadouble (*iconv) (gadouble *, gadouble);
gadouble (*jconv) (gadouble *, gadouble);
struct gagrid *pgr,*pgr2;
struct gafile *pfi;
struct gastat pst2;
gadouble *gr1, *gr2, *gr3, *grid3;
gadouble res,x1,x2,y1,y2,mn1,mn2,s1,s2,ss,cov;
gaint i,dim,wflag,rc,cnt,gflag=0;
char *ch,*gr1u,*gr2u,*grid3u,*gr3u,s1u,s2u,mn1u,mn2u,resu,covu;
size_t sz;

   mn1=mn2=s1=s2=cov=ss=res=0;

  /* Check for valid number of args       */
  if (pfc->argnum==3 && !strncmp(pfc->argpnt[2],"global",1)) gflag=1;
  if (pfc->argnum!=6 && !gflag) {
    gaprnt (0,"Error from SCORR:  Too many or too few args \n");
    gaprnt (0,"                   6 arguments expected \n");
    return (1);
  }

  /* Check environment.  Z or T or E can't vary.  */
  if (pst->idim>1 || pst->jdim>1) {
    gaprnt (0,"Error from SCORR:  Invalid environment.  ");
    gaprnt (0,"Z, T, or E cannot vary.\n");
    return (1);
  }

  /* Parse the dimension expressions */
  pfi = pst->pfid;
  if (gflag) {
    ch = dimprs ("lon=0", pst, pfi, &dim, &x1, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag) x1 = x1 - 0.5;
    ch = dimprs ("lon=360", pst, pfi, &dim, &x2, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag) x2 = x2 + 0.5;
    ch = dimprs ("lat=-90", pst, pfi, &dim, &y1, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag) y1 = y1 - 0.5;
    ch = dimprs ("lat=90", pst, pfi, &dim, &y2, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag) y2 = y2 + 0.5;
  } else {
    ch = dimprs (pfc->argpnt[2], pst, pfi, &dim, &x1, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag) x1 = x1 - 0.5;
    ch = dimprs (pfc->argpnt[3], pst, pfi, &dim, &x2, 1, &wflag);
    if (ch==NULL || dim!=0) goto err1;
    if (!wflag) x2 = x2 + 0.5;
    ch = dimprs (pfc->argpnt[4], pst, pfi, &dim, &y1, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag) y1 = y1 - 0.5;
    ch = dimprs (pfc->argpnt[5], pst, pfi, &dim, &y2, 1, &wflag);
    if (ch==NULL || dim!=1) goto err1;
    if (!wflag) y2 = y2 + 0.5;
  }

  /* Set up pst block properly to get the grids */
  iconv = pfi->gr2ab[0];
  pst->dmin[0] = iconv(pfi->grvals[0],x1);
  pst->dmax[0] = iconv(pfi->grvals[0],x2);
  jconv = pfi->gr2ab[1];
  pst->dmin[1] = jconv(pfi->grvals[1],y1);
  pst->dmax[1] = jconv(pfi->grvals[1],y2);
  pst->idim = 0;
  pst->jdim = 1;
  pst2 = *pst;

  /* Get the first grid */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gafree (pst);
    return (-1);
  }
  pgr = pst->result.pgr;

  /* Get the 2nd grid */
  rc = gaexpr(pfc->argpnt[1],&pst2);
  if (rc) return (rc);
  if (pst2.type!=1) {
    gafree (&pst2);
    gafree (pst);
    return (-1);
  }
  pgr2 = pst2.result.pgr;

  /* Verify that the grids are compatible for operations */
  if (gagchk(pgr,pgr2,0) || gagchk(pgr,pgr2,1)) {
    gaprnt (0,"Error from SCORR:  Incompatable grids\n");
    gafree (&pst2);
    gafree (pst);
    return (1);
  }

  /* Force missing data values to be reflected in each grid.  */
  cnt = pgr->isiz * pgr->jsiz;
  gr1 = pgr->grid;
  gr2 = pgr2->grid;
  gr1u = pgr->umask;
  gr2u = pgr2->umask;
  for (i=0; i<cnt; i++) {
    if (*gr1u==0 || *gr2u==0) {
      *gr1u = 0;
      *gr2u = 0;
    }
    gr1++;  gr2++;
    gr1u++; gr2u++;
  }

  /* Obtain areal average over each grid */
  mn1u = doaave(pgr, pst->dmin[0],pst->dmax[0],pst->dmin[1],pst->dmax[1],1,&mn1);
  mn2u = doaave(pgr2,pst->dmin[0],pst->dmax[0],pst->dmin[1],pst->dmax[1],1,&mn2);

  /* result from doaave was undefined */
  if (mn1u==0 || mn2u==0) {
    resu = 0;
  }
  else {

    /* Remove the mean from the fields */
    gr1 = pgr->grid;
    gr2 = pgr2->grid;
    gr1u = pgr->umask;
    gr2u = pgr2->umask;
    for (i=0; i<cnt; i++) {
      if (*gr1u!=0) *gr1 = *gr1 - mn1;
      if (*gr2u!=0) *gr2 = *gr2 - mn2;
      gr1++;  gr2++;
      gr1u++; gr2u++;
    }

    /* Get gr1 * gr2; stash in a safe place.  Also get the squares of each variable. */
    sz = sizeof(gadouble)*cnt;
    grid3 = (gadouble *)galloc(sz,"scorr3");
    if (grid3==NULL) {
      gafree (pst);
      gafree (&pst2);
      gaprnt (0,"Error from SCORR:  Memory Allocation \n");
      return (1);
    }
    sz = sizeof(char)*cnt;
    grid3u = (char *)galloc(sz,"scorr3u");
    if (grid3u==NULL) {
      if (grid3) gree(grid3,"f408");
      gafree (pst);
      gafree (&pst2);
      gaprnt (0,"Error from SCORR:  Memory Allocation \n");
      return (1);
    }
    gr1 = pgr->grid;
    gr2 = pgr2->grid;
    gr3 = grid3;
    gr1u = pgr->umask;
    gr2u = pgr2->umask;
    gr3u = grid3u;
    for (i=0; i<cnt; i++) {
      if (*gr1u!=0 && *gr2u!=0) {
	*gr3 = *gr1 * *gr2;
	*gr3u = 1;
      }
      else {
	*gr3u = 0;
      }
      if (*gr1u!=0) *gr1 = *gr1 * *gr1;
      if (*gr2u!=0) *gr2 = *gr2 * *gr2;
      gr1++;  gr2++;  gr3++;
      gr1u++; gr2u++; gr3u++;
    }

    /* Get the areal average of the squares, then the
       areal average of gr1 * gr2, then the final result */
    /* hard wire to 1 for now as this is the default behaviour */
    s1u = doaave(pgr, pst->dmin[0],pst->dmax[0],pst->dmin[1],pst->dmax[1],1,&s1);
    s2u = doaave(pgr2,pst->dmin[0],pst->dmax[0],pst->dmin[1],pst->dmax[1],1,&s2);
    gr1 = pgr->grid;
    gr1u = pgr->umask;
    gr3 = grid3;
    gr3u = grid3u;
    for (i=0; i<cnt; i++) {
      if (*gr3u!=0) {
	*gr1 = *gr3;
	*gr1u = 1;
      }
      else {
	*gr1u=0;
      }
      gr1++;  gr3++;
      gr1u++; gr3u++;
    }
    covu = doaave(pgr,pst->dmin[0],pst->dmax[0],pst->dmin[1],pst->dmax[1],1,&cov);
    if (sel == 1) ss = sqrt(s1*s2);
    if (sel == 2) ss = sqrt(s1*s1);
    if (ss>0.0) {
      res = cov/ss;
      resu = 1;
    }
    else resu = 0;
    gree(grid3,"f408a");
    gree(grid3u,"f408c");
  }

  gafree (pst);
  gafree (&pst2);
  pgr = gagrvl(res);
  pgr->umin = resu;
  pst->type = 1;
  pst->result.pgr = pgr;
  return (0);

err1:
  gaprnt (0,"Error from SCORR:  Invalid dimension expression \n");
  return (1);
}

/* Function that actually does area average over a grid */

/* Shared coordinate weights for plane and streamed profile reductions. */
static void aave_weights(struct gagrid *pgr, gadouble dmin0, gadouble dmax0,
                          gadouble dmin1, gadouble dmax1, gaint sel,
                          gadouble *xw, gadouble *yw) {
  gadouble (*iconv)(gadouble *,gadouble)=pgr->igrab;
  gadouble (*jconv)(gadouble *,gadouble)=pgr->jgrab;
  gadouble *ivals=pgr->ivals,*jvals=pgr->jvals;
  gadouble x1,y1,alo,ahi,alen,w1,w2=0,d2r=M_PI/180.0;
  gaint i,j;
    for (i=0; i<pgr->isiz; i++) {
      x1 = (gadouble)(i+pgr->dimmin[0]);
      alo = iconv(ivals, x1-0.5);
      ahi = iconv(ivals, x1+0.5);
      alen=fabs(ahi-alo);
      if (alo < dmin0) alo = dmin0;
      if (alo > dmax0) alo = dmax0;
      if (ahi < dmin0) ahi = dmin0;
      if (ahi > dmax0) ahi = dmax0;
      if (sel==1 || sel==2) w2 = ahi-alo;
      else if (sel==5) w2 = d2r*(ahi-alo);
      else if (sel==3) {
        if (alen > FUZZ_SCALE) w2=fabs(ahi-alo)/alen;
        else w2=0.0;
      }
      else w2=1.0;
      xw[i] = w2;
    }
    for (j=0; j<pgr->jsiz; j++) {
      y1 = (gadouble)(j+pgr->dimmin[1]);
      alo = jconv(jvals, y1-0.5);
      ahi = jconv(jvals, y1+0.5);
      alen=fabs(ahi-alo);
      if (alo < dmin1) alo = dmin1;
      if (alo > dmax1) alo = dmax1;
      if (ahi < dmin1) ahi = dmin1;
      if (ahi > dmax1) ahi = dmax1;
      if (alo < -90.0) alo = -90.0; if (ahi < -90.0) ahi = -90.0;
      if (alo >  90.0) alo =  90.0; if (ahi >  90.0) ahi =  90.0;
      w1 = 1.0;
      if (sel==1 || sel==5) w1 = fabs(sin(ahi*d2r)-sin(alo*d2r));
      else if (sel==2) w1 = fabs(ahi-alo);
      else if (sel==3) {
        if (alen > FUZZ_SCALE) w1=fabs(ahi-alo)/alen;
        else w1=0.0;
      }
      yw[j] = w1;
    }

}

static void aave_row(struct gagrid *grid, size_t base, const gadouble *xw,
                       gadouble yw, gaint sel, gadouble *sum, gadouble *weight) {
  gadouble rowsum=0,rowwt=0,cellwt;
  gaint i;
#if USEOPENMP == 1
#pragma omp simd reduction(+:rowsum,rowwt) private(cellwt)
#endif
  for (i=0;i<grid->isiz;i++) {
    if (grid->umask[base+i]) {
      cellwt=xw[i]*yw;
      if (sel==4) rowsum+=grid->grid[base+i];
      else rowsum+=grid->grid[base+i]*cellwt;
      rowwt+=cellwt;
    }
  }
  *sum=rowsum; *weight=rowwt;
}

char doaave(struct gagrid *grid, gadouble x1, gadouble x2, gadouble y1,
              gadouble y2, gaint sel, gadouble *result) {
  return doaave_stride(grid,x1,x2,y1,y2,sel,(size_t)grid->isiz,result);
}

static char doaave_stride(struct gagrid *pgr, gadouble dmin0, gadouble dmax0,
                          gadouble dmin1, gadouble dmax1, gaint sel,
                          size_t stride, gadouble *result) {
gadouble (*iconv) (gadouble *, gadouble);
gadouble (*jconv) (gadouble *, gadouble);
gadouble *ivals, *jvals, *gr, *xw=NULL, *yw=NULL;
gadouble d2r,sum,w1,w2=0,y1,x1,abs,alo,ahi,alen,wt,cellwt;
gaint i,j,n,size;
char *gru,sumu=0;
size_t sz;

  d2r = M_PI/180.0;
  iconv = pgr->igrab;
  jconv = pgr->jgrab;
  ivals = pgr->ivals;
  jvals = pgr->jvals;
  sum = 0.0;
  wt = 0.0;
  gr  = pgr->grid;
  gru = pgr->umask;

  /* Coordinate weights are independent of the data values.  Calculate
     them once, then reduce the grid in parallel. */
  sz = pgr->isiz*sizeof(gadouble);
  xw = (gadouble *)galloc(sz,"aavexw");
  sz = pgr->jsiz*sizeof(gadouble);
  yw = (gadouble *)galloc(sz,"aaveyw");
  if (xw!=NULL && yw!=NULL) {
    aave_weights(pgr,dmin0,dmax0,dmin1,dmax1,sel,xw,yw);

    size = pgr->isiz*pgr->jsiz;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) reduction(+:sum,wt) private(i,cellwt) if(ga_omp_parallelize(size))
#endif
    for (j=0; j<pgr->jsiz; j++) {
      gadouble rowsum=0,rowwt=0;
      aave_row(pgr,(size_t)j*stride,xw,yw[j],sel,&rowsum,&rowwt);
      sum+=rowsum; wt+=rowwt;
    }
    gree(xw,"aavexw");
    gree(yw,"aaveyw");
    if (wt>0.0) {
      sumu = 1;
      if (sel<=2) sum = sum/wt;
    }
    else {
      sumu = 0;
      sum = pgr->undef;
    }
    *result = sum;
    return (sumu);
  }
  if (xw!=NULL) gree(xw,"aavexw");
  if (yw!=NULL) gree(yw,"aaveyw");

  for (j=0; j<pgr->jsiz; j++) {
    gr=pgr->grid+(size_t)j*stride; gru=pgr->umask+(size_t)j*stride;
    y1 = (gadouble)(j+pgr->dimmin[1]);
    abs = jconv(jvals, y1);
    alo = jconv(jvals, y1-0.5);
    ahi = jconv(jvals, y1+0.5);
    alen=fabs(ahi-alo);                /* length of the grid side in world coord */
    if (alo < dmin1) alo = dmin1;
    if (alo > dmax1) alo = dmax1;
    if (ahi < dmin1) ahi = dmin1;
    if (ahi > dmax1) ahi = dmax1;
    if (alo < -90.0) alo = -90.0; if (ahi < -90.0) ahi = -90.0;
    if (alo >  90.0) alo =  90.0; if (ahi >  90.0) ahi =  90.0;
    w1 = 1.0;
    if (sel==1 || sel==5) {
      w1 = fabs(sin(ahi*d2r)-sin(alo*d2r));  /* for aave and atot, area weighting by latitude */
    }
    else if (sel==2) {
      w1 = fabs(ahi-alo);           /* for amean, weight is length of interval in world coords */
    }
    else if (sel==3) {
      if (alen > FUZZ_SCALE) {    /* grid weighting (asum), weighted by length of interval in grid coords */
	w1=fabs(ahi-alo)/alen;
      }
      else {
	w1=0.0;
      }
    }
    for (i=0; i<pgr->isiz; i++) {
      x1 = (gadouble)(i+pgr->dimmin[0]);
      alo = iconv(ivals, x1-0.5);
      ahi = iconv(ivals, x1+0.5);
      alen=fabs(ahi-alo);
      if (alo < dmin0) alo = dmin0;
      if (alo > dmax0) alo = dmax0;
      if (ahi < dmin0) ahi = dmin0;
      if (ahi > dmax0) ahi = dmax0;

      if (sel==1 || sel==2) {
	w2 = ahi - alo;                      /* for aave and amean */
      }
      else if (sel==5) {
	w2 = d2r*(ahi - alo);                /* for atot */
      }
      else if (sel==3) {
	if (alen > FUZZ_SCALE) {             /* grid weighting (asum) */
	  w2=fabs(ahi-alo)/alen;
	}
	else {
	  w2=0.0;
	}
      }
      else if (sel==4) {
	w2=1.0;              /* no weighting (asumg) */
      }

      if (*gru != 0) {
	if (sel==4) {
	  sum = sum + *gr;    /* no weighting for asumg */
	}
	else {
	  sum = sum + (*gr * w1 * w2);  /* otherwise apply weights */
	}
        wt = wt + (w1 * w2);
      }
      gr++; gru++;
    }
  }
  if (wt>0.0) {
    sumu = 1;
    if (sel<=2 ) {
      sum = sum / wt;
    }
  }
  else {
    sumu = 0;
    sum = pgr->undef;
  }
  *result = sum;
  return (sumu);
}


/* Return 1 for an entirely missing view, -1 for a layout needing the
   evaluator, or 0 with a clipped XY view and the source's row stride. */
static gaint defined_area_view(struct gafile *file, struct gastat *pst,
                                struct gagrid *view, size_t *stride) {
  gaint d,lo[2],hi[2];
  gadouble v,a,b,nearest;
  size_t offset=0,multiplier=1,index[5];
  if (!file || file->type!=4 || file->climo || file->dnum[0]<2 || file->dnum[1]<2) return -1;
  for (d=0;d<2;d++) {
    a=floor(file->ab2gr[d](file->abvals[d],pst->dmin[d])+0.0001);
    b=ceil(file->ab2gr[d](file->abvals[d],pst->dmax[d])-0.0001);
    if (!isfinite(a) || !isfinite(b) || b<=a) return -1;
    if (a<file->dimoff[d]+1) a=file->dimoff[d]+1;
    if (b>file->dimoff[d]+file->dnum[d]) b=file->dimoff[d]+file->dnum[d];
    if (a>b) return 1;
    lo[d]=(gaint)a; hi[d]=(gaint)b;
    index[d]=(size_t)(lo[d]-file->dimoff[d]-1);
  }
  for (d=2;d<5;d++) {
    index[d]=0;
    if (file->dnum[d]==1) continue;
    v=d==3 ? t2gr(file->abvals[3],&pst->tmin) : file->ab2gr[d](file->abvals[d],pst->dmin[d]);
    nearest=round(v);
    if (!isfinite(v) || dequal(v,nearest,1e-8)) return -1;
    if (nearest<file->dimoff[d]+1 || nearest>file->dimoff[d]+file->dnum[d]) return 1;
    index[d]=(size_t)(nearest-file->dimoff[d]-1);
  }
  for (d=0;d<5;d++) { offset+=index[d]*multiplier; multiplier*=file->dnum[d]; }
  memset(view,0,sizeof(*view));
  view->pfile=file; view->undef=file->undef;
  view->idim=0; view->jdim=1;
  view->isiz=hi[0]-lo[0]+1; view->jsiz=hi[1]-lo[1]+1;
  view->dimmin[0]=lo[0]; view->dimmax[0]=hi[0];
  view->dimmin[1]=lo[1]; view->dimmax[1]=hi[1];
  view->igrab=file->gr2ab[0]; view->ivals=file->grvals[0];
  view->jgrab=file->gr2ab[1]; view->jvals=file->grvals[1];
  view->grid=file->rbuf+offset; view->umask=file->ubuf+offset;
  *stride=(size_t)file->dnum[0]; return 0;
}

static gaint aave_defined(char *expression, struct gastat *pst, gaint sel,
                           gadouble *result, char *mask) {
  struct gafile *file=reduction_defined(expression,pst);
  struct gagrid view;
  size_t stride;
  gaint rc=defined_area_view(file,pst,&view,&stride);
  if (rc<0) return -1;
  if (rc) { *result=file->undef; *mask=0; return 0; }
  *mask=doaave_stride(&view,pst->dmin[0],pst->dmax[0],pst->dmin[1],pst->dmax[1],sel,stride,result);
  return 0;
}

/* Reuse weights and distribute independent Z planes, without copying any
   defined-array values or calling the parser/allocator on worker threads. */
static gaint aave_defined_span(struct gafunc *call, struct gastat *pst, gaint axis,
                                  gaint first, gaint last, gadouble *out, char *mask, gadouble undef) {
  struct gafile *file=reduction_defined(call->argpnt[0],pst);
  struct gastat sample=*pst;
  struct gagrid *views=NULL;
  gadouble *xw=NULL,*yw=NULL,bounds[4],value,sum,weight;
  gaint nz=last-first+1,k,j,i,dim,wflag=0,rc=-1,model=-1;
  size_t stride=0,one_stride;
  if (!file || call->argnum!=5) return -1;
  for (i=0;i<4;i++) {
    if (!dimprs(call->argpnt[i+1],pst,pst->pfid,&dim,bounds+i,1,&wflag) || dim!=i/2) return -1;
    if (!wflag) bounds[i]+=(i%2 ? 0.5 : -0.5);
  }
  for (i=0;i<2;i++) {
    sample.dmin[i]=pst->pfid->gr2ab[i](pst->pfid->grvals[i],bounds[2*i]);
    sample.dmax[i]=pst->pfid->gr2ab[i](pst->pfid->grvals[i],bounds[2*i+1]);
  }
  views=galloc((size_t)nz*sizeof(*views),"definedviews");
  if (!views) return -1;
  memset(views,0,(size_t)nz*sizeof(*views));
  for (k=0;k<nz;k++) {
    if (axis==2) sample.dmin[2]=sample.dmax[2]=pst->pfid->gr2ab[2](pst->pfid->grvals[2],first+k);
    else { gr2t(pst->pfid->grvals[3],first+k,&sample.tmin); sample.tmax=sample.tmin; }
    i=defined_area_view(file,&sample,views+k,&one_stride);
    if (i<0) goto done;
    if (i) continue;
    if (model<0) { model=k; stride=one_stride; }
    else if (views[k].isiz!=views[model].isiz || views[k].jsiz!=views[model].jsiz || one_stride!=stride) goto done;
  }
  if (model>=0) {
    xw=galloc((size_t)views[model].isiz*sizeof(*xw),"definedxw");
    yw=galloc((size_t)views[model].jsiz*sizeof(*yw),"definedyw");
    if (!xw || !yw) goto done;
    aave_weights(views+model,sample.dmin[0],sample.dmax[0],sample.dmin[1],sample.dmax[1],1,xw,yw);
  }
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(j,value,sum,weight) \
    if(model>=0 && nz>1 && ga_omp_parallelize((double)nz*views[model].isiz*views[model].jsiz>INT_MAX ? INT_MAX : nz*views[model].isiz*views[model].jsiz))
#endif
  for (k=0;k<nz;k++) {
    sum=weight=0;
    if (views[k].grid) for (j=0;j<views[k].jsiz;j++) {
      gadouble rowweight;
      aave_row(views+k,(size_t)j*stride,xw,yw[j],1,&value,&rowweight);
      sum+=value; weight+=rowweight;
    }
    mask[k]=weight>0; out[k]=mask[k] ? sum/weight : undef;
  }
  rc=gaqsig() ? 1 : 0;
done:
  if (views) gree(views,"definedviews");
  if (xw) gree(xw,"definedxw"); if (yw) gree(yw,"definedyw");
  return rc;
}


/* Time correlation */

gaint fftcor (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = tmaskf (pfc, pst, 2);
   return (rc);
}

/* Time regression */  /* Timlin */

gaint fftreg (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
  rc = tmaskf (pfc,pst,3);
  return (rc);
}

/* Time mean, masked and arbitrarily weighted. */

gaint fftmav (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = tmaskf (pfc, pst, 1);
   return (rc);
}

char *tmnam[3] = {"TMAVE","TCORR","TREGR"};

/* Following function does time series operations that
   involve a mask grid, where if the first mask grid
   is undefined, the 2nd grid is not even accessed.
   This saves processing time for composite means and
   such.  Variable sel determines the function operation:

     sel = 1:  time mean.
     sel = 2:  time correlation.
     sel = 3:  time regression.     */

gaint tmaskf (struct gafunc *pfc, struct gastat *pst, gaint sel) {
struct gafile *pfi;
struct gagrid *pgr;
gadouble *gr, *mn1, *mn2, *cnt, *s1, *s2, *cov, *wt;
gadouble t1,t2,vv,res=0,v1,v2;
gaint dim,wflag=0,d1,d2,d,rc,i,siz,size=0;
char *ch,*fnam, uu, *gru, *mn1u, *mn2u, *cntu, *s1u, *s2u, *covu, resu=0;
size_t sz;

  cov=s2=s1=cnt=mn2=wt=0;
  s1u=mn2u=mn1u=NULL;

  fnam = tmnam[sel-1];
  /* Check for valid number of args       */
  if (pfc->argnum!=4) {
    snprintf(pout,1255,"Error from %s:  Too many or too few args\n",fnam);
    gaprnt (0,pout);
    gaprnt (0,"                   4 arguments expected \n");
    return (1);
  }

  /* Parse the dimension expression       */
  pfi = pst->pfid;
  ch = dimprs (pfc->argpnt[2], pst, pfi, &dim, &t1, 1, &wflag);
  if (ch==NULL || dim!=3 || wflag==2) {
    snprintf(pout,1255,"Error from %s:  1st dimension expr invalid\n",fnam);
    gaprnt (0,pout);
    if (dim!=3) gaprnt (0,"  expr does not describe time dimension\n");
    if (wflag==2) {
      snprintf(pout,1255,"  offt expression not supported as an arg to %s\n",fnam);
      gaprnt (0,pout);
    }
    return (1);
  }

  /* Now parse the 2nd dimension expression.  */
  ch = dimprs (pfc->argpnt[3], pst, pfi, &dim, &t2, 1, &wflag);
  if (ch==NULL || dim!=3 || t2<t1 || wflag==2) {
    snprintf(pout,1255,"Error from %s:  2nd dimension expr invalid\n",fnam);
    gaprnt (0,pout);
    if (dim!=3) gaprnt (0,"  expr does not describe time dimension\n");
    if (t2<t1)  gaprnt (0,"  end time is earlier than start time\n");
    if (wflag==2) {
      snprintf(pout,1255,"  offt expression not supported as an arg to %s\n",fnam);
      gaprnt (0,pout);
    }
    return (1);
  }

  /* Loop through all times and get means */
  if (pst->jdim==dim) pst->jdim = -1;
  d1 =  ceil(t1-0.001);          /* Loop limits are integers    */
  d2 = floor(t2+0.001);          /* No weighting  */
  snprintf(pout,1255,"%s:  dim = %i, start = %i, end = %i\n", fnam, dim, d1, d2);
  gaprnt (2,pout);

  rc = 0;
  mn1 = NULL;
  for (d=d1; d<=d2 && !rc; d++) {
    gr2t (pfi->grvals[3],d,&(pst->tmin));
    pst->tmax = pst->tmin;
    rc = gaexpr(pfc->argpnt[0],pst);
    if (rc) goto err2;
    if (!rc && pst->type==0) {
      gafree (pst);
      goto err3;
    }
    pgr = pst->result.pgr;
    if (pgr->idim != -1 || pgr->isiz!=1 || pgr->jsiz!=1) {
      snprintf(pout,1255,"Error from %s:  1st arg must be 0-D\n",fnam);
      gaprnt (0,pout);
      gafree (pst);
      goto err2;
    }
    vv = pgr->rmin;
    uu = pgr->umin;
    gafree (pst);
    if (uu!=0) {
      rc = gaexpr(pfc->argpnt[1],pst);
      if (rc) goto err2;
      if (!rc && pst->type==0) {
        gafree (pst);
        goto err3;
      }
      pgr = pst->result.pgr;
      siz = pgr->isiz * pgr->jsiz;
      if (mn1==NULL) {
        i = 2;
        if (sel==2 || sel==3) i = 6;
	sz = sizeof(gadouble)*siz*i;
        mn1 = (gadouble *)galloc(sz,"tmaskmn1");
        if (mn1==NULL) {
          gafree (pst);
          goto err1;
        }
	sz = sizeof(char)*siz*i;
        mn1u = (char *)galloc(sz,"tmaskmn1u");
        if (mn1u==NULL) {
          gafree (pst);
          goto err1;
        }
        if (sel==1) {
          wt = mn1 + siz;
          for (i=0; i<siz; i++) {
	    *(mn1+i)=0.0;
	    *(wt+i)=0.0;
	  }
        }
        if (sel==2 || sel==3) {
          mn2  = mn1  + siz;       /* these are data grids */
          cnt = mn2 + siz;
          s1  = cnt + siz;
          s2  = s1 + siz;
          cov = s2 + siz;

	  mn2u = mn1u + siz;       /* these are undef masks */
          cntu = mn2u + siz;
          s1u  = cntu + siz;
          s2u  = s1u + siz;
          covu = s2u + siz;

          for (i=0; i<siz; i++) {
            *(mn1+i) = 0.0;
	    *(mn2+i) = 0.0;
            *(cnt+i) = 0.0;
            *(s1+i)  = 0.0;
	    *(s2+i)  = 0.0;
            *(cov+i) = 0.0;
          }
        }
        size = siz;
      }
      if (size != siz) {
        gafree (pst);
        goto err2;
      }
      gr  = pgr->grid;
      gru = pgr->umask;
      for (i=0; i<siz; i++) {
        if (uu!=0 && *gru!=0) {
          if (sel==1) {
            *(mn1+i) += *gr * vv;
            *(wt+i) += vv;
          }
          if (sel==2 || sel==3) {
            *(mn1+i) += vv;
            *(mn2+i) += *gr;
            *(cnt+i) += 1.0;
          }
        }
        gr++; gru++;
      }
      gafree (pst);
    }
  }

  /* Calculate mean of each time series */

  if (mn1) {
    if (sel==1) {
      for (i=0; i<size; i++) {
        if (*(wt+i)>0.0) {
          *(mn1+i) = *(mn1+i) / *(wt+i);
	  *(mn1u+i) = 1;
        } else {
          *(mn1u+i) = 0;
        }
      }
    }
    if (sel==2 || sel==3) {
      for (i=0; i<size; i++) {
        if (*(cnt+i)>0.0) {
          *(mn1+i) = *(mn1+i) / *(cnt+i);
          *(mn2+i) = *(mn2+i) / *(cnt+i);
	  *(mn1u+i) = 1;
	  *(mn2u+i) = 1;
        } else {
          *(mn1u+i) = 0;
          *(mn2u+i) = 0;
        }
      }
    }
  }

  /* Loop through time again if needed; do squares and cov.
     Less error checking this time through. */

  if ((sel==2 || sel==3) && mn1) {
    rc = 0;
    for (d=d1; d<=d2 && !rc; d++) {
      gr2t (pfi->grvals[3],d,&(pst->tmin));
      pst->tmax = pst->tmin;
      rc = gaexpr(pfc->argpnt[0],pst);
      if (rc) goto err2;
      pgr = pst->result.pgr;
      vv = pgr->rmin;
      uu = pgr->umin;
      gafree (pst);
      rc = gaexpr(pfc->argpnt[1],pst);
      if (rc) goto err2;
      pgr = pst->result.pgr;
      gr  = pgr->grid;
      gru = pgr->umask;
      for (i=0; i<size; i++) {
        if (uu!=0 && *gru!=0) {
          if (*(cnt+i) > 0.0) {
            v1 = vv - *(mn1+i);
            *(s1+i) += v1*v1;
            v2 = *gr - *(mn2+i);
            *(s2+i) += v2*v2;
            *(cov+i) += v1*v2;
          }
        }
        gr++; gru++;
      }
      gafree (pst);
    }

    for (i=0; i<size; i++) {
      if (*(cnt+i) > 0.0) {
        *(s1+i) = *(s1+i) / *(cnt+i);
        *(s2+i) = *(s2+i) / *(cnt+i);
        *(cov+i) = *(cov+i) / *(cnt+i);
        if (sel==2) {
	  res = sqrt(*(s1+i) * *(s2+i));
	  resu = 1;
	}
        if (sel==3) {
	  res = sqrt(*(s1+i) * *(s1+i));
	  resu = 1;
	}
        if (res==0.0) {
	  resu = 0;
	}
        else {
	  res = *(cov+i)/res;
	  resu = 1;
	}
      } else resu = 0;
      *(s1+i)  = res;
      *(s1u+i) = resu;
    }
  }

  /* Get one final grid, and use it to return the result. */

  gr2t (pfi->grvals[3],d1,&(pst->tmin));
  pst->tmax = pst->tmin;
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) goto err2;
  if (!rc && pst->type==0) {
    gafree (pst);
    goto err3;
  }
  pgr = pst->result.pgr;
  siz = pgr->isiz * pgr->jsiz;
  gr  = pgr->grid;
  gru = pgr->umask;
  if (mn1) {
    if (size != siz) {
      gafree (pst);
      goto err2;
    }
    if (sel==1) {
      for (i=0; i<siz; i++) {
	if (*(mn1u+i)!=0) {
	  *(gr+i) = *(mn1+i);
	  *(gru+i) = 1;
	}
	else {
	  *(gru+i) = 0;
	}
      }
    }
    if (sel==2 || sel==3) {
      for (i=0; i<siz; i++) {
	if (*(s1u+i)!=0) {
	  *(gr+i) = *(s1+i);
	  *(gru+i) = 1;
	}
	else {
	  *(gru+i) = 0;
	}
      }
    }
    gree(mn1,"f409");
    gree(mn1u,"f410");
  } else {
    for (i=0; i<siz; i++) *(gru+i) = 0;
  }
  return (0);

err1:
  snprintf(pout,1255,"Error from %s:  Memory allocation error\n",fnam);
  gaprnt (0,pout);
  if (mn1) gree(mn1,"f411");
  if (mn1u) gree(mn1u,"f412");
  return (1);
err2:
  snprintf(pout,1255,"Error from %s:  Error getting grids\n",fnam);
  gaprnt (0,pout);
  if (mn1) gree(mn1,"f413");
  if (mn1u) gree(mn1u,"f414");
  return (1);
err3:
  snprintf(pout,1255,"Error from %s:  Args must be grid data\n",fnam);
  gaprnt (0,pout);
  if (mn1) gree(mn1,"f415");
  if (mn1u) gree(mn1u,"416");
  return (1);
}


char *avenam[8] = {"AVE","MEAN","SUM","SUMG","MIN","MAX","MINLOC","MAXLOC"};
static char *avelow[8] = {"ave","mean","sum","sumg","min","max","minloc","maxloc"};

gaint ffave (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 1);
   return (rc);
}

gaint ffmn (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 2);
   return (rc);
}

gaint ffsum (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 3);
   return (rc);
}


gaint ffsumg (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 4);
   return (rc);
}

gaint ffmin (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 5);
   return (rc);
}

gaint ffmax (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 6);
   return (rc);
}

gaint ffminl (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 7);
   return (rc);
}

gaint ffmaxl (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
   rc = ave (pfc, pst, 8);
   return (rc);
}


/* Grid points per block when many times are folded in at once */
#define AVE_TILE 1024

/* Fold one grid point of one time into the running result of ave and its
   kin: sum and cnt are the two grids they keep, val the time's value. The
   step-by-step loop and the many-times-at-once loop both use it, so the two
   accumulate identically. */
static inline void ave_accumulate (gaint sel, gaint d, gadouble wt,
                                   gadouble *sum, char *sumu,
                                   gadouble *cnt, char *cntu,
                                   gadouble val, char valu) {
  if (sel>=5 && sel<=8) {
    if (*sumu==0 || valu==0) {
      if (valu!=0) {
        *sum = val;
        *cnt = d;
        *sumu = 1;
        *cntu = 1;
      }
    }
    else {
      if ((sel==5 || sel==7) && val < *sum) {*sum = val; *cnt = d;}
      if ((sel==6 || sel==8) && val > *sum) {*sum = val; *cnt = d;}
    }
  } else {
    if (valu!=0) {
      /* weight for ave,mean,sum  for sumg just accum */
      if (sel<=3) val = val*wt;
      if (*sumu==0) {
        *sum = val;
        *sumu = 1;
        *cnt += wt;
      } else {
        *sum += val;
        *cnt += wt;
      }
    }
  }
}

/* The weight -b gives time d of an average from gr1 to gr2: how much of its
   cell, d-0.5 to d+0.5, lies between them. Times inside count fully, the
   first and last by the part of their cell inside, as the other dimensions
   are weighted. The first two times, the time-at-a-time loop, and the
   many-times-at-once loop all take their weights from here. */
static inline gadouble ave_tbndwt (gadouble gr1, gadouble gr2, gaint d) {
  gadouble lo, hi;
  lo = d-0.5;
  hi = d+0.5;
  if (gr1 > lo) lo = gr1;
  if (gr2 < hi) hi = gr2;
  if (hi < lo) return 0.0;
  return hi - lo;
}

/* The file variable an averaging argument names when it is nothing else: a
   bare variable of the default file, not a defined grid, a function, or an
   expression. Only then can many times be read at once rather than through
   gaexpr one time at a time; pgr, the grid gaexpr returned for the first
   time, must have come from that variable. */
static struct gavar *ave_plain_var (char *expr, struct gastat *pst,
                                    struct gagrid *pgr) {
  struct gafile *pfi;
  struct gavar *pvar;
  char name[20], *ch;
  gaint i, fnum;

  ch = expr;
  while (*ch==' ') ch++;
  if (*ch<'a' || *ch>'z') return NULL;
  i = 0;
  while ((*ch>='a' && *ch<='z') || (*ch>='0' && *ch<='9') || *ch=='_') {
    if (i>=16) return NULL;
    name[i++] = *ch++;
  }
  name[i] = '\0';
  pfi = pst->pfid;
  if (*ch=='.') {
    ch = intprs(ch+1,&fnum);
    if (ch==NULL || fnum<1) return NULL;
    pfi = pst->pfi1;
    for (i=1; i<fnum && pfi!=NULL; i++) pfi = pfi->pforw;
  }
  else if (getdfn(name,pst)!=NULL) return NULL;
  while (*ch==' ') ch++;
  if (*ch!='\0' || pfi==NULL || pfi!=pst->pfid || pfi->type!=1) return NULL;
  pvar = pfi->pvar1;
  for (i=0; i<pfi->vnum && !cmpwrd(name,pvar->abbrv); i++) pvar++;
  if (i>=pfi->vnum) return NULL;
  if (pgr && (pgr->pvar!=pvar || pgr->pfile!=pfi || pgr->toff)) return NULL;
  return pvar;
}

/* Recognize only a sum of two unmodified, equally shaped file fields.
   Keep the addition inside each time's mask: ave(a+b) is not in general
   ave(a)+ave(b). Other expressions retain the ordinary evaluator. */
static struct gavar *ave_sum_vars(char *expr, struct gastat *pst,
                                  struct gagrid *grid, struct gavar **other) {
  char terms[2][128],*plus=strchr(expr,'+');
  struct gavar *a,*b;
  size_t n;
  gaint d;
  if (!plus || strchr(plus+1,'+') || strlen(expr)>=sizeof(terms[0]) ||
      grid->pfile!=pst->pfid || grid->toff) return NULL;
  n=plus-expr; memcpy(terms[0],expr,n); terms[0][n]=0;
  strcpy(terms[1],plus+1);
  a=ave_plain_var(terms[0],pst,NULL); b=ave_plain_var(terms[1],pst,NULL);
  if (!a || !b || a->levels!=b->levels || a->nh5vardims!=b->nh5vardims || a->nvardims!=b->nvardims) return NULL;
  for (d=0;d<a->nh5vardims || d<a->nvardims;d++) if (a->units[d]!=b->units[d]) return NULL;
  *other=b;
  return a;
}

/* Spatial batching uses the ordinary evaluator and owned, temporary DEFINE
   views. No reader or parser state is shared with OpenMP threads. */
static gaint spatial_enabled(void) {
  const char *value=getenv("GA_SPATIAL_BATCH");
  return !value || strcmp(value,"off");
}
/* Small full-plane reductions already amortize their reads. NetCDF uses the
   established per-level reader, so staging a volume adds copies there too.
   FORCE exercises these paths in numerical tests without enabling them by
   default. Strip reductions still benefit from sections at small sizes. */
static gaint spatial_bulk_worthwhile(struct gafile *file, size_t plane, gaint strips) {
  const char *value=getenv("GA_SPATIAL_BATCH");
  return (value && !strcmp(value,"force")) ||
         ((file->adios2flg || file->ncflg==3) && (strips || plane>=262144));
}
#define SPATIAL_FIELDS 8
#define SPATIAL_BYTES ((size_t)128*1024*1024)
struct spatial_field {
  struct gafile *file;
  struct gavar *var;
  char alias[17];
};
struct spatial_plan {
  struct spatial_field fields[SPATIAL_FIELDS];
  gaint count;
  gaint fixed_axes;
  char expression[4096];
};

/* Eligibility only, not a second expression evaluator. Unsupported calls,
   offsets and plugins always retain the established evaluator. */
static gaint spatial_plan(char *expr, struct gastat *pst, struct spatial_plan *plan) {
  static const char *calls[]={"abs","sqrt","exp","log","log10","sin","cos","tan",
    "asin","acos","pow","mag","atan2","maskout","if","const","esat","qvsat",NULL};
  struct gafile *file;
  struct gavar *var;
  char name[32],*p=expr,*start,*end,alias[17];
  size_t used=0,len;
  gaint i,j,n,id,qualified,slot,compatible,d,k,literal;
  memset(plan,0,sizeof(*plan));
  if (!spatial_enabled() || !pst->pfid || strlen(expr)>=sizeof(plan->expression)) return 0;
  while (*p) {
    start=p; slot=-1;
    if ((*p>='0' && *p<='9') || (*p=='.' && p[1]>='0' && p[1]<='9')) {
      (void)strtod(p,&end); if (end==p) return 0; p=end;
    } else if (*p>='a' && *p<='z') {
      n=0;
      while ((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='_') {
        if (n>=sizeof(name)-1) return 0;
        name[n++]=*p++;
      }
      name[n]=0; file=pst->pfid; qualified=0;
      if (*p=='.') {
        qualified=1; p++; end=intprs(p,&id);
        if (!end || id<1) return 0;
        p=end;
        for (file=pst->pfi1,i=1;file && i<id;i++) file=file->pforw;
        if (!file) return 0;
      }
      literal=!qualified && n==1 && (name[0]=='u' || name[0]=='a') &&
              start>expr+1 && start[-1]=='-' && start[-2]==',' && (*p==')' || *p==',');
      var=NULL;
      if ((qualified || !getdfn(name,pst)) && strcmp(name,"lon") &&
          strcmp(name,"lat") && strcmp(name,"lev")) {
        for (i=0;i<file->vnum;i++) if (!strcmp(name,file->pvar1[i].abbrv)) {
          var=file->pvar1+i; break;
        }
      }
      if (*p=='(') {
        if (qualified || var || getdfn(name,pst)) return 0; /* field modifiers */
        for (i=0;calls[i] && strcmp(calls[i],name);i++);
        if (!calls[i]) return 0;
      } else if (var) {
        if (file->type!=1 || file->ppflag || file->idxflg || file->climo) return 0;
        for (d=0;d<3;d++) if (file->dnum[d]==1 || (d==2 && !var->levels))
          plan->fixed_axes|=1<<d;
        /* Profiles keep their original native broadcasting. Prefetch only
           complete, matching spatial axes; .N clocks are mapped separately. */
        compatible=!literal && strcmp(name,"auto") && var->levels>0 && file->calendar==pst->pfid->calendar;
        for (d=0;compatible && d<3;d++) {
          if (file->dnum[d]!=pst->pfid->dnum[d]) { compatible=0; break; }
          for (k=1;k<=file->dnum[d];k++)
            if (file->gr2ab[d](file->grvals[d],k)!=pst->pfid->gr2ab[d](pst->pfid->grvals[d],k)) {
              compatible=0; break;
            }
        }
        if (compatible) {
          for (slot=0;slot<plan->count;slot++)
            if (plan->fields[slot].file==file && plan->fields[slot].var==var) break;
          if (slot==plan->count) {
            if (slot==SPATIAL_FIELDS) return 0;
            plan->fields[slot].file=file; plan->fields[slot].var=var;
            for (j=0;j<1000;j++) {
              snprintf(alias,sizeof(alias),"ga_sp%d_%d",slot,j);
              if (!strstr(expr,alias) && !getdfn(alias,pst)) break;
            }
            if (j==1000) return 0;
            strcpy(plan->fields[slot].alias,alias); plan->count++;
          }
        }
      } else if (!literal && !getdfn(name,pst) && strcmp(name,"lon") && strcmp(name,"lat") &&
                 strcmp(name,"lev") && strcmp(name,"auto") &&
                 !(p-start==1 && (name[0]=='a' || name[0]=='u') && start>expr && start[-1]=='-')) return 0;
    } else {
      if (!strchr("()+-*/<>=!,&| ",*p)) return 0;
      p++;
    }
    if (slot>=0) {
      len=strlen(plan->fields[slot].alias);
      if (used+len>=sizeof(plan->expression)) return 0;
      memcpy(plan->expression+used,plan->fields[slot].alias,len);
    } else {
      len=p-start;
      if (used+len>=sizeof(plan->expression)) return 0;
      memcpy(plan->expression+used,start,len);
    }
    used+=len;
  }
  plan->expression[used]=0;
  return 1;
}

/* Borrow axes and file metadata, own only the supplied value/mask storage.
   These views are linked to a local gastat, never the user's DEFINE list. */
static void spatial_view(struct gafile *view, struct gafile *source,
                          struct gagrid *box, gaint first, gaint last,
                          gadouble *values, char *mask) {
  gaint d;
  *view=*source; view->type=4; view->climo=0;
  for (d=0;d<5;d++) {
    view->dimoff[d]=box->dimmin[d]-1;
    view->dnum[d]=box->dimmax[d]-box->dimmin[d]+1;
  }
  view->dimoff[2]=first-1; view->dnum[2]=last-first+1;
  view->rbuf=values; view->ubuf=mask;
}

/* Read each distinct full field once per bounded slab. Evaluate all pointwise
   operations with the existing gaexpr kernels, including masks and libm rules. */
static gaint spatial_volume(char *expr, struct gastat *pst, struct gagrid *box,
                             gaint first, gaint last, gadouble *values, char *mask) {
  struct spatial_plan plan;
  struct spatial_storage { struct gafile file; struct gadefn node; } *storage=NULL;
  struct gastat sample=*pst;
  struct gagrid read=*box,*grid;
  struct gavar *plain=ave_plain_var(expr,pst,NULL);
  size_t plane=(size_t)(box->dimmax[0]-box->dimmin[0]+1)*(box->dimmax[1]-box->dimmin[1]+1);
  size_t count,at;
  gadouble index;
  gaint i,d,k,rc=-1;
  if (plain) {
    read.pvar=plain; read.pfile=pst->pfid;
    rc=gagrdvolume(&read,first,last,values,mask);
    if (!rc) { ga_perf_source(read.pfile);ga_perf_spatial(); }
    return rc;
  }
  if (!spatial_plan(expr,pst,&plan) || !plan.count ||
      last<first || plane>SPATIAL_BYTES/((sizeof(*values)+1)*(plan.count+1))/(last-first+1)) return -1;
  count=plane*(last-first+1);
  storage=galloc((size_t)plan.count*sizeof(*storage),"spatialviews");
  if (!storage) return -1;
  memset(storage,0,(size_t)plan.count*sizeof(*storage));
  for (i=0;i<plan.count;i++) {
    struct gafile *source=plan.fields[i].file;
    rc=-1;
    read=*box; read.pfile=source; read.pvar=plan.fields[i].var;
    for (d=0;d<2;d++) {
      if (!d && source->wrap && (read.dimmin[d]<1 || read.dimmax[d]>source->dnum[d])) goto done;
      if (read.dimmin[d]<1) read.dimmin[d]=1;
      if (read.dimmax[d]>source->dnum[d]) read.dimmax[d]=source->dnum[d];
      if (read.dimmin[d]>read.dimmax[d] ||
          (read.dimmin[d]==read.dimmax[d] && box->dimmin[d]!=box->dimmax[d])) goto done;
    }
    index=t2gr(source->abvals[3],&pst->tmin);
    if (!isfinite(index) || fabs(index-floor(index+0.5))>1e-8 || index<1 || index>source->dnum[3]) goto done;
    read.dimmin[3]=read.dimmax[3]=(gaint)floor(index+0.5);
    index=source->ab2gr[4](source->abvals[4],pst->dmin[4]);
    if (!isfinite(index) || fabs(index-floor(index+0.5))>1e-8 || index<1 || index>source->dnum[4]) goto done;
    read.dimmin[4]=read.dimmax[4]=(gaint)floor(index+0.5);
    storage[i].file.rbuf=galloc(count*sizeof(*values),"spatialvalues");
    storage[i].file.ubuf=galloc(count,"spatialmask");
    if (!storage[i].file.rbuf || !storage[i].file.ubuf) goto done;
    rc=gagrdvolume(&read,first,last,storage[i].file.rbuf,storage[i].file.ubuf);
    if (rc) goto done;
    ga_perf_source(source);
    spatial_view(&storage[i].file,source,&read,first,last,storage[i].file.rbuf,storage[i].file.ubuf);
    strcpy(storage[i].node.abbrv,plan.fields[i].alias);
    storage[i].node.pfi=&storage[i].file;
    storage[i].node.pforw=sample.pdf1; sample.pdf1=&storage[i].node;
  }
  sample.idim=box->dimmax[0]>box->dimmin[0] ? 0 : box->dimmax[1]>box->dimmin[1] ? 1 : -1;
  sample.jdim=box->dimmax[0]>box->dimmin[0] && box->dimmax[1]>box->dimmin[1] ? 1 : -1;
  for (d=0;d<2;d++) {
    sample.dmin[d]=pst->pfid->gr2ab[d](pst->pfid->grvals[d],box->dimmin[d]);
    sample.dmax[d]=pst->pfid->gr2ab[d](pst->pfid->grvals[d],box->dimmax[d]);
  }
  for (k=first;k<=last;k++) {
    sample.dmin[2]=sample.dmax[2]=pst->pfid->gr2ab[2](pst->pfid->grvals[2],k);
    sample.result.pgr=NULL;
    rc=gaexpr(plan.expression,&sample);
    if (rc) goto done;
    if (sample.type!=1) { gafree(&sample); rc=1; goto done; }
    grid=sample.result.pgr;
    if (grid->idim!=sample.idim || grid->jdim!=sample.jdim || (size_t)grid->isiz*grid->jsiz!=plane ||
        (box->dimmax[0]>box->dimmin[0] && grid->dimmin[0]!=box->dimmin[0]) ||
        (box->dimmax[1]>box->dimmin[1] && grid->dimmin[1]!=box->dimmin[1])) {
      gagfre(grid); rc=1; goto done;
    }
    at=(size_t)(k-first)*plane;
    memcpy(values+at,grid->grid,plane*sizeof(*values)); memcpy(mask+at,grid->umask,plane);
    gagfre(grid);
    if (gaqsig()) { rc=1; goto done; }
  }
  rc=0;
done:
  for (i=0;i<plan.count;i++) {
    if (storage[i].file.rbuf) gree(storage[i].file.rbuf,"spatialvalues");
    if (storage[i].file.ubuf) gree(storage[i].file.ubuf,"spatialmask");
  }
  gree(storage,"spatialviews");
  if (!rc) ga_perf_spatial();
  return rc;
}

static gaint spatial_series(struct gafunc *,struct gastat *,gaint,gaint,gaint,gaint,
                              struct gagrid *,gadouble *,char *,gadouble);
static gaint spatial_cross_section(struct gafunc *,struct gastat *,gaint,gaint);

#define AVE_SECTION_MAX 33554432.0     /* values in one read, 256 MiB */

/* Plan a complete section before evaluating its first positions. The
   section supplies the result's metadata too: no duplicate native reads
   are needed just to discover its shape. */
static struct gagrid *ave_section(struct gafunc *pfc, struct gastat *pst,
                                  struct gafile *file, gaint dim, gaint first, gaint last) {
  struct gastat sample=*pst;
  struct gagrid *sec;
  struct gafile *source=file;
  gaint r=pst->idim,rc,k;
  struct spatial_plan plan;
  gadouble length=1,lo,hi;
  /* Time and ensemble axes do not use spatial ab2gr/gr2ab callbacks.
     Keep those output axes on the position-at-a-time evaluator. */
  if (dim>2 || r>2 || last<=first || pst->jdim>=0 || r==dim) return NULL;
  if (!ave_plain_var(pfc->argpnt[0],pst,NULL)) {
    source=reduction_defined(pfc->argpnt[0],pst);
    if (!source) {
      if (!spatial_plan(pfc->argpnt[0],pst,&plan) || (plan.fixed_axes&(1<<dim))) return NULL;
      source=file;
    }
    if (source->climo || source->dnum[dim]<=1 || (r>=0 && source->dnum[r]<=1)) return NULL;
    /* Average limits/weights use the default file. Only accept a defined
       section whose averaged coordinates agree at every requested point. */
    for (k=first;k<=last;k++) if (file->gr2ab[dim](file->grvals[dim],k)!=
         source->gr2ab[dim](source->grvals[dim],k)) return NULL;
  }
  if (r>=0) {
    lo=floor(source->ab2gr[r](source->abvals[r],pst->dmin[r])+0.0001);
    hi=ceil(source->ab2gr[r](source->abvals[r],pst->dmax[r])-0.0001);
    length=hi-lo+1;
  }
  if (length<1 || (last-first+1)*length>AVE_SECTION_MAX) return NULL;
  sample.dmin[dim]=file->gr2ab[dim](file->grvals[dim],first);
  sample.dmax[dim]=file->gr2ab[dim](file->grvals[dim],last);
  if (r<0) { sample.idim=dim; sample.jdim=-1; }
  else { sample.idim=r<dim ? r : dim; sample.jdim=r<dim ? dim : r; }
  rc=gaexpr(pfc->argpnt[0],&sample);
  if (rc) return NULL;
  if (sample.type!=1) { gafree(&sample); return NULL; }
  sec=sample.result.pgr;
  if (sec->alocf || sec->dimmin[dim]!=first || sec->dimmax[dim]!=last ||
      sec->isiz*sec->jsiz!=(last-first+1)*length ||
      sec->idim!=sample.idim || sec->jdim!=sample.jdim) {
    gagfre(sec); return NULL;
  }
  ga_perf_spatial();
  return sec;
}

/* The grid at position k (from the first) of a section: siz values */
static void ave_section_at (struct gagrid *sec, gaint dim, gaint k, gaint siz,
                            gadouble *val, char *valu) {
  gaint i;
  size_t o;
  for (i=0; i<siz; i++) {
    if (sec->jdim<0) o = (size_t)k;                                /* a point */
    else if (sec->idim==dim) o = (size_t)i*sec->isiz + k;          /* dim varies faster */
    else o = (size_t)k*sec->isiz + i;
    val[i] = sec->grid[o];
    valu[i] = sec->umask[o];
  }
}

/* An owned output grid sliced from an immutable input section. */
static struct gagrid *ave_section_grid(struct gagrid *sec, gaint dim, gaint at) {
  struct gagrid *grid=galloc(sizeof(*grid),"avesectiongrid");
  gaint r=sec->idim==dim ? sec->jdim : sec->idim;
  if (!grid) return NULL;
  *grid=*sec;
  grid->idim=r; grid->jdim=-1; grid->jsiz=1;
  grid->isiz=r<0 ? 1 : sec->idim==r ? sec->isiz : sec->jsiz;
  grid->dimmin[dim]=grid->dimmax[dim]=sec->dimmin[dim]+at;
  if (r==sec->jdim) {
    grid->igrab=sec->jgrab; grid->iabgr=sec->jabgr;
    grid->ivals=sec->jvals; grid->iavals=sec->javals; grid->ilinr=sec->jlinr;
  }
  if (r<0) { grid->grid=&grid->rmin; grid->umask=&grid->umin; }
  else {
    grid->grid=galloc((size_t)grid->isiz*sizeof(*grid->grid),"avesectionvalues");
    grid->umask=galloc((size_t)grid->isiz,"avesectionmask");
    if (!grid->grid || !grid->umask) { gagfre(grid); return NULL; }
  }
  ave_section_at(sec,dim,at,grid->isiz,grid->grid,grid->umask);
  return grid;
}

/* Spatial weights follow the evaluator's cell edges and clipping order. */
static gadouble ave_spatial_weight(struct gafile *file, gaint dim, gaint d,
                                    gaint sel, gaint bounded, gadouble wlo, gadouble whi) {
  gadouble alo=file->gr2ab[dim](file->grvals[dim],d-0.5);
  gadouble ahi=file->gr2ab[dim](file->grvals[dim],d+0.5),alen=fabs(ahi-alo);
  if (bounded) {
    gadouble low=whi<wlo ? whi : wlo,high=whi<wlo ? wlo : whi;
    if (alo<low) alo=low; if (alo>high) alo=high;
    if (ahi<low) ahi=low; if (ahi>high) ahi=high;
  }
  if (dim==1) {
    if (alo>90) alo=90; if (ahi>90) ahi=90;
    if (alo< -90) alo= -90; if (ahi< -90) ahi= -90;
    if (sel==1) return fabs(sin(ahi*(M_PI/180))-sin(alo*(M_PI/180)));
    if (sel==2) return fabs(ahi-alo);
  } else if (sel<=2) return ahi-alo;
  if (sel==3) return alen>FUZZ_SCALE ? fabs(ahi-alo)/alen : 0;
  return 1;
}

/* One region for the whole resident section. Every result point retains
   the evaluator's position-by-position addition and min/max tie order. */
static gaint ave_fold_section(struct gagrid *sec, struct gagrid *sum, struct gagrid *cnt,
                               struct gafile *file, gaint dim, gaint first, gaint last,
                               gaint sel, gaint bounded, gadouble wlo, gadouble whi) {
  gadouble *weights;
  gaint i,k,n=last-first+1,size=sum->isiz*sum->jsiz;
  size_t pos;
  if (n<1) return 0;
  weights=galloc((size_t)n*sizeof(*weights),"avesectionweights");
  if (!weights) return -1;
  for (k=0;k<n;k++) weights[k]=ave_spatial_weight(file,dim,first+k,sel,bounded,wlo,whi);
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(k,pos) \
    if(ga_omp_parallelize((double)size*n>INT_MAX ? INT_MAX : size*n))
#endif
  for (i=0;i<size;i++) {
    for (k=0;k<n;k++) {
      size_t at=(size_t)(first+k-sec->dimmin[dim]);
      pos=sec->jdim<0 ? at : sec->idim==dim ? (size_t)i*sec->isiz+at : at*sec->isiz+i;
      ave_accumulate(sel,first+k,weights[k],&sum->grid[i],&sum->umask[i],
                     &cnt->grid[i],&cnt->umask[i],sec->grid[pos],sec->umask[pos]);
    }
  }
  gree(weights,"avesectionweights");
  return gaqsig() ? 1 : 0;
}

/* A bounded XYZ slab for a vertical reduction of a file field or an eligible
   pointwise expression. Expression and prefetch storage share the budget.
   Each output column keeps its original level-by-level addition order.
   Reads are serial; independent columns run on the calculation threads. */
static gaint ave_volume(struct gafunc *pfc, struct gastat *pst, struct gafile *file,
                         struct gagrid *sum, struct gagrid *cnt, gaint first, gaint last,
                         gaint sel, gaint bounded, gadouble wlo, gadouble whi) {
  struct gagrid tile=*sum;
  struct gavar *var=ave_plain_var(pfc->argpnt[0],pst,sum);
  gadouble *values=NULL,*weights=NULL,alo,ahi,alen,value;
  char *mask=NULL;
  gaint nx,ny,tx,ty,x,y,i,j,k,rc=-1,nz=last-first+1,begun=0;
  size_t plane,n,pos,dst;
  struct spatial_plan plan;
  if (!spatial_enabled() && ((!file->adios2flg && file->ncflg!=3) || !var || sel>4)) return -1;
  if (!spatial_bulk_worthwhile(file,(size_t)sum->isiz*sum->jsiz,var && sel<=4)) return -1;
  if (!var && (!spatial_plan(pfc->argpnt[0],pst,&plan) || !plan.count || (plan.fixed_axes&3))) return -1;
  if (var) plan.count=0;
  else { gaint f; for (f=0;f<plan.count;f++) if (plan.fields[f].var->levels<last) return -1; }
  if (first<1 || (var && last>var->levels) || sum->idim>1 || sum->jdim>1 ||
      file->ppflag || file->idxflg || sum->toff ||
      sum->dimmin[0]<1 || sum->dimmax[0]>file->dnum[0] ||
      sum->dimmin[1]<1 || sum->dimmax[1]>file->dnum[1] ||
      sum->dimmin[3]<1 || sum->dimmax[3]>file->dnum[3] ||
      sum->dimmin[4]<1 || sum->dimmax[4]>file->dnum[4]) return -1;
  nx=sum->dimmax[0]-sum->dimmin[0]+1; ny=sum->dimmax[1]-sum->dimmin[1]+1;
  tx=file->adios2flg && nx>512 ? 512 : nx; ty=ny<256 ? ny : 256;
  /* At most 128 MiB of values/masks, plus the backend's native buffer. */
  while ((double)tx*ty*nz>SPATIAL_BYTES/((sizeof(*values)+1)*(plan.count+1)) && ty>1) ty=(ty+1)/2;
  while ((double)tx*ty*nz>SPATIAL_BYTES/((sizeof(*values)+1)*(plan.count+1)) && tx>1) tx=(tx+1)/2;
  n=(size_t)tx*ty*nz;
  if (n>SPATIAL_BYTES/((sizeof(*values)+1)*(plan.count+1))) return -1;
  values=galloc(n*sizeof(*values),"avevolume"); mask=galloc(n,"avevolumemask");
  weights=galloc((size_t)nz*sizeof(*weights),"avevolumeweights");
  if (!values || !mask || !weights) goto done;
  for (k=first;k<=last;k++) {
    alo=file->gr2ab[2](file->grvals[2],k-0.5);
    ahi=file->gr2ab[2](file->grvals[2],k+0.5); alen=fabs(ahi-alo);
    if (bounded) {
      gadouble low=whi<wlo ? whi : wlo,high=whi<wlo ? wlo : whi;
      if (alo<low) alo=low; if (alo>high) alo=high;
      if (ahi<low) ahi=low; if (ahi>high) ahi=high;
    }
    weights[k-first]=sel<=2 ? ahi-alo : sel==3 ? (alen>FUZZ_SCALE ? fabs(ahi-alo)/alen : 0) : 1;
  }
  for (y=0;y<ny;y+=ty) for (x=0;x<nx;x+=tx) {
    gaint width=nx-x<tx ? nx-x : tx,height=ny-y<ty ? ny-y : ty;
    tile.dimmin[0]=sum->dimmin[0]+x; tile.dimmax[0]=tile.dimmin[0]+width-1;
    tile.dimmin[1]=sum->dimmin[1]+y; tile.dimmax[1]=tile.dimmin[1]+height-1;
    tile.pvar=var; plane=(size_t)width*height;
    rc=spatial_volume(pfc->argpnt[0],pst,&tile,first,last,values,mask);
    if (rc) { if (begun || rc>0) rc=1; goto done; }
    begun=1;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(i,j,k,pos,dst,value) \
    if(ga_omp_parallelize(plane*(size_t)nz>INT_MAX ? INT_MAX : (gaint)(plane*nz)))
#endif
    for (n=0;n<plane;n+=256) {
      size_t q,end=n+256<plane ? n+256 : plane,indices[256];
      for (q=n;q<end;q++) {
        i=x+q%width; j=y+q/width;
        indices[q-n]=sum->idim==0 ? (size_t)j*nx+i : sum->idim==1 ? j : 0;
      }
      for (k=0;k<nz;k++) {
        for (q=n;q<end;q++) {
          dst=indices[q-n]; pos=(size_t)k*plane+q; value=values[pos];
          ave_accumulate(sel,first+k,weights[k],&sum->grid[dst],&sum->umask[dst],
                         &cnt->grid[dst],&cnt->umask[dst],value,mask[pos]);
        }
      }
    }
    if (gaqsig()) { rc=1; goto done; }
  }
  pst->dmin[2]=pst->dmax[2]=file->gr2ab[2](file->grvals[2],last);
  rc=0;
done:
  if (values) gree(values,"avevolume");
  if (mask) gree(mask,"avevolumemask");
  if (weights) gree(weights,"avevolumeweights");
  return rc;
}

static gaint ave_run (struct gafunc *, struct gastat *, gaint);

/* ave and its kin, with their progress shown while they run */
gaint ave (struct gafunc *pfc, struct gastat *pst, gaint sel) {
gaint lv, rc;
  lv = gaprogbeg(avelow[sel-1], 0);
  rc = ave_run(pfc, pst, sel);
  gaprogend(lv);
  return (rc);
}

static gaint ave_run (struct gafunc *pfc, struct gastat *pst, gaint sel) {
struct gagrid *pgr1, *pgr2, *pgr;
struct gafile *pfi;
struct dt tinc;
gadouble (*conv) (gadouble *, gadouble);
gadouble gr1, gr2, *sum, *cnt, *val;
gadouble alo, ahi, alen, wlo=0, whi=0;
gadouble d2r, wt, wt1, abs;
gaint mos, mns, wflag=0;
gaint i, rc, siz, dim, d, d1, d2, dim2, ilin, incr, bndflg, normerr;
gaint k, nstep, chunk, ib, ie, ndone, evaluate=0;
char *ch,*fnam,*sumu,*cntu,*valu;
struct gavar *direct,*other=NULL;
struct gagrid readgrid,readother;
gadouble *steps=NULL, *wts=NULL, *secv=NULL,*stepsb=NULL;
char *stepsu=NULL, *secu=NULL,*stepsbu=NULL;
struct gagrid *sect=NULL;

  d2r = M_PI/180;
  fnam=avenam[sel-1];

  /* Check for valid number of args */
  if (pfc->argnum<3 || pfc->argnum>5) {
    snprintf(pout,1255,"Error from %s:  Too many or too few args \n",fnam);
    gaprnt(0,pout);
    gaprnt (0,"                 3 to 5 arguments expected \n");
    return (1);
  }

  /* Parse the 1st dimension expression */
  pfi = pst->pfid;
  ch = dimprs (pfc->argpnt[1], pst, pfi, &dim, &gr1, 1, &wflag);
  if (ch==NULL || wflag==2) {
    snprintf(pout,1255,"Error from %s:  1st dimension expression invalid\n",fnam);
    gaprnt(0,pout);
    if (wflag==2) {
      snprintf(pout,1255,"  offt expression not supported as an arg to %s\n",fnam);
      gaprnt (0,pout);
    }
    return (1);
  }

  /* Parse the 2nd dimension expression */
  ch = dimprs (pfc->argpnt[2], pst, pfi, &dim2, &gr2, 1, &wflag);
  if (ch==NULL || dim2!=dim || gr2<gr1 || wflag==2) {
    snprintf(pout,1255,"Error from %s:  2nd dimension expression invalid\n",fnam);
    gaprnt(0,pout);
    if (dim2!=dim) gaprnt (0,"  start and end points have different dimensions\n");
    if (gr2<gr1)   gaprnt (0,"  end grid point is less than start grid point \n");
    if (wflag==2) {
      snprintf(pout,1255,"  offt expression not supported as an arg to %s\n",fnam);
      gaprnt (0,pout);
    }
    return (1);
  }

  /* Check for 4th argument.  Could be a time increment, or it may
     be option flags.  Time increment only valid for time averaging */
  bndflg = 0;
  incr = 1;
  if (pfc->argnum == 4) {
    if (*(pfc->argpnt[3]) == '-') {    /* Option flags? */
      if (*(pfc->argpnt[3]+1) == 'b') bndflg = 1;
      else {
        snprintf(pout,1255,"Error from %s: Invalid option flags\n",fnam);
        gaprnt(0,pout);
        return(1);
      }
    } else {
      /* Must be time increment */
      if (dim!=3) {
        snprintf(pout,1255,"Error from %s: Invalid usage of increment value\n",fnam);
        gaprnt(0,pout);
        gaprnt (0,"                Can only be used with time averaging\n");
        return (1);
      }
      ch = intprs(pfc->argpnt[3],&incr);
      if (ch==NULL) goto err3;

      /* If a relative date/time was given, the increment is obtained
         by looking at the default file structure (which assumes
         knowledge of how date/time conversions are done) */
      if (*ch!='\0') {
        ch = rdtprs(pfc->argpnt[3],&tinc);
        if (ch==NULL) goto err3;
        mos = tinc.yr*12 + tinc.mo;
        mns = tinc.dy*1140 + tinc.hr*60 + tinc.mn;
        val = pfi->grvals[3];
        if (mos>0 && *(val+5)>0) {
          incr = mos / (*(val+5));
          if (mos!=incr*(*(val+5))) goto err3;
        }
        else if (mns>0 && *(val+6)>0) {
          incr = mns / (*(val+6));
          if (mns!=incr*(*(val+6))) goto err3;
        }
        else goto err3;
      }
    }
  }
  if (pfc->argnum == 5) {
    if (*(pfc->argpnt[4]) == '-' &&
        *(pfc->argpnt[4]+1) == 'b') bndflg = 1;
    else {
      snprintf(pout,1255,"Error from %s: Invalid option flags\n",fnam);
      gaprnt(0,pout);
      return(1);
    }
  }

  rc=spatial_cross_section(pfc,pst,sel,dim);
  if (rc>=0) return rc;

  /* Get the first two grids */

  if (pst->idim==dim) {          /* Fewer varying dims if user */
    pst->idim = pst->jdim;       /* averaging over varying dim */
    pst->jdim = -1;
  }
  ilin = pfi->linear[dim];
  if (pst->jdim==dim) pst->jdim = -1;
  d1 = ceil(gr1-0.001);          /* Ave limits are integers    */
  d2 = floor(gr2+0.001);
  if (bndflg) {
    d1 = floor(gr1+0.5);
    d2 = ceil(gr2-0.5);
    if (dim!=3) {
      conv = pfi->gr2ab[dim];
      wlo = conv(pfi->grvals[dim],gr1);
      whi = conv(pfi->grvals[dim],gr2);
    }
  }

  if(mfcmn.warnflg > 0) {
    if (sel == 1) {
      snprintf(pout,1255,"Averaging.  dim = %i, start = %i, end = %i\n", dim, d1, d2);
    } else {
      snprintf(pout,1255,"%sing.  dim = %i, start = %i, end = %i\n", fnam, dim, d1, d2);
    }
    gaprnt (2,pout);
  }

  gaprogtotal((d2-d1)/incr + 1);

  /* Figure out weights for 1st grid */
  wt1 = 1.0;

  /*-----  time */
  if (dim==3) {
    gr2t (pfi->grvals[3],d1,&(pst->tmin));
    pst->tmax = pst->tmin;
    if (bndflg) wt1 = ave_tbndwt(gr1,gr2,d1);
  }
  /*-----  lon,lat,lev,ens */
  else {
    conv = pfi->gr2ab[dim];
    abs = conv(pfi->grvals[dim],d1);
    alo = conv(pfi->grvals[dim],d1-0.5);
    ahi = conv(pfi->grvals[dim],d1+0.5);
    alen=fabs(ahi-alo);
    pst->dmin[dim] = abs;
    pst->dmax[dim] = abs;
    if (bndflg) {
      if (whi<wlo) {
        if (alo > wlo) alo = wlo;
        if (ahi > wlo) ahi = wlo;
        if (alo < whi) alo = whi;
        if (ahi < whi) ahi = whi;
      } else {
        if (alo < wlo) alo = wlo;
        if (ahi < wlo) ahi = wlo;
        if (alo > whi) alo = whi;
        if (ahi > whi) ahi = whi;
      }
    }
    /*-----  lat scaling */
    if (dim==1) {
      if (alo >  90.0) alo =  90.0;
      if (ahi >  90.0) ahi =  90.0;
      if (alo < -90.0) alo = -90.0;
      if (ahi < -90.0) ahi = -90.0;
      if (sel==1) {                                                   /* ave */
	wt1 = fabs(sin(ahi*d2r)-sin(alo*d2r));
      } else if (sel==2) {                                            /* mean */
	wt1 = fabs(ahi-alo);
      } else if (sel==3) {                                            /* sum */
	if (alen > FUZZ_SCALE) {
	  wt1=fabs(ahi-alo)/alen;
	} else {
	  wt1=0.0;
	}
      } else if (sel==4) {                                            /* sumg */
	wt1=1.0;
      }
    }
    /* -----   lon,lev,ens scaling */
    else {
      if(sel<=2) {                                        /* ave, mean */
	wt1 = ahi - alo;
      } else if (sel==3) {                                /* sum */
	if (alen > FUZZ_SCALE) {
	  wt1=fabs(ahi-alo)/alen;
	} else {
	  wt1=0.0;
	}
      } else if (sel==4) {                                /* sumg */
	wt1=1.0;
      }
    }
  }

  /* Read a simple spatial section before its first two output grids. */
  sect=ave_section(pfc,pst,pfi,dim,d1,d2);
  if (sect) {
    pst->result.pgr=ave_section_grid(sect,dim,0);
    if (!pst->result.pgr) { gagfre(sect); return 1; }
    pst->type=1; rc=0;
  } else rc=gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type == 0) {
    gafree (pst);
    return(-1);
  }
  pgr1 = pst->result.pgr;
  gaprogstep(1);

  d = d1 + incr;                       /* If only grid, just return */
  if (d>d2)  {
    if (sel==7 || sel==8) {
      siz = pgr1->isiz * pgr1->jsiz;
      sum  = pgr1->grid;
      sumu = pgr1->umask;
      for (i=0; i<siz; i++) {
        if (*sumu != 0) *sum = d1;
        sum++; sumu++;
      }
    }
    return (0);
  }

  /* Figure out weights for 2nd grid */
  wt = 1.0;

  /*-----    time 22222222222222 */
  if (dim==3) {
    gr2t (pfi->grvals[3],d,&(pst->tmin));
    pst->tmax = pst->tmin;
    if (bndflg) wt = ave_tbndwt(gr1,gr2,d);
  }
  /*----- lon,lat,lev,ens 22222222222 */
  else {
    conv = pfi->gr2ab[dim];
    abs = conv(pfi->grvals[dim],d);
    alo = conv(pfi->grvals[dim],d-0.5);
    ahi = conv(pfi->grvals[dim],d+0.5);
    alen=fabs(ahi-alo);
    pst->dmin[dim] = abs;
    pst->dmax[dim] = abs;
    if (bndflg) {
      if (whi<wlo) {
        if (alo > wlo) alo = wlo;
        if (ahi > wlo) ahi = wlo;
        if (alo < whi) alo = whi;
        if (ahi < whi) ahi = whi;
      } else {
        if (alo < wlo) alo = wlo;
        if (ahi < wlo) ahi = wlo;
        if (alo > whi) alo = whi;
        if (ahi > whi) ahi = whi;
      }
    }
    /* ---- lat scaling 2222222222222*/
    if (dim==1) {
      if (alo >  90.0) alo =  90.0;
      if (ahi >  90.0) ahi =  90.0;
      if (alo < -90.0) alo = -90.0;
      if (ahi < -90.0) ahi = -90.0;
      if(sel==1) {                                                 /* ave */
	wt = fabs(sin(ahi*d2r)-sin(alo*d2r));
      } else if (sel==2) {                                         /* mean */
	wt = fabs(ahi-alo);
      } else if (sel==3) {                                         /* sum */
	if(alen > FUZZ_SCALE) {
	  wt=fabs(ahi-alo)/alen;
	} else  {
	  wt=0.0;
	}
      } else if (sel==4) {                                         /* sumg */
	wt=1.0;
      }
    }
    /* ---- lon,lev,ens  scaling 2222222222222*/
    else {
      if(sel<=2) {                                  /* ave, mean */
	wt = ahi - alo;
      } else if(sel==3) {                           /* sum */
	if(alen > FUZZ_SCALE) {
	  wt=fabs(ahi-alo)/alen;
	} else {
	  wt=0.0;
	}
      } else if(sel==4) {                           /* sumg */
	wt=1.0;
      }
    }
  }

  if (sect) {
    pst->result.pgr=ave_section_grid(sect,dim,d-d1);
    if (!pst->result.pgr) { gagfre(pgr1); gagfre(sect); return 1; }
    pst->type=1; rc=0;
  } else rc=gaexpr(pfc->argpnt[0],pst);    /* Get 2nd grid */
  if (rc) {
    gagfre(pgr1);
    return (rc);
  }
  if (pst->type==0) {
    gafree(pst);
    gagfre(pgr1);
    return (-1);
  }
  pgr2 = pst->result.pgr;
  gaprogstep(2);
  ndone = 2;


  /* We will sum into the first grid, and keep the
     count in the 2nd grid.  Set this up...        */

  siz = pgr1->isiz * pgr1->jsiz;
  sum  = pgr1->grid;
  cnt  = pgr2->grid;
  sumu = pgr1->umask;
  cntu = pgr2->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(siz))
#endif
  for (i=0; i<siz; i++) {
    if (sel>=5 && sel<=8) {
      if (sumu[i]==0 || cntu[i]==0) {
        if (cntu[i]!=0) {
	  sum[i] = cnt[i];
	  sumu[i] = 1;
	  cnt[i] = d;
	}
        else if (sumu[i]!=0) {
	  cnt[i] = d1;
	  cntu[i] = 1;
	}
      }
      else {
        if (sel==5 || sel==7) {
          if (cnt[i] < sum[i]) {sum[i] = cnt[i]; cnt[i] = d;}
          else cnt[i] = d1;
        }
        if (sel==6 || sel==8) {
          if (cnt[i] > sum[i]) {sum[i] = cnt[i]; cnt[i] = d;}
          else cnt[i] = d1;
        }
      }
    }
    else {
      if (sumu[i]==0) {
        if (cntu[i]==0) {
	  cnt[i] = 0.0;
	  cntu[i] = 1;
	}
        else {
    	  if (sel<=3) {                                          /* ave, mean sum */
	    sum[i] = cnt[i]*wt;
	    sumu[i] = 1;
	    cnt[i] = wt;
          }
	  else if (sel==4) {                                   /* sumg */
	    sum[i] = cnt[i];
	    sumu[i] = 1;
          }
	}
      }
      else if (cntu[i]==0 && (sel<=3) ) {              /* ave, mean sum */
	cnt[i] = wt1;
	cntu[i] = 1;
	sum[i] = sum[i]*wt1;
      } else {
        if (sel<=3) {
          sum[i] = sum[i]*wt1 + cnt[i]*wt;                       /* ave, mean sum */
        }
	else if (sel==4) {
          sum[i] = sum[i] + cnt[i];
        }
        cnt[i] = wt1 + wt;
	cntu[i] = 1;
      }
    }
  }

  /* Now sum the rest of the grids.

     Averaging a file variable over time reads many times at once where the
     format supports it (BP5): the reads go out together and are served in
     parallel, and the times are folded in on the calculation threads, each
     grid point taking its times in order, as the loop below does. Anything
     else, or a format without such a reader, goes a time at a time. */
  d+=incr;
  rc = 0;
  if (sect && d<=d2) {
    rc=ave_fold_section(sect,pgr1,pgr2,pfi,dim,d,d2,sel,bndflg,wlo,whi);
    if (rc>=0) {
      pst->dmin[dim]=pst->dmax[dim]=pfi->gr2ab[dim](pfi->grvals[dim],d2);
      gaprogstep(d2-d1+1); goto average_done;
    }
    rc=0;
  }
  if (dim==2 && d<=d2 && !sect) {
    rc=ave_volume(pfc,pst,pfi,pgr1,pgr2,d,d2,sel,bndflg,wlo,whi);
    if (rc>=0) { gaprogstep(d2-d1+1); goto average_done; }
    rc=0;
  }
  direct = (dim==3) ? ave_plain_var(pfc->argpnt[0],pst,pgr1) : NULL;
  if (dim==3 && !direct) direct=ave_sum_vars(pfc->argpnt[0],pst,pgr1,&other);
  evaluate = dim==3 && (d2-d1)/incr+1>=32 &&
             ga_time_can_evaluate(pfc->argpnt[0],pst);
  /* Short means and binary means are faster without extra batch buffers. */
  if (direct && !pfi->adios2flg && (!pfi->ncflg || (d2-d)/incr+1<8)) direct=NULL;
  if (direct) {
    readgrid=*pgr1; readgrid.pvar=direct;
    readother=*pgr1; readother.pvar=other;
  }
  if (sect) {
    secv = (gadouble *)galloc(sizeof(gadouble)*(size_t)siz,"avesecv");
    secu = (char *)galloc((size_t)siz,"avesecu");
    if (!secv || !secu) {
      gagfre(sect);
      sect = NULL;
    }
  }
  chunk = 1;
  if (direct || evaluate) {
    chunk = (gaint)(ga_time_batch_bytes(pfi) / ((gadouble)siz*(sizeof(gadouble)+1)));
    if (other) chunk/=2;
    if (chunk>256) chunk = 256;
    if (chunk<2) { direct=NULL;evaluate=0; }
  }
  if (direct || evaluate) {
    steps  = (gadouble *)galloc(sizeof(gadouble)*(size_t)siz*(size_t)chunk,"avesteps");
    stepsu = (char *)galloc((size_t)siz*(size_t)chunk,"avestepsu");
    wts    = (gadouble *)galloc(sizeof(gadouble)*(size_t)chunk,"avewts");
    if (!steps || !stepsu || !wts) { direct=NULL;evaluate=0; }
    if (other) {
      stepsb=galloc(sizeof(gadouble)*(size_t)siz*(size_t)chunk,"avestepsb");
      stepsbu=galloc((size_t)siz*(size_t)chunk,"avestepsbu");
      if (!stepsb || !stepsbu) { direct=NULL;evaluate=0; }
    }
  }
  for (d=d; d<=d2 && !rc; d+=incr) {
    /* times outside the file go the usual way, which warns about them */
    if ((direct || evaluate) && d>=1 && d<=pfi->dnum[3]) {
      if (gaqsig()) {                  /* Ctrl-C; gaexpr notices it itself */
        rc = 1;
        break;
      }
      nstep = (d2-d)/incr + 1;
      if (nstep>(pfi->dnum[3]-d)/incr+1) nstep = (pfi->dnum[3]-d)/incr+1;
      if (nstep>chunk) nstep = chunk;
      rc=-1;
      if (direct) {
        if (other && !pfi->adios2flg && !ga_time_can_read(pfi)) {
          /* Keep template files open while reading both fields at a time. */
          rc=0;
          for (k=0;k<nstep && !rc;k++) {
            size_t at=(size_t)k*siz;
            rc=gagrdsteps(&readgrid,d+k*incr,1,incr,steps+at,stepsu+at);
            if (!rc) rc=gagrdsteps(&readother,d+k*incr,1,incr,stepsb+at,stepsbu+at);
          }
        } else {
          rc=gagrdsteps(&readgrid,d,nstep,incr,steps,stepsu);
          if (!rc && other) rc=gagrdsteps(&readother,d,nstep,incr,stepsb,stepsbu);
        }
      }
      if (rc<0 && evaluate) {
        direct=NULL;other=NULL; /* whole expression already combines fields */
        rc=ga_time_evaluatesteps(pfc->argpnt[0],pst,pgr1,d,nstep,incr,steps,stepsu);
      }
      if (rc==0) {
        for (k=0; k<nstep; k++) {
          wts[k] = bndflg ? ave_tbndwt(gr1,gr2,d+k*incr) : 1.0;
        }
        sum  = pgr1->grid;
        sumu = pgr1->umask;
        cnt  = pgr2->grid;
        cntu = pgr2->umask;
        /* Each thread takes a block of grid points through the times in
           order, so every point sums exactly as the time-at-a-time loop
           does, while memory is read a row at a time. */
#if USEOPENMP == 1
#pragma omp parallel for private(k,i,ie) schedule(static) \
    if(ga_omp_parallelize((gadouble)siz*nstep>(gadouble)INT_MAX ? INT_MAX : siz*nstep))
#endif
        for (ib=0; ib<siz; ib+=AVE_TILE) {
          ie = ib+AVE_TILE < siz ? ib+AVE_TILE : siz;
          for (k=0; k<nstep; k++) {
            for (i=ib; i<ie; i++) {
              size_t pos=(size_t)k*siz+i;
              gadouble value=steps[pos];
              char valid=stepsu[pos];
              if (other) {
                valid=valid && stepsbu[pos];
                if (valid) value+=stepsb[pos];
              }
              ave_accumulate(sel, d+k*incr, wts[k], &sum[i], &sumu[i], &cnt[i], &cntu[i],
                             value,valid);
            }
          }
        }
        d += (nstep-1)*incr;
        gr2t (pfi->grvals[3],d,&(pst->tmin));
        pst->tmax = pst->tmin;
        ndone += nstep;
        gaprogstep(ndone);
        continue;
      }
      if (rc>0) break;
      rc = 0;
      direct=NULL;evaluate=0; /* unsupported batch: ordinary evaluator */
    }
    /* Get weight for this grid */
    wt = 1.0;

    /*---- time 3333333*/
    if (dim==3) {
      gr2t (pfi->grvals[3],d,&(pst->tmin));
      pst->tmax = pst->tmin;
      if (bndflg) wt = ave_tbndwt(gr1,gr2,d);
    }
    /*---- lat,lon,lev,ens 3333333*/
    else {
      conv = pfi->gr2ab[dim];
      abs = conv(pfi->grvals[dim],d);
      alo = conv(pfi->grvals[dim],d-0.5);
      ahi = conv(pfi->grvals[dim],d+0.5);
      alen=fabs(ahi-alo);
      pst->dmin[dim] = abs;
      pst->dmax[dim] = abs;
      if (bndflg) {
	if (whi<wlo) {
	  if (alo > wlo) alo = wlo;
	  if (ahi > wlo) ahi = wlo;
	  if (alo < whi) alo = whi;
	  if (ahi < whi) ahi = whi;
	} else {
	  if (alo < wlo) alo = wlo;
	  if (ahi < wlo) ahi = wlo;
	  if (alo > whi) alo = whi;
	  if (ahi > whi) ahi = whi;
	}
      }
      /*---- lat 3333333*/
      if (dim==1) {
	if (alo >  90.0) alo =  90.0;
	if (ahi >  90.0) ahi =  90.0;
	if (alo < -90.0) alo = -90.0;
	if (ahi < -90.0) ahi = -90.0;
	if(sel==1) {                                                  /* ave */
	  wt = fabs(sin(ahi*d2r)-sin(alo*d2r));
	} else if (sel==2) {                                          /* mean */
	  wt = fabs(ahi-alo);
	} else if (sel==3) {                                          /* sum */
	  if(alen > FUZZ_SCALE) {
	    wt=fabs(ahi-alo)/alen;
	  } else  {
	    wt=0.0;
	  }
	} else if (sel==4) {                                          /* sumg */
	  wt=1.0;
	}
      }
      /*---- lon,lev,ens 3333333*/
      else {
	if(sel<=2) {                        /* ave, mean */
	  wt = ahi - alo;
	} else if(sel==3) {                 /* sum */
	  if(alen > FUZZ_SCALE) {
	    wt=fabs(ahi-alo)/alen;
	  } else {
	    wt=0.0;
	  }
	} else if(sel==4) {                 /* sumg */
	  wt=1.0;
	}
      }
    }

    pgr = NULL;
    if (sect) {                        /* read already, with the others */
      ave_section_at(sect,dim,d-d1,siz,secv,secu);
      val = secv;
      valu = secu;
      if (gaqsig()) rc = 1;            /* Ctrl-C, as gaexpr would notice */
    }
    else {
      rc = gaexpr(pfc->argpnt[0],pst);
      if (!rc && pst->type==0) rc = -1;
      if (!rc) {
        pgr = pst->result.pgr;
        val = pgr->grid;
        valu = pgr->umask;
      }
    }
    if (!rc) {
      cnt = pgr2->grid;
      sum = pgr1->grid;
      cntu = pgr2->umask;
      sumu = pgr1->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(siz))
#endif
      for (i=0; i<siz; i++) {
        ave_accumulate(sel, d, wt, &sum[i], &sumu[i], &cnt[i], &cntu[i], val[i], valu[i]);
      }
      if (pgr) gagfre(pgr);
      gaprogstep(++ndone);
    }
  }

average_done:
  if (steps)  gree(steps,"avesteps");
  if (stepsu) gree(stepsu,"avestepsu");
  if (stepsb) gree(stepsb,"avestepsb");
  if (stepsbu) gree(stepsbu,"avestepsbu");
  if (wts)    gree(wts,"avewts");
  if (sect)   gagfre(sect);
  if (secv)   gree(secv,"avesecv");
  if (secu)   gree(secu,"avesecu");
  if (rc) {
    if (rc==-1) gafree (pst);
    gagfre(pgr1);
    gagfre(pgr2);
    snprintf(pout,1255,"Error from %s:  Error getting grids \n",fnam);
    gaprnt(0,pout);
    return (rc);
  } else {
    cnt = pgr2->grid;         /* Normalize if needed */
    sum = pgr1->grid;
    cntu = pgr2->umask;
    sumu = pgr1->umask;
    if (sel==1 || sel==2 || sel==7 || sel==8) {
      normerr = 0;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) reduction(max:normerr) if(ga_omp_parallelize(siz))
#endif
      for (i=0; i<siz; i++) {
        if (sumu[i]!=0) {
          if (sel < 3 && cnt[i]==0.0) {
            normerr = 100;
          }
          else if (sel > 6 && cntu[i]==0) {
            normerr = 101;
          }
          else if (sel==1 || sel==2) {
	    sum[i] = sum[i] / cnt[i];
          } else {
            sum[i] = cnt[i];
          }
        }
      }
      if (normerr) {
        snprintf(pout,1255,"Error from %s:  Internal logic check %i\n",fnam,normerr);
        gaprnt(0,pout);
        return (1);
      }
    }
  }

  gagfre(pgr2);

  pst->type = 1;
  pst->result.pgr = pgr1;

  return (0);

err3:
  snprintf(pout,1255,"Error from %s: Invalid time increment argument\n",fnam);
  gaprnt(0,pout);
  return (1);
}


/* Split a complete outer reduction, keeping nested argument expressions for
   the ordinary evaluator. Expressions outside the call retain that evaluator. */
static gaint reduction_call(char *expression, char *name, struct gafunc *call) {
  char *p=strchr(expression,'('),*q,*out=call->buff;
  size_t n;
  gaint depth=0;
  if (!p || (n=(size_t)(p-expression))>8 || !n || strlen(p+1)>=sizeof(call->buff)) return 0;
  memcpy(name,expression,n); name[n]=0;
  call->argnum=1; call->argpnt[0]=out;
  for (q=p+1;*q;q++) {
    if (*q==')' && !depth) break;
    if (*q=='(') depth++;
    if (*q==')') depth--;
    if (*q==',' && !depth) {
      if (call->argnum>=20) return 0;
      *out++=0; call->argpnt[call->argnum++]=out;
    } else *out++=*q;
  }
  if (*q!=')' || q[1]) return 0;
  *out=0; return 1;
}

/* One bounded XYZ producer for both DEFINE profiles and displayed XZ/YZ
   reductions. All numerical work still uses AVE/AAVE/MNMX themselves. */
static gaint spatial_series(struct gafunc *pfc, struct gastat *pst, gaint family,
                             gaint sel, gaint first, gaint last, struct gagrid *shape,
                             gadouble *out, char *outmask, gadouble undef) {
  struct spatial_plan plan;
  struct gafile *file=pst->pfid,view;
  struct gadefn node;
  struct gastat sample=*pst;
  struct gafunc call=*pfc;
  struct gagrid box,*grid;
  gadouble *values=NULL,bounds[4],gr;
  char *mask=NULL,alias[17];
  size_t plane,size=(size_t)shape->isiz*shape->jsiz,n,at;
  gaint d,i,k,z,count,chunk,dim,wflag=0,bounded=0,rc=-1,started=0;
  if (last<=first || first<1 || last>file->dnum[2] || shape->idim>1 || shape->jdim>=0 ||
      file->type!=1 || file->ppflag || file->idxflg || file->climo ||
      !spatial_bulk_worthwhile(file,0,1) ||
      !spatial_plan(pfc->argpnt[0],pst,&plan) || !plan.count) return -1;
  for (i=0;i<plan.count;i++) if (plan.fields[i].var->levels<last) return -1;
  memset(&box,0,sizeof(box)); box.pfile=file; box.undef=file->undef;
  box.idim=0; box.jdim=1;
  for (d=0;d<5;d++) {
    if (d==3) gr=t2gr(file->abvals[3],&pst->tmin);
    else gr=file->ab2gr[d](file->abvals[d],pst->dmin[d]);
    if (!isfinite(gr) || fabs(gr)>INT_MAX/2) return -1;
    box.dimmin[d]=box.dimmax[d]=(gaint)floor(gr+0.5);
    if (d<2 && (pst->idim==d || pst->jdim==d)) {
      box.dimmin[d]=(gaint)floor(gr+0.0001);
      box.dimmax[d]=(gaint)ceil(file->ab2gr[d](file->abvals[d],pst->dmax[d])-0.0001);
    }
  }
  if (family==0) {
    if (pfc->argnum<3 || pfc->argnum>4 ||
        (pfc->argnum==4 && strcmp(pfc->argpnt[3],"-b"))) return -1;
    bounded=pfc->argnum==4;
    for (i=0;i<2;i++) {
      if (!dimprs(pfc->argpnt[i+1],pst,file,&dim,&gr,1,&wflag) || dim>1 || wflag==2) return -1;
      if (!i) d=dim; else if (dim!=d) return -1;
      if (plan.fixed_axes&(1<<dim)) return -1;
      if (!isfinite(gr) || fabs(gr)>INT_MAX/2) return -1;
      if (!i) box.dimmin[d]=bounded ? floor(gr+0.5) : ceil(gr-0.001);
      else box.dimmax[d]=bounded ? ceil(gr-0.5) : floor(gr+0.001);
    }
  } else {
    if (pfc->argnum!=5 || shape->idim>=0) return -1;
    for (i=0;i<4;i++) {
      if (!dimprs(pfc->argpnt[i+1],pst,file,&dim,bounds+i,1,&wflag) || dim!=i/2) return -1;
      if (!wflag && (family!=1 || sel!=4)) bounds[i]+=(i%2 ? 0.5 : -0.5);
      if (!isfinite(bounds[i]) || fabs(bounds[i])>INT_MAX/2) return -1;
      if (i%2) box.dimmax[dim]=(gaint)ceil(bounds[i]-0.0001);
      else box.dimmin[dim]=(gaint)floor(bounds[i]+0.0001);
    }
  }
  for (d=0;d<2;d++) {
    if (!d && file->wrap && (box.dimmin[d]<1 || box.dimmax[d]>file->dnum[d])) return -1;
    if (box.dimmin[d]>box.dimmax[d] || box.dimmax[d]<1 || box.dimmin[d]>file->dnum[d]) return -1;
  }
  if (box.dimmin[3]<1 || box.dimmax[3]>file->dnum[3] ||
      box.dimmin[4]<1 || box.dimmax[4]>file->dnum[4]) return -1;
  box.isiz=box.dimmax[0]-box.dimmin[0]+1; box.jsiz=box.dimmax[1]-box.dimmin[1]+1;
  plane=(size_t)box.isiz*box.jsiz;
  if (!spatial_bulk_worthwhile(file,plane,family==0)) return -1;
  chunk=SPATIAL_BYTES/((sizeof(*values)+1)*(plan.count+1))/plane;
  if (chunk<1) return -1;
  if (chunk>32) chunk=32;
  if (chunk>last-first+1) chunk=last-first+1;
  n=plane*chunk;
  values=galloc(n*sizeof(*values),"spatialseries"); mask=galloc(n,"spatialseriesmask");
  if (!values || !mask) goto done;
  for (i=0;i<1000;i++) {
    snprintf(alias,sizeof(alias),"ga_spout%d",i);
    if (!getdfn(alias,pst) && !strstr(pfc->argpnt[0],alias)) break;
  }
  if (i==1000) goto done;
  memset(&node,0,sizeof(node)); strcpy(node.abbrv,alias); node.pfi=&view;
  node.pforw=pst->pdf1; sample.pdf1=&node; call.argpnt[0]=alias;
  for (z=first;z<=last;z+=count) {
    count=last-z+1<chunk ? last-z+1 : chunk;
    rc=spatial_volume(pfc->argpnt[0],pst,&box,z,z+count-1,values,mask);
    if (rc) { if (started || rc>0) rc=1; goto done; }
    started=1;
    spatial_view(&view,file,&box,z,z+count-1,values,mask);
    for (k=z;k<z+count;k++) {
      sample=*pst; sample.pdf1=&node;
      sample.idim=shape->idim; sample.jdim=shape->jdim;
      sample.dmin[2]=sample.dmax[2]=file->gr2ab[2](file->grvals[2],k);
      sample.result.pgr=NULL;
      rc=family==0 ? ave(&call,&sample,sel) : family==1 ? aave(&call,&sample,sel) : mnmx(&call,&sample,sel);
      if (rc) goto done;
      grid=sample.result.pgr;
      if (sample.type!=1 || grid->idim!=shape->idim || grid->jdim!=shape->jdim ||
          grid->isiz!=shape->isiz || grid->jsiz!=shape->jsiz ||
          (shape->idim>=0 && gagchk(grid,shape,shape->idim))) {
        gafree(&sample); rc=1; goto done;
      }
      at=(size_t)(k-first)*size;
      for (n=0;n<size;n++) {
        outmask[at+n]=grid->umask[n]; out[at+n]=outmask[at+n] ? grid->grid[n] : undef;
      }
      gafree(&sample);
      if (gaqsig()) { rc=1; goto done; }
    }
  }
  rc=0;
done:
  if (values) gree(values,"spatialseries"); if (mask) gree(mask,"spatialseriesmask");
  return rc;
}

/* DISPLAY can retain Z while reducing X/Y; the same producer fills DEFINE.
   The initial ordinary section supplies metadata, not a mutable alias. */
static gaint spatial_cross_section(struct gafunc *pfc, struct gastat *pst,
                                    gaint sel, gaint dim) {
  struct gastat sample=*pst;
  struct gafile *file=pst->pfid;
  struct spatial_plan plan;
  struct gagrid *shape=NULL,*result=NULL;
  gadouble gr;
  gaint first,last,r,rc=-1;
  size_t n;
  if (dim>1 || (pst->idim!=2 && pst->jdim!=2) ||
      !spatial_bulk_worthwhile(file,0,1) ||
      !spatial_plan(pfc->argpnt[0],pst,&plan) || !plan.count) return -1;
  gr=file->ab2gr[2](file->abvals[2],pst->dmin[2]);
  if (!isfinite(gr) || gr<1 || gr>file->dnum[2]) return -1;
  first=floor(gr+0.0001);
  gr=file->ab2gr[2](file->abvals[2],pst->dmax[2]);
  if (!isfinite(gr) || gr<first || gr>file->dnum[2]) return -1;
  last=ceil(gr-0.0001);
  if (last<=first || (plan.fixed_axes&(1<<2))) return -1;
  sample.dmin[2]=sample.dmax[2]=file->gr2ab[2](file->grvals[2],first);
  if (sample.idim==2) { sample.idim=sample.jdim; sample.jdim=-1; }
  if (sample.jdim==2) sample.jdim=-1;
  rc=ave(pfc,&sample,sel);
  if (rc) return rc>0 ? rc : 1;
  shape=sample.result.pgr;
  if (sample.type!=1 || shape->jdim>=0 || shape->idim>1) { gafree(&sample); return 1; }
  if (shape->isiz>INT_MAX/(last-first+1)) { rc=-1; goto done; }
  n=(size_t)shape->isiz*(last-first+1);
  result=galloc(sizeof(*result),"spatialresult"); if (!result) { rc=1; goto done; }
  *result=*shape; result->alocf=0; result->grid=NULL; result->umask=NULL;
  if (result->idim<0) result->idim=0; /* free allocated buffers on errors too */
  result->grid=galloc(n*sizeof(*result->grid),"spatialresultvalues");
  result->umask=galloc(n,"spatialresultmask");
  if (!result->grid || !result->umask) { rc=1; goto done; }
  rc=spatial_series(pfc,pst,0,sel,first,last,shape,result->grid,result->umask,shape->undef);
  if (rc) goto done;
  r=shape->idim;
  if (r<0) {
    result->idim=2; result->jdim=-1; result->isiz=last-first+1; result->jsiz=1;
    result->igrab=file->gr2ab[2]; result->iabgr=file->ab2gr[2];
    result->ivals=file->grvals[2]; result->iavals=file->abvals[2]; result->ilinr=file->linear[2];
  } else {
    result->jdim=2; result->jsiz=last-first+1;
    result->jgrab=file->gr2ab[2]; result->jabgr=file->ab2gr[2];
    result->jvals=file->grvals[2]; result->javals=file->abvals[2]; result->jlinr=file->linear[2];
  }
  result->dimmin[2]=first; result->dimmax[2]=last;
  pst->type=1; pst->result.pgr=result; result=NULL; rc=0;
done:
  if (shape) gagfre(shape); if (result) gagfre(result);
  return rc;
}

/* Fill one time/member of a DEFINE across Z. The destination belongs to
   DEFINE and is never exposed as a mutable view to the expression parser.
   Return -1 for the established level-at-a-time path, 0 on success. */
gaint gadefseries(char *expression, struct gastat *pst, gaint z1, gaint z2,
                   struct gagrid *shape, gadouble *out, char *outmask, gadouble undef) {
  struct gafunc call;
  struct gastat sample=*pst;
  struct gafile *file=pst->pfid;
  struct gavar *var;
  struct gagrid *plane=NULL,tile;
  char name[9],*mask=NULL;
  gadouble bounds[4],*values=NULL,*xw=NULL,*yw=NULL,*sums=NULL,*counts=NULL,gr;
  gaint dim,wflag=0,rc=-1,k,x,y,i,j,nz=z2-z1+1,tx,ty,nx,ny,lx,ly,ox,oy;
  size_t n,pos,pp;
  if (z2<=z1 || z1<1 || !reduction_call(expression,name,&call)) return -1;
  if (!strcmp(name,"aave") && shape->idim<0 && reduction_defined(call.argpnt[0],pst))
    return aave_defined_span(&call,pst,2,z1,z2,out,outmask,undef);
  {
    static const char *names[]={"ave","mean","sum","sumg","min","max","minloc","maxloc",
      "aave","amean","asum","asumg","atot","amin","amax","aminlocx","aminlocy","amaxlocx","amaxlocy",NULL};
    gaint family,selector;
    for (i=0;names[i] && strcmp(names[i],name);i++);
    if (names[i] && (strcmp(name,"aave") || !ave_plain_var(call.argpnt[0],pst,NULL))) {
      family=i<8 ? 0 : i<13 ? 1 : 2;
      selector=family==0 ? i+1 : family==1 ? i-7 : i-12;
      rc=spatial_series(&call,pst,family,selector,z1,z2,shape,out,outmask,undef);
      if (rc>=0) return rc;
    }
  }
  var=ave_plain_var(call.argpnt[0],pst,NULL);
  if (!var) {
    struct gafile *defined=reduction_defined(call.argpnt[0],pst);
    if (!defined || defined->dnum[2]<2 || z1<=defined->dimoff[2] ||
        z2>defined->dimoff[2]+defined->dnum[2]) return -1;
  } else if (z2>var->levels) return -1;
  if (!strcmp(name,"ave") && shape->idim<0 && call.argnum>=3 && call.argnum<=4) {
    if (!dimprs(call.argpnt[1],pst,file,&dim,&gr,1,&wflag) || dim>1) return -1;
    sample.idim=2; sample.jdim=-1;
    sample.dmin[2]=file->gr2ab[2](file->grvals[2],z1);
    sample.dmax[2]=file->gr2ab[2](file->grvals[2],z2);
    rc=gaexpr(expression,&sample);
    if (rc) return rc;
    if (sample.type!=1) { gafree(&sample); return 1; }
    plane=sample.result.pgr;
    if (plane->idim!=2 || plane->jdim>=0 || plane->isiz!=nz || plane->dimmin[2]!=z1) {
      gafree(&sample); return 1;
    }
    for (k=0;k<nz;k++) { outmask[k]=plane->umask[k]; out[k]=outmask[k] ? plane->grid[k] : undef; }
    gafree(&sample); return 0;
  }
  if (!var || (!file->adios2flg && file->ncflg) || strcmp(name,"aave") || call.argnum!=5 ||
      shape->idim>=0 || file->ppflag || file->idxflg) return -1;
  for (i=0;i<4;i++) {
    if (!dimprs(call.argpnt[i+1],pst,file,&dim,bounds+i,1,&wflag) || dim!=i/2) return -1;
    if (!wflag) bounds[i]+=(i%2 ? 0.5 : -0.5);
  }
  for (i=0;i<2;i++) {
    sample.dmin[i]=file->gr2ab[i](file->grvals[i],bounds[2*i]);
    sample.dmax[i]=file->gr2ab[i](file->grvals[i],bounds[2*i+1]);
  }
  sample.idim=0; sample.jdim=1;
  if (gaexpr("lon",&sample)) return 1;
  if (sample.type!=1) { gafree(&sample); return -1; }
  plane=sample.result.pgr; tile=*plane; tile.pvar=var;
  nx=plane->isiz; ny=plane->jsiz;
  if ((file->wrap && (plane->dimmin[0]<1 || plane->dimmax[0]>file->dnum[0])) ||
      plane->dimmin[3]<1 || plane->dimmax[3]>file->dnum[3] ||
      plane->dimmin[4]<1 || plane->dimmax[4]>file->dnum[4]) goto done;
  xw=galloc((size_t)nx*sizeof(*xw),"seriesxw"); yw=galloc((size_t)ny*sizeof(*yw),"seriesyw");
  sums=galloc((size_t)nz*sizeof(*sums),"seriessum"); counts=galloc((size_t)nz*sizeof(*counts),"seriescount");
  if (!xw || !yw || !sums || !counts) goto done;
  aave_weights(plane,sample.dmin[0],sample.dmax[0],sample.dmin[1],sample.dmax[1],1,xw,yw);
  for (k=0;k<nz;k++) { sums[k]=counts[k]=0; }
  lx=plane->dimmin[0]<1 ? 1 : plane->dimmin[0];
  ly=plane->dimmin[1]<1 ? 1 : plane->dimmin[1];
  nx=plane->dimmax[0]>file->dnum[0] ? file->dnum[0] : plane->dimmax[0];
  ny=plane->dimmax[1]>file->dnum[1] ? file->dnum[1] : plane->dimmax[1];
  tx=nx-lx+1; if (file->adios2flg && tx>512) tx=512;
  ty=ny-ly+1; if (ty>256) ty=256;
  if (tx<1 || ty<1) { rc=-1; goto done; }
  while ((double)tx*ty*nz>128.0*1024*1024/(sizeof(*values)+1) && ty>1) ty=(ty+1)/2;
  while ((double)tx*ty*nz>128.0*1024*1024/(sizeof(*values)+1) && tx>1) tx=(tx+1)/2;
  n=(size_t)tx*ty*nz;
  if (n>128*1024*1024/(sizeof(*values)+1)) goto done;
  values=galloc(n*sizeof(*values),"seriesvalues"); mask=galloc(n,"seriesmask");
  if (!values || !mask) goto done;
  for (y=ly;y<=ny;y+=ty) for (x=lx;x<=nx;x+=tx) {
    gaint width=nx-x+1<tx ? nx-x+1 : tx,height=ny-y+1<ty ? ny-y+1 : ty;
    tile.dimmin[0]=x; tile.dimmax[0]=x+width-1;
    tile.dimmin[1]=y; tile.dimmax[1]=y+height-1;
    pp=(size_t)width*height;
    rc=gagrdvolume(&tile,z1,z2,values,mask);
    if (rc) { if (x!=lx || y!=ly || rc>0) rc=1; goto done; }
    ox=x-plane->dimmin[0]; oy=y-plane->dimmin[1];
    /* Independent levels own their sums, with a fixed row/cell order.
       Thread count therefore does not change this reduction's grouping. */
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(j,i,pos) \
    if(nz>1 && ga_omp_parallelize(pp*(size_t)nz>INT_MAX ? INT_MAX : (gaint)(pp*nz)))
#endif
    for (k=0;k<nz;k++) {
      gadouble sum=0,weight=0;
      for (j=0;j<height;j++) {
        gadouble rowsum=0,rowweight=0;
#if USEOPENMP == 1
#pragma omp simd reduction(+:rowsum,rowweight)
#endif
        for (i=0;i<width;i++) {
          pos=(size_t)k*pp+(size_t)j*width+i;
          if (mask[pos]) {
            gadouble w=xw[ox+i]*yw[oy+j];
            rowsum+=values[pos]*w; rowweight+=w;
          }
        }
        sum+=rowsum; weight+=rowweight;
      }
      sums[k]+=sum; counts[k]+=weight;
    }
    if (gaqsig()) { rc=1; goto done; }
  }
  for (k=0;k<nz;k++) { outmask[k]=counts[k]>0; out[k]=outmask[k] ? sums[k]/counts[k] : undef; }
  rc=0;
done:
  if (plane) gagfre(plane);
  if (xw) gree(xw,"seriesxw"); if (yw) gree(yw,"seriesyw");
  if (sums) gree(sums,"seriessum"); if (counts) gree(counts,"seriescount");
  if (values) gree(values,"seriesvalues"); if (mask) gree(mask,"seriesmask");
  return rc;
}


/* Area means at several times: one serial I/O producer, bounded batches,
   and independent per-time reductions. No shared parser/file state is
   touched by worker threads. Only a complete simple AAVE call is eligible. */
gaint gadeftimeseries(char *expression, struct gastat *pst, gaint first, gaint last,
                       struct gagrid *shape, gadouble *out, char *outmask,
                       gadouble undef, gaint reuse, gaint progress) {
  struct gafunc call;
  struct gafile *file=pst->pfid;
  struct gavar *var;
  struct gastat sample=*pst;
  struct gagrid *plane=NULL,read;
  char name[9],*mask=NULL;
  gadouble bounds[4],*values=NULL,*xw=NULL,*yw=NULL;
  size_t size,n;
  gaint i,dim,wflag=0,rc=-1,chunk,t,count,k,j;
  if (last<=first || shape->idim>=0 || !reduction_call(expression,name,&call) ||
      strcmp(name,"aave") || call.argnum!=5) return -1;
  if (reduction_defined(call.argpnt[0],pst))
    return aave_defined_span(&call,pst,3,first,last,out,outmask,undef);
  var=ave_plain_var(call.argpnt[0],pst,NULL);
  if (!var || file->ppflag || file->idxflg || first<1 || last>file->dnum[3]) return -1;
  /* Serial NetCDF/HDF5 readers benefit only when enough independent times
     amortize the producer workspace and keep several workers busy. */
  if (!file->adios2flg && file->ncflg &&
      (last-first+1<8 || ga_omp_get_threads()<4)) return -1;
  for (i=0;i<4;i++) {
    if (!dimprs(call.argpnt[i+1],pst,file,&dim,bounds+i,1,&wflag) || dim!=i/2) return -1;
    if (!wflag) bounds[i]+=(i%2 ? 0.5 : -0.5);
  }
  for (i=0;i<2;i++) {
    sample.dmin[i]=file->gr2ab[i](file->grvals[i],bounds[2*i]);
    sample.dmax[i]=file->gr2ab[i](file->grvals[i],bounds[2*i+1]);
  }
  sample.idim=0; sample.jdim=1;
  if (gaexpr("lon",&sample)) return 1;
  if (sample.type!=1) { gafree(&sample); return -1; }
  plane=sample.result.pgr; read=*plane; read.pvar=var;
  if ((file->wrap && (read.dimmin[0]<1 || read.dimmax[0]>file->dnum[0])) ||
      read.dimmin[4]<1 || read.dimmax[4]>file->dnum[4]) goto done;
  for (i=0;i<2;i++) {
    if (read.dimmin[i]<1) read.dimmin[i]=1;
    if (read.dimmax[i]>file->dnum[i]) read.dimmax[i]=file->dnum[i];
    if (read.dimmin[i]>read.dimmax[i]) goto done;
  }
  read.isiz=read.dimmax[0]-read.dimmin[0]+1;
  read.jsiz=read.dimmax[1]-read.dimmin[1]+1;
  size=(size_t)read.isiz*read.jsiz;
  chunk=(gaint)(64.0*1024*1024/((double)size*(sizeof(*values)+1)));
  if (chunk>256) chunk=256;
  /* Serial readers need only enough planes to keep workers occupied. */
  if (!file->adios2flg) {
    gaint target=ga_omp_get_threads()==1 ? 1 : 4*ga_omp_get_threads();
    if (chunk>target) chunk=target;
  }
  if (chunk>last-first+1-reuse) chunk=last-first+1-reuse;
  if (chunk<1) goto done;
  n=(size_t)chunk*size;
  values=galloc(n*sizeof(*values),"areatimes"); mask=galloc(n,"areatimemasks");
  xw=galloc((size_t)read.isiz*sizeof(*xw),"areatimexw");
  yw=galloc((size_t)read.jsiz*sizeof(*yw),"areatimeyw");
  if (!values || !mask || !xw || !yw) goto done;
  aave_weights(&read,sample.dmin[0],sample.dmax[0],sample.dmin[1],sample.dmax[1],1,xw,yw);
  if (reuse) { outmask[0]=shape->umask[0]; out[0]=outmask[0] ? shape->grid[0] : undef; }
  for (t=first+reuse;t<=last;t+=count) {
    count=last-t+1<chunk ? last-t+1 : chunk;
    if (gaqsig()) { rc=1; goto done; }
    rc=gagrdsteps(&read,t,count,1,values,mask);
    if (rc) goto done;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(j) \
    if(count>1 && ga_omp_parallelize((double)size*count>INT_MAX ? INT_MAX : (gaint)(size*count)))
#endif
    for (k=0;k<count;k++) {
      struct gagrid view=read;
      gadouble sum=0,weight=0;
      view.grid=values+(size_t)k*size; view.umask=mask+(size_t)k*size;
      for (j=0;j<read.jsiz;j++) {
        gadouble rowsum,rowweight;
        aave_row(&view,(size_t)j*read.isiz,xw,yw[j],1,&rowsum,&rowweight);
        sum+=rowsum; weight+=rowweight;
      }
      outmask[t-first+k]=weight>0;
      out[t-first+k]=weight>0 ? sum/weight : undef;
    }
    gaprogstep(progress+t-first+count);
  }
  rc=gaqsig() ? 1 : 0;
done:
  if (plane) gagfre(plane);
  if (values) gree(values,"areatimes"); if (mask) gree(mask,"areatimemasks");
  if (xw) gree(xw,"areatimexw"); if (yw) gree(yw,"areatimeyw");
  return rc;
}


static gaint ffgint_run (struct gafunc *, struct gastat *);

/* gint, with its progress shown while it runs */
gaint ffgint (struct gafunc *pfc, struct gastat *pst) {
gaint lv, rc;
  lv = gaprogbeg("gint", 0);
  rc = ffgint_run(pfc, pst);
  gaprogend(lv);
  return (rc);
}

static gaint ffgint_run (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgr1, *pgr;
struct gafile *pfi;
gadouble (*conv) (gadouble *, gadouble);
gadouble gr1,gr2,*sum, *val;
gadouble wt, abs, abslo, abshi, d2r;
gaint i, rc, siz, dim, d, d1, d2, dim2, ilin, bndflg, wflag=0;
char *ch,*sumu,*valu;

  d2r = M_PI/180;

  /* Check for valid number of args       */
  if (pfc->argnum<3 || pfc->argnum>4) {
    gaprnt (0,"Error from GINT:  Too many or too few args \n");
    gaprnt (0,"                  3 or 4 arguments expected \n");
    return (1);
  }

  /* Parse the dimension expression       */
  pfi = pst->pfid;
  ch = dimprs (pfc->argpnt[1], pst, pfi, &dim, &gr1, 1, &wflag);
  if (ch==NULL || wflag==2) {
    gaprnt (0,"Error from GINT:  1st dimension expression invalid\n");
    if (wflag==2) gaprnt (0,"  offt expression not supported as an arg to GINT\n");
    return (1);
  }

  /* Now parse the 2nd dimension expression.  */
  ch = dimprs (pfc->argpnt[2], pst, pfi, &dim2, &gr2, 1, &wflag);
  if (ch==NULL || dim2!=dim || gr2<gr1 || wflag==2) {
    gaprnt(0,"Error from GINT:  2nd dimension expression invalid\n");
    if (dim2!=dim) gaprnt (0,"  start and end points have different dimensions\n");
    if (gr2<gr1)   gaprnt (0,"  end grid point is less than start grid point \n");
    if (wflag==2)  gaprnt (0,"  offt expression not supported as an arg to GINT\n");
    return (1);
  }

  /* Check for 4th argument.  Should be flags.  */
  bndflg = 0;
  if (pfc->argnum == 5) {
    if (*(pfc->argpnt[4]) == '-' &&
        *(pfc->argpnt[4]+1) == 'b') bndflg = 1;
    else {
      gaprnt (0,"Error from GINT: Invalid option flags\n");
      return(1);
    }
  }

  /* Get the first grid.             */
  if (pst->idim==dim) {          /* Fewer varying dims if user */
    pst->idim = pst->jdim;       /* integrating over varng dim */
    pst->jdim = -1;
  }
  ilin = pfi->linear[dim];
  if (pst->jdim==dim) pst->jdim = -1;
  d1 = ceil(gr1-0.001);          /* dim limits must be integer */
  d2 = floor(gr2+0.001);

  if(mfcmn.warnflg > 0) {
    snprintf(pout,1255,"Integrating.  dim = %i, start = %i, end = %i\n", dim, d1, d2);
    gaprnt (2,pout);
  }
  gaprogtotal(d2-d1+1);

  wt = 1.0;                     /* Figure out weight for 1st grid */
  if (dim==3) {
    gr2t (pfi->grvals[3],d1,&(pst->tmin));
    pst->tmax = pst->tmin;
    wt = *(pfi->grvals[3]+5) + *(pfi->grvals[3]+6);
  } else {
    conv = pfi->gr2ab[dim];
    abs = conv(pfi->grvals[dim],d1);
    pst->dmin[dim] = abs;
    pst->dmax[dim] = abs;
    abslo = conv(pfi->grvals[dim],d1-0.5);
    abshi = conv(pfi->grvals[dim],d1+0.5);
    wt = abshi - abslo;
    wt = fabs(wt);
    if (dim==0 || dim==1) wt = wt*6.37E6*d2r;
  }
  if (bndflg) wt = wt*0.5;

  rc = gaexpr(pfc->argpnt[0],pst);     /* Get first grid */
  if (rc) return (rc);
  if (pst->type == 0) {
    gafree (pst);
    return(-1);
  }
  pgr1 = pst->result.pgr;

  if (dim==0) {                        /* Adjust weights if needed  */
    if (pgr1->idim==1) cosadj(pgr1);
    else wt = wt * cos(pst->dmin[1]*d2r);
  }

  siz = pgr1->isiz * pgr1->jsiz;       /* Apply weights to this grid */
  sum  = pgr1->grid;
  sumu = pgr1->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(siz))
#endif
  for (i=0; i<siz; i++) {
    if (sumu[i]!=0) sum[i] = sum[i] * wt;
  }

  d = d1 + 1;
  if (d>d2) return(0);                 /* If only one grid, return  */

  /* Now sum the rest of the grids into the first grid */

  rc = 0;
  for (d=d; d<=d2 && !rc; d++) {
    wt = 1.0;
    if (dim==3) {
      gr2t (pfi->grvals[3],d,&(pst->tmin));
      pst->tmax = pst->tmin;
      wt = *(pfi->grvals[3]+5) + *(pfi->grvals[3]+6);
    } else {
      conv = pfi->gr2ab[dim];
      abs = conv(pfi->grvals[dim],d);
      pst->dmin[dim] = abs;
      pst->dmax[dim] = abs;
      abslo = conv(pfi->grvals[dim],d-0.5);
      abshi = conv(pfi->grvals[dim],d+0.5);
      wt = abshi - abslo;
      wt = fabs(wt);
      if (dim==0 || dim==1) wt = wt*6.37E6*d2r;
    }
    if (d==d2 && bndflg) wt = wt*0.5;
    rc = gaexpr(pfc->argpnt[0],pst);
    if (!rc && pst->type==0) rc = -1;
    if (!rc) {
      pgr = pst->result.pgr;
      if (dim==0) {                   /* Adjust weights if needed  */
        if (pgr->idim==1) cosadj(pgr);
        else wt = wt * cos(pst->dmin[1]*d2r);
      }
      val  = pgr->grid;
      valu = pgr->umask;
      sum  = pgr1->grid;
      sumu = pgr1->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(siz))
#endif
      for (i=0; i<siz; i++) {
        if (valu[i]!=0) {
          val[i] = val[i]*wt;
          if (sumu[i]==0) {
	    sum[i]  = val[i];
	    sumu[i] = 1;
	  }
          else sum[i] += val[i];
        }
      }
      gagfre(pgr);
      gaprogstep(d-d1+1);
    }
  }

  if (rc) {
    if (rc==-1) gafree (pst);
    gagfre(pgr1);
    gaprnt (0,"Error from GINT:  Error getting grids \n");
    return (rc);
  }

  pst->type = 1;
  pst->result.pgr = pgr1;

  return (0);
}

void cosadj (struct gagrid *pgr) {
gadouble *ltvals;
gadouble (*ltconv) (gadouble *, gadouble);
gadouble lat,*gr,d2r;
gaint i,j;
char *gru;
  d2r = M_PI/180;
  ltvals = pgr->ivals;
  ltconv = pgr->igrab;
  gr = pgr->grid;
  gru = pgr->umask;
  for (j=0; j<pgr->jsiz; j++) {
    for (i=0; i<pgr->isiz; i++) {
      lat  = ltconv(ltvals,(gadouble)(i+pgr->dimmin[1]))*d2r;
      if (*gru!=0) *gr = *gr * cos(lat);
      gr++; gru++;
    }
  }
}

gaint ffhdiv (struct gafunc *pfc, struct gastat *pst) {
gaint rc,size,i,j;
struct gagrid *pgr1, *pgr2;
gadouble *result;
gadouble *p1, *p2, *p3, *p4, *p;
gadouble lat2, lat4, lat, lon1, lon3, ri, rj, temp, d2r;
gadouble *lnvals, *ltvals;
gadouble (*lnconv) (gadouble *, gadouble);
gadouble (*ltconv) (gadouble *, gadouble);
char *resultu, *p1u, *p2u, *p3u, *p4u, *pu;
size_t sz;

  d2r = M_PI/180;
  result = NULL;
  resultu = NULL;

  /* Check for user errors */
  if (pfc->argnum!=2) {
    gaprnt (0,"Error from HDIVG:  Too many or too few args \n");
    gaprnt (0,"                   Two arguments expected \n");
    return (1);
  }
  if (pst->idim!=0 || pst->jdim!=1) {
    gaprnt (0,"Error from HDIVG:  Invalid dimension environment\n");
    gaprnt (0,"  Horizontal environment (X, Y Varying) is required\n");
    return (1);
  }

  /* Get the u and v fields.  User responsible for validity. */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    gafree (pst);
    return (-1);
  }
  pgr1 = pst->result.pgr;
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) {
    gagfre(pgr1);
    return (rc);
  }
  if (pst->type==0) {
    gafree (pst);
    gagfre(pgr1);
    return (-1);
  }
  pgr2 = pst->result.pgr;

  /* Check that an operation between these grids is valid */
  if (gagchk(pgr1,pgr2,pst->idim) ||
      gagchk(pgr1,pgr2,pst->jdim) ) {
    gaprnt (0,"Error from HDIVG:  Incompatable grids \n");
    gaprnt (0,"                   Dimension ranges unequal \n");
    goto erret;
  }

  /* Get memory for result grid and umask */
  size = pgr1->isiz * pgr1->jsiz;
  sz = size*sizeof(gadouble);
  result = (gadouble *)galloc(sz,"hdivres");
  if (result==NULL) {
    gaprnt (0,"Memory Allocation Error:  HDIVG function \n");
    goto erret;
  }
  sz = size*sizeof(char);
  resultu = (char *)galloc(sz,"hdivresu");
  if (resultu==NULL) {
    gaprnt (0,"Memory Allocation Error:  HDIVG function \n");
    goto erret;
  }

  /* Perform the divergence calculation except at grid borders */
  for (i=0; i<size; i++) *(resultu+i) = 0;

  lnvals = pgr1->ivals;
  ltvals = pgr1->jvals;
  lnconv = pgr1->igrab;
  ltconv = pgr1->jgrab;

  /*             p4
                 |
             p1--p--p3
                 |
                 p2                           */

  p = result + (pgr1->isiz + 1);
  p1 = pgr1->grid + pgr1->isiz;
  p2 = pgr2->grid + 1;
  p3 = p1 + 2;
  p4 = p2 + (2 * pgr1->isiz);

  pu = resultu + (pgr1->isiz + 1);
  p1u = pgr2->umask + pgr2->isiz;
  p2u = pgr1->umask + 1;
  p3u = p1u + 2;
  p4u = p2u + (2 * pgr1->isiz);

  for (j=(pgr1->dimmin[1]+1); j<pgr1->dimmax[1]; j++) {
    rj = (gadouble)j;
    lat  = ltconv(ltvals,rj    ) * d2r;
    lat2 = ltconv(ltvals,rj-1.0) * d2r;
    lat4 = ltconv(ltvals,rj+1.0) * d2r;
    for (i=(pgr1->dimmin[0]+1); i<pgr1->dimmax[0]; i++) {
      if (*p1u!=0 &&
	  *p2u!=0 &&
          *p3u!=0 &&
	  *p4u!=0 ) {
        ri = (gadouble)i;
        lon1 = lnconv(lnvals,ri-1.0) * d2r;
        lon3 = lnconv(lnvals,ri+1.0) * d2r;
        *p = (*p3 - *p1)/(lon3-lon1);
        *p = *p + (*p4*cos(lat4) - *p2*cos(lat2))/(lat4-lat2);
        temp = 6.37E6 * cos(lat);
        if (temp>1E-10) {
	  *p = *p / temp;
	  *pu = 1;
	}
        else *pu = 0;
      }
      p++; p1++; p2++; p3++; p4++;
      pu++; p1u++; p2u++; p3u++; p4u++;
    }
    p+=2; p1+=2; p2+=2; p3+=2; p4+=2;
    pu+=2; p1u+=2; p2u+=2; p3u+=2; p4u+=2;
  }
  gree(pgr1->grid,"f417");
  gree(pgr1->umask,"f418");
  gagfre(pgr2);
  pgr1->grid = result;
  pgr1->umask = resultu;
  pst->type = 1;
  pst->result.pgr = pgr1;
  return (0);

erret:
  if (result) gree(result,"f419");
  gagfre(pgr1);
  gagfre(pgr2);
  return (1);

}

gaint ffhcrl (struct gafunc *pfc, struct gastat *pst) {
gaint rc,size,i,j;
struct gagrid *pgr1, *pgr2;
gadouble *result=NULL;
gadouble *p1, *p2, *p3, *p4, *p;
gadouble lat2, lat4, lat, lon1, lon3, ri, rj, temp, d2r;
gadouble *lnvals, *ltvals;
gadouble (*lnconv) (gadouble *, gadouble);
gadouble (*ltconv) (gadouble *, gadouble);
char *p1u, *p2u, *p3u, *p4u, *pu, *resultu;
size_t sz;

  d2r = M_PI/180;

  /* Check for user errors */

  if (pfc->argnum!=2) {
    gaprnt (0,"Error from HCURL:  Too many or too few args \n");
    gaprnt (0,"                   Two arguments expected \n");
    return (1);
  }
  if (pst->idim!=0 || pst->jdim!=1) {
    gaprnt (0,"Error from HCURL:  Invalid dimension environment\n");
    gaprnt (0,"  Horizontal environment (X, Y Varying) is required\n");
    return (1);
  }

  /* Get the u and v fields.  User responsible for validity. */

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    gafree (pst);
    return(-1);
  }
  pgr1 = pst->result.pgr;
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) {
    gagfre(pgr1);
    return (rc);
  }
  if (pst->type==0) {
    gafree (pst);
    gagfre(pgr1);
    return (-1);
  }
  pgr2 = pst->result.pgr;

  /* Check that an operation between these grids is valid */

  if (gagchk(pgr1,pgr2,pst->idim) ||
      gagchk(pgr1,pgr2,pst->jdim) ) {
    gaprnt (0,"Error from HCURL:  Incompatable grids \n");
    gaprnt (0,"                   Dimension ranges unequal \n");
    goto erret;
  }

  /* Get memory for result grid and umask */
  size = pgr1->isiz * pgr1->jsiz;
  sz = size*sizeof(gadouble);
  result = (gadouble *)galloc(sz,"hcurlres");
  if (result==NULL) {
    gaprnt (0,"Memory Allocation Error:  HCURL function\n");
    goto erret;
  }
  sz = size*sizeof(char);
  resultu = (char *)galloc(sz,"hcurlresu");
  if (resultu==NULL) {
    gaprnt (0,"Memory Allocation Error:  HCURL function\n");
    goto erret;
  }

  /* Perform the vorticity calculation except at grid borders */
  for (i=0; i<size; i++) *(resultu+i) = 0;

  lnvals = pgr1->ivals;
  ltvals = pgr1->jvals;
  lnconv = pgr1->igrab;
  ltconv = pgr1->jgrab;

  /*             p4
                 |
             p1--p--p3
                 |
                 p2                           */

  p  = result  + (pgr1->isiz + 1);
  p1  = pgr2->grid + pgr2->isiz;
  p2  = pgr1->grid + 1;
  p3  = p1 + 2;
  p4  = p2  + (2 * pgr1->isiz);

  pu = resultu + (pgr1->isiz + 1);
  p1u = pgr2->umask + pgr2->isiz;
  p2u = pgr1->umask + 1;
  p3u = p1u + 2;
  p4u = p2u + (2 * pgr1->isiz);

  for (j=(pgr1->dimmin[1]+1); j<pgr1->dimmax[1]; j++) {
    rj = (gadouble)j;
    lat  = ltconv(ltvals,rj    ) * d2r;
    lat2 = ltconv(ltvals,rj-1.0) * d2r;
    lat4 = ltconv(ltvals,rj+1.0) * d2r;
    for (i=(pgr1->dimmin[0]+1); i<pgr1->dimmax[0]; i++) {
      if (*p1u!=0 &&
	  *p2u!=0 &&
          *p3u!=0 &&
	  *p4u!=0 ) {
        ri = (gadouble)i;
        lon1 = lnconv(lnvals,ri-1.0) * d2r;
        lon3 = lnconv(lnvals,ri+1.0) * d2r;
        *p = (*p3 - *p1)/(lon3-lon1);
        *p = *p - (*p4*cos(lat4) - *p2*cos(lat2))/(lat4-lat2);
        temp = 6.37E6 * cos(lat);
        if (temp>1E-10) {
	  *p = *p / temp;
	  *pu = 1;
	}
        else {
	  *pu = 0;
	}
      }
      p++;  p1++;  p2++;  p3++;  p4++;
      pu++; p1u++; p2u++; p3u++; p4u++;
    }
    p+=2;  p1+=2;  p2+=2;  p3+=2;  p4+=2;
    pu+=2; p1u+=2; p2u+=2; p3u+=2; p4u+=2;
  }
  gree(pgr1->grid,"f420");
  gree(pgr1->umask,"f421");
  gagfre (pgr2);
  pgr1->grid = result;
  pgr1->umask = resultu;
  pst->type = 1;
  pst->result.pgr = pgr1;
  return (0);

erret:
  if (result) gree(result,"f422");
  gagfre(pgr1);
  gagfre(pgr2);
  return (1);

}

gaint fftv2q (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
  rc = fftv2 (pfc, pst, 0);
  return (rc);
}

gaint fftv2t (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
  rc = fftv2 (pfc, pst, 1);
  return (rc);
}

gaint fftv2 (struct gafunc *pfc, struct gastat *pst, gaint tflag) {
struct gagrid *pgrtv, *pgrrh;
gadouble *lvvals;
gadouble (*lvconv) (gadouble *, gadouble);
gaint i,j, rc, errcnt;
gadouble *tv, *rh, t, q, p;
char *tvu, *rhu;

  if (pfc->argnum!=2) {
    if (tflag) {
      gaprnt (0,"Error from TVRH2T:  Too many or too few args \n");
    } else {
      gaprnt (0,"Error from TVRH2Q:  Too many or too few args \n");
    }
    gaprnt (0,"                    Two arguments expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type == 0) {
    gafree (pst);
    return(-1);
  }
  pgrtv = pst->result.pgr;

  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) return (rc);
  if (pst->type == 0) {
    gagfre(pgrtv);
    gafree (pst);
    return(-1);
  }
  pgrrh = pst->result.pgr;

  if ((pgrrh->idim!=pgrtv->idim) ||
      (pgrrh->jdim!=pgrtv->jdim) ||
      (pgrrh->idim>-1 && gagchk(pgrrh,pgrtv,pgrrh->idim)) ||
      (pgrrh->jdim>-1 && gagchk(pgrrh,pgrtv,pgrrh->jdim)) ) {
    gaprnt (0,"Error in TVRH2Q: Grids don't have same scaling");
    gagfre (pgrtv);
    gagfre (pgrrh);
    return (1);
  }

  errcnt = 0;
  if (pgrrh->idim == 2) {
    lvconv = pgrrh->igrab;
    lvvals = pgrrh->ivals;
    tv = pgrtv->grid;
    rh = pgrrh->grid;
    tvu = pgrtv->umask;
    rhu = pgrrh->umask;
    for (j=0; j<pgrrh->jsiz; j++) {
      for (i=0; i<pgrrh->isiz; i++) {
        if (*rhu==0 || *tvu==0) {
          *rhu = 0;
        } else {
          p = lvconv(lvvals, (gadouble)(i+pgrrh->dimmin[2]));
          rc = tvrh2q (p, *tv, *rh, &q, &t);
          if (rc) {
            *rhu = 0;
            errcnt++;
          } else {
            if (tflag) {
	      *rh = t;
	      *rhu = 1;
	    }
            else {
	      *rh = q;
	      *rhu = 1;
	    }
          }
        }
        rh++;  tv++;
        rhu++; tvu++;
      }
    }
  } else if (pgrrh->jdim == 2) {
    lvconv = pgrrh->jgrab;
    lvvals = pgrrh->jvals;
    tv = pgrtv->grid;
    rh = pgrrh->grid;
    tvu = pgrtv->umask;
    rhu = pgrrh->umask;
    for (j=0; j<pgrrh->jsiz; j++) {
      p = lvconv(lvvals, (gadouble)(j+pgrrh->dimmin[2]));
      for (i=0; i<pgrrh->isiz; i++) {
        if (*rhu==0 || *tvu==0) {
          *rhu = 0;
        } else {
          rc = tvrh2q (p, *tv, *rh, &q, &t);
          if (rc) {
            *rhu = 0;
            errcnt++;
          } else {
            if (tflag) {
	      *rh = t;
	      *rhu = 1;
	    }
            else {
	      *rh = q;
	      *rhu = 1;
	    }
          }
        }
        rh++; tv++;
        rhu++; tvu++;
      }
    }
  } else {
    p = pst->dmin[2];
    snprintf(pout,1255," Using fixed pressure level %g mb\n",p);
    if (tflag) {
      gaprnt (2,"Notice from TVRH2T:");
    } else {
      gaprnt (2,"Notice from TVRH2Q:");
    }
    gaprnt (1,pout);
    tv = pgrtv->grid;
    rh = pgrrh->grid;
    tvu = pgrtv->umask;
    rhu = pgrrh->umask;
    for (j=0; j<pgrrh->jsiz; j++) {
      for (i=0; i<pgrrh->isiz; i++) {
        if (*rhu==0 || *tvu==0) {
          *rhu = 0;
        } else {
          rc = tvrh2q (p, *tv, *rh, &q, &t);
          if (rc) {
            *rhu = 0;
            errcnt++;
          } else {
            if (tflag) {
	      *rh = t;
	      *rhu = 1;
	    }
            else {
	      *rh = q;
	      *rhu = 1;
	    }
          }
        }
        rh++; tv++;
        rhu++; tvu++;
      }
    }
  }

  if (errcnt) {
    snprintf(pout,1255," Convergence failed for %i grid points\n",errcnt);
    if (tflag) {
      gaprnt (1,"Warning from TVRH2T:");
    } else {
      gaprnt (1,"Warning from TVRH2Q:");
    }
    gaprnt (1,pout);
  }
  gagfre (pgrtv);
  return (0);
}

/* Routine to convert tv and rh to t and q.  FORTRAN version
   provided by J. Kinter.  Converted to C by B. Doty.    */

gaint tvrh2q (gadouble p, gadouble tv, gadouble rh, gadouble *qret, gadouble *tret) {
gadouble eps,a,b,c,t,q,al10,tc,esat,desdt,qr,f,denom,dfdt,fn,qn,tn;
gaint i;

  eps = 0.622;
  a = 0.7854;
  b = 0.03477;
  c = 0.00412;

  /* Convert rh to fraction.  Convert pressure to pascals. */

  rh = rh * 0.01;
  p = p*100.0;

  /* Set first guess for t and q */

  t = tv;
  q = 0.01 * rh * eps;

  /* Iterate to convergence */

  al10 = log(10.0);
  for (i=0; i<25; i++) {

    /* Set saturation vapor pressure (compute from smisthonian tables)*/

    tc = t - 273.16;
    esat = 100.0 * exp(al10*(a+b*tc)/(1.0+c*tc));
    desdt = al10 * esat * (b-a*c)/((1.0+c*tc)*(1.0+c*tc));
    qr = eps*rh*esat/(p-(1.0-eps)*esat);
    f = q-qr;

    /* Compute derivative of q wrt q given fixed virtual temp and
       rh (constant pressure) */

    denom = p-(1.0-eps)*esat;
    denom = denom * denom;
    dfdt = eps*rh*((p-(1.0-eps)*esat)*desdt+esat*(1.0-eps)*desdt);
    dfdt = 1.0-dfdt/denom;

    /* Newton's method */

    fn = f-f/dfdt;
    qn = fn+qr;
    tn = eps*tv*((1.0-qn)/(eps*(1.0-qn)+qn*(1.0-eps)));

    /* Test for convergence */

    if (fabs((tn-t)/t) <= 1.0E-6) break;
    q = qn;
    t = tn;
  }

  /* Print results */

  if (i==25) return(1);
  *qret = qn;
  *tret = tn;
  return(0);
}

/* Saturation diagnostics use the Flatau (1992, table 4) coefficients and
   low-temperature Goff-Gratch branches of VVM P3 polysvp1. T is K, es is Pa.
   Automatic phase is P3's ice selection: ice strictly below 273.15 K,
   liquid otherwise. Explicit phase 0 selects liquid at all temperatures. */
static gadouble saturation_es(gadouble t, gadouble phase) {
  static const gadouble liquid[9]={6.11239921,0.443987641,0.142986287e-1,
    0.264847430e-3,0.302950461e-5,0.206739458e-7,
    0.640689451e-10,-0.952447341e-13,-0.976195544e-15};
  static const gadouble ice[9]={6.11147274,0.503160820,0.188439774e-1,
    0.420895665e-3,0.615021634e-5,0.602588177e-7,
    0.385852041e-9,0.146898966e-11,0.252751365e-14};
  const gadouble *coeff;
  gadouble dt,value,exponent;
  gaint k,frozen;
  if (!isfinite(t) || !isfinite(phase) || t<=0) return NAN;
  frozen=phase>0 && t<273.15;
  if (t>=(frozen ? 195.8 : 202.0)) {
    coeff=frozen ? ice : liquid;
    dt=t-273.15; value=coeff[8];
    for (k=7;k>=0;k--) value=coeff[k]+dt*value;
    value*=100;
  } else {
    if (frozen) exponent=-9.09718*(273.16/t-1)-3.56654*log10(273.16/t)+
                         0.876793*(1-t/273.16)+log10(6.1071);
    else exponent=-7.90298*(373.16/t-1)+5.02808*log10(373.16/t)-
                  1.3816e-7*(pow(10,11.344*(1-t/373.16))-1)+
                  8.1328e-3*(pow(10,-3.49149*(373.16/t-1))-1)+log10(1013.246);
    value=pow(10,exponent)*100;
  }
  return isfinite(value) && value>=0 ? value : NAN;
}

/* As in ordinary GrADS arithmetic, align shared coordinate axes and expand
   a scalar or a one-axis grid along axes it lacks. No interpolation. a is
   temperature (mode 0) or es (mode 1); b is phase or pressure respectively.
   Success consumes both inputs, returning the grid with more varying axes. */
static struct gagrid *saturation_combine(struct gagrid *a, struct gagrid *b, gaint mode) {
  struct gagrid *out,*small;
  gaint na=(a->idim>=0)+(a->jdim>=0),nb=(b->idim>=0)+(b->jdim>=0);
  gaint broadcast=0,n;
  size_t i,j,size;
  gadouble av,bv,value;
  out=na>=nb ? a : b; small=out==a ? b : a;
  if (na==nb) {
    if (a->idim!=b->idim || a->jdim!=b->jdim ||
        a->isiz!=b->isiz || a->jsiz!=b->jsiz ||
        gagchk(a,b,a->idim) || gagchk(a,b,a->jdim)) goto incompatible;
  } else if (small->idim<0) broadcast=1;
  else {
    if (gagchk(small,out,small->idim)) goto incompatible;
    if (small->idim==out->idim) broadcast=2;
    else if (small->idim==out->jdim) broadcast=3;
    else goto incompatible;
  }
  size=(size_t)out->isiz*out->jsiz;
  if (size>(size_t)INT_MAX) goto incompatible;
  n=(gaint)size;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(j,av,bv,value) if(ga_omp_parallelize(n))
#endif
  for (i=0;i<(size_t)n;i++) {
    j=broadcast==0 ? i : broadcast==1 ? 0 : broadcast==2 ? i%small->isiz : i/out->isiz;
    if (!out->umask[i] || !small->umask[j]) { out->umask[i]=0; out->grid[i]=out->undef; continue; }
    av=a==out ? out->grid[i] : small->grid[j];
    bv=b==out ? out->grid[i] : small->grid[j];
    if (!mode) value=saturation_es(av,bv);
    else value=isfinite(av) && isfinite(bv) && av>=0 && bv>0 ?
                 0.622*av/fmax(1.e-3,bv-av) : NAN;
    if (!isfinite(value)) { out->umask[i]=0; out->grid[i]=out->undef; }
    else out->grid[i]=value;
  }
  gagfre(small);
  return out;
incompatible:
  gaprnt(0,"Saturation error: input grids must have matching shared coordinate axes\n");
  return NULL;
}

static void saturation_description(gaint mixing) {
  gaprnt(0,mixing ? "QVSAT: saturation water vapor mixing ratio (kg/kg of dry air).\n" :
                   "ESAT: saturation vapor pressure (Pa).\n");
}

static gaint saturation_function(struct gafunc *pfc, struct gastat *pst, gaint mixing) {
  struct gastat other;
  struct gagrid *grid=NULL,*operand=NULL,*result;
  gaint phase_arg=mixing ? 2 : 1,n,rc,automatic;
  size_t i,size;
  gadouble value;
  char *phase_text;
  if (pfc->argnum<phase_arg || pfc->argnum>phase_arg+1) {
    saturation_description(mixing);
    gaprnt(0,mixing ? "QVSAT error: expected qvsat(T,p[,phase=auto]); T in K, p in Pa\n" :
                     "ESAT error: expected esat(T[,phase=auto]); T in K\n");
    return 1;
  }
  automatic=pfc->argnum==phase_arg;
  if (!automatic) {
    phase_text=pfc->argpnt[phase_arg];
    while (*phase_text==' ') phase_text++;
    if (!strncmp(phase_text,"auto",4)) {
      phase_text+=4;
      while (*phase_text==' ') phase_text++;
      automatic=!*phase_text;
    }
  }
  rc=gaexpr(pfc->argpnt[0],pst);
  if (rc) { saturation_description(mixing); return rc; }
  if (pst->type!=1) { gafree(pst); goto invalid; }
  grid=pst->result.pgr;
  if (!automatic) {
    other=*pst;
    if (gaexpr(pfc->argpnt[phase_arg],&other)) goto error;
    if (other.type!=1) { gafree(&other); goto invalid; }
    operand=other.result.pgr;
    result=saturation_combine(grid,operand,0);
    if (!result) goto error;
    grid=result; operand=NULL;
    pst->result.pgr=grid;
  } else {
    size=(size_t)grid->isiz*grid->jsiz;
    if (size>(size_t)INT_MAX) goto invalid;
    n=(gaint)size;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(value) if(ga_omp_parallelize(n))
#endif
    for (i=0;i<(size_t)n;i++) {
      value=grid->umask[i] ? saturation_es(grid->grid[i],1) : NAN;
      if (!isfinite(value)) { grid->umask[i]=0; grid->grid[i]=grid->undef; }
      else grid->grid[i]=value;
    }
  }
  if (mixing) {
    other=*pst;
    if (gaexpr(pfc->argpnt[1],&other)) goto error;
    if (other.type!=1) { gafree(&other); goto invalid; }
    operand=other.result.pgr;
    result=saturation_combine(grid,operand,1);
    if (!result) goto error;
    grid=result; operand=NULL;
  }
  pst->type=1; pst->result.pgr=grid;
  return 0;
invalid:
  gaprnt(0,"Saturation error: inputs must be grid expressions or numbers\n");
error:
  saturation_description(mixing);
  if (grid) gagfre(grid);
  if (operand) gagfre(operand);
  pst->type=1; pst->result.pgr=NULL;
  return 1;
}

gaint ffesat(struct gafunc *pfc, struct gastat *pst) {
  return saturation_function(pfc,pst,0);
}

gaint ffqvsat(struct gafunc *pfc, struct gastat *pst) {
  return saturation_function(pfc,pst,1);
}

/* Identify an unmodified field (also file-qualified), for indexed I/O. All
   other expressions use the normal evaluator once per requested Z level. */
static struct gafile *zslice_field(char *expr, struct gastat *pst,
                                  struct gavar **var, gaint *height) {
  struct gafile *file = pst->pfid;
  char name[17], *ch = expr;
  gaint n=0, number, i;
  *var = NULL; *height = 0;
  while (*ch==' ') ch++;
  if (*ch<'a' || *ch>'z') return NULL;
  while ((*ch>='a' && *ch<='z') || (*ch>='0' && *ch<='9') || *ch=='_') {
    if (n>=16) return NULL;
    name[n++] = *ch++;
  }
  name[n] = '\0';
  if (*ch=='.') {
    ch = intprs(ch+1,&number);
    if (!ch || number<1) return NULL;
    file = pst->pfi1;
    for (i=1; i<number && file; i++) file = file->pforw;
  } else if (getdfn(name,pst)!=NULL) return NULL;
  while (*ch==' ') ch++;
  if (*ch || !file || file->type!=1) return NULL;
  if (!strcmp(name,"lev")) { *height = 1; return file; }
  for (i=0; i<file->vnum; i++)
    if (cmpwrd(name,file->pvar1[i].abbrv)) {
      *var = file->pvar1+i;
      return file;
    }
  return NULL;
}

/* Select one (1-based) Z index per horizontal point. Invalid indices remain
   missing, rather than rounding or searching upwards. No interpolation. */
static gaint zslice_run(struct gafunc *pfc, struct gastat *pst) {
  struct gafile *file;
  struct gavar *var;
  struct gagrid *map=NULL, *out=NULL, *plane=NULL;
  gadouble value, level;
  gaint *indices=NULL, height, nz, first=1, k, rc=1, nused=0, done=0;
  gaint metadata=0, number;
  struct gafile *search;
  char coordinate[32], *expression;
  unsigned char *used=NULL;
  size_t i, size;

  if (pfc->argnum!=2 || !pst->pfid) {
    gaprnt(0,"ZSLICE error: expected zslice(expression,index_map) with a grid file open\n");
    return 1;
  }
  expression=pfc->argpnt[0];
  if (pst->idim==2) { pst->idim = pst->jdim; pst->jdim = -1; }
  if (pst->jdim==2) pst->jdim = -1;
  if (pst->idim>1 || pst->jdim>1 || pst->pfid->type==2 || pst->pfid->type==3) {
    gaprnt(0,"ZSLICE error: only X/Y may vary; fix T/E or use tloop/eloop\n");
    return 1;
  }
  file = zslice_field(pfc->argpnt[0],pst,&var,&height);
  if (!file) file = pst->pfid;
  nz = var && var->levels>0 ? var->levels : file->dnum[2];
  pst->dmin[2] = pst->dmax[2] = pst->pfid->gr2ab[2](pst->pfid->grvals[2],1.0);
  if (gaexpr(pfc->argpnt[1],pst)) return 1;
  if (pst->type!=1) { gafree(pst); goto invalid; }
  map = pst->result.pgr;
  if (map->idim>1 || map->jdim>1 || nz<1) goto invalid;
  size = (size_t)map->isiz*map->jsiz;
  for (i=0; i<size; i++)
    if (map->umask[i] && isfinite(map->grid[i]) && map->grid[i]>=1 &&
        map->grid[i]<=nz && floor(map->grid[i])==map->grid[i]) {
      first = (gaint)map->grid[i]; break;
    }
  level = file->gr2ab[2](file->grvals[2],(gadouble)first);
  pst->dmin[2] = pst->dmax[2] = level;
  /* A coordinate request supplies identical grid metadata without reading a
     data plane. Native readers can then gather only the requested indices. */
  if (var && var->levels>0 && !file->ppflag &&
      !file->idxflg && file->ncflg!=2) {
    search=pst->pfi1; number=1;
    while (search && search!=file) { search=search->pforw; number++; }
    if (search) {
      snprintf(coordinate,sizeof(coordinate),"lev.%d",number);
      expression=coordinate; metadata=1;
    }
  }
  if (gaexpr(expression,pst)) goto cleanup;
  if (pst->type!=1) { gafree(pst); goto invalid; }
  out = pst->result.pgr;
  if (metadata) { out->pvar=var; out->undef=file->undef; }
  if (out->idim>1 || out->jdim>1) goto invalid;
  if (map->idim>=0 && (map->idim!=out->idim || map->jdim!=out->jdim ||
      map->isiz!=out->isiz || map->jsiz!=out->jsiz ||
      gagchk(map,out,map->idim) || gagchk(map,out,map->jdim))) goto invalid;
  size = (size_t)out->isiz*out->jsiz;
  if (size>(size_t)INT_MAX) goto invalid;
  indices = galloc(size*sizeof(*indices),"zsliceindex");
  used = galloc((size_t)nz+1,"zslicelevels");
  if (!indices || !used) {
    gaprnt(0,"ZSLICE error: unable to allocate index buffer\n");
    goto cleanup;
  }
  memset(used,0,(size_t)nz+1);
  for (i=0; i<size; i++) {
    k = map->idim<0 ? 0 : (gaint)i;
    value = map->grid[k];
    indices[i] = 0;
    if (map->umask[k] && isfinite(value) && value>=1 && value<=nz && floor(value)==value) {
      indices[i] = (gaint)value;
      if (!used[indices[i]]) { used[indices[i]] = 1; nused++; }
    }
  }
  gagfre(map); map = NULL;
  gaprogtotal(nused);
  if (height) {
    for (i=0; i<size; i++) {
      out->umask[i] = indices[i]!=0;
      out->grid[i] = indices[i] ? file->gr2ab[2](file->grvals[2],indices[i]) : out->undef;
    }
    rc = 0; goto cleanup;
  }
  if (var && var->levels>0 && out->pfile==file && out->pvar==var && nused && (metadata || nused>1)) {
    rc = gagrdzslice(out,indices,out->grid,out->umask);
    if (rc>=0) goto cleanup;
  }
  if (metadata && nused) {
    if (gaexpr(pfc->argpnt[0],pst)) { rc=1; goto cleanup; }
    if (pst->type!=1) { gafree(pst); goto invalid; }
    plane=pst->result.pgr;
    if (plane->idim!=out->idim || plane->jdim!=out->jdim ||
        plane->isiz!=out->isiz || plane->jsiz!=out->jsiz ||
        gagchk(plane,out,out->idim) || gagchk(plane,out,out->jdim)) goto invalid;
    gagfre(out); out=plane; plane=NULL;
  }
  for (i=0; i<size; i++)
    if (!indices[i] || (indices[i]!=first && !(var && !var->levels))) {
      out->umask[i] = 0; out->grid[i] = out->undef;
    }
  rc = 0;
  for (k=1; k<=nz; k++) {
    if (!used[k]) continue;
    if (k!=first && !(var && !var->levels)) {
      level = file->gr2ab[2](file->grvals[2],(gadouble)k);
      pst->dmin[2] = pst->dmax[2] = level;
      if (gaexpr(pfc->argpnt[0],pst)) { rc=1; goto cleanup; }
      if (pst->type!=1) { gafree(pst); goto invalid; }
      plane = pst->result.pgr;
      if (plane->idim!=out->idim || plane->jdim!=out->jdim ||
          plane->isiz!=out->isiz || plane->jsiz!=out->jsiz ||
          gagchk(plane,out,out->idim) || gagchk(plane,out,out->jdim)) goto invalid;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize((gaint)size))
#endif
      for (i=0; i<size; i++) if (indices[i]==k) {
        out->umask[i] = plane->umask[i];
        out->grid[i] = plane->umask[i] ? plane->grid[i] : out->undef;
      }
      gagfre(plane); plane = NULL;
    }
    gaprogstep(++done);
  }
  goto cleanup;

invalid:
  gaprnt(0,"ZSLICE error: expression and index map must have matching horizontal grids\n");
  rc = 1;
cleanup:
  if (map) gagfre(map);
  if (plane) gagfre(plane);
  if (indices) gree(indices,"zsliceindex");
  if (used) gree(used,"zslicelevels");
  if (rc) { if (out) gagfre(out); pst->result.pgr=NULL; }
  else { pst->type=1; pst->result.pgr=out; }
  return rc;
}

gaint ffzslice(struct gafunc *pfc, struct gastat *pst) {
  gaint level=gaprogbeg("zslice",0), rc;
  rc = zslice_run(pfc,pst);
  gaprogend(level);
  return rc;
}

/* ZINT integrates a cellwise-constant expression in the file's height
   coordinate. Unlike VINT it applies no pressure/gravity or density factor. */
static gaint zint_match(struct gagrid *a, struct gagrid *b) {
  return b->idim<0 || (a->idim==b->idim && a->jdim==b->jdim &&
         a->isiz==b->isiz && a->jsiz==b->jsiz &&
         !gagchk(a,b,a->idim) && !gagchk(a,b,a->jdim));
}

static gaint zint_auto(char *arg) {
  while (*arg==' ') arg++;
  if (strncmp(arg,"auto",4)) return 0;
  arg+=4;
  while (*arg==' ') arg++;
  return !*arg;
}

static gaint zint_profile(struct gafile *file, struct gavar *var) {
  gaint d, z=0;
  if (!var || var->levels<1 || (!file->adios2flg && file->ncflg!=3)) return 0;
  if (file->dnum[0]==1 && file->dnum[1]==1) return 1;
  for (d=0; d<var->nh5vardims; d++) {
    if (var->units[d]==-100 || var->units[d]==-101) return 0;
    if (var->units[d]==-102) z=1;
  }
  return z;
}

static gaint zint_run(struct gafunc *pfc, struct gastat *pst) {
  struct gafile *file, *pf=NULL, *f[2], *search;
  struct gavar *var=NULL, *v[2];
  struct gagrid *out=NULL, *plane=NULL;
  struct gastat sample;
  struct gazint integral;
  gadouble *edges=NULL, *bottom=NULL, *top=NULL, *factor=NULL;
  gadouble center, previous=0, first=0, last=0, direction=0, value, lo, hi;
  gadouble minbottom=0, maxtop=0;
  gaint nz,k,b,rc=1,height,number,active=0,done=0;
  size_t i,n;
  char coordinate[32], terms[2][1000], *star, *profile=NULL, *source;

  if (pfc->argnum<1 || pfc->argnum>3 || !pst->pfid) {
    gaprnt(0,"ZINT error: expected zint(expression[,bottom[,top]]) with a grid file open\n");
    return 1;
  }
  if (pst->idim==2) { pst->idim=pst->jdim; pst->jdim=-1; }
  if (pst->jdim==2) pst->jdim=-1;
  if (pst->idim>1 || pst->jdim>1 || pst->pfid->type!=1) {
    gaprnt(0,"ZINT error: only X/Y may vary; fix T/E or use tloop/eloop\n");
    return 1;
  }
  file=zslice_field(pfc->argpnt[0],pst,&var,&height);
  source=pfc->argpnt[0];
  if (!file) file=pst->pfid;
  /* The common CWV form can stream its 3-D input by native blocks/chunks,
     with a small per-level multiplier. Other expressions use gaexpr. */
  star=strchr(pfc->argpnt[0],'*');
  if (!var && star && !strchr(star+1,'*') && strlen(pfc->argpnt[0])<sizeof(terms[0])) {
    n=(size_t)(star-pfc->argpnt[0]);
    memcpy(terms[0],pfc->argpnt[0],n); terms[0][n]=0;
    strcpy(terms[1],star+1);
    f[0]=zslice_field(terms[0],pst,v,&height);
    f[1]=zslice_field(terms[1],pst,v+1,&height);
    for (b=0;b<2;b++) if (f[b] && f[1-b] && v[b] && v[b]->levels>0 &&
        zint_profile(f[1-b],v[1-b]) && f[b]->dnum[2]==pst->pfid->dnum[2] &&
        v[b]->levels==v[1-b]->levels) {
      for (k=1;k<=v[b]->levels;k++) {
        center=f[b]->gr2ab[2](f[b]->grvals[2],k);
        if (center!=f[1-b]->gr2ab[2](f[1-b]->grvals[2],k) ||
            center!=pst->pfid->gr2ab[2](pst->pfid->grvals[2],k)) break;
      }
      if (k<=v[b]->levels) continue;
      file=f[b]; var=v[b]; pf=f[1-b]; profile=terms[1-b]; source=terms[b]; break;
    }
  }
  nz=var && var->levels>0 ? var->levels : file->dnum[2];
  if (nz<2) {
    gaprnt(0,"ZINT error: at least two distinct height levels are required to infer cell edges\n");
    return 1;
  }
  edges=galloc(((size_t)nz+1)*sizeof(*edges),"zintedges");
  factor=galloc((size_t)nz*sizeof(*factor),"zintfactor");
  if (!edges || !factor) goto allocation;
  for (k=0;k<nz;k++) {
    center=file->gr2ab[2](file->grvals[2],k+1);
    if (!isfinite(center) || (k && (center==previous ||
        (k>1 && (center-previous)*direction<=0)))) {
      gaprnt(0,"ZINT error: height levels must be finite and strictly monotonic\n"); goto cleanup;
    }
    if (!k) first=center;
    else {
      edges[k]=0.5*previous+0.5*center;
      if (k==1) { direction=center-previous; edges[0]=first-0.5*direction; }
    }
    previous=center; factor[k]=1;
  }
  edges[nz]=previous+0.5*(previous-file->gr2ab[2](file->grvals[2],nz-1));
  last=fmax(edges[0],edges[nz]);
  for (k=0;k<=nz;k++) if (!isfinite(edges[k])) {
    gaprnt(0,"ZINT error: inferred height edges overflow\n"); goto cleanup;
  }
  pst->dmin[2]=pst->dmax[2]=first;
  search=pst->pfi1; number=1;
  while (search && search!=file) { search=search->pforw; number++; }
  if (!search) goto invalid;
  snprintf(coordinate,sizeof(coordinate),"lev.%d",number);
  if (gaexpr(coordinate,pst)) goto cleanup;
  if (pst->type!=1) { gafree(pst); goto invalid; }
  out=pst->result.pgr;
  n=(size_t)out->isiz*out->jsiz;
  if (out->idim>1 || out->jdim>1 || n>(size_t)INT_MAX) goto invalid;
  bottom=galloc(n*sizeof(*bottom),"zintbottom");
  top=galloc(n*sizeof(*top),"zinttop");
  if (!bottom || !top) goto allocation;
  for (i=0;i<n;i++) { bottom[i]=0; top[i]=last; }
  for (b=1;b<pfc->argnum;b++) {
    if (zint_auto(pfc->argpnt[b])) continue;
    pst->dmin[2]=pst->dmax[2]=first;
    if (gaexpr(pfc->argpnt[b],pst)) goto cleanup;
    if (pst->type!=1) { gafree(pst); goto invalid; }
    plane=pst->result.pgr;
    if (!zint_match(out,plane)) goto invalid;
    for (i=0;i<n;i++) {
      size_t j=plane->idim<0 ? 0 : i;
      value=plane->umask[j] && isfinite(plane->grid[j]) ? plane->grid[j] : NAN;
      if (b==1) bottom[i]=value; else top[i]=value;
    }
    gagfre(plane); plane=NULL;
  }
  for (i=0;i<n;i++) {
    out->umask[i]=isfinite(bottom[i]) && isfinite(top[i]) && bottom[i]<=top[i];
    out->grid[i]=out->umask[i] ? 0 : out->undef;
    if (out->umask[i] && bottom[i]<top[i]) {
      if (!active || bottom[i]<minbottom) minbottom=bottom[i];
      if (!active || top[i]>maxtop) maxtop=top[i];
      active=1;
    }
  }
  integral.nz=nz; integral.edges=edges; integral.bottom=bottom; integral.top=top;
  integral.factor=factor; integral.sum=out->grid; integral.mask=out->umask;
  integral.minbottom=minbottom; integral.maxtop=maxtop;
  if (!active) { rc=0; goto cleanup; }
  if (profile) {
    sample=*pst; sample.idim=sample.jdim=-1;
    sample.dmin[0]=sample.dmax[0]=pf->gr2ab[0](pf->grvals[0],1);
    sample.dmin[1]=sample.dmax[1]=pf->gr2ab[1](pf->grvals[1],1);
    if(pf->ncflg==3 && !pf->ppflag && !pf->idxflg && !pf->climo) {
      /* Native HDF5 profile coordinates were checked against the source
         above. Read the complete vector once, with the same unpack/mask
         rules as the scalar reader, then retain the original Z order. */
      sample.idim=2;
      sample.dmin[2]=file->gr2ab[2](file->grvals[2],1);
      sample.dmax[2]=file->gr2ab[2](file->grvals[2],nz);
      if (gaexpr(profile,&sample)) goto cleanup;
      if (sample.type!=1) { gafree(&sample); goto invalid; }
      plane=sample.result.pgr;
      if(plane->idim!=2 || plane->jdim>=0 || plane->isiz!=nz ||
         plane->dimmin[2]!=1 || plane->dimmax[2]!=nz)goto invalid;
      for(k=0;k<nz;k++)
        factor[k]=plane->umask[k] && isfinite(plane->grid[k]) ? plane->grid[k] : NAN;
      gagfre(plane); plane=NULL;
      if(getenv("GA_ZINT_TRACE"))gaprnt(2,"ZINT I/O: bulk HDF5 density profile\n");
    } else {
      for (k=0;k<nz;k++) {
        sample.dmin[2]=sample.dmax[2]=file->gr2ab[2](file->grvals[2],k+1);
        if (gaexpr(profile,&sample)) goto cleanup;
        if (sample.type!=1) { gafree(&sample); goto invalid; }
        plane=sample.result.pgr;
        factor[k]=plane->umask[0] && isfinite(plane->grid[0]) ? plane->grid[0] : NAN;
        gagfre(plane); plane=NULL;
      }
    }
  }
  if (var && var->levels>0 && !file->ppflag && !file->idxflg &&
      (file->adios2flg || file->ncflg==3)) {
    out->pvar=var; out->undef=file->undef;
    rc=gagrdzint(out,&integral);
    if (rc>=0) goto cleanup;
  }
  rc=1;
  gaprogtotal(nz);
  for (k=0;k<nz;k++) {
    lo=fmin(edges[k],edges[k+1]); hi=fmax(edges[k],edges[k+1]);
    if (hi<=minbottom || lo>=maxtop) continue;
    pst->dmin[2]=pst->dmax[2]=file->gr2ab[2](file->grvals[2],k+1);
    if (gaexpr(source,pst)) goto cleanup;
    if (pst->type!=1) { gafree(pst); goto invalid; }
    plane=pst->result.pgr;
    if (!zint_match(out,plane)) goto invalid;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) if(ga_omp_parallelize((gaint)n))
#endif
    for (i=0;i<n;i++) {
      size_t j=plane->idim<0 ? 0 : i;
      if (plane->umask[j]) gazintadd(&integral,i,k,plane->grid[j]);
    }
    gagfre(plane); plane=NULL;
    gaprogstep(++done);
  }
  if (getenv("GA_ZINT_TRACE")) gaprnt(2,"ZINT I/O: evaluator level fallback\n");
  rc=0;
  goto cleanup;
allocation:
  gaprnt(0,"ZINT error: unable to allocate column buffers\n"); goto cleanup;
invalid:
  gaprnt(0,"ZINT error: expression and bounds must be scalars or matching horizontal grids\n");
  rc=1;
cleanup:
  if (plane) gagfre(plane);
  if (edges) gree(edges,"zintedges");
  if (factor) gree(factor,"zintfactor");
  if (bottom) gree(bottom,"zintbottom");
  if (top) gree(top,"zinttop");
  if (rc) { if (out) gagfre(out); pst->result.pgr=NULL; }
  else { pst->type=1; pst->result.pgr=out; }
  return rc;
}

gaint ffzint(struct gafunc *pfc, struct gastat *pst) {
  gaint progress=gaprogbeg("zint",0), rc;
  rc=zint_run(pfc,pst);
  gaprogend(progress);
  return rc;
}

gaint ffvint (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgrb, *pgr, *pgrv;
struct gafile *pfi;
gadouble *ps, *var, *res;
gadouble *lvvals;
gadouble (*lvconv) (gadouble *, gadouble);
gadouble top,clev,ulev,blev,ulevi,blevi,ulevt,blevt,kgm;
gaint rc, size, i, j, lvt;
char *psu, *varu, *resu;

  if (pfc->argnum!=3) {
    gaprnt (0,"Error from VINT:  Too many or too few args \n");
    gaprnt (0,"                  Three arguments expected \n");
    return (1);
  }

  /* Get top pressure level.  It is a character value in 3rd arg */
  if (getdbl(pfc->argpnt[2],&top)==NULL) {
    gaprnt (0,"Error from VINT:  3rd argument invalid. \n");
    return (1);
  }

  /* Get the range of levels from the default file.  Set the
     level in the status block to the first level.  */

  pfi = pst->pfid;
  lvt = pfi->dnum[2];
  if (lvt<3) {
    gaprnt (0,"Error from VINT:  Too few levels in default file \n");
    return (1);
  }
  lvconv = pfi->gr2ab[2];
  lvvals = pfi->grvals[2];
  clev = lvconv(lvvals, 1.0);
  ulev = lvconv(lvvals, 2.0);
  ulev = clev + ((ulev-clev)/2.0);
  pst->dmin[2] = clev;
  pst->dmax[2] = clev;
  if (pst->idim==2) {
    pst->idim = pst->jdim;
    pst->jdim = -1;
  }
  if (pst->jdim==2) pst->jdim = -1;

  /* Get the surface pressure field (1st arg).  User is responsible
     for valid argument.  Then get the lowest level of the
     field to integrate.  */

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (1);
  if (pst->type==0) {
    gafree (pst);
    return (-1);
  }
  pgrb = pst->result.pgr;

  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) {
    gagfre (pgrb);
    return (1);
  }
  if (pst->type==0) {
    gafree (pst);
    gagfre (pgrb);
    return (-1);
  }
  pgr = pst->result.pgr;

  /* Check that the two grids are equivalent.  */
  if (pgrb->isiz!=pgr->isiz || pgrb->jsiz!=pgr->jsiz) {
    gaprnt (0,"Error from VINT:  Incompatible grids. \n");
    goto erret;
  }

  /* Apply appropriate mass weight (kg/m**2) to first level.
     It is assumed the vertical coordinate system is mb.    */
  size = pgr->isiz * pgr->jsiz;
  kgm = 100.0/9.8;
  ps  = pgrb->grid;
  psu = pgrb->umask;
  res  = pgr->grid;
  resu = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(size))
#endif
  for (i=0; i<size; i++) {
    if (psu[i]==0 || resu[i]==0) {
      resu[i] = 0;
    }
    else if (ps[i] < clev) {
      resu[i] = 0;
    }
    else {
      res[i] = res[i] * kgm * (ps[i] - ulev);
      resu[i] = 1;
    }
  }

  /* Go through the intermediate levels and apply mass weight. */
  for (i=2; i<lvt; i++) {
    clev = lvconv(lvvals, (gadouble)i);
    if (clev<top) break;
    ulev = lvconv(lvvals, (gadouble)(i+1));
    ulevi = clev + ((ulev-clev)/2.0);
    blev = lvconv(lvvals, (gadouble)(i-1));
    blevi = clev + ((blev-clev)/2.0);
    pst->dmin[2] = clev;
    pst->dmax[2] = clev;
    rc = gaexpr(pfc->argpnt[1],pst);
    if (rc) goto erret;
    if (pst->type==0) {
      rc = -1;
      gafree (pst);
      goto erret;
    }
    pgrv = pst->result.pgr;
    ps  = pgrb->grid;
    res = pgr->grid;
    var = pgrv->grid;
    psu  = pgrb->umask;
    resu = pgr->umask;
    varu = pgrv->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) private(ulevt,blevt) if(ga_omp_parallelize(size))
#endif
    for (j=0; j<size; j++) {
      if ((psu[j]!=0) && (varu[j]!=0) && (ps[j]>=clev)) {
        ulevt = ulevi;
        if (top>ulev) ulevt = top;
        blevt = blevi;
        if (ps[j]<blev) blevt = ps[j];
        if (resu[j]==0) {
	  res[j] = var[j] * kgm * (blevt - ulevt);
	  resu[j] = 1;
	}
        else {
	  res[j] = res[j] + (var[j] * kgm * (blevt - ulevt) );
	  resu[j] = 1;
	}
      }
    }
    gafree (pst);
  }

  /* Do top, and last, level */

  clev = lvconv(lvvals, (gadouble)i);
  if (top<=clev) {
    blev = lvconv(lvvals, (gadouble)(i-1));
    blevi = clev + ((blev-clev)/2.0);
    pst->dmin[2] = clev;
    pst->dmax[2] = clev;
    rc = gaexpr(pfc->argpnt[1],pst);
    if (rc) goto erret;
    if (pst->type==0) {
      rc = -1;
      gafree (pst);
      goto erret;
    }
    pgrv = pst->result.pgr;
    ps  = pgrb->grid;
    res = pgr->grid;
    var = pgrv->grid;
    psu  = pgrb->umask;
    resu = pgr->umask;
    varu = pgrv->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) private(blevt) if(ga_omp_parallelize(size))
#endif
    for (i=0; i<size; i++) {
      if (psu[i]!=0 && varu[i]!=0) {
        blevt = blevi;
        if (ps[i]<blev) blevt = ps[i];
        if (resu[i]==0) {
	  res[i] = var[i] * kgm * (blevt - top);
	  resu[i] = 1;
	}
        else {
	  res[i] = res[i] + (var[i] * kgm * (blevt - top) );
	  resu[i] = 1;
	}
      }
    }
  }
  gafree (pst);

  /* Release storage and return */

  rc = 0;
  pst->type = 1;
  pst->result.pgr = pgr;
  gagfre (pgrb);
  return (0);

  /* Error return */

erret:

  gagfre (pgrb);
  gagfre (pgr);
  return (rc);
}

static gaint fftlp_run (struct gafunc *, struct gastat *);

/* tloop, with its progress shown while it runs */
gaint fftlp (struct gafunc *pfc, struct gastat *pst) {
gaint lv, rc;
  lv = gaprogbeg("tloop", 0);
  rc = fftlp_run(pfc, pst);
  gaprogend(lv);
  return (rc);
}

static gaint fftlp_run (struct gafunc *pfc, struct gastat *pst) {
struct gafile *pfi;
struct gagrid *pgr, *res;
gaint size, rc, t1, t2, tfirst, i, cont;
gadouble gr1, gr2,*in, *out;
char *inu, *outu;
size_t sz;

  /* Check for valid number of args       */

  if (pfc->argnum != 1 ) {
    gaprnt (0,"Error from TLOOP:  Too many or too few args \n");
    gaprnt (0,"                   1 argument expected \n");
    return (1);
  }

  /* If t is non-varying, treat this as a no-op.  */

  if (pst->idim!=3 && pst->jdim!=3) {
    rc = gaexpr(pfc->argpnt[0],pst);
    return (rc);
  }

  /* Get start and end times in terms of grid space */

  pfi = pst->pfid;
  gr1 = t2gr(pfi->abvals[3], &pst->tmin);
  gr2 = t2gr(pfi->abvals[3], &pst->tmax);
  gr1 = ceil(gr1-0.001);
  gr2 = floor(gr2+0.001);
  t1 = (gaint)gr1;
  t2 = (gaint)gr2;
  if (t2<t1) t2 = t1;
  tfirst = t1;
  gaprogtotal(t2-t1+1);

  /* Get 1st grid. */

  gr2t(pfi->grvals[3], gr1, &pst->tmin);
  if (pst->idim==3) {
    pst->idim = pst->jdim;
    pst->jdim = -1;
  }
  if (pst->jdim==3) pst->jdim = -1;
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    gafree (pst);
    return (-1);
  }
  pgr = pst->result.pgr;

  /* Check validity of 1st grid.  It should be a 1-D or a 0-D grid,
     and it should not have a time-varying dimension.  */

  if (pgr->jdim!=-1 || pgr->idim==3) {
    gaprnt (0,"Error from TLOOP: Internal logic check 36\n");
    goto err1;
  }

  /* Create output grid */

  size = sizeof(struct gagrid);
  sz = size;
  res = (struct gagrid *)galloc(sz,"tloopres");
  if (res==NULL) {
    gaprnt (0,"Memory Allocation Error:  TLOOP function\n");
    goto err1;
  }
  size = 1+t2-t1;
  size = size * pgr->isiz;
  if (size>1) {
    sz = size*sizeof(gadouble);
    res->grid = (gadouble *)galloc(sz,"tloopgr");
    if (res->grid==NULL) {
      gaprnt (0,"Memory Allocation Error:  TLOOP function\n");
      gree(res,"f423");
      goto err1;
    }
    sz = size*sizeof(char);
    res->umask = (char *)galloc(sz,"tloopgru");
    if (res->umask==NULL) {
      gaprnt (0,"Memory Allocation Error:  TLOOP function\n");
      gree(res->grid,"f424");
      gree(res,"f425");
      goto err1;
    }
  } else {
    res->grid = &(res->rmin);
    res->umask = &(res->umin);
  }

  res->alocf = 0;
  res->pfile = NULL;
  res->undef = pgr->undef;
  res->pvar  = NULL;
  res->exprsn = NULL;
  for (i=0;i<5;i++) {
    res->dimmin[i] = 0;
    res->dimmax[i] = 0;
  }
  res->dimmin[3] = t1;
  res->dimmax[3] = t2;
  res->jwrld = 0;
  if (pgr->isiz>1) {
    res->dimmin[pgr->idim] = pgr->dimmin[pgr->idim];
    res->dimmax[pgr->idim] = pgr->dimmax[pgr->idim];
    res->idim = pgr->idim;
    res->iwrld = pgr->iwrld;
    res->isiz = pgr->isiz;
    res->igrab = pgr->igrab;
    res->ilinr = pgr->ilinr;
    res->ivals = pgr->ivals;
    if (t1==t2) {
      res->jdim = -1;
      res->jsiz = 1;
    } else {
      res->jdim = 3;
      res->jsiz = 1+t2-t1;
      res->jvals = pfi->grvals[3];
      res->jgrab = NULL;
      res->jlinr = 1;
    }
  } else {
    res->jdim = -1;
    res->jsiz = 1;
    if (t1==t2) {
      res->idim = -1;
      res->isiz = 1;
    } else {
      res->idim = 3;
      res->isiz = 1+t2-t1;
      res->ivals = pfi->grvals[3];
      res->igrab = NULL;
      res->ilinr = 1;
    }
  }

  /* Loop and fill output grid.  */

  cont = 1;
  out = res->grid;
  outu = res->umask;
  while (cont) {
    in = pgr->grid;
    inu = pgr->umask;
    for (i=0; i<pgr->isiz; i++) {
      if (*inu==0) {
	*outu=0;
      }
      else {
	*out = *in;
	*outu = 1;
      }
      in++; inu++; out++; outu++;
    }
    gagfre(pgr);
    gaprogstep(t1-tfirst+1);
    t1++;
    if (t1<=t2) {
      gr2t(pfi->abvals[3], (gadouble)t1, &pst->tmin);
      pst->tmax = pst->tmin;
      rc = gaexpr(pfc->argpnt[0],pst);
      if (rc) goto err2;
      pgr = pst->result.pgr;
    } else cont = 0;
  }
  pst->result.pgr = res;
  return (0);

err1:
  gagfre (pgr);
  return (1);

err2:
  gagfre(res);
  return(1);
}

static gaint ffelp_run (struct gafunc *, struct gastat *);

/* eloop, with its progress shown while it runs */
gaint ffelp (struct gafunc *pfc, struct gastat *pst) {
gaint lv, rc;
  lv = gaprogbeg("eloop", 0);
  rc = ffelp_run(pfc, pst);
  gaprogend(lv);
  return (rc);
}

static gaint ffelp_run (struct gafunc *pfc, struct gastat *pst) {
struct gafile *pfi;
struct gagrid *pgr, *res;
gaint size, rc, e1, e2, efirst, i, cont;
gadouble gr1, gr2,*in, *out;
char *inu, *outu;
size_t sz;

  /* Check for valid number of args       */
  if (pfc->argnum != 1 ) {
    gaprnt (0,"Error from ELOOP:  Too many or too few args \n");
    gaprnt (0,"                   1 argument expected \n");
    return (1);
  }

  /* If e is non-varying, treat this as a no-op.  */
  if (pst->idim!=4 && pst->jdim!=4) {
    rc = gaexpr(pfc->argpnt[0],pst);
    return (rc);
  }

  /* Get start and end E indices */
  pfi = pst->pfid;
  gr1 = pst->dmin[4];
  gr2 = pst->dmax[4];
  gr1 = ceil(gr1-0.001);
  gr2 = floor(gr2+0.001);
  e1 = (gaint)gr1;
  e2 = (gaint)gr2;
  if (e2<e1) e2 = e1;
  efirst = e1;
  gaprogtotal(e2-e1+1);

  /* Get 1st grid. */
  pst->dmin[4] = e1;
  if (pst->idim==4) {
    pst->idim = pst->jdim;
    pst->jdim = -1;
  }
  if (pst->jdim==4) pst->jdim = -1;
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    gafree (pst);
    return (-1);
  }
  pgr = pst->result.pgr;

  /* Check validity of 1st grid.  It should be a 1-D or a 0-D grid,
     and it should not have a ensemble-varying dimension.  */
  if (pgr->jdim!=-1 || pgr->idim==4) {
    gaprnt (0,"Error from ELOOP: Internal logic check 36\n");
    goto err1;
  }

  /* Create output grid */
  size = sizeof(struct gagrid);
  sz = size;
  res = (struct gagrid *)galloc(sz,"eloopres");
  if (res==NULL) {
    gaprnt (0,"Memory Allocation Error:  ELOOP function\n");
    goto err1;
  }
  size = 1+e2-e1;
  size = size * pgr->isiz;
  if (size>1) {
    sz = size*sizeof(gadouble);
    res->grid = (gadouble *)galloc(sz,"eloopgr");
    if (res->grid==NULL) {
      gaprnt (0,"Memory Allocation Error:  ELOOP function\n");
      gree(res,"f423e");
      goto err1;
    }
    sz = size*sizeof(char);
    res->umask = (char *)galloc(sz,"eloopgru");
    if (res->umask==NULL) {
      gaprnt (0,"Memory Allocation Error:  ELOOP function\n");
      gree(res->grid,"f424e");
      gree(res,"f425e");
      goto err1;
    }
  } else {
    res->grid = &(res->rmin);
    res->umask = &(res->umin);
  }

  res->alocf = 0;
  res->pfile = NULL;
  res->undef = pgr->undef;
  res->pvar  = NULL;
  res->exprsn = NULL;
  for (i=0;i<5;i++) {
    res->dimmin[i] = 0;
    res->dimmax[i] = 0;
  }
  res->dimmin[4] = e1;
  res->dimmax[4] = e2;
  res->jwrld = 0;
  if (pgr->isiz>1) {
    res->dimmin[pgr->idim] = pgr->dimmin[pgr->idim];
    res->dimmax[pgr->idim] = pgr->dimmax[pgr->idim];
    res->idim = pgr->idim;
    res->iwrld = pgr->iwrld;
    res->isiz = pgr->isiz;
    res->igrab = pgr->igrab;
    res->ilinr = pgr->ilinr;
    res->ivals = pgr->ivals;
    if (e1==e2) {
      res->jdim = -1;
      res->jsiz = 1;
    } else {
      res->jdim = 4;
      res->jsiz = 1+e2-e1;
      res->jvals = pfi->grvals[4];
      res->jgrab = NULL;
      res->jlinr = 1;
    }
  } else {
    res->jdim = -1;
    res->jsiz = 1;
    if (e1==e2) {
      res->idim = -1;
      res->isiz = 1;
    } else {
      res->idim = 4;
      res->isiz = 1+e2-e1;
      res->ivals = pfi->grvals[4];
      res->igrab = NULL;
      res->ilinr = 1;
    }
  }

  /* Loop and fill output grid.  */
  cont = 1;
  out = res->grid;
  outu = res->umask;
  while (cont) {
    in = pgr->grid;
    inu = pgr->umask;
    for (i=0; i<pgr->isiz; i++) {
      if (*inu==0) {
	*outu=0;
      }
      else {
	*out = *in;
	*outu = 1;
      }
      in++; inu++; out++; outu++;
    }
    gagfre(pgr);
    gaprogstep(e1-efirst+1);
    e1++;
    if (e1<=e2) {
      pst->dmin[4] = e1;
      pst->dmax[4] = pst->dmin[4];
      rc = gaexpr(pfc->argpnt[0],pst);
      if (rc) goto err2;
      pgr = pst->result.pgr;
    } else cont = 0;
  }
  pst->result.pgr = res;
  return (0);

err1:
  gagfre (pgr);
  return (1);

err2:
  gagfre(res);
  return(1);
}

/* Reuse a native input for the common maskout(f,(f>cut)-0.5) form.
   Comparisons in GrADS return +/-1; subtracting 0.5 retains exactly the
   true cells. Unknown syntax and different fields keep the evaluator. */
static gaint mask_threshold(struct gafunc *pfc,struct gastat *pst) {
  struct gagrid *grid=pst->result.pgr;
  char *field=pfc->argpnt[0],*p=pfc->argpnt[1],*end;
  size_t length,n,i;
  gadouble cutoff,shift;
  gaint inclusive=0;
  if(!ave_plain_var(field,pst,grid))return 0;
  while(*field==' ')field++;
  length=strlen(field);while(length && field[length-1]==' ')length--;
  while(*p==' ')p++;
  if(*p++!='(')return 0;
  while(*p==' ')p++;
  if(strncmp(p,field,length))return 0;
  p+=length;while(*p==' ')p++;
  if(*p++!='>')return 0;
  if(*p=='=') { inclusive=1;p++; }
  while(*p==' ')p++;
  end=p;if(*end=='-')end++;
  if(*end<'0' || *end>'9')return 0;
  p=getdbl(p,&cutoff);if(!p || !isfinite(cutoff))return 0;
  while(*p==' ')p++;
  if(*p++!=')')return 0;
  while(*p==' ')p++;
  if(*p++!='-')return 0;
  while(*p==' ')p++;
  if(*p<'0' || *p>'9')return 0;
  p=getdbl(p,&shift);if(!p || shift!=0.5)return 0;
  while(*p==' ')p++;
  if(*p)return 0;
  n=(size_t)grid->isiz*grid->jsiz;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(n>INT_MAX ? INT_MAX : (gaint)n))
#endif
  for(i=0;i<n;i++)if(grid->umask[i] &&
       !(inclusive ? grid->grid[i]>=cutoff : grid->grid[i]>cutoff))grid->umask[i]=0;
  if(getenv("GA_MASK_TRACE"))gaprnt(2,"MASKOUT I/O: reused native input for threshold\n");
  return 1;
}

gaint ffmask (struct gafunc *pfc, struct gastat *pst) {
struct gastat pst2;
struct gastn *stn;
struct garpt *rpt;
char *ch,c1,c2;
gaint rc,cnt,flag,i;

  if (pfc->argnum!=2) {
    gaprnt (0,"Error from MASKOUT:  Too many or too few args \n");
    gaprnt (0,"                     Two arguments expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if(pst->type==1 && mask_threshold(pfc,pst))return 0;

  if (pst->type!=0 || *(pfc->argpnt[1])!='\'') {

    pst2 = *pst;
    rc = gaexpr(pfc->argpnt[1],&pst2);
    if (rc) {
      gafree (pst);
      return (rc);
    }

    rc = gafopr (pst, &pst2, 13);
    if (rc) {
      gafree (pst);
      gafree (&pst2);
    }

    /* Handle maskout of stn data by stid */

  } else {
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      ch = pfc->argpnt[1]+1;
      flag = 1; cnt = 0;
      while (*ch!='\'' && flag && cnt<8) {
        if (*ch!='?') {
          c1 = *ch;
          c2 = rpt->stid[cnt];
          i = c1;
          if (i>64 && i<91) {i+=32; c1=i;}
          i = c2;
          if (i>64 && i<91) {i+=32; c2=i;}
          if (c1 != c2) flag = 0;
        }
        ch++; cnt++;
      }
      if (flag==0) rpt->umask = 0;
      rpt=rpt->rpt;
    }
  }
  return (rc);
}

/* Given a grid and a set of stations, interpolate to the
   stations and return the set of stations.                       */

gaint ffg2s  (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble (*iconv) (gadouble *, gadouble);
gadouble (*jconv) (gadouble *, gadouble);
gadouble *ivars, *jvars, *p1, *p2, *p3, *p4;
gadouble gi,gj,w1,w2,lon,lat,lnmin,lnmax,lnscl,ltmin,ltmax,ltscl;
gadouble w3,w4,wm;
gaint rc,ig,jg,nearn;
char *p1u, *p2u, *p3u, *p4u;


  if (pfc->argnum<2 || pfc->argnum>4) {
    gaprnt (0,"Error from GR2STN:  Too many or too few args \n");
    gaprnt (0,"                    2 or 3 or 4 arguments expected \n");
    return (1);
  }

  /* If we are doing the form of gr2stn that involves
     interpolating to a 1-D profile or time series,
     branch to a different routine */

  if ( (pst->idim == -1 || pst->idim>1) && pst->jdim == -1) {
    rc = ffg2s2 (pfc,pst);
    return (rc);
  }

  /* Check for nearest neighbor flag -- instead of bilin interp from
     grid to station, use the nearest grid point to the station. */

  nearn = 0;
  if (pfc->argnum==3 && strcmp("-n",pfc->argpnt[2])==0) nearn = 1;
  if (nearn) gaprnt (2,"Notice: Using nearest neighbor instead of bilinear interpolation\n");

  /* Evaluate the grid expression */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gaprnt (0,"Error from GR2STN: 1st argument is not a grid\n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;
  if (pgr->idim!=0 || pgr->jdim!=1) {
    gaprnt (0,"Error from GR2STN: 2-D grid must vary in X and Y\n");
    gafree (pst);
    return (1);
  }
  if (!pgr->ilinr || !pgr->jlinr) {
    gaprnt (0,"Error from GR2STN: 2-D grid dimensions must have linear scaling\n");
    gafree (pst);
    return (1);
  }

  /* Evaluate the station expression */
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) {
    gagfre (pgr);
    return (rc);
  }
  if (pst->type!=0) {
    gaprnt (0,"Error from GR2STN: 2nd argument is not a station expression\n");
    gafree (pst);
    gagfre (pgr);
    return (1);
  }
  stn = pst->result.stn;

  /* Set up scaling for converting lon-lat to grid units */
  iconv = pgr->igrab;
  ivars = pgr->ivals;
  jconv = pgr->jgrab;
  jvars = pgr->jvals;
  lnmin = iconv(ivars,(gadouble)pgr->dimmin[0]);
  lnmax = iconv(ivars,(gadouble)pgr->dimmax[0]);
  ltmin = iconv(jvars,(gadouble)pgr->dimmin[1]);
  ltmax = iconv(jvars,(gadouble)pgr->dimmax[1]);
  lnscl = (lnmax-lnmin)/((gadouble)pgr->isiz-1);
  ltscl = (ltmax-ltmin)/((gadouble)pgr->jsiz-1);

  /* Now loop through each stn report, convert stn lat/lon to grid
     units, then interpolate from grid to stn */

  rpt = stn->rpt;
  while (rpt!=NULL) {
    lon = rpt->lon;
    lat = rpt->lat;
    if (lon<lnmin) lon+=360.0;
    else if (lon>lnmax) lon-=360.0;
    if (lon<lnmin || lon>=lnmax || lat<ltmin || lat>=ltmax) {
      rpt->umask = 0;
    } else {
      gi = (lon-lnmin)/lnscl;
      gj = (lat-ltmin)/ltscl;
      ig = (gaint)gi;
      jg = (gaint)gj;
      p1 = pgr->grid + jg*pgr->isiz + ig;
      p1u = pgr->umask + jg*pgr->isiz + ig;
      p2 = p1+1;
      p2u = p1u+1;
      p3 = p2 + pgr->isiz;
      p3u = p2u + pgr->isiz;
      p4 = p1 + pgr->isiz;
      p4u = p1u + pgr->isiz;
      if (nearn) {                   /* nearest neighbor */
        gi = gi - (gadouble)ig;
        gj = gj - (gadouble)jg;
        w1 = hypot(gi,gj);
        w2 = hypot(1.0-gi,gj);
        w3 = hypot(1.0-gi,1.0-gj);
        w4 = hypot(gi,1.0-gj);
        wm = 999.0;
        if (w1<wm) {
          wm = w1;
          if (*p1u==0) rpt->umask = 0;
          else rpt->val = *p1;
        }
        if (w2<wm) {
          wm = w2;
          if (*p2u==0) rpt->umask = 0;
          else rpt->val = *p2;
        }
        if (w3<wm) {
          wm = w3;
          if (*p3u==0) rpt->umask = 0;
          else rpt->val = *p3;
        }
        if (w4<wm) {
          wm = w4;
          if (*p4u==0) rpt->umask = 0;
          else rpt->val = *p4;
        }
      } else {                        /* bilinear */
        if (*p1u==0 || *p2u==0 || *p3u==0 || *p4u==0) {
          rpt->umask = 0;
        } else {
          gi = gi - (gadouble)ig;
          gj = gj - (gadouble)jg;
/*
 Weighted by distance or use bilinear?  bilinear looks to be
 more valid to me......
          w1 = 1.0 - hypot(gi,gj);
          w2 = 1.0 - hypot(1.0-gi,gj);
          w3 = 1.0 - hypot(1.0-gi,1.0-gj);
          w4 = 1.0 - hypot(gi,1.0-gj);
          if (w1<0.0) w1=0.0;
          if (w2<0.0) w2=0.0;
          if (w3<0.0) w3=0.0;
          if (w4<0.0) w4=0.0;
          rpt->val = *p1*w1 + *p2*w2 + *p3*w3 + *p4*w4;
          rpt->val = rpt->val / (w1+w2+w3+w4);
*/
          w1 = *p1 + (*p2 - *p1)*gi;
          w2 = *p4 + (*p3 - *p4)*gi;
          rpt->val = w1 + (w2-w1)*gj;
        }
      }
    }
    rpt=rpt->rpt;
  }
  gagfre (pgr);
  pst->type = 0;
  pst->result.stn=stn;
  return(0);
}

/* gr2stn where we interpolate to a lat-lon for
   a profile or time series */

gaint ffg2s2  (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgr, *pgr2;
struct gastn *stn, *stn2;
struct garpt *rpt;
gadouble (*iconv) (gadouble *, gadouble);
gadouble (*icnv) (gadouble *, gadouble);
gadouble (*jcnv) (gadouble *, gadouble);
gadouble *ivars, lon, lat, *grid, lev=0, val=0;
gadouble *p1, *p2, *p3, *p4;
gadouble gi,gj,w1,w2,w3,w4,wm;
gaint i,rc,gr1,gr2,gr,ig,jg,nearn;
char *p1u, *p2u, *p3u, *p4u, umask=0;
size_t sz;

  /* Get lat-lon to interpolate to.  This is either provided
     as two string arguments, or as a single stn-data argument. */

  iconv = NULL;
  stn2 = NULL;
  ivars = 0;
  nearn = 0;
  /* Evaluate the 2nd argument  */
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    /* 2nd arg is station expression */
    stn2 = pst->result.stn;
    rpt = stn2->rpt;
    if (rpt == NULL) {
      printf("Error in GR2STN: 2nd arg returned a null result \n");
      return(1);
    }
    lat = rpt->lat;
    lon = rpt->lon;
    /* Check for nearest neighbor flag in 3rd arg */
    if (pfc->argnum==3 && strcmp("-n",pfc->argpnt[2])==0) nearn = 1;
  }
  else {
    /* 2nd & 3rd args are lon & lat */
    pgr = pst->result.pgr;
    if (pgr->idim!=-1 || pgr->jdim!=-1) {
      gaprnt (0,"Error in GR2STN:  2nd arg invalid\n");
      return (1);
    }
    lon = pgr->rmin;
    gafree(pst);
    if (pfc->argnum<3) {
      gaprnt(0,"Error in GR2STN:  3rd Argument Required\n");
      return (1);
    }
    if (getdbl(pfc->argpnt[2],&lat)==NULL) {
      gaprnt (0,"Error from GR2STN:  3rd argument invalid. \n");
      return (1);
    }
    /* Check for nearest neighbor flag in 4th arg */
    if (pfc->argnum==4 && strcmp("-n",pfc->argpnt[3])==0) nearn = 1;
  }
  /* Evaluate the 1st argument */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gaprnt (0,"Error from GR2STN: 1st argument is not a grid\n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;
  if (pgr->idim==3) {
    ivars = pgr->ivals;
    gr1 = pgr->dimmin[3];
    gr2 = pgr->dimmax[3];
  } else if (pgr->idim==2) {
    iconv = pgr->igrab;
    ivars = pgr->ivals;
    gr1 = pgr->dimmin[2];
    gr2 = pgr->dimmax[2];
  } else if (pgr->idim== -1) {
    gr1 = 1;
    gr2 = 1;
  } else if (pgr->idim==4) {
    gaprnt(0,"Error from GR2STN: 1-D grid may vary only in the Z or T dimension \n");
    gafree (pst);
    return (1);
  } else {
    gaprnt (0,"Error from GR2STN:  Logic Error 4\n");
    gafree (pst);
    return (1);
  }

  /* Set up stn structure for the returned data */
  sz = sizeof(struct gastn);
  stn = (struct gastn *)galloc(sz,"gr2stn");
  if (stn==NULL) {
    gaprnt (0,"Memory Allocation Error:  Station Request Block \n");
    gagfre (pgr);
    return (1);
  }
  stn->rpt = NULL;
  stn->rnum = 0;
  stn->idim = pgr->idim;
  stn->jdim = pgr->jdim;
  stn->undef = pgr->undef;
  stn->pvar = NULL;
  stn->dmin[0] = lon;  stn->dmax[0] = lon;
  stn->dmin[1] = lat;  stn->dmax[1] = lat;
  stn->rflag = 0;
  stn->radius = 1.0;
  stn->sflag = 1;
  if (stn2) {
    for (i=0; i<8; i++) stn->stid[i] = stn2->stid[i];
  } else {
    for (i=0; i<8; i++) stn->stid[i] = 'x';
  }
  sz = sizeof(gadouble)*8;
  stn->tvals = (gadouble *)galloc(sz,"gr2stnt");
  if (stn->tvals==NULL) {
    gaprnt (0,"Memory Allocation Error:  Station Request Block \n");
    gree(stn,"f426");
    gagfre (pgr);
    return (1);
  }
  stn->jdim = -1;
  if (pgr->idim==3) {                  /* time series */
    for (i=0; i<8; i++) *(stn->tvals+i) = *(ivars+i);
    stn->tmin = gr1; stn->tmax = gr2;
    stn->dmin[2] = pst->dmin[2];
    stn->dmax[2] = pst->dmin[2];
    stn->idim = 3;
  } else if (pgr->idim== -1) {         /* single point */
    stn->dmin[2] = pst->dmin[2];
    stn->dmax[2] = pst->dmin[2];
    stn->tmin = 1; stn->tmax = 1;
    *(stn->tvals) = pst->tmin.yr;
    *(stn->tvals+1) = pst->tmin.mo;
    *(stn->tvals+2) = pst->tmin.dy;
    *(stn->tvals+3) = pst->tmin.hr;
    *(stn->tvals+4) = pst->tmin.mn;
    *(stn->tvals+5) = 0.0;
    *(stn->tvals+6) = 1.0;
    *(stn->tvals+7) = -999.9;
  } else {                             /* vertical profile */
    stn->dmin[2] = gr1;
    stn->dmax[2] = gr2;
    stn->tmin = 1;
    stn->tmax = 1;
    *(stn->tvals) = pst->tmin.yr;
    *(stn->tvals+1) = pst->tmin.mo;
    *(stn->tvals+2) = pst->tmin.dy;
    *(stn->tvals+3) = pst->tmin.hr;
    *(stn->tvals+4) = pst->tmin.mn;
    *(stn->tvals+5) = 0.0;
    *(stn->tvals+6) = 1.0;
    *(stn->tvals+7) = -999.9;
  }

  if (nearn) gaprnt (2,"Notice: Using nearest neighbor instead of bilinear interpolation\n");

  pst->idim = 0;
  pst->jdim = 1;
  pst->dmin[0] = lon;
  pst->dmax[0] = lon + 0.1;
  pst->dmin[1] = lat;
  pst->dmax[1] = lat + 0.1;
  /* loop over grid points in the profile/time series */
  for (gr=gr1; gr<=gr2; gr++) {
    if (pgr->idim==2) {
      lev = iconv(ivars,(gadouble)gr);
      pst->dmin[2] = lev;
      pst->dmax[2] = lev;
    } else if (pgr->idim==3) {
      gr2t (ivars, (gadouble)gr, &(pst->tmin));
      pst->tmax = pst->tmin;
    }
    rc = gaexpr(pfc->argpnt[0],pst);
    if (rc) {
      gagfre(pgr);
      return (rc);
    }
    pgr2 = pst->result.pgr;
    grid = pgr2->grid;
    icnv = pgr2->iabgr;
    jcnv = pgr2->jabgr;
    gi = icnv(pgr2->iavals,lon) - (gadouble)pgr2->dimmin[0];
    gj = jcnv(pgr2->javals,lat) - (gadouble)pgr2->dimmin[1];
    ig = (gaint)gi;
    jg = (gaint)gj;
    p1 = pgr2->grid + jg*pgr2->isiz + ig;
    p1u = pgr2->umask + jg*pgr2->isiz + ig;
    p2 = p1+1;
    p2u = p1u+1;
    p3 = p2 + pgr2->isiz;
    p3u = p2u + pgr2->isiz;
    p4 = p1 + pgr2->isiz;
    p4u = p1u + pgr2->isiz;
    if (nearn) {                   /* nearest neighbor */
      gi = gi - (gadouble)ig;
      gj = gj - (gadouble)jg;
      w1 = hypot(gi,gj);
      w2 = hypot(1.0-gi,gj);
      w3 = hypot(1.0-gi,1.0-gj);
      w4 = hypot(gi,1.0-gj);
      wm = 999.0;
      if (w1<wm) {
	wm = w1;
	if (*p1u==0) umask = 0;
	else {umask = 1; val = *p1;}
      }
      if (w2<wm) {
	wm = w2;
	if (*p2u==0) umask = 0;
	else {umask = 1; val = *p2;}
      }
      if (w3<wm) {
	wm = w3;
	if (*p3u==0) umask = 0;
	else {umask = 1; val = *p3;}
      }
      if (w4<wm) {
	wm = w4;
	if (*p4u==0) umask = 0;
	else {umask = 1; val = *p4;}
      }
    } else {                        /* bilinear */
      if (*p1u==0 || *p2u==0 || *p3u==0 || *p4u==0) {
	umask = 0;
      } else {
	gi = gi - (gadouble)ig;
	gj = gj - (gadouble)jg;
	w1 = *p1 + (*p2 - *p1)*gi;
	w2 = *p4 + (*p3 - *p4)*gi;
	val = w1 + (w2-w1)*gj;
	umask = 1;
      }
    }

    rpt = gaarpt (stn);
    if (rpt==NULL) {
      gaprnt (0,"Memory Allocation Error:  Station Block \n");
      gagfre(pgr);
      gafree(pst);
      gasfre(stn);
      return (1);
    }
    rpt->lat = lat;
    rpt->lon = lon;
    if (pgr->idim==2) { rpt->lev = lev; rpt->tim = 1; }
    else { rpt->lev = stn->dmin[2]; rpt->tim = gr; }
    if (umask==1) {
      rpt->umask = 1;
      rpt->val = val;
    }
    else {
      rpt->umask = 0;
    }
    for (i=0; i<8; i++) *(rpt->stid+i) = *(stn->stid+i);
    stn->rnum++;
    gafree(pst);
  }

  gagfre(pgr);  /* Release the grid from evaluating the first argument */
  pst->result.stn = stn;
  pst->type = 0;
  pst->idim = stn->idim;
  pst->jdim = -1;
  return (0);
}

gaint ffclgr (struct gafunc *pfc, struct gastat *pst) {
  struct gaclct *clct, *clct0;
  struct gastn *stn;
  struct garpt *rpt;
  struct gagrid *pgr=NULL;
  gadouble *levs, lev, vlo, vhi, uu=0, *gr=NULL;
  gadouble *iv=NULL,*jv=NULL,diff,lld,lhd,llo,lhi,xdiff;
  gaint i,j,cnt,lcnt=0,scnt,flag,clnm,dim,lflg,ucnt;
  gaint noundef,rev=0;
  char *gru=NULL;
  size_t sz;

  lflg = 1;
  ucnt = 10;

  /* set noundef=1 to use only defined points in vertical interploation
     default is 0 */
  noundef=0;

  if (pfc->argnum>3) {
    gaprnt (0,"Error from COLL2GR:  Too many args \n");
    gaprnt (0,"                     One to three arguments expected \n");
    return (1);
  }
  if (intprs(pfc->argpnt[0],&clnm)==NULL) {
    gaprnt (0,"Error from COLL2GR:  1st argument must be an integer\n");
    return(1);
  }
  if (pfc->argnum>1) {
    if (cmpwrd("-u",pfc->argpnt[1])) lflg = 2;
    else if (intprs(pfc->argpnt[1],&i) != NULL) ucnt = i;
    else gaprnt (1,"COLL2GR Warning:  2nd arg Invalid; Ignored\n");
  }
  /* JMA this option not documented */
  if (pfc->argnum>2) {
    if (cmpwrd("-n0",pfc->argpnt[2])) noundef=0;
    else if (cmpwrd("-n1",pfc->argpnt[2])) noundef=1;
    else gaprnt (1,"COLL2GR Warning:  3nd arg Invalid; Ignored\n");
  }
  clct0 = *(pst->pclct+clnm);
  clct = clct0;
  if (clct==NULL) {
     snprintf(pout,1255,"Error from COLL2GR:  Collection %i empty\n",clnm);
     gaprnt (0,pout);
     return (1);
  }

  /* Count number of soundings, number of levels, and check dimension validity */
  cnt = 0;
  scnt = 0;
  dim = -1;
  while (clct) {
    stn = clct->stn;
    cnt += stn->rnum;
    scnt += 1;
    if (dim==-1) dim = stn->idim;
    if (dim != stn->idim) dim = -999;
    clct = clct->forw;
  }
  if (dim == -999 || dim != pst->jdim || pst->idim != 0 || dim<2 || dim>3 ) {
    gaprnt (0,"Error from COLL2GR:  Invalid dimension environment\n");
    return (1);
  }
  if (dim==3) {
    gaprnt  (0,"COLL2GR does not yet support time slices\n");
    return (1);
  }


  /* Obtain sorted list of levels or times, depending on
     what sort of interpolation was requested */
  if (lflg==1) cnt = ucnt;
  sz = sizeof(gadouble)*cnt;
  levs = (gadouble *)galloc(sz,"clgrlevs");
  if (levs==NULL) {
    gaprnt (0,"Error from COLL2GR: failled to allocate memory for levels \n");
    goto err;
  }

  if (lflg==1) {
    /* fixed number of levels */
    vlo = pst->dmin[2];
    vhi = pst->dmax[2];
    uu = (vhi - vlo)/((gadouble)cnt-1.0);
    lev = vlo;
    for (i=0; i<cnt; i++) {
      *(levs+i) = lev;
      lev += uu;
    }
    lcnt = cnt;
  }
  else if (lflg==2) {
    /* union of all levels  */
    if (pst->dmin[2] < pst->dmax[2]) rev=1;
    diff = fabs(pst->dmin[2] - pst->dmax[2])/1e4;
    lcnt = 0;
    clct = clct0;
    while (clct) {
      stn = clct->stn;
      rpt = stn->rpt;
      while (rpt) {
        lev = rpt->lev;
        i = 0;
        flag = 1;
	/* don't use this level if the data value is undefined */
	if (noundef && (rpt->umask == 0) ) flag = 0;
	/* don't use this level if out of range */
        if ((lev > pst->dmin[2] || lev < pst->dmax[2]) && rev==0) flag = 0;
        if ((lev < pst->dmin[2] || lev > pst->dmax[2]) && rev==1) flag = 0;
        while (i<lcnt && flag) {
          if (fabs(*(levs+i)-lev)<diff) {
            flag = 0;
            break;
          }
          if (*(levs+i) < lev && rev==0) break;
          if (*(levs+i) > lev && rev==1) break;
          i = i + 1;
        }
        if (flag) {
          if (i<lcnt) {
            for (j=lcnt; j>i; j--) *(levs+j) = *(levs+j-1);
          }
          *(levs+i) = lev;
          lcnt++;
        }
        rpt = rpt->rpt;
      }
      clct = clct->forw;
    }
  }

  /* Allocate and fill the interpolated grid */
  sz = sizeof(gadouble)*lcnt*scnt;
  gr = (gadouble *)galloc(sz,"col2grgr");
  if (gr==NULL) {
    gaprnt (0,"Error from COLL2GR: failled to allocate memory for collection grid \n");
    goto err;
  }
  sz = sizeof(char)*lcnt*scnt;
  gru = (char *)galloc(sz,"col2grgru");
  if (gru==NULL) {
    gaprnt (0,"Error from COLL2GR: failled to allocate memory for collection umask \n");
    goto err;
  }
  clct = clct0;
  i = 0;
  while (clct) {
    stn = clct->stn;
    if (i==0) uu = stn->undef;
    for (j=0; j<lcnt; j++) {
      lev = *(levs+j);
      rpt = stn->rpt;
      lld = 9.99e33;
      lhd = 9.99e33;
      llo = lev; lhi = lev;
      vlo = stn->undef;
      vhi = stn->undef;
      flag = 0;
      while (rpt) {
        if (dequal(rpt->lev,lev,1.0e-8)==0) {
	  /* level in rpt matches level in grid, no need to interpolate */
          flag = 1;
          break;
        }
        if (rpt->lev<lev) {
	  if (noundef) {
	    if (lev-rpt->lev<lld && (rpt->umask != 0) ) {
	      lld = lev-rpt->lev;
	      llo = rpt->lev;
	      vlo = rpt->val;
	    }
	  } else {
	    if (lev-rpt->lev<lld) {
	      lld = lev-rpt->lev;
	      llo = rpt->lev;
	      vlo = rpt->val;
	    }
          }
        } else {
	  if(noundef) {
	    if (rpt->lev-lev<lhd && (rpt->umask != 0) ) {
	      lhd = rpt->lev-lev;
	      lhi = rpt->lev;
	      vhi = rpt->val;
	    }
	  } else {
	    if (rpt->lev-lev<lhd) {
	      lhd = rpt->lev-lev;
	      lhi = rpt->lev;
	      vhi = rpt->val;
	    }
	  }
	}
        rpt = rpt->rpt;
      }
      if (flag) {
	*(gr+j*scnt+i) = rpt->val;
	*(gru+j*scnt+i) = rpt->umask;
      }
      else {
        if (dequal(vhi,stn->undef,1.0e-8)==0 || dequal(vlo,stn->undef,1.0e-8)==0) {
          *(gru+j*scnt+i) = 0;
        } else {
          *(gr+j*scnt+i) = vlo + (vhi-vlo)*(lev-llo)/(lhi-llo);
          *(gru+j*scnt+i) = 1;
        }
      }
    }
    clct = clct->forw;
    i++;
  }

  /* Now create the grid structure for our new grid and
     chain it in all the right places */

  sz = sizeof(struct gagrid);
  pgr = (struct gagrid *)galloc(sz,"col2grpgr");
  if (pgr==NULL) {
    gaprnt (0,"Error from COLL2GR: failled to allocate memory for gagrid structure\n");
    goto err;
  }

  pgr->grid = gr;
  pgr->umask = gru;
  pgr->undef = uu;
  pgr->isiz = scnt;
  pgr->jsiz = lcnt;
  pgr->idim  = 0;  /* arbitrary */
  pgr->jdim  = dim;
  pgr->iwrld = 1; pgr->jwrld = 0;
  pgr->dimmin[0] = 1; pgr->dimmax[0] = scnt;
  pgr->dimmin[2] = 1; pgr->dimmax[2] = lcnt;
  pgr->exprsn = NULL;
  pgr->ilinr = 0;
  pgr->jlinr = 0;
  sz = sizeof(gadouble)*(scnt+2);
  iv = (gadouble *)galloc(sz,"col2griv");
  if (iv==NULL) {
    gaprnt (0,"Error from COLL2GR: failled to allocate memory for scnt \n");
    goto err;
  }
  sz = sizeof(gadouble)*(lcnt+2);
  jv = (gadouble *)galloc(sz,"col2grjv");
  if (jv==NULL) {
    gaprnt (0,"Error from COLL2GR: failled to allocate memory for lcnt \n");
    goto err;
  }
  *iv = (gadouble)scnt;
  *jv = (gadouble)lcnt;
  xdiff = pst->dmax[0] - pst->dmin[0];
  xdiff = xdiff / ( (gadouble)(scnt-1) );
  for (i=1; i<=scnt; i++) *(iv+i) = pst->dmin[0] + (gadouble)(i-1) * xdiff;
  for (i=0; i<lcnt; i++) *(jv+i+1) = *(levs+i);
  *(jv+lcnt+1) = -999.9;
  *(iv+scnt+1) = -999.9;
  pgr->ivals = iv;
  pgr->jvals = jv;
  pgr->iavals = pgr->ivals;
  pgr->javals = pgr->jvals;
  pgr->igrab = gr2lev;
  pgr->jgrab = gr2lev;
  pgr->iabgr = lev2gr;
  pgr->jabgr = lev2gr;
  pgr->alocf = 1;

  pst->type = 1;
  pst->result.pgr = pgr;
  gree(levs,"f428");
  return (0);

 err:
  if (levs) gree(levs,"f429");
  if (gr) gree(gr,"f430");
  if (gru) gree(gru,"f431");
  if (pgr) gagfre(pgr);
  if (iv) gree(iv,"f432");
  if (jv) gree(jv,"f433");
  return (1);


}

/* Given a grid and a set of stations, interpolate to the
   grid using cressman technique and return the grid.             */

static gadouble rads[5] = {10.0, 7.0, 4.0, 2.0, 1.0};

gaint ffoacr (struct gafunc *pfc, struct gastat *pst) {
struct gastat pst2;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble (*iconv) (gadouble *, gadouble);
gadouble (*jconv) (gadouble *, gadouble);
gadouble *ivars, *jvars, *gr, *nw, *newbuf, *p1, *p2, *p3, *p4;
gadouble lon,lat,lnmin,lnmax,lnscl,ltmin,ltmax,ltscl;
gadouble sum;
gadouble e1,e2,e,wsum,x,y,xmin,xmax,ymin,ymax,d,d2,rad,rad2,w;
gaint *flgbuf, *ii;
gaint rc,i,j,p,siz,icnt,irad,radflg;
gadouble fgsum=-1e20;
gadouble radii[30];
char sumu,*gru,*nwu,*newbufu;
size_t sz;

  if (pfc->argnum<2) {
    gaprnt (0,"Error from OACRES:  Too many or too few args \n");
    gaprnt (0,"                    Two arguments expected \n");
    return (1);
  }
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    gaprnt (0,"Error from OACRES: 1st argument must be a grid\n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;
  if (pgr->idim!=0 || pgr->jdim!=1) {
    gaprnt (0,"Error from OACRES: Grid must vary in X, Y\n");
    gafree (pst);
    return (1);
  }
  if (!pgr->ilinr || !pgr->jlinr) {
    gaprnt (0,"Error from OACRES: Grid must have linear scaling\n");
    gafree (pst);
    return (1);
  }
  pst2 = *pst;
  rc = gaexpr(pfc->argpnt[1],&pst2);
  if (rc) {
    gafree (pst);
    return (rc);
  }
  if (pst2.type!=0) {
    gaprnt (0,"Error from OACRES: 2nd argument must be stns\n");
    gafree (&pst2);
    gafree (pst);
    return (1);
  }
  stn = pst2.result.stn;

  /* Check for user provided radii */
  irad = 5;
  radflg = 0;
  if (pfc->argnum>2) {
    radflg = 1;
    irad = pfc->argnum-2;
    if (irad>30) {
      gaprnt (1,"Warning from OACRES:  Using 30 pass radii\n");
      irad = 30;
    }
    for (i=0; i<irad; i++) {
      if (getdbl(pfc->argpnt[i+2],radii+i)==NULL) {
        gaprnt (0,"Error from OACRES:  Radii must be constant\n");
        gaprnt (0,"                    Using default radii\n");
        radflg = 0;
        i = irad+1;
      } else {
	if (radii[i]<0.0) {
	  i++;
	  if(irad != i+1) {
	    gaprnt (0,"Error from OACRES:  Improper setting of 1st guess\n");
	    gaprnt (0,"                    Must provide the value or make the value the last argument\n");
	    gaprnt (0,"                    Using default radii\n");
	    irad=5;
	    radflg=0;
            i=irad+1;
	  } else if (getdbl(pfc->argpnt[i+2],&fgsum)==NULL) {
	    gaprnt (0,"Error from OACRES:  Improper setting of 1st guess\n");
	    gaprnt (0,"                    Bad value\n");
	    gaprnt (0,"                    Using default radii\n");
	    irad=5;
	    radflg=0;
	    fgsum=-1e20;
            i=irad+2;
	  } else {
	    if(irad<1) radflg=0;
	    i=irad+2;
	    irad=irad-2;
	  }

	}
        if ((radii[i]<0.01 || radii[i]>50.0) && i<irad) {
          gaprnt (0,"Error from OACRES:  Radii must be 0<radii<50\n");
          gaprnt (0,"                    Using default radii\n");
          radflg = 0;
          i = irad+1;
        }
      }
    }
  }

  /* Set up scaling for converting lon-lat to grid units */

  iconv = pgr->igrab;
  ivars = pgr->ivals;
  jconv = pgr->jgrab;
  jvars = pgr->jvals;
  lnmin = iconv(ivars,(gadouble)pgr->dimmin[0]);
  lnmax = iconv(ivars,(gadouble)pgr->dimmax[0]);
  ltmin = iconv(jvars,(gadouble)pgr->dimmin[1]);
  ltmax = iconv(jvars,(gadouble)pgr->dimmax[1]);
  lnscl = (lnmax-lnmin)/((gadouble)pgr->isiz-1);
  ltscl = (ltmax-ltmin)/((gadouble)pgr->jsiz-1);

  /* Now loop through each stn report and convert stn lat/lon to grid coordinates */
  rpt = stn->rpt;
  sum = 0.0;
  icnt = 0;
  while (rpt!=NULL) {
    lon = rpt->lon;
    lat = rpt->lat;
    if (lon<lnmin) lon+=360.0;
    else if (lon>lnmax) lon-=360.0;
    rpt->lon = (lon-lnmin)/lnscl;
    rpt->lat = (lat-ltmin)/ltscl;
    i = (gaint)rpt->lon;
    j = (gaint)rpt->lat;
    if (i<0 || i>(pgr->isiz-1) ||
	j<0 || j>(pgr->jsiz-1) ||
        rpt->umask==0)
      rpt->work = -999;
    else {
      rpt->work = j*pgr->isiz + i;
      sum = sum + rpt->val;
      icnt++;
      i = (gaint)rpt->lon;
      j = (gaint)rpt->lat;
      rpt->lev = rpt->lon - (gadouble)i;
      rpt->tim = rpt->lat - (gadouble)j;
    }
    rpt=rpt->rpt;
  }
  if (icnt<2) {
    gaprnt (1,"Warning from OACRES:  Less than two stations\n");
    gaprnt (1,"    Grid will be all missing values\n");
    sumu = 0;
  }
  else {
    sum = sum/((gadouble)icnt);
    sumu = 1;
  }

  /* Need some buffer space */
  siz = pgr->isiz * pgr->jsiz;
  sz = sizeof(gadouble)*siz;
  newbuf = (gadouble *)galloc(sz,"oacrbuf");
  if (newbuf==NULL) {
    goto err;
  }
  sz = sizeof(char)*siz;
  newbufu = (char *)galloc(sz,"oacrbufu");
  if (newbufu==NULL) {
    gree(newbuf,"f434");
    goto err;
  }
  sz = sizeof(gaint)*siz;
  flgbuf = (gaint *)galloc(sz,"oacrbuff");
  if (flgbuf==NULL) {
    gree(newbuf,"f435");
    gree(newbufu,"f436");
    goto err;
  }

  /* Initial grid values are average of station reports */
  if (fabs(fgsum) < 1e20 ) {
    sum=fgsum;
    sumu=1;
  }
  gr = pgr->grid;
  gru = pgr->umask;
  nw = newbuf;
  nwu = newbufu;
  ii = flgbuf;
  for (i=0; i<siz; i++) {
    if (sumu==1) {
      *gr = sum; *gru = 1;
      *nw = sum; *nwu = 1;
    }
    else {
      *gru = 0;
      *nwu = 0;
    }
    *ii = 0;
    ii++;
    gr++; gru++;
    nw++; nwu++;
  }
  if (sumu==0) goto retrn;

  /* Perform the objective analysis */
  for (p=0; p<irad; p++) {
    if (radflg) rad = radii[p];
    else rad = rads[p];
    rad2 = rad*rad;
    gr = pgr->grid;
    nw = newbuf;
    nwu = newbufu;
    ii = flgbuf;
    for (j=0; j<pgr->jsiz; j++) {
      y = (gadouble)j;
      ymin = y - rad;
      ymax = y + rad;
      for (i=0; i<pgr->isiz; i++) {
        x = (gadouble)i;
        xmin = x - rad;
        xmax = x + rad;
        sum = 0.0; wsum = 0.0;
        rpt = stn->rpt;
        while (rpt) {
          if (rpt->work==-999 ||
	      rpt->lon < xmin || rpt->lon > xmax ||
              rpt->lat < ymin || rpt->lat > ymax ||
              (d = hypot(x-rpt->lon,y-rpt->lat)) > rad ) rpt = rpt->rpt;
          else {
            d2 = d*d;
            p1 = pgr->grid + rpt->work;
            p2 = p1+1;
            p4 = p1 + pgr->isiz;
            p3 = p4 + 1;
            e1 = *p1 + ( (*p2 - *p1)*rpt->lev );
            e2 = *p4 + ( (*p3 - *p4)*rpt->lev );
            e = e1 + ( (e2 - e1)*rpt->tim );
            e = rpt->val - e;
            w = (rad2-d2)/(rad2+d2);
            sum += e*w;
            wsum += w;
            rpt = rpt->rpt;
          }
        }
        if (wsum>1e-6) {
	  *nw = *gr + sum/wsum;
	  *nwu = 1;
	}
        else if (p==2) *ii = 1;
        nw++; nwu++; gr++; ii++;
      }
    }
    nw  = newbuf;
    nwu = newbufu;
    gr = pgr->grid;
    gru = pgr->umask;
    for (i=0; i<siz; i++) {
      if (*nwu==1) {
	*gr = *nw;
	*gru = 1;
      }
      else {
	*gru = 0;
      }
      gr++; gru++; nw++; nwu++;
    }
  }
  ii = flgbuf;
  gru = pgr->umask;
  for (i=0; i<siz; i++) {
    if (*ii) *gru = 0;
    gru++; ii++;
  }

  gr  = pgr->grid;
  gru = pgr->umask;

retrn:
  gafree (&pst2);
  gree(newbuf,"f437");
  gree(newbufu,"f438");
  gree(flgbuf,"f439");
  return(0);

err:
  gaprnt (0,"Error in OACRES:  Unable to allocate memory\n");
  gafree (&pst2);
  gafree (pst);
  return (1);
}

gaint ffoabn (struct gafunc *pfc, struct gastat *pst) {
  struct gastat pst2;
  struct gagrid *pgr;
  struct gastn *stn;
  struct garpt *rpt;
  gadouble (*iconv) (gadouble *, gadouble);
  gadouble (*jconv) (gadouble *, gadouble);
  gadouble *ivars, *jvars, *gr;
  gadouble lon,lat,lnmin,lnmax,lnscl,ltmin,ltmax,ltscl;
  gaint *cnt, *ii;
  gaint rc,i,j,siz,icnt,cntflg;
  char *ch, *gru;
  size_t sz;

  if (pfc->argnum<2) {
    gaprnt (0,"Error from OABIN:  Too many or too few args \n");
    gaprnt (0,"                   Two arguments expected \n");
    return (1);
  }
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    gaprnt (0,"Error from OABIN: 1st argument must be a grid\n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;
  if (pgr->idim!=0 || pgr->jdim!=1) {
    gaprnt (0,"Error from OABIN: Grid must vary in X, Y\n");
    gafree (pst);
    return (1);
  }
  if (!pgr->ilinr || !pgr->jlinr) {
    gaprnt (0,"Error from OABIN: Grid must have linear scaling\n");
    gafree (pst);
    return (1);
  }
  pst2 = *pst;
  rc = gaexpr(pfc->argpnt[1],&pst2);
  if (rc) {
    gafree (pst);
    return (rc);
  }
  if (pst2.type!=0) {
    gaprnt (0,"Error from OABIN: 2nd argument must be stns\n");
    gafree (&pst2);
    gafree (pst);
    return (1);
  }
  stn = pst2.result.stn;

  cntflg = 0;
  if (pfc->argnum>2) {
    ch = pfc->argpnt[2];
    if      (*ch == '-' && *(ch+1) == 'c') cntflg = 1;
    else if (*ch == '-' && *(ch+1) == 'f') cntflg = 2;
    else gaprnt (1,"Warning from OABIN: Invalid flag.  Ignored.\n");
  }

  /* Set up scaling for converting lon-lat to grid units */
  iconv = pgr->igrab;
  ivars = pgr->ivals;
  jconv = pgr->jgrab;
  jvars = pgr->jvals;
  lnmin = iconv(ivars,(gadouble)pgr->dimmin[0]);
  lnmax = iconv(ivars,(gadouble)pgr->dimmax[0]);
  ltmin = iconv(jvars,(gadouble)pgr->dimmin[1]);
  ltmax = iconv(jvars,(gadouble)pgr->dimmax[1]);
  lnscl = (lnmax-lnmin)/((gadouble)pgr->isiz-1);
  ltscl = (ltmax-ltmin)/((gadouble)pgr->jsiz-1);

  /* Now loop through each stn report and convert stn lat/lon to grid  coordinates */
  rpt = stn->rpt;
  icnt = 0;
  while (rpt!=NULL) {
    lon = rpt->lon;
    lat = rpt->lat;
    if (lon<lnmin) lon+=360.0;
    else if (lon>lnmax) lon-=360.0;
    rpt->lon = (lon-lnmin)/lnscl;
    rpt->lat = (lat-ltmin)/ltscl;
    /* nearest grid point center */
    i = (gaint)(rpt->lon+0.5);
    j = (gaint)(rpt->lat+0.5);
    if (i<0 || i>(pgr->isiz-1) ||
	j<0 || j>(pgr->jsiz-1) ||
        rpt->umask==0)
      rpt->work = -999;
    else {
      rpt->work = j*pgr->isiz + i;
    }
    rpt=rpt->rpt;
  }

  /* cnt space */
  siz = pgr->isiz * pgr->jsiz;
  sz = sizeof(gaint)*siz;
  cnt = (gaint *)galloc(sz,"oabincnt");
  if (cnt==NULL) {
    goto err;
  }

  /* initialize cnt and grid to 0.0 for summing */
  gr=pgr->grid;
  for(i=0;i<siz;i++) {
    *gr=0.0;
    gr++;
  }
  ii = cnt;
  for (i=0; i<siz; i++) {
    *ii = 0;
    ii++;
  }

  /* Perform the bin analysis */
  rpt = stn->rpt;
  while (rpt) {
    gr = pgr->grid;
    ii = cnt;
    if (rpt->work==-999) {
      rpt = rpt->rpt;
    }
    else {
      gr += rpt->work;
      ii += rpt->work;
      if( (cntflg <= 1) || (cntflg == 2 && *ii <= 1)) {
	*gr += rpt->val;
	*ii += 1;
      }
      rpt = rpt->rpt;
    }
  }

  gr=pgr->grid;
  gru=pgr->umask;
  ii=cnt;
  for(i=0;i<siz;i++) {
    if(*ii > 0) {
      *gr=(*gr)/(*ii);
      if(cntflg==1) *gr=(gadouble)(*ii);
      *gru = 1;
    } else {
      *gru=0;
    }
    gr++ ; gru++; ii++;
  }

  gafree (&pst2);
  gree(cnt,"f440");
  return(0);

err:
  gaprnt (0,"Error in OABIN:  Unable to allocate memory\n");
  gafree (&pst2);
  gafree (pst);
  return (1);
}



/* Nine point smoother */

gaint ffsmth  (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgr;
gadouble *buff, *gr;
gadouble w,s,mid,sid,cor;
gaint i,j,k,rc,siz,p;
char *gru, *buffu;
size_t sz;

  if (pfc->argnum!=1 && pfc->argnum!=4) {
    gaprnt (0,"Error from SMTH9:  Too many or too few args \n");
    gaprnt (0,"                   One or 4 arguments expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type!=1) {
    gaprnt (0,"Error from SMTH9:  Grid Required\n");
    gafree (pst);
    return (1);
  }

  pgr = pst->result.pgr;
  siz = pgr->isiz * pgr->jsiz;
  sz = siz*sizeof(gadouble);
  buff = (gadouble *)galloc(sz,"sm9buf");
  if (buff==NULL) {
    gaprnt (0,"Error from SMTH9:  Unable to allocate memory\n");
    gafree (pst);
    return (1);
  }
  sz = siz*sizeof(char);
  buffu = (char *)galloc(sz,"sm9bufu");
  if (buffu==NULL) {
    gaprnt (0,"Error from SMTH9:  Unable to allocate memory\n");
    gafree (pst);
    return (1);
  }

  mid = 1.0; sid = 0.5; cor = 0.3;
  if (pfc->argnum==4) {
    if (getdbl(pfc->argpnt[1],&mid)==NULL ||
        getdbl(pfc->argpnt[2],&sid)==NULL ||
        getdbl(pfc->argpnt[3],&cor)==NULL )  {
      gaprnt (0,"Error from SMTH9:  Invalid Weight Values\n");
      gaprnt (0,"  Using defaults:  1.0  0.5  0.3\n");
      mid = 1.0; sid = 0.5; cor = 0.3;
    }
  }

  gr = pgr->grid;
  gru = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(i,j,p,s,w) if(ga_omp_parallelize(siz))
#endif
  for (k=0; k<siz; k++) {
      i = k % pgr->isiz;
      j = k / pgr->isiz;
      if (*(gru+k)!=0) {
        s = *(gr+k)*mid;
        w = mid;
        if (i!=0 && *(gru+k-1)!=0) {
	  s = s + *(gr+k-1)*sid;
	  w+=sid;
	}
        if (i!=pgr->isiz-1 && *(gru+k+1)!=0) {
	  s = s + *(gr+k+1)*sid;
	  w+=sid;
	}
        if (j!=0) {
          p = k - pgr->isiz;
          if (*(gru+p)!=0) {
	    s = s + *(gr+p)*sid;
	    w+=sid;
	  }
          if (i!=0 && *(gru+p-1)!=0) {
	    s = s + *(gr+p-1)*cor;
	    w+=cor;
	  }
          if (i!=pgr->isiz-1 && *(gru+p+1)!=0) {
	    s = s + *(gr+p+1)*cor;
	    w+=cor;
	  }
        }
        if (j!=pgr->jsiz-1) {
          p = k + pgr->isiz;
          if (*(gru+p)!=0) {
	    s = s + *(gr+p)*sid;
	    w+=sid;
	  }
          if (i!=0 && *(gru+p-1)!=0) {
	    s = s + *(gr+p-1)*cor;
	    w+=cor;
	  }
          if (i!=pgr->isiz-1 && *(gru+p+1)!=0) {
	    s = s + *(gr+p+1)*cor;
	    w+=cor;
	  }
        }
	buff[k] = s/w;
	buffu[k] = 1;
      }
      else {
	buffu[k] = 0;
      }
  }
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(siz))
#endif
  for (i=0; i<siz; i++) {
    gr[i]  = buff[i];
    gru[i] = buffu[i];
  }

  gree(buff,"f441");
  gree(buffu,"f442");
  return (0);
}

/* Station Averaging.  Averaging is done in time only.
   If there are multiple stations per time, they are
   averaged with equal weight, then that value for that time
   is averaged with equal weight with the other times for that
   station.  The user may specify the number of times required
   for an average to be reported for a station.    */

gaint ffsave (struct gafunc *pfc, struct gastat *pst) {
struct gafile *pfi;
struct gastn *stnr, *stn;
struct garpt *rpt,*rpt2;
struct dt tinc,tloc,twrk,tstrt,tend;
gadouble gr1,gr2,*val,sum,cnt,ttt;
gaint dim,dim2,rc,d1,d2,d,fflg,i,mcnt,wflag=0;
gaint mos=0,mns=0,incr;
char *ch,chs[20],che[20];
size_t sz;

  /* Check for X, Y varying environment */

  if (pst->idim!=0 || pst->jdim!=1) {
    gaprnt (0,"Error from STNAVE:  X, Y varying environment required\n");
    return(1);
  }

  /* Check for valid number of args       */

  if (pfc->argnum<3 || pfc->argnum>5) {
    gaprnt (0,"Error from STNAVE:  Too many or too few args \n");
    gaprnt (0,"                    3 to 5 arguments expected \n");
    return (1);
  }

  /* Parse the dimension expression       */

  pfi = pst->pfid;
  ch = dimprs (pfc->argpnt[1], pst, pfi, &dim, &gr1, 1, &wflag);
  if (ch==NULL || dim!=3 || wflag==2) {
    gaprnt (0,"Error from STNAVE:  1st dimension expression invalid\n");
    if (wflag==2) gaprnt (0,"  offt expression not supported as an arg to STNAVE\n");
    return (1);
  }

  /* Now parse the 2nd dimension expression.  */

  ch = dimprs (pfc->argpnt[2], pst, pfi, &dim2, &gr2, 1, &wflag);
  if (ch==NULL || dim2!=dim || wflag==2) {
    gaprnt (0,"Error from STNAVE:  2nd dimension expression invalid\n");
    if (wflag==2) gaprnt (0,"  offt expression not supported as an arg to STNAVE\n");
    return (1);
  }

  /* Get optional arguments:  time increment and min number of times
     required for a successful average */

  val = pfi->grvals[3];
  if (*(val+5)>0) { mos = (*(val+5)); mns = 0; }
  if (*(val+6)>0) { mns = (*(val+6)); mos = 0; }

  mcnt = 1;
  if (pfc->argnum >= 4) {
    if (*(pfc->argpnt[3]) == '-') {    /* Option flags? */
      i = fndarg (pfc->argpnt[3], &mcnt);
      if (i) return (1);
    } else {                           /* Assume time increment */
                               /*  Done differently from grid ave */
                               /*  to allow increment of 1yr for
                                   daily data.  Might want to adopts
                                   this for grid ave also.  Think about
                                   a flag to handle leap years?  */
      ch = intprs(pfc->argpnt[3],&incr);
      if (ch==NULL) goto err3;
      if (*(val+5)>0) { mos = incr * (*(val+5)); mns = 0; }
      if (*(val+6)>0) { mns = incr * (*(val+6)); mos = 0; }
      if (*ch!='\0') {
        ch = rdtprs(pfc->argpnt[3],&tinc);
        if (ch==NULL) goto err3;
        mos = tinc.yr*12 + tinc.mo;
        mns = tinc.dy*1140 + tinc.hr*60 + tinc.mn;
        if (mos>0 && *(val+5)>0) {
          incr = mos / (*(val+5));
          if (mos!=incr*(*(val+5))) goto err3;
        }
        else if (mos>0 && *(val+6)>0) {
          incr = mos;  /* not really used */
        }
        else if (mns>0 && *(val+6)>0) {
          incr = mns / (*(val+6));
          if (mns!=incr*(*(val+6))) goto err3;
        }
        else goto err3;
      }
    }
  }
  if (pfc->argnum == 5) {
    i = fndarg (pfc->argpnt[4], &mcnt);
    if (i) return (1);
  }

  tinc.yr = 0; tinc.mo = mos; tinc.dy = 0; tinc.hr = 0; tinc.mn = mns;

  d1 = ceil(gr1-0.001);          /* Ave limits are integers    */
  d2 = floor(gr2+0.001);

  /* Set up the result stn block */

  sz = sizeof(struct gastn);
  stnr = (struct gastn *)galloc(sz,"stnavr");
  if (stnr==NULL) {
    gaprnt(0,"Memory allocation error:  station averaging function\n");
    return(1);
  }

  /* Loop over time */
  gr2t (pfi->grvals[3],d1,&tstrt);
  gr2t (pfi->grvals[3],d2,&tend);
  gat2ch (&tstrt,5,chs,20);
  gat2ch (&tend,5,che,20);
  snprintf(pout,1255,"Stn Averaging.  Dim = %i, Start = %s, End = %s Incr(mos,mns) = %i %i\n",
	   dim, chs, che, mos, mns);
  gaprnt (2,pout);

  rc = 0;
  fflg = 1;
  d = d1;
  while (d<=d2 && !rc) {
    gr2t (pfi->grvals[3],d,&(pst->tmin));
    pst->tmax = pst->tmin;
    /* evaluate the expression at this time */
    rc = gaexpr(pfc->argpnt[0],pst);
    if (rc || pst->type==1) goto err;
    stn = pst->result.stn;
    /* set up the block to hold the final result */
    if (fflg) {
      fflg = 0;       /* makes sure this is only done once */
      *stnr = *stn;
      stnr->rnum = 0;
      stnr->rpt = NULL;
      sz = sizeof(gadouble)*8;
      stnr->tvals = (gadouble *)galloc(sz,"stnavtvals");
      if (stnr->tvals==NULL) {
        gaprnt (0,"Memory Allocation Error:  Station Request Block \n");
        goto err;
      }
      for (i=0; i<8; i++) *(stnr->tvals+i) = *(stn->tvals+i);
    }

    /* Average multiple stations for this time, if any.  */

    rpt = stn->rpt;
    while (rpt) {
      rpt->work = 1;
      rpt = rpt->rpt;
    }
    rpt = stn->rpt;
    while (rpt) {
      if (rpt->work && rpt->umask!=0) {
        rpt2 = rpt->rpt;
        sum = rpt->val;
        cnt = 1.0;
	/* check for more reports to include in the average for this time */
        while (rpt2) {
	  /* additional reports must have same stid and location */
          if (rpt2->umask!=0 &&
              !cmpch(rpt->stid,rpt2->stid,8) &&
              rpt->lat == rpt2->lat && rpt->lon == rpt2->lon) {
            sum = sum + rpt2->val;
            cnt = cnt+1.0;
            rpt2->work = 0;       /* flag these reports as already used in calculation */
          }
          rpt2 = rpt2->rpt;
        }
        rpt->val = sum/cnt;       /* rpt->val now has the average of all reports */
      }
      rpt = rpt->rpt;
    }

    /* Now sum these with the items already in the result list */

    rpt = stn->rpt;
    while (rpt) {
      if (rpt->work) {
        rpt2 = stnr->rpt;  /* this will be NULL for the first time step */
        while (rpt2) {
          if (!cmpch(rpt->stid,rpt2->stid,8) &&
              rpt->lat == rpt2->lat && rpt->lon == rpt2->lon) break;
          rpt2 = rpt2->rpt;
        }
        if (rpt2==NULL) {
	  /* at first time step, allocate result (rpt2), copy rpt values into it  */
          rpt2 = gaarpt(stnr);
          if (rpt2==NULL) goto err;
          stnr->rnum++;
          *rpt2 = *rpt;
          rpt2->rpt = NULL;
          rpt2->work = 1;
          if (rpt->umask == 0) rpt2->umask = 0;
        } else {
	  /* for subsequent time steps, check if result is undefined */
          if (rpt2->umask == 0) {
	    /* no previous result, copy current time's average into result if defined */
	    if (rpt->umask != 0) {
              rpt2->val = rpt->val;
              rpt2->work = 1;
	      rpt2->umask = 1;
	    }
          } else if (rpt->umask != 0) {
	    /* add current time's average to result, increment work flag */
            rpt2->val = rpt2->val + rpt->val;
            rpt2->work++;
          }
        }
      }
      rpt = rpt->rpt;
    }
    gafree(pst);

    /*  Apply time increment */

    gr2t (pfi->grvals[3],d,&tloc);
    twrk = tinc;
    timadd (&tloc, &twrk);
    ttt = t2gr(pfi->grvals[3],&twrk);
    d = ttt;
    if (fabs(((gadouble)d)-ttt) > 0.001) {
      gaprnt(0,"Logic Error 16 in stnave.  Contact Developer(s).\n");
      goto err;
    }
  }

  /* Calculate final result, return */

  rpt = stnr->rpt;
  while (rpt) {
    if (rpt->umask!=0) {
      if (rpt->work<mcnt) rpt->umask = 0;
      else {
        cnt = (gadouble)rpt->work;
        rpt->val = rpt->val/cnt;
	rpt->umask = 1;
      }
    }
    rpt = rpt->rpt;
  }
  pst->result.stn = stnr;
  return(0);

err:
  gaprnt (0,"Error from STNAVE:  Unable to evaluate expression\n");
  gafree(pst);
  gasfre(stnr);
  return(1);
err3:
  snprintf(pout,1255,"Error from STNAVE: Invalid time increment argument\n");
  gaprnt(0,pout);
  return (1);
}

/* Set every nth grid point value to missing in a grid */

gaint ffskip (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgr;
gaint rc,iskip,jskip,ii,jj,i,j;
char *uval;

  if (pfc->argnum<2 || pfc->argnum>3) {
    gaprnt (0,"Error from SKIP:  Too many or too few args \n");
    gaprnt (0,"                  Two or 3 arguments expected \n");
    return (1);
  }
  if (intprs(pfc->argpnt[1],&iskip)==NULL) {
    gaprnt (0,"Error from SKIP:  2nd argument must be an integer \n");
    return(1);
  }
  if (pfc->argnum>2) {
    if (intprs(pfc->argpnt[2],&jskip)==NULL) {
      gaprnt (0,"Error from SKIP:  3rd argument must be an integer\n");
      return(1);
    }
  }
  else jskip = iskip;

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type!=1) {
    gaprnt (0,"Error from SKIP:  Grid Required\n");
    gafree (pst);
    return (1);
  }

  pgr = pst->result.pgr;
  uval = pgr->umask;
  if (pgr->idim==-1) return(0);

  iskip = iskip - 1;
  jskip = jskip - 1;
  jj = -1;
  for (j=0; j<pgr->jsiz; j++) {
    jj++;
    if (jj>jskip) jj=0;
    ii = -1;
    for (i=0; i<pgr->isiz; i++) {
      ii++;
      if (ii>iskip) ii=0;
      if (ii || jj) *uval = 0;
      uval++;
    }
  }
  return (0);
}

gaint ffgrarea (struct gafunc *pfc, struct gastat *pst) {
  struct gagrid *pgr;
  gadouble *val;
  gaint rc,i,j;
  gadouble (*iconv) (gadouble *, gadouble);
  gadouble (*jconv) (gadouble *, gadouble);
  gadouble *ivals, *jvals;
  gadouble rad,scl,w1,w2,y1,x1,alo,ahi;
  char *uval;

  if (pfc->argnum>3) {
    gaprnt (0,"Error from GRAREA:  Too many args \n");
    gaprnt (0,"                  1 arguments expected \n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type!=1) {
    gaprnt (0,"Error from GRAREA:  Grid Required\n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;
  if( (pgr->idim)!=0 || (pgr->jdim)!=1 ) {
    gaprnt (0,"Error from GRAREA:  XY (lon-lat) grid required\n");
    gafree (pst);
    return (1);
  }

  rad = acos(0.0)/90.0;
  scl=1.0/720.0;
  jconv = pgr->jgrab;
  ivals = pgr->ivals;
  iconv = pgr->igrab;
  jvals = pgr->jvals;
  val = pgr->grid;
  uval = pgr->umask;
  for (j=0; j<pgr->jsiz; j++) {
    y1 = (gadouble)(j+pgr->dimmin[1]);
    alo = jconv(jvals, y1-0.5);
    ahi = jconv(jvals, y1+0.5);
    if (alo < pst->dmin[1]) alo = pst->dmin[1];
    if (alo > pst->dmax[1]) alo = pst->dmax[1];
    if (ahi < pst->dmin[1]) ahi = pst->dmin[1];
    if (ahi > pst->dmax[1]) ahi = pst->dmax[1];
    if (alo < -90.0) alo = -90.0; if (ahi < -90.0) ahi = -90.0;
    if (alo >  90.0) alo =  90.0; if (ahi >  90.0) ahi =  90.0;
    w1 = fabs(sin(ahi*rad)-sin(alo*rad));  /* area weighting (aave) */

    for (i=0; i<pgr->isiz; i++) {
      x1 = (gadouble)(i+pgr->dimmin[0]);
      alo = iconv(ivals, x1-0.5);
      ahi = iconv(ivals, x1+0.5);
      if (alo<pst->dmin[0]) alo = pst->dmin[0];
      if (alo>pst->dmax[0]) alo = pst->dmax[0];
      if (ahi<pst->dmin[0]) ahi = pst->dmin[0];
      if (ahi>pst->dmax[0]) ahi = pst->dmax[0];
      w2 = (ahi - alo)*scl;
      if (*uval!=0) *val = w1 * w2;
      val++; uval++;
    }
  }
  return (0);
}

gaint ffcnst  (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,cnt,flg;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val,cnst;
char *ch, *uval;

  if (pfc->argnum<2 || pfc->argnum>3) {
    gaprnt (0,"Error from CONST:  Too many or too few args \n");
    gaprnt (0,"                   Two or 3 arguments expected \n");
    return (1);
  }

  if (getdbl(pfc->argpnt[1],&cnst)==NULL) {
    gaprnt (0,"Error from CONST:  2nd argument must be a constant\n");
    return(1);
  }

  flg = 0;
  if (pfc->argnum>2) {
    ch = pfc->argpnt[2];
    if      (*ch == '-' && *(ch+1) == 'u') flg = 1;
    else if (*ch == '-' && *(ch+1) == 'a') flg = 2;
    else gaprnt (1,"Warning from CONST: Invalid flag.  Ignored.\n");
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    /* gridded data */
    pgr = pst->result.pgr;
    cnt = pgr->isiz * pgr->jsiz;
    val = pgr->grid;
    uval = pgr->umask;
#if USEOPENMP == 1
#pragma omp parallel for simd schedule(static) if(ga_omp_parallelize(cnt))
#endif
    for (i=0; i<cnt; i++) {
      if (flg==0) {
	/* change valid data to a constant, missing data unchanged */
        if (uval[i]!=0) val[i] = cnst;
      }
      else if (flg==1) {
	/* change missing data to a constant, update mask value */
        if (uval[i]==0) {
	  val[i] = cnst;
	  uval[i] = 1;
	}
      }
      else if (flg==2) {
	/* change valid and missing data to a constaont, update mask values */
        val[i] = cnst;
	uval[i] = 1;
      }
    }
  }
  else {
    /* station data */
    stn = pst->result.stn;
    rpt = stn->rpt;
    while (rpt!=NULL) {
      if (flg==0) {
	/* change valid data to a constant, missing data unchanged */
        if (rpt->umask!=0) rpt->val = cnst;
      } else if (flg==1) {
	/* change missing data to a constant, update mask value */
        if (rpt->umask==0) {
	  rpt->val = cnst;
	  rpt->umask = 1;
	}
      } else if (flg==2) {
	/* change valid and missing data to a constaont, update mask values */
        rpt->val = cnst;
        rpt->umask = 1;
      }
      rpt=rpt->rpt;
    }
  }

  return (0);
}

/* Station min or max.  Done over time only.     */
char *smnxnam[2] = {"STNMIN","STNMAX"};

gaint ffsmin (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
  rc = ffsmnx (pfc, pst, 0);
  return (rc);
}

gaint ffsmax (struct gafunc *pfc, struct gastat *pst) {
gaint rc;
  rc = ffsmnx (pfc, pst, 1);
  return (rc);
}

gaint ffsmnx (struct gafunc *pfc, struct gastat *pst, gaint flg) {
struct gafile *pfi;
struct gastn *stnr, *stn;
struct garpt *rpt,*rpt2;
gadouble gr1,gr2;
gaint dim,dim2,rc,d1,d2,d,fflg,i,mcnt,wflag=0;
char *ch,*fnam;
size_t sz;

  fnam = smnxnam[flg];

  /* Check for X, Y varying environment */
  if (pst->idim!=0 || pst->jdim!=1) {
    snprintf(pout,1255,"Error from %s:  X, Y varying environment required\n",fnam);
    gaprnt (0,pout);
    return(1);
  }

  /* Check for valid number of args */
  if (pfc->argnum<3 || pfc->argnum>4) {
    snprintf(pout,1255,"Error from %s:  Too many or too few args \n",fnam);
    gaprnt (0,pout);
    gaprnt (0,"                    3 or 4 arguments expected \n");
    return (1);
  }

  /* Parse the dimension expression */
  pfi = pst->pfid;
  ch = dimprs (pfc->argpnt[1], pst, pfi, &dim, &gr1, 1, &wflag);
  if (ch==NULL || dim!=3 || wflag==2) {
    snprintf(pout,1255,"Error from %s:  1st dimension expression invalid\n",fnam);
    gaprnt (0,pout);
    if (wflag==2) {
      snprintf(pout,1255,"  offt expression not supported as an arg to %s\n",fnam);
      gaprnt (0,pout);
    }
    return (1);
  }

  /* Now parse the 2nd dimension expression. */
  ch = dimprs (pfc->argpnt[2], pst, pfi, &dim2, &gr2, 1, &wflag);
  if (ch==NULL || dim2!=dim || wflag==2) {
    snprintf(pout,1255,"Error from %s:  2nd dimension expression invalid\n",fnam);
    gaprnt (0,pout);
    if (wflag==2) {
      snprintf(pout,1255,"  offt expression not supported as an arg to %s\n",fnam);
      gaprnt (0,pout);
    }
    return (1);
  }

  /* Get the min number of times required for average, if provided */
  mcnt = 1;
  if (pfc->argnum==4) {
    i = fndarg (pfc->argpnt[3], &mcnt);
    if (i) return (1);
  }
  /* Ave limits are integers */
  d1 = ceil(gr1-0.001);
  d2 = floor(gr2+0.001);

  /* Set up the result stn block */
  sz = sizeof(struct gastn);
  stnr = (struct gastn *)galloc(sz,"stnminr");
  if (stnr==NULL) {
    gaprnt(0,"Memory allocation error:  station averaging function\n");
    return(1);
  }

  /* Loop over time */
  rc = 0;
  fflg = 1;
  for (d=d1; d<=d2 && !rc; d+=1) {

    /* Get next set of stations */
    gr2t (pfi->grvals[3],d,&(pst->tmin));
    pst->tmax = pst->tmin;
    rc = gaexpr(pfc->argpnt[0],pst);
    if (rc || pst->type==1) goto err;
    stn = pst->result.stn;
    if (fflg) {
      fflg = 0;
      *stnr = *stn;
      stnr->rnum = 0;
      stnr->rpt = NULL;
      sz = sizeof(gadouble)*8;
      stnr->tvals = (gadouble *)galloc(sz,"stnmintv");
      if (stnr->tvals==NULL) {
        gaprnt (0,"Memory Allocation Error:  Station Request Block \n");
        goto err;
      }
      for (i=0; i<8; i++) *(stnr->tvals+i) = *(stn->tvals+i);
    }

    /* Tabulate mins or maxes */
    rpt = stn->rpt;
    while (rpt) {
      rpt2 = stnr->rpt;
      while (rpt2) {
        if (!cmpch(rpt->stid,rpt2->stid,8) &&
            rpt->lat == rpt2->lat && rpt->lon == rpt2->lon) break;
        rpt2 = rpt2->rpt;
      }
      if (rpt2==NULL) {
        rpt2 = gaarpt(stnr);
        if (rpt2==NULL) goto err;
        stnr->rnum++;
        *rpt2 = *rpt;
        rpt2->work = 1;
        rpt2->rpt = NULL;
        if (rpt->umask == 0) rpt2->umask = 0;
      } else {
        if (rpt2->umask == 0) {
          rpt2->val = rpt->val;
          rpt2->umask = rpt->umask;
        } else if (rpt->umask != 0) {
          if (flg) {
            if (rpt->val > rpt2->val) rpt2->val = rpt->val;
          } else {
            if (rpt->val < rpt2->val) rpt2->val = rpt->val;
          }
          rpt2->work++;
        }
      }
      rpt = rpt->rpt;
    }
    gafree(pst);
  }

  /* Check min count if provided */
  if (mcnt>1) {
    rpt = stnr->rpt;
    while (rpt) {
      if (rpt->work<mcnt) rpt->umask = 0;
      rpt = rpt->rpt;
    }
  }

  /* Return final result. */
  pst->result.stn = stnr;
  return(0);

err:
  snprintf(pout,1255,"Error from %s:  Unable to evaluate expression\n",fnam);
  gaprnt (0,pout);
  gafree(pst);
  gasfre(stnr);
  return(1);
}

/*  Find args for the stnave, stnmin, and stnmax functions */

gaint fndarg (char *ch, gaint *iv) {
gaint ival;

   while (*ch==' ') ch++;
   if (*ch!='-' && *(ch+1)!='m') {
     gaprnt (0,"Invalid option argument in STN function\n");
     return (1);
   }

   ch+=2;
   while (*ch==' ') ch++;

   ch = intprs(ch,&ival);
   if (ch==NULL || ival<1) {
     gaprnt (0,"Invalid option argument in STN function\n");
     return (1);
   }

   *iv = ival;
   return (0);
}

/* Roll the evaluated grid, including its mask, within the selected X/Y
   extent. Coordinates and all other metadata stay in place. A fixed or
   absent axis is a singleton. Decimal shifts are reduced digit by digit so
   even values outside the machine integer range cannot overflow. */
gaint ffroll (struct gafunc *pfc, struct gastat *pst) {
  struct gagrid *grid;
  char *digits,*end,*axis,*tail,*mask=NULL;
  gadouble *values=NULL;
  gaint dim,n,shift,row,source_row,rc,negative=0;
  size_t count,width,at,source;
  unsigned long long reduced=0;

  if(pfc->argnum!=3)goto usage;
  axis=pfc->argpnt[2];while(*axis==' ')axis++;
  if(*axis!='x' && *axis!='y')goto usage;
  dim=*axis++=='x' ? 0 : 1;
  while(*axis==' ')axis++;
  if(*axis)goto usage;
  digits=pfc->argpnt[1];while(*digits==' ')digits++;
  if(*digits=='-' || *digits=='+')negative=*digits++=='-';
  end=digits;while(*end>='0' && *end<='9')end++;
  if(end==digits)goto usage;
  tail=end;while(*tail==' ')tail++;
  if(*tail)goto usage;

  rc=gaexpr(pfc->argpnt[0],pst);if(rc)return rc;
  if(pst->type!=1) {
    gaprnt(0,"ROLL error: expression must return a grid\n");
    gafree(pst);return 1;
  }
  grid=pst->result.pgr;
  if(dim==grid->idim)n=grid->isiz;
  else if(dim==grid->jdim)n=grid->jsiz;
  else return 0;
  if(n<=1)return 0;
  while(digits<end)reduced=(reduced*10+(unsigned)(*digits++-'0'))%(unsigned)n;
  if(negative && reduced)reduced=(unsigned)n-reduced;
  shift=(gaint)reduced;
  if(!shift)return 0;
  if(grid->isiz<1 || grid->jsiz<1 ||
     (size_t)grid->isiz>SIZE_MAX/(size_t)grid->jsiz)goto size_error;
  count=(size_t)grid->isiz*grid->jsiz;
  if(count>SIZE_MAX/sizeof(*values))goto size_error;
  values=galloc(count*sizeof(*values),"rollvalues");
  mask=galloc(count,"rollmask");
  if(!values || !mask) {
    if(values)gree(values,"rollvalues");
    if(mask)gree(mask,"rollmask");
    gaprnt(0,"ROLL error: unable to allocate result grid\n");
    gafree(pst);return 1;
  }
  width=(size_t)grid->isiz;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(at,source,source_row) if(grid->jsiz>1 && ga_omp_parallelize(count>INT_MAX ? INT_MAX : (gaint)count))
#endif
  for(row=0;row<grid->jsiz;row++) {
    at=(size_t)row*width;
    if(dim==grid->idim) {
      source=at+width-shift;
      memcpy(values+at,grid->grid+source,(size_t)shift*sizeof(*values));
      memcpy(mask+at,grid->umask+source,(size_t)shift);
      memcpy(values+at+shift,grid->grid+at,(width-shift)*sizeof(*values));
      memcpy(mask+at+shift,grid->umask+at,width-shift);
    } else {
      source_row=row>=shift ? row-shift : row+(n-shift);
      source=(size_t)source_row*width;
      memcpy(values+at,grid->grid+source,width*sizeof(*values));
      memcpy(mask+at,grid->umask+source,width);
    }
  }
  gree(grid->grid,"rollsource");gree(grid->umask,"rollsource");
  grid->grid=values;grid->umask=mask;
  return 0;
size_error:
  gaprnt(0,"ROLL error: grid is too large\n");gafree(pst);return 1;
usage:
  gaprnt(0,"ROLL error: expected roll(expr, integer_shift, x|y)\n");return 1;
}

gaint ffcdif (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgr;
gaint dim,rc,is,siz,i,j,k,sflag;
gadouble *res,*gr;
char *resundef, *gru;
size_t sz;

  /* Check for user errors */

  if (pfc->argnum!=2) {
    gaprnt (0,"Error from CDIFF:  Too many or too few args \n");
    gaprnt (0,"                   Two arguments expected \n");
    return (1);
  }

  if      (*(pfc->argpnt[1])=='x') dim = 0;
  else if (*(pfc->argpnt[1])=='y') dim = 1;
  else if (*(pfc->argpnt[1])=='z') dim = 2;
  else if (*(pfc->argpnt[1])=='t') dim = 3;
  else if (*(pfc->argpnt[1])=='e') dim = 4;
  else {
    gaprnt (0,"Error from CDIFF:  Invalid dimension argument\n");
    gaprnt (0,"  2nd argument must be X, Y, Z, T or E\n");
    return (1);
  }
  sflag = 0;
  if (*(pfc->argpnt[1]+1)=='p') sflag = 1;
  if (*(pfc->argpnt[1]+1)=='m') sflag = 2;


  /* Get the result grid. */

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type==0) {
    gafree (pst);
    return(-1);
  }
  pgr = pst->result.pgr;

  /* Verify that the dimension specified is a varying dimension */

  if (dim!=pgr->idim && dim!=pgr->jdim) {
    gaprnt (0,"Error from CDIFF:  Specified dimension non varying\n");
    gafree(pst);
    return(1);
  }

  /* Get the output grid and its undef mask */

  siz = pgr->isiz * pgr->jsiz;
  sz = siz*sizeof(gadouble);
  res = (gadouble *)galloc(sz,"cdifres");
  if (res==NULL) {
    gaprnt (0,"Error from CDIFF:  Memory allocation error\n");
    gafree(pst);
    return (1);
  }
  sz = siz*sizeof(char);
  resundef = (char *)galloc(sz,"cdifresu");
  if (resundef==NULL) {
    gaprnt (0,"Error from CDIFF:  Memory allocation error\n");
    gafree(pst);
    return (1);
  }

  /* Do the differencing */

  gr = pgr->grid;
  gru = pgr->umask;
  is = pgr->isiz;
#if USEOPENMP == 1
#pragma omp parallel for schedule(static) private(i,j) if(ga_omp_parallelize(siz))
#endif
  for (k=0; k<siz; k++) {
    i = k % is;
    j = k / is;
    if (dim==pgr->jdim) {
        if (sflag == 0) {
          if (j==0 || j==pgr->jsiz-1) {
	    resundef[k] = 0;
	  }
          else {
            if (gru[k+is]==0 || gru[k-is]==0) {
              resundef[k] = 0;
            }
	    else {
	      res[k] = gr[k+is] - gr[k-is];
	      resundef[k] = 1;
	    }
          }
        } else if (sflag == 1) {
          if (j==pgr->jsiz-1) {
	    resundef[k] = 0;
	  }
          else {
            if (gru[k+is]==0 || gru[k]==0) {
              resundef[k] = 0;
            }
	    else {
	      res[k] = gr[k+is] - gr[k];
	      resundef[k] = 1;
	    }
          }
        } else {
          if (j==0) {
	    resundef[k] = 0;
	  }
          else {
            if (gru[k]==0 || gru[k-is]==0) {
              resundef[k] = 0;
            }
	    else {
	      res[k] = gr[k] - gr[k-is];
	      resundef[k] = 1;
	    }
          }
        }
    }
    else {
        if (sflag==0) {
          if (i==0 || i==pgr->isiz-1) {
	    resundef[k] = 0;
	  }
          else {
            if (gru[k+1]==0 || gru[k-1]==0) {
              resundef[k] = 0;
            }
	    else {
	      res[k] = gr[k+1] - gr[k-1];
	      resundef[k] = 1;
	    }
          }
        }
	else if (sflag==1) {
          if (i==pgr->isiz-1) {
	    resundef[k] = 0;
	  }
          else {
            if (gru[k+1]==0 || gru[k]==0) {
              resundef[k] = 0;
            }
	    else {
	      res[k] = gr[k+1] - gr[k];
	      resundef[k] = 1;
	    }
          }
        }
	else {
          if (i==0) {
	    resundef[k] = 0;
	  }
          else {
            if (gru[k]==0 || gru[k-1]==0) {
              resundef[k] = 0;
            }
	    else {
	      res[k] = gr[k] - gr[k-1];
	      resundef[k] = 1;
	    }
          }
        }
    }
  }

  gree(pgr->grid,"f443");
  gree(pgr->umask,"f443a");
  pgr->grid = res;
  pgr->umask = resundef;
  return (0);
}


gaint ffflvl (struct gafunc *pfc, struct gastat *pst) {
struct gagrid *pgr1, *pgr2, *pgrv;
struct gafile *pfi;
gadouble (*lvconv) (gadouble *, gadouble);
gadouble levf, levl, *levs, clev, ulev, flev, llev, lev, lev1, lev2;
gadouble *lvvals, *gr1, *gr2, *grv, *grr, *res;
gaint rc, i, j, lvt, dim, wflag, cnt, size;
char *ch, *resundef, *gr1u, *gr2u, *grru;
size_t sz;

  if (pfc->argnum!=4) {
    gaprnt (0,"Error from FNDLVL:  Too many or too few args \n");
    gaprnt (0,"                    Four arguments expected \n");
    return (1);
  }

  /* Get 1st and last level.  They are args 3 and 4 and should
     be valid 'z' dimension expressions. */

  pfi = pst->pfid;

  ch = dimprs (pfc->argpnt[2], pst, pfi, &dim, &levf, 1, &wflag);
  if (ch==NULL || dim!=2) {
    gaprnt (0,"Error from FNDLVL:  Arg 3 an invalid Z dimension expression\n");
    return (1);
  }
  ch = dimprs (pfc->argpnt[3], pst, pfi, &dim, &levl, 1, &wflag);
  if (ch==NULL || dim!=2) {
    gaprnt (0,"Error from FNDLVL:  Arg 4 an invalid Z dimension expression\n");
    return (1);
  }

  /* Determine the levels we need to process, given the range provided
     by the user and the data levels available in the default file */

  lvt = pfi->dnum[2];
  if (lvt<3) {
    gaprnt (0,"Error from FNDLVL:  Too few levels in default file \n");
    return (1);
  }

  sz = sizeof(gadouble)*lvt;
  levs = (gadouble *)galloc(sz,"fndlevs");
  if (levs==NULL) {
    gaprnt (0,"Error from FNDLVL:  Memory allocation error \n");
    return (1);
  }
  cnt = 0;

  lvconv = pfi->gr2ab[2];
  lvvals = pfi->grvals[2];
  clev = lvconv(lvvals, 1.0);
  ulev = lvconv(lvvals, (gadouble)lvt);
  flev = lvconv(lvvals, levf);
  llev = lvconv(lvvals, levl);

  if ( (clev<ulev && flev<llev) ||
       (clev>ulev && flev>llev) ) {  /* User ordering is same as file */
    for (i=1; i<=lvt; i++) {
      lev = lvconv(lvvals, (gadouble)i);
      if ( (flev<llev && (lev>=flev && lev<=llev)) ||
           (flev>llev && (lev<=flev && lev>=llev)) ) {
        *(levs+cnt) = lev;  cnt++;
      }
    }
  } else {                           /* User ordering is reverse of file */
    for (i=lvt; i>=1; i--) {
      lev = lvconv(lvvals, (gadouble)i);
      if ( (flev<llev && (lev>=flev && lev<=llev)) ||
           (flev>llev && (lev<=flev && lev>=llev)) ) {
        *(levs+cnt) = lev;  cnt++;
      }
    }
  }

  /* Insure z is not a varying dimension */

  lev1 = *levs;
  if (pst->idim==2) {
    pst->idim = pst->jdim;
    pst->jdim = -1;
    pst->dmin[2] = lev1;
    pst->dmax[2] = lev1;
  }
  if (pst->jdim==2) {
    pst->jdim = -1;
    pst->dmin[2] = lev1;
    pst->dmax[2] = lev1;
  }

  /* Get the level to find (2nd arg).   Must be an expression that
     yields the same grid the 1st arg will yield. */

  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) {
    gree(levs,"f444");
    return (1);
  }
  if (pst->type==0) {
    gree(levs,"f445");
    gafree (pst);
    return (-1);
  }
  pgrv = pst->result.pgr;

  /* Get first grid (at 1st level) from 1st arg. */

  pst->dmin[2] = lev1;
  pst->dmax[2] = lev1;
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) {
    gree(levs,"f446");
    gagfre (pgrv);
    return (1);
  }
  if (pst->type==0) {
    gree(levs,"f447");
    gafree (pst);
    gagfre (pgrv);
    return (-1);
  }
  pgr1 = pst->result.pgr;

  /* Check that the two grids are equivalent.  */

  if (pgrv->idim!=pgr1->idim || pgrv->jdim != pgr1->jdim ||
      gagchk(pgrv,pgr1,pgrv->idim) || gagchk(pgrv,pgr1,pgrv->jdim)) {
    gaprnt (0,"Error from FNDLVL :  Incompatible grids. \n");
    gree(levs,"f448");
    gafree (pst);
    gagfre (pgrv);
    return (1);
  }

  /* Allocate space to hold the result and its undef mask */

  size = pgrv->isiz * pgrv->jsiz;
  sz = sizeof(gadouble)*size;
  res = (gadouble *)galloc(sz,"fndlvlres");
  if (res==NULL) {
    gaprnt (0,"Error from FNDLVL :  Memory allocation error \n");
    gree(levs,"f449");
    gafree (pst);
    gagfre (pgrv);
    return (1);
  }
  sz = sizeof(char)*size;
  resundef = (char *)galloc(sz,"fndlvlresu");
  if (resundef==NULL) {
    gaprnt (0,"Error from FNDLVL :  Memory allocation error \n");
    gree(levs,"f450");
    gafree (pst);
    gagfre (pgrv);
    return (1);
  }
  for (i=0; i<size; i++) *(resundef+i) = 0;

  /* Loop through the levels and, when appropriate, interpolate to find
     the level.  */

  for (i=1; i<cnt; i++) {
    lev2 = *(levs+i);
    pst->dmin[2] = lev2;
    pst->dmax[2] = lev2;
    rc = gaexpr(pfc->argpnt[0],pst);
    if (rc) {
      gree(levs,"f451");
      gree(res,"f452");
      gagfre (pgr1);
      gagfre (pgrv);
      return (1);
    }
    if (pst->type==0) {
      gree(levs,"f453");
      gree(res,"f454");
      gafree (pst);
      gagfre (pgr1);
      gagfre (pgrv);
      return (-1);
    }
    pgr2 = pst->result.pgr;

    gr1 = pgr1->grid; gr1u = pgr1->umask;
    gr2 = pgr2->grid; gr2u = pgr2->umask;
    grr = res;        grru = resundef;
    grv = pgrv->grid;
    for (j=0; j<size; j++) {
     if (*grru == 0) {                                       /* If we haven't yet found a level...  */
      if (*gr1u != 0 && *gr2u != 0) {                        /* and data is available... */
       if ((*gr1 <  *gr2 && *grv >= *gr1 && *grv <= *gr2) ||
           (*gr1 >= *gr2 && *grv <= *gr1 && *grv >= *gr2)) { /* and the level falls in this layer... */
         if (*gr2 == *gr1) {      /* a flat layer: any test wider than equal
                                     would miss small values (1e-5 apart) */
	   *grr = lev1;
	   *grru = 1;
	 }
         else {
	   *grr = lev1 + (lev2-lev1)*(*grv - *gr1)/(*gr2 - *gr1);   /* then interpolate. */
	   *grru = 1;
	 }
       }
      }
     }
     gr1++; gr1u++;
     gr2++; gr2u++;
     grr++; grru++;
     grv++;
    }
    gagfre(pgr1);
    pgr1 = pgr2;
    lev1 = lev2;
  }

  /* Release storage and return */

  gagfre (pgr1);
  if (pgrv->idim>-1) gree(pgrv->grid,"f455");
  if (pgrv->idim==-1) {
    pgrv->rmin = *res;
    pgrv->umin = *resundef;
    gree(res,"f456");
    gree(resundef,"457");
  }
  else {
    pgrv->grid = res;
    pgrv->umask = resundef;
  }

  pst->type = 1;
  pst->result.pgr = pgrv;
  gree(levs,"f458");
  return (0);
}


/*  Convert a station data time series into a grid;
    this allows more graphics operations and
    analytical comparisons.  */

gaint ffs2g1d (struct gafunc *pfc, struct gastat *pst) {
gaint i,rc,size,idim,jdim;
struct gagrid *pgr;
struct gastn *stn;
struct garpt *rpt;
gadouble *val,*tvals;
char *valundef;
size_t sz;

  if (pfc->argnum!=1) {
    gaprnt (0,"Error from S2G1D: Too many or too few args \n");
    gaprnt (0,"                  One argument expected \n");
    return (1);
  }

  if (pst->idim != 3 || pst->jdim != -1) {
    gaprnt (0,"Error from S2G1D: Time can be only varying dimension\n");
    return (1);
  }

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);

  if (pst->type==1) {
    gaprnt (0,"Error from S2G1D: Station data argument expected\n");
    return (1);
  }

  stn = pst->result.stn;

  if (stn->idim != 3 || stn->jdim != -1) {
    gaprnt (0,"Error from S2G1D: Logic Error 4; contact developer\n");
    return (1);
  }

  sz = sizeof(struct gagrid);
  pgr = (struct gagrid *)galloc(sz,"s2g1dpgr");

  if (pgr==NULL) {
    gaprnt (0,"Memory Allocation Error:  Grid Request Block\n");
    return (1);
  }

  /* Fill in gagrid variables */

  pgr->pfile = NULL;
  pgr->undef = stn->undef;
  pgr->isiz = 1 + stn->tmax - stn->tmin;
  pgr->jsiz = 1;
  idim = stn->idim; jdim = stn->jdim;
  pgr->exprsn = NULL;
  pgr->alocf = 1;
  pgr->pvar  = NULL;
  pgr->idim  = idim;
  pgr->jdim  = jdim;
  pgr->iwrld = 0;  pgr->jwrld = 0;
  for (i=0;i<3;i++) {
    pgr->dimmin[i] = 1;
    pgr->dimmax[i] = 1;
  }
  pgr->dimmin[3] = stn->tmin;
  pgr->dimmax[3] = stn->tmax;
  pgr->ilinr = 1;
  pgr->jlinr = 0;

  sz = sizeof(gadouble)*8;
  tvals = (gadouble *)galloc(sz,"s2g1dtvals");
  if (tvals == NULL) {
    gaprnt (0,"Memory Allocation Error: Dimension array\n");
    gree(pgr,"f459");
    return (1);
  }

  for (i=0; i<8; i++) {
    *(tvals+i) = *(stn->tvals+i);
  }
  pgr->ivals = tvals;
  pgr->iavals = tvals;
  pgr->jvals = NULL;
  /* this is for the grid */
  sz = sizeof(gadouble)*pgr->isiz;
  pgr->grid = (gadouble *)galloc(sz,"s2g1dgr");
  if (pgr->grid == NULL) {
    gaprnt (0,"Memory Allocation Error: Data array\n");
    gree(pgr,"f460");
    return (1);
  }
  /* this is for the undef mask */
  sz = sizeof(char)*pgr->isiz;
  pgr->umask = (char *)galloc(sz,"s2g2dgru");
  if (pgr->umask == NULL) {
    gaprnt (0,"Memory Allocation Error: Undef mask array\n");
    gree(pgr,"f461");
    return (1);
  }

  /* pre-fill the grid with undef values */
  valundef = pgr->umask;
  size = pgr->isiz;
  for (i=0; i<size; i++) {
    *valundef = 0;
    valundef++;
  }
  /* now populate the grid with station values */
  valundef = pgr->umask;
  val = pgr->grid;
  rpt = stn->rpt;
  while (rpt!=NULL) {
    if (rpt->val != stn->undef) {
      i = (gaint)(rpt->tim) - stn->tmin;
      *(val+i) = rpt->val;
      *(valundef+i) = 1;
    }
    rpt=rpt->rpt;
  }

  gafree(pst);

  pst->type = 1;
  pst->result.pgr = pgr;

  return (0);
}

gaint ffudpi (struct gafunc *pfc, struct gastat *pst2, struct gaupb *upb) {
struct gaudpinfo *pudpinfo;
void *handle;
char *error;
gaint rc;
gaint (*pfunc)(struct gafunc *, struct gastat *, struct gaudpinfo *);

  /* set up the pointer to gaexpr */
  pudpinfo = malloc(sizeof(struct gaudpinfo));
  pudpinfo->exprptr = gaexpr;
  pudpinfo->version = UDPVERS;

  /* load the shared object file and get the function pointer */
  if (upb->pfunc == NULL) {
    handle = dlopen(upb->fname,RTLD_LAZY);
    if (handle==NULL) {
      snprintf (pout,1255,"Error: dlopen failed to get a handle on %s \n",upb->fname);
      gaprnt (0,pout);
      return (1);
    }
    dlerror();
    pfunc = dlsym(handle,upb->alias);
    if ((error=dlerror()) != NULL) {
      snprintf (pout,1255,"Error: dlsym failed to load %s \n%s \n",upb->alias,error);
      gaprnt (0,pout);
      return (1);
    }
    /* save the function pointer */
    upb->pfunc = pfunc;
  }
  else {
    pfunc = upb->pfunc;
  }

  /* call the function */
  rc = (*pfunc)(pfc,pst2,pudpinfo);

  free (pudpinfo);
  return (rc);
}

/* Prints the contents of the chain of upb structures */
void gaprntupb (void) {
struct gaupb *upb;
  upb = upba;
  while (upb) {
    if (upb->type==1) {
      if (!strcmp(upb->name,upb->alias))
	sprintf (pout,"function   %-15s  %s\n",upb->name, upb->fname);
      else
	sprintf (pout,"function   %-15s  %s  %s\n",upb->name, upb->fname, upb->alias);
    }
    if (upb->type==2) sprintf (pout,"defop      %-15s  %s\n",upb->name, upb->fname);
    if (upb->type==3) sprintf (pout,"gxdisplay  %-15s  %s\n",upb->name, upb->fname);
    if (upb->type==4) sprintf (pout,"gxprint    %-15s  %s\n",upb->name, upb->fname);
    gaprnt (2,pout);
    upb = upb->upb;
  }
}
