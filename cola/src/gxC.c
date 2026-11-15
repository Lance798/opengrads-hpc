/* Copyright (C) 1988-2018 by George Mason University. See file COPYRIGHT for more information. */
/* Modified in 2026 to build the Cairo plug-ins without Xlib; see COPYING. */

/* This file contains the primary Cairo-GrADS interface. 
   The interactive interface (X windows) is managed by routines in gxX.c and the routines here.  
   Hardcopy output is generated here, but managed in gxprint.c  */


/* Include ./configure's header file */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cairo.h>
#include <cairo-ps.h>
#include <cairo-svg.h>
#include <cairo-pdf.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "gatypes.h"
#include "gx.h"
#include "gxC.h"
#include "gxCunite.h"

#define SX 800                              /* default X dimension of the image output */

void gxdXflush(void);                       /* function prototype (only used here and in gxX.c) */

/* local variables */
static gaint lcolor=-999,lwidth;            /* Current attributes */
static gaint actualcolor;                   /* current *actual* color */
static gaint lcolorsave,lwidthsave;         /* for save and restore */
static gaint rsav,gsav,bsav,psav;           /* Avoid excess color change calls */ 
static gaint aaflg=0;                       /* Anti-Aliasing flag */
static gaint filflg=0;                      /* Polygon-Filling flag */
static gaint brdrflg=0;                     /* Vector graphic border flag */
static gadouble brdrwid=0.0;                /* Vector graphic border size */
static gaint rotate=0;                      /* rotate landscape plots for hardcopy output */
static gadouble borderscale=1.0,borderx=0.0,bordery=0.0;
static gaint aasave,typesave;
static gaint width, height;                 /* Drawable size */
static gaint Xwidth, Xheight;               /* Window size */
static gaint Bwidth, Bheight;               /* Batch surface size */
static gaint force=0;                       /* force a color change */
static gadouble xsize, ysize;               /* GrADS page size (inches) */
static gadouble xscl,yscl;                  /* Window Scaling */
static gadouble xxx,yyy;                    /* Old position */
static FT_Library library=NULL;             /* for drawing fonts with FreeType with cairo */
static FT_Face face[100];                   /* for saving FreeType font faces */ 
static gaint faceinit=0;                    /* for knowing whether font faces have been initialized */
static cairo_surface_t *surface=NULL;       /* Surface being drawn to */   
static cairo_t *cr;                         /* graphics context */
static gaint surftype;                      /* Type of current surface.
                                               1=X, 2=Image, 3=PS/EPS, 4=PDF, 5=SVG */
static cairo_surface_t *Xsurface=NULL, *Hsurface=NULL, *Bsurface=NULL;
                                            /* X, Hardcopy, and Batch mode surfaces */
                                            /* X surface is passed to us by gxX. */
                                            /* Others are created as needed. */
static cairo_t *Xcr, *Hcr, *Bcr;            /* X, Hardcopy, and Batch mode contexts */  
static cairo_pattern_t *pattern[2048];      /* Save our patterns here */
static cairo_surface_t *pattsurf[2048];
static cairo_surface_t *surfmask, *surfsave;  /* Stuff for masking */
static cairo_t *crmask, *crsave;
static cairo_pattern_t *patternmask;
static gaint drawing;                         /* In the middle of line-to's? */
static gaint maskflag;                        /* Masking going on */
static gaint batch=0;                         /* Batch mode */
static gadouble clx,cly,clw,clh;              /* current clipping coordinates */
static gadouble clxsav,clysav,clwsav,clhsav;  /* saved clipping coordinates */

/* A grfill pass may contain a million cells but only a few colors. Keep
   exact rectangles until the complete pass has been checked for overlap.
   Unsupported geometry, states or allocation limits replay in original order. */
struct gxCrect { gadouble x0,y0,x1,y1; gaint color; };
struct gxCrgba { gaint r,g,b,a; };
static gaint vectorcompact=0;
static struct {
  struct gxCrect *rect;
  size_t count,capacity;
  struct gxCrgba color[COLORMAX];
  gaint colors,active,disabled;
} gridfill;
static struct { gaint active,pending; size_t points; } shadefill;
static struct { gaint active,pending; } character;
static struct { gaint active,pending; } gridline;

static void gxCgridlineflush(void) {
  if (!gridline.pending) return;
  cairo_stroke(cr);
  gridline.pending=0;
  drawing=0;
}

void gxCgridline(gaint begin) {
  if (begin) {
    if (!vectorcompact || surftype<=2 || maskflag) return;
    gxCflush(1);
    gridline.active=1;
  } else {
    gxCgridlineflush();
    gridline.active=0;
  }
}

/* PS/EPS retain Cairo's existing partial-alpha fallback. Negative alpha
   uses a separate mask context and must not enter either collector. */
static gaint gxCcompactalpha(gaint alpha) {
  return alpha>=0 && alpha<=255 && (alpha==255 || surftype==4 || surftype==5);
}

static void gxCcharacterflush(void) {
  if (!character.pending) return;
  cairo_stroke(cr);
  character.pending=0;
  drawing=0;
}

void gxCcharacter(gaint begin) {
  if (begin) {
    if (!vectorcompact || surftype<=2 || maskflag) return;
    gxCflush(1);
    character.active=1;
  } else {
    gxCcharacterflush();
    character.active=0;
  }
}

static void gxCshadeflush(void) {
  if (!shadefill.pending) return;
  cairo_save(cr);
  cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE);
  gxCunite(cr);
  cairo_fill(cr);
  cairo_restore(cr);
  shadefill.pending=0;
  shadefill.points=0;
}

void gxCshade(gaint begin) {
  if (begin) {
    if (!vectorcompact || surftype<=2 || maskflag) return;
    gxCflush(1);
    shadefill.active=1;
  } else {
    gxCshadeflush();
    shadefill.active=0;
  }
}

static int gxCrect_ycmp(const void *aa,const void *bb) {
const struct gxCrect *a=aa,*b=bb;
  if (a->color!=b->color) return a->color<b->color ? -1:1;
#define RCMP(field) if (a->field!=b->field) return a->field<b->field ? -1:1
  RCMP(y0); RCMP(y1); RCMP(x0); RCMP(x1);
  return 0;
}
static int gxCrect_xcmp(const void *aa,const void *bb) {
const struct gxCrect *a=aa,*b=bb;
  if (a->color!=b->color) return a->color<b->color ? -1:1;
  RCMP(x0); RCMP(x1); RCMP(y0); RCMP(y1);
#undef RCMP
  return 0;
}

static void gxCgridfree(void) {
  free(gridfill.rect);
  gridfill.rect=NULL;
  gridfill.count=gridfill.capacity=0;
  gridfill.colors=0;
}

static void gxCgridpaint(gaint combine) {
size_t i;
gaint color=-1;
struct gxCrgba *rgba;
struct gxCrect *r;
  if (!gridfill.count) return;
  cairo_save(cr);
  cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE);
  cairo_set_fill_rule(cr,CAIRO_FILL_RULE_WINDING);
  for (i=0;i<gridfill.count;i++) {
    r=gridfill.rect+i;
    if (color!=r->color) {
      if (combine && color!=-1) { gxCunite(cr); cairo_fill(cr); }
      color=r->color;
      rgba=gridfill.color+color;
      cairo_set_source_rgba(cr,rgba->r/255.0,rgba->g/255.0,rgba->b/255.0,rgba->a/255.0);
    }
    cairo_rectangle(cr,r->x0,r->y0,r->x1-r->x0,r->y1-r->y0);
    if (!combine) cairo_fill(cr);
  }
  if (combine) { gxCunite(cr); cairo_fill(cr); }
  cairo_restore(cr);
}

static void gxCgridabort(void) {
  if (!gridfill.active || gridfill.disabled) return;
  gxCgridpaint(0);
  gxCgridfree();
  gridfill.disabled=1;
}

/* Row-major monotone rectangles prove disjointness across all colors.
   Missing cells and rows are allowed; overlapping/folded projections are not. */
static gaint gxCgriddisjoint(void) {
size_t i;
gaint xdir=0,ydir=0,direction;
struct gxCrect row,last,r;
  if (!gridfill.count) return 1;
  row=last=gridfill.rect[0];
  for (i=1;i<gridfill.count;i++) {
    r=gridfill.rect[i];
    if (r.y0==row.y0 && r.y1==row.y1) {
      direction=r.x0>=last.x1 ? 1 : r.x1<=last.x0 ? -1 : 0;
      if (!direction || (xdir && direction!=xdir)) return 0;
      xdir=direction;
    } else {
      direction=r.y0>=row.y1 ? 1 : r.y1<=row.y0 ? -1 : 0;
      if (!direction || (ydir && direction!=ydir)) return 0;
      ydir=direction;
      row=r;
    }
    last=r;
  }
  return 1;
}

static void gxCgridmerge(void) {
size_t i,n=0;
struct gxCrect r,*previous;
  if (!gridfill.count) return;
  qsort(gridfill.rect,gridfill.count,sizeof(*gridfill.rect),gxCrect_ycmp);
  for (i=0;i<gridfill.count;i++) {
    r=gridfill.rect[i];
    previous=n ? gridfill.rect+n-1 : NULL;
    if (previous && previous->color==r.color && previous->y0==r.y0 && previous->y1==r.y1 && previous->x1==r.x0)
      previous->x1=r.x1;
    else gridfill.rect[n++]=r;
  }
  gridfill.count=n;
  qsort(gridfill.rect,n,sizeof(*gridfill.rect),gxCrect_xcmp);
  n=0;
  for (i=0;i<gridfill.count;i++) {
    r=gridfill.rect[i];
    previous=n ? gridfill.rect+n-1 : NULL;
    if (previous && previous->color==r.color && previous->x0==r.x0 && previous->x1==r.x1 && previous->y1==r.y0)
      previous->y1=r.y1;
    else gridfill.rect[n++]=r;
  }
  gridfill.count=n;
  /* Row order also gives PDF's stream compressor local coordinate reuse. */
  qsort(gridfill.rect,n,sizeof(*gridfill.rect),gxCrect_ycmp);
}

static gaint gxCgridadd(gadouble *xy,gaint count) {
gadouble x[32],y[32],x0,x1,y0,y1;
gaint i,j,k,n=0,changed,color=-1;
size_t capacity,limit=(64u*1024u*1024u)/sizeof(struct gxCrect);
struct gxCrect *buffer;
struct gxCrgba *rgba;
  if (count<3 || count>32 || !gxCcompactalpha(psav) || rsav<0 || maskflag ||
      cairo_get_operator(cr)!=CAIRO_OPERATOR_OVER ||
      cairo_pattern_get_type(cairo_get_source(cr))!=CAIRO_PATTERN_TYPE_SOLID) return 0;
  for (i=0;i<count;i++) {
    gxCxycnv(xy[2*i],xy[2*i+1],x+n,y+n);
    if (!isfinite(x[n]) || !isfinite(y[n])) return 0;
    if (!n || x[n]!=x[n-1] || y[n]!=y[n-1]) n++;
  }
  if (n>1 && x[n-1]==x[0] && y[n-1]==y[0]) n--;
  do {
    changed=0;
    for (i=0;n>2 && i<n;i++) {
      j=(i+n-1)%n; k=(i+1)%n;
      if ((x[j]==x[i] && x[i]==x[k] && y[i]>=fmin(y[j],y[k]) && y[i]<=fmax(y[j],y[k])) ||
          (y[j]==y[i] && y[i]==y[k] && x[i]>=fmin(x[j],x[k]) && x[i]<=fmax(x[j],x[k]))) {
        for (j=i;j<n-1;j++) { x[j]=x[j+1]; y[j]=y[j+1]; }
        n--; changed=1; break;
      }
    }
  } while (changed);
  if (n<3) return 1;
  if (n!=4) return 0;
  x0=x1=x[0]; y0=y1=y[0];
  for (i=0;i<n;i++) {
    x0=fmin(x0,x[i]); x1=fmax(x1,x[i]); y0=fmin(y0,y[i]); y1=fmax(y1,y[i]);
    j=(i+1)%n;
    if (x[i]!=x[j] && y[i]!=y[j]) return 0;
  }
  if (x0==x1 || y0==y1) return 1;
  for (i=0;i<n;i++) {
    if ((x[i]!=x0 && x[i]!=x1) || (y[i]!=y0 && y[i]!=y1)) return 0;
    for (j=0;j<i;j++) if (x[i]==x[j] && y[i]==y[j]) return 0;
  }
  for (i=0;i<gridfill.colors;i++) {
    rgba=gridfill.color+i;
    if (rgba->r==rsav && rgba->g==gsav && rgba->b==bsav && rgba->a==psav) { color=i; break; }
  }
  if (color<0) {
    if (gridfill.colors==COLORMAX) return 0;
    color=gridfill.colors++;
    rgba=gridfill.color+color;
    rgba->r=rsav; rgba->g=gsav; rgba->b=bsav; rgba->a=psav;
  }
  if (gridfill.count==gridfill.capacity) {
    if (gridfill.count==limit) return 0;
    capacity=gridfill.capacity ? gridfill.capacity*2 : 1024;
    if (capacity>limit) capacity=limit;
    buffer=realloc(gridfill.rect,capacity*sizeof(*buffer));
    if (!buffer) return 0;
    gridfill.rect=buffer; gridfill.capacity=capacity;
  }
  gridfill.rect[gridfill.count++]=(struct gxCrect){x0,y0,x1,y1,color};
  return 1;
}

void gxCgrid(gaint begin) {
  if (begin) {
    if (!vectorcompact || surftype<=2 || maskflag) return;
    gxCflush(1);
    gridfill.active=1;
    gridfill.disabled=0;
  } else {
    if (gridfill.active && !gridfill.disabled) {
      if (gxCgriddisjoint()) { gxCgridmerge(); gxCgridpaint(1); }
      else gxCgridpaint(0);
    }
    gxCgridfree();
    gridfill.active=gridfill.disabled=0;
  }
}

/* Cairo normally crops EPS BoundingBox to the ink. Keep the real canvas,
   including a transparent background, without inserting a visible shape. */
static struct {
  FILE *file;
  char line[256];
  size_t length;
  gaint header;
  gadouble width,height;
} psoutput;

static cairo_status_t gxCpswrite(void *closure, const unsigned char *data, unsigned int length) {
unsigned int i=0;
int written;
  (void)closure;
  while (psoutput.header && i<length) {
    if (psoutput.length==sizeof(psoutput.line)-1) return CAIRO_STATUS_WRITE_ERROR;
    psoutput.line[psoutput.length++]=(char)data[i++];
    if (psoutput.line[psoutput.length-1]!='\n') continue;
    psoutput.line[psoutput.length]='\0';
    if (!strncmp(psoutput.line,"%%BoundingBox:",14)) {
      written=fprintf(psoutput.file,"%%%%BoundingBox: 0 0 %d %d\n",
                      (int)ceil(psoutput.width),(int)ceil(psoutput.height));
      if (written<0) return CAIRO_STATUS_WRITE_ERROR;
    } else if (!strncmp(psoutput.line,"%%HiResBoundingBox:",19)) {
      written=fprintf(psoutput.file,"%%%%HiResBoundingBox: 0 0 %.9g %.9g\n",psoutput.width,psoutput.height);
      if (written<0) return CAIRO_STATUS_WRITE_ERROR;
    } else if (fwrite(psoutput.line,1,psoutput.length,psoutput.file)!=psoutput.length) {
      return CAIRO_STATUS_WRITE_ERROR;
    }
    if (!strncmp(psoutput.line,"%%EndComments",13)) psoutput.header=0;
    psoutput.length=0;
  }
  if (i<length && fwrite(data+i,1,length-i,psoutput.file)!=length-i) return CAIRO_STATUS_WRITE_ERROR;
  return CAIRO_STATUS_SUCCESS;
}


/* Initialize X interface, allocate cr */
/* If in batch mode, we don't get here */

void gxCbgn (cairo_surface_t *Xsfc, gadouble xsz, gadouble ysz, gaint dw, gaint dh) {
gaint i;

  batch = 0;
  for (i=0; i<2048; i++) pattern[i] = NULL;
  for (i=0; i<2048; i++) pattsurf[i] = NULL;
  if (!faceinit) {
    for (i=0; i<100; i++) face[i] = NULL; 
    faceinit=1;
    gxCftinit();         /* make sure FreeType library has been initialized */
  }

  drawing = 0;
  maskflag = 0;

  /* window dimensions */
  width = dw;   
  height = dh;  
  Xwidth = dw; 
  Xheight = dh;

  /* set page sizes  in inches */
  xsize = xsz; 
  ysize = ysz;

  /* initialize clipping parameters */
  clx = 0;     
  cly = 0;
  clw = width;
  clh = height;

  /* scale factors */
  xscl = (gadouble)(dw)/xsz;
  yscl = (gadouble)(dh)/ysz;


  /* make the X surface active for drawing */
  surface = Xsfc;
  surftype = 1;
  Xsurface = surface;
  cr = cairo_create(surface);
  Xcr = cr;
    
  /* default drawing settings */
  cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
  gxCaa(1);                   /* initialize anti-aliasing to be ON */ 
  gxCcol(1);                  /* initial color is foreground */
}

/* Close down and clean up */

void gxCend (void) {
  if (batch) {
    /* batch mode */
    cairo_destroy(Bcr);
    cairo_surface_finish(Bsurface);
    cairo_surface_destroy(Bsurface);
    Bcr = NULL;
    Bsurface = NULL;
  }
  else {
    /* the interactive session */
    gxCflush(0);
    cairo_destroy(cr);
    cr = NULL;
  }
  /* close the FreeType library */
  gxCftend();     
}


/* Initialize an image surface for batch mode, created on startup.
   This will be used to get the lengths of strings. 
   Called by the printing layer.
   If in interactive mode, we don't get here. */

void gxCbatch (gadouble xsz, gadouble ysz) {
gaint dh,dw;               /* image size in pixels */
gaint i;

  batch=1;
  if (!faceinit) {
    for (i=0; i<100; i++) face[i] = NULL; 
    faceinit=1;
    gxCftinit();         /* make sure FreeType library has been initialized */
  }

  /* set page sizes  in inches */
  xsize = xsz; 
  ysize = ysz;

  /* set window dimensions; 100 times the page size or 1100 by 850 */
  if (xsize > ysize) { 
    dw = 1100;
    dh = 850;
  } 
  else {
    dw = 850;
    dh = 1100;
  }
  width = dw;
  height = dh; 
  Bwidth = dw; 
  Bheight = dh;

  /* scale factors (pixels per inch) */
  xscl = (gadouble)(dw)/xsize;
  yscl = (gadouble)(dh)/ysize;

  /* initialize clipping parameters */
  clx = 0;     
  cly = 0;
  clw = width;
  clh = height;

  /* create the image surface */
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, dw, dh);
  surftype = 2;
  cr = cairo_create (surface);

  Bsurface = surface;
  Bcr = cr;
}


/* Restore the display context on both successful and failed exports. */
static void gxChrestore (void) {
  gxCgridfree();
  gridfill.active=gridfill.disabled=vectorcompact=0;
  shadefill.active=shadefill.pending=0;
  shadefill.points=0;
  character.active=character.pending=0;
  gridline.active=gridline.pending=0;
  Hsurface=NULL;
  Hcr=NULL;
  rotate=0;
  brdrflg=0;
  brdrwid=0.0;
  borderscale=1.0;
  borderx=bordery=0.0;
  gxdboutbck(-1);
  gxdbsettransclr(-1);
  if (batch) {
    surface=Bsurface; cr=Bcr; width=Bwidth; height=Bheight;
  } else {
    surface=Xsurface; cr=Xcr; width=Xwidth; height=Xheight;
  }
  surftype=typesave;
  lcolor=lcolorsave;
  lwidth=lwidthsave;
  clx=clxsav; cly=clysav; clw=clwsav; clh=clhsav;
  xscl=(gadouble)width/xsize;
  yscl=(gadouble)height/ysize;
  drawing=maskflag=filflg=0;
  /* The print plug-in may have its own state and no display/batch context
     when GrADS runs interactively. There is then no Cairo state to restore. */
  if (!cr) return;
  force=1;
  if (lcolor>=0) gxCcol(lcolor);
  gxCwid(lwidth);
  gxCaa(aasave);
  force=0;
}

/* Initialize a hardcopy surface and make it the active surface for drawing.
   Save settings for the interactive surface so they can be restored when we are done.  
   fmtflg == 1  for eps
   fmtflg == 2  for ps
   fmtflg == 3  for pdf
   fmtflg == 4  for svg
   fmtflg == 5  for png

   For vector graphic outputs, we need the output file name at initialization.
   For image output, we need the output file name at the end.

   We typically dump a single image to a file, and then close it. 
   Page breaks are not (yet?) in the new metabuffer structure
   Cairo docs: "An Encapsulated PostScript file should never contain more than one page"

*/

gaint gxChinit (gadouble xsz, gadouble ysz, gaint xin, gaint yin, gaint bwin, 
		char *fname, gaint fmtflg, char *bgImage, gadouble border) {
gaint status;
gaint dh=0,dw=0;          /* image size in pixels */
gadouble psx=0,psy=0;     /* page size in points */
gaint bcol,i;
gaint bgwidth,bgheight,len;

  if (!faceinit) {
    for (i=0; i<100; i++) face[i] = NULL;  
    faceinit=1;
    gxCftinit();         /* make sure FreeType library has been initialized */
  }

  gxCflush (1);
  drawing = 0;
  maskflag = 0;

  /* save these settings to restore when hardcopy drawing is finished */
  lcolorsave = lcolor; 
  lwidthsave = lwidth;
  aasave = aaflg;
  typesave = surftype;
  clxsav = clx; 
  clysav = cly;
  clwsav = clw;
  clhsav = clh;

  /* set page sizes in inches */
  xsize = xsz;
  ysize = ysz;
  rotate=0;
  brdrflg=0;
  borderscale=1.0;
  borderx=bordery=0.0;

  if (fmtflg==5) {  /* Image output */
    /*  Set the image width and height */
    if (xin<0 || yin<0) {     /* user has not specified image size */
      if (xsize > ysize) {    /* landscape */
	dw = SX; 
	dh = (gaint)((ysize/xsize)*SX);
      }
      else {
	dw = (gaint)((xsize/ysize)*SX); 
	dh = SX;
      }
    } 
    else {
      dw = xin; 
      dh = yin;
    }
    /* image dimensions */
    width = dw; 
    height = dh;
    /* scale factors (pixels per inch) */
    xscl = (gadouble)(dw)/xsize;
    yscl = (gadouble)(dh)/ysize;
  }

  if (fmtflg==1 || fmtflg==2 || fmtflg==3 || fmtflg==4) {  /* vector graphic output */
    /* Every vector format follows the real canvas, without rotating it. */
    psx = xsize * 72.0;
    psy = ysize * 72.0;
    /* page dimensions */
    width = (gaint)psx;
    height = (gaint)psy;
    /* scale factors (points per inch) */
    xscl = 72.0;
    yscl = 72.0;
    /* set a border for ps, eps, and pdf formats */
    if (fmtflg<=3) {
      brdrwid = border * xscl;
      brdrflg = 1; 
      borderscale=fmin((psx-2.0*brdrwid)/psx,(psy-2.0*brdrwid)/psy);
      borderx=0.5*psx*(1.0-borderscale);
      bordery=0.5*psy*(1.0-borderscale);
    }
  }

  /* initialize clipping parameters */
  clx = 0;     
  cly = 0;
  clw = width;
  clh = height;

  /* From here surface only refers to the new output, so failures cannot
     destroy the saved display surface. */
  surface=NULL;
  Hsurface=NULL;
  Hcr=NULL;
  if (!(borderscale>0.0)) {
    printf("Error in gxChinit: edge width leaves no drawable page\n");
    status=1;
    goto failure;
  }

  /* Create the hardcopy surface */
  if (fmtflg==1 || fmtflg==2) {            /* PS or EPS */
    surftype = 3;
    psoutput.file=fopen(fname,"wb");
    if (!psoutput.file) { status=1; goto failure; }
    psoutput.length=0;
    psoutput.header=1;
    psoutput.width=psx;
    psoutput.height=psy;
    surface = cairo_ps_surface_create_for_stream(gxCpswrite,NULL,psx,psy);
    if (fmtflg==1) cairo_ps_surface_set_eps(surface,1);
  }
  else if (fmtflg==3) {                    /* PDF */
    surftype = 4;
    surface = cairo_pdf_surface_create(fname,psx,psy);
  }
  else if (fmtflg==4) {                    /* SVG */
    surftype = 5;
    surface = cairo_svg_surface_create(fname,psx,psy);
  }
  else if (fmtflg==5) {                    /* PNG */
    surftype = 2;

    if (*bgImage) {
      /* Make sure bgImage is a .png  */
      len = 0;
      while (*(bgImage+len)) len++;
      len = len-4;
      if (len>0) {
	if (*(bgImage+len+1)!='p' || *(bgImage+len+2)!='n' || *(bgImage+len+3)!='g' ) {
	  if (*(bgImage+len+1)!='P' || *(bgImage+len+2)!='N' || *(bgImage+len+3)!='G' ) {
          status=5;
          goto failure;
	  }
	}
      }
      /* create a new surface from the background image */
      surface = cairo_image_surface_create_from_png (bgImage);
      status = cairo_surface_status (surface);
      if (status) {
	printf("Error in gxChinit: unable to import background image %s\n",bgImage); 
        printf("Cairo status: %s\n",cairo_status_to_string(status));
	status=7;
	goto failure;
      }
      /* make sure background image size matches output image size */
      bgwidth = cairo_image_surface_get_width (surface);
      bgheight = cairo_image_surface_get_height (surface);
      if (bgwidth != width || bgheight != height) {
	printf(" background image size is %d x %d \n",bgwidth,bgheight);
	printf("     output image size is %d x %d \n",width,height);
	status=10;
	goto failure;
      }
    }
    else {
      /* no background image specified by user */
      surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, dw, dh);
    }
  }

  /* Make sure surface set up without any problems */
  status = cairo_surface_status (surface);
  if (status) {
    printf("Error in gxChinit: failed to initialize hardcopy surface \n");
    printf("Cairo status: %s\n",cairo_status_to_string(status));
    status=1;
    goto failure;
  }
 
  /* Set the Cairo context */
  cr = cairo_create (surface);
  Hsurface = surface; 
  Hcr = cr;
  if (cairo_status(cr)!=CAIRO_STATUS_SUCCESS) {
    status=1;
    goto failure;
  }
  {
    const char *setting=getenv("OPENGRADS_VECTOR_COMPACT");
    vectorcompact=fmtflg<=4 && !(setting && (!strcmp(setting,"0") || !strcmp(setting,"off")));
  }

  /* default drawing settings */
  cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
  gxCaa(1);                   /* initialize anti-aliasing to be ON */ 

  force = 1;             /* force a color change */
  if (bwin==0 || bwin==1) gxdboutbck(bwin);
  bcol=gxdbkq();
  if (bwin!=GX_BG_TRANSPARENT) {
    /* The page background is independent of the inset plot and of all
       data polygons, even when they use the same color. */
    gxCcol(bcol>1 ? bcol : 0);
    cairo_paint(cr);
  }

  gxCcol(1);             /* set the initial color to 'foreground' */
  force = 0;             /* back to unforced color changes */
  return (0);

failure:
  if (Hcr) cairo_destroy(Hcr);
  if (surface) {
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
  }
  if (psoutput.file) { fclose(psoutput.file); psoutput.file=NULL; }
  gxChrestore();
  return status;
}


/* End the hardcopy drawing and reset drawing to the interactive/batch surface */

gaint gxChend (char *fname, gaint fmtflg, char *fgImage) {
gaint fgwidth,fgheight,len,status,result=0;
cairo_surface_t *sfc2=NULL;     /* Surface for fgImage */   

  gxCflush(1);         /* finish drawing */
  
  /* bgImage and fgImage only work when output format is PNG */
  if (fmtflg==5) {              
    if (*fgImage) {
      /* Make sure fgImage is a .png  */
      len = 0;
      while (*(fgImage+len)) len++;
      len = len-4;
      if (len>0) {
	if (*(fgImage+len+1)!='p' || *(fgImage+len+2)!='n' || *(fgImage+len+3)!='g' ) {
	  if (*(fgImage+len+1)!='P' || *(fgImage+len+2)!='N' || *(fgImage+len+3)!='G' ) {
	    result=6;
	    goto cleanup;
	  }
	}
      }
      /* create a new surface from the foreground image */
      sfc2 = cairo_image_surface_create_from_png (fgImage);
      status = cairo_surface_status (sfc2);
      if (status) {
	printf("Error in gxChend: unable to import foreground image %s\n",fgImage);
	printf("Cairo status: %s\n",cairo_status_to_string(status));
	result=8;
	goto cleanup;
      }
      /* check to make sure foreground and output images are the same size */
      fgwidth = cairo_image_surface_get_width (sfc2);
      fgheight = cairo_image_surface_get_height (sfc2);
      if (fgwidth != width || fgheight != height) {
	printf(" foreground image size is %d x %d \n",fgwidth,fgheight);
	printf("     output image size is %d x %d \n",width,height);
	result=11;
	goto cleanup;
      }
      /* draw the foreground image OVER the hardcopy surface */
      cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
      cairo_set_source_surface(cr, sfc2, 0, 0);
      cairo_paint(cr);
      /* clean up */
      cairo_surface_finish(sfc2);
      cairo_surface_destroy(sfc2);
      sfc2=NULL;
    }
  }

  /* Make sure surface was rendered without any problems */
  status = cairo_surface_status (surface);
  if (status) {
    printf("Error in gxChend: an error occured during rendering of hardcopy surface \n");
    printf("Cairo status: %s\n",cairo_status_to_string(status));
    result=1;
    goto cleanup;
  }

  /* dump everything to hardcopy output, finish and destroy the surface */
  cairo_surface_show_page (surface);
  if (fmtflg==5) {
    if (cairo_surface_write_to_png(surface,fname)!=CAIRO_STATUS_SUCCESS) result=1;
  }
cleanup:
  if (sfc2) {
    cairo_surface_finish(sfc2);
    cairo_surface_destroy(sfc2);
  }
  cairo_destroy(Hcr);
  cairo_surface_finish(surface);
  status=cairo_surface_status(surface);
  if (result==0 && status!=CAIRO_STATUS_SUCCESS) result=1;
  cairo_surface_destroy(surface);
  if (psoutput.file) {
    if (fclose(psoutput.file) && !result) result=1;
    psoutput.file=NULL;
  }
  gxChrestore();
  return result;
}


/* Make this X surface active for drawing */

void gxCsfc (cairo_surface_t *Xsfc) {
  surface = Xsfc;
  surftype = 1;
  Xsurface = surface;
  cr = cairo_create(surface);
  Xcr = cr;
}

/* Swap action for double buffering */

void gxCswp (cairo_surface_t *Xsfc, cairo_surface_t *Xsfc2) {
  /* draw the background (sfc2) onto the foreground (sfc) */
  cr = cairo_create(Xsfc);
  cairo_set_source_surface(cr, Xsfc2, 0, 0);
  cairo_paint(cr);
  /* reset the cairo context to the background surface,  */
  cr = cairo_create(Xsfc2);
}

/* Frame action.  Clear the frame. */ 

void gxCfrm (void) {
gaint bcolr,savcol;

  gxCflush(0);
  savcol = lcolor;    /* Ccol will change lcolor value */
  bcolr = gxdbkq();   /* get background color from data base */
  force = 1;          /* force the color change */
  if (bcolr>1) 
    gxCcol(bcolr);    /* the background color is neither black nor white */
  else {
    gxCcol(0);        /* 0 here means 'background' */
  }
  cairo_paint(cr);    /* paint the source */
  gxCcol(savcol);     /* restore the current color */
  force = 0;          /* back to unforced color changes */
}

/* Our interactive surface got resized. */

void gxCrsiz (gaint dw, gaint dh) {
  width = dw;  
  height = dh; 
  Xwidth = width; 
  Xheight = height;
  xscl = (gadouble)(dw)/xsize;
  yscl = (gadouble)(dh)/ysize;
  gxCfrm();
}

/*  Finish up any pending actions, like drawing, or masking */
/*  opt=0 discard everything. opt=1 finish rendering. */

void gxCflush (gaint opt) {
  gxCgridlineflush();
  gxCcharacterflush();
  gxCgridabort();
  gxCshadeflush();
  if (drawing) cairo_stroke(cr);
  drawing = 0;
  if (maskflag) {
    cr = crsave;  
    surface = surfsave;
    if (opt) {
      cairo_surface_show_page(surfmask);
      patternmask = cairo_pattern_create_for_surface(surfmask);
      cairo_set_source_rgba(cr, (double)(rsav)/255.0, (double)(gsav)/255.0, 
			    (double)(bsav)/255.0, (double)(-1*psav)/255.0);
      cairo_mask(cr, patternmask);
    }
    cairo_destroy(crmask);
    cairo_surface_finish (surfmask);
    cairo_surface_destroy (surfmask);
    maskflag = 0;
  }
  if (surftype==1 && opt) gxdXflush();  /* force completed graphics to display */
}

/* New color value */

void gxCcol (gaint clr) {
struct gxdbquery dbq;
gaint bcol;

  gxCgridlineflush();
  gxCcharacterflush();
  if (drawing) cairo_stroke(cr);
  drawing = 0;
  lcolor = clr;                  /* outside this routine lcolor 0/1 still means background/foreground */
  bcol = gxdbkq();               /* get background color */
  if (bcol==1) {                 /* if background is white ...  */
    if (clr==0)      clr = 1;    /*  ...change color 0 to white (1) */
    else if (clr==1) clr = 0;    /*  ...change color 1 to black (0) */
  }
  gxdbqcol(clr, &dbq);           /* query the data base for color values */
  if (clr == gxdbqtransclr()) {  /* If this is the transparent color, override values  */
    dbq.red   = 0;
    dbq.green = 0;
    dbq.blue  = 0;
    dbq.alpha = 0;
    dbq.tile  = -999;
  }
  if (dbq.tile>-900 || !gxCcompactalpha(dbq.alpha)) gxCgridabort();
  if (dbq.tile>-900 || dbq.red!=rsav || dbq.green!=gsav || dbq.blue!=bsav || dbq.alpha!=psav)
    gxCshadeflush();
  if (force || clr!=actualcolor || dbq.red!=rsav || dbq.green!=gsav || dbq.blue!=bsav || dbq.alpha!=psav) {
    /* change to new color */
    if (maskflag) gxCflush(1);
    actualcolor = clr;           /* inside this routine actualcolor 0/1 means black or white */
    if (dbq.tile > -900) {
      /* new color is a pattern */
      if (pattern[dbq.tile]==NULL)  gxCpattc (dbq.tile);
      cairo_set_source(cr, pattern[dbq.tile]);
      cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    } 
    else {
      if (dbq.alpha==255) {
        cairo_set_source_rgb(cr, (double)(dbq.red)/255.0, (double)(dbq.green)/255.0, (double)(dbq.blue)/255.0);
      } 
      else {
	/* set up color masking if alpha value is negative */
        if (dbq.alpha < 0) {
          maskflag = 1;
          surfmask = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width , height);
          crmask = cairo_create(surfmask);                    /* create a new graphics context */
          cairo_set_source_rgba(crmask, 1.0, 1.0, 1.0, 1.0);  /* set mask color */  
	  gxdbqwid(lwidth-1,&dbq);                            /* set current line width */
	  cairo_set_line_width(crmask,dbq.wid);  
	  cairo_set_line_cap(crmask,CAIRO_LINE_CAP_ROUND);    /* set line drawing specs */
	  cairo_set_line_join(crmask,CAIRO_LINE_JOIN_ROUND);
	  /* save the current surface/cr, set the new surfmask/crmask to be the current one */
          crsave = cr;
          surfsave = surface;
          cr = crmask;
          surface = surfmask;
        }
	else {
          cairo_set_source_rgba(cr, (double)(dbq.red)/255.0, (double)(dbq.green)/255.0,
			 	    (double)(dbq.blue)/255.0, (double)(dbq.alpha)/255.0);
        }
      }
    }
    /* save these color values */
    rsav = dbq.red; 
    gsav = dbq.green; 
    bsav = dbq.blue; 
    psav = dbq.alpha; 
  }
}

/* Set the line width */

void gxCwid (gaint wid) {                 
struct gxdbquery dbq;
  gxCgridlineflush();
  gxCcharacterflush();
  gxCgridabort();
  gxCshadeflush();
  if (drawing) cairo_stroke(cr);
  drawing = 0;
  lwidth = wid;
  gxdbqwid(wid-1,&dbq);  /* at this point wid still starts at 1, so subtract to get index right */
  cairo_set_line_width(cr,dbq.wid);  
}


/* Convert x,y coordinates from inches on the page into coordinates for current drawing surface */

void gxCxycnv (gadouble x, gadouble y, gadouble *nx, gadouble *ny) {
  if (surftype>2 && rotate) {
    *ny = (gadouble)height - x*xscl;
    *nx = (gadouble)width - (y*yscl);
  }
  else {
    *nx = x*xscl;
    *ny = (gadouble)height - (y*yscl);
  }
  if (brdrflg) {
    *nx = borderx + *nx*borderscale;
    *ny = bordery + *ny*borderscale;
  }
}


/* Move to x,y */

void gxCmov (gadouble x, gadouble y) {
  if (!filflg) gxCgridabort();
  if (!filflg) gxCshadeflush();
  if (drawing && !character.active && !gridline.active) cairo_stroke(cr);
  drawing = 0;
  gxCxycnv (x,y,&xxx,&yyy);
}


/* Draw to x,y */

void gxCdrw (gadouble x, gadouble y) {
gadouble di, dj;
  gxCxycnv (x,y,&di,&dj);
  if (!filflg) {           /* we're not filling a polygon */
    if (character.active) character.pending=1;
    if (gridline.active) gridline.pending=1;
    if (!drawing) {        /* this is the start of a line */
      drawing = 1;
      cairo_move_to(cr,xxx,yyy);
      cairo_line_to(cr,di,dj);
    }
    else {
      cairo_line_to(cr,di,dj);
    }
  }
  xxx = di;
  yyy = dj;
}


/* Set the clipping coordinates */

void gxCclip (gadouble x1, gadouble x2, gadouble y1, gadouble y2) {
gadouble di1,di2,dj1,dj2;
  gxCgridlineflush();
  gxCcharacterflush();
  gxCgridabort();
  gxCshadeflush();
  gxCxycnv (x1,y1,&di1,&dj1);
  gxCxycnv (x2,y2,&di2,&dj2);
  /* set the clipping variables -- these get used when drawing characters */
  clx = di1;
  cly = dj2;
  clw = di2-di1;
  clh = dj1-dj2;
}


/* Draw a filled rectangle */

void gxCrec (gadouble x1, gadouble x2, gadouble y1, gadouble y2) {
gadouble di1,di2,dj1,dj2;
  gxCgridlineflush();
  gxCcharacterflush();
  gxCgridabort();
  gxCshadeflush();
  if (drawing) cairo_stroke(cr);
  drawing = 0;
  gxCxycnv (x1,y1,&di1,&dj1);
  gxCxycnv (x2,y2,&di2,&dj2);
  if (di1!=di2 && dj1!=dj2) { 
    cairo_rectangle(cr,di1,dj2,di2-di1,dj1-dj2);  
    /* disable antialiasing, otherwise faint lines appear around the edges */
    if (aaflg) {
      cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE); 
      cairo_fill(cr);
      cairo_set_antialias(cr,CAIRO_ANTIALIAS_DEFAULT); 
    }
    else {
      cairo_fill(cr);
    }
  } else {
    cairo_move_to(cr,di1,dj1);
    cairo_line_to(cr,di2,dj2);
    cairo_stroke(cr);
  }
}


/* initialize a polygon fill by turning on a flag */

void gxCbfil (void) {
  filflg = 1;
}

/* Draw a filled polygon */

void gxCfil (gadouble *xy, gaint n) {
gadouble *pt,x,y;
gaint i,combine;

  gxCgridlineflush();
  gxCcharacterflush();
  if (gridfill.active && !gridfill.disabled) {
    if (gxCgridadd(xy,n)) { filflg=0; return; }
    gxCgridabort();
  }

  /* shade2 and shade2b provide non-overlapping contour regions. Preserve
     their vertex order and holes, combining only consecutive compatible fills
     with an unchanged source/state. Bound additional pending path size. */
  combine=shadefill.active && gxCcompactalpha(psav) && rsav>=0 && !maskflag &&
          cairo_get_operator(cr)==CAIRO_OPERATOR_OVER &&
          cairo_pattern_get_type(cairo_get_source(cr))==CAIRO_PATTERN_TYPE_SOLID && n>=3 && n<=262144;
  if (!combine || shadefill.points+(size_t)n>262144) gxCshadeflush();

  if (drawing) cairo_stroke(cr);
  drawing = 0;
  pt = xy;
  for (i=0; i<n; i++) {
    gxCxycnv (*pt,*(pt+1),&x,&y);
    if (i==0) cairo_move_to(cr,x,y);
    else cairo_line_to(cr,x,y);
    pt+=2;
  }
  if (combine) {
    cairo_close_path(cr);
    shadefill.pending=1;
    shadefill.points+=(size_t)n;
    filflg=0;
    return;
  }
  /* disable antialiasing, otherwise faint lines appear around the edges */
  if (aaflg) {
    cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE);
    cairo_fill(cr);                                  
    cairo_set_antialias(cr,CAIRO_ANTIALIAS_DEFAULT);
  }
  else {
    cairo_fill(cr);                                  
  }
  /* turn off polygon-filling flag */
  filflg = 0;
  return;
}


/* Turn anti-aliasing on (flag=1) or off (flag=0) 
   Anti-aliasing is also automatically disabled in gxCrec and gxCfil so that
   faint lines do not appear around the edges of filled rectangles and polygons.
*/

void gxCaa (gaint flag) {
  gxCgridlineflush();
  gxCcharacterflush();
  gxCgridabort();
  gxCshadeflush();
  if (cr!=NULL) {
    if (flag) {
      if (aaflg==0) {
	cairo_set_antialias(cr,CAIRO_ANTIALIAS_DEFAULT);
	aaflg = 1;
      }
    }
    else {
      if (aaflg) {
	cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE);
	aaflg = 0; 
      }
    }
  }
}


/* Get the x bearing (width) used to plot a character in the specific font and size */

gadouble gxCqchl (char ch, gaint fn, gadouble w) {
cairo_text_extents_t te;
gadouble usize,awidth,swidth,fontsize=100.0;
char str[2];
char *astr = "A";
  gxCgridlineflush();
  gxCcharacterflush();

  gxCselfont (fn);

  /* get the scale factor (for width only) based on the size of "A" */
  cairo_set_font_size (cr, fontsize);
  cairo_text_extents (cr, astr, &te);
  awidth = fontsize/te.width; 
  usize = w * awidth * xscl;
  if (brdrflg) usize *= borderscale;

  /* get the text extents of the character */
  str[0] = ch;
  str[1] = '\0';
  cairo_text_extents (cr, str, &te);

  /* return the scaled width of the character */
  swidth = te.x_advance*usize/(fontsize*xscl);
  return(swidth);
}

/* Draw a character. 
   Specify a large font size and then scale it down, to prevent 
   the pixelation from interfereing with the exact positioning.
   Not much code, but it took a lot of work to get this right.  */

gadouble gxCch (char ch, gaint fn, gadouble x, gadouble y, gadouble w, gadouble h, gadouble rot) {
cairo_text_extents_t te;
gadouble xpage,ypage,usize,vsize;
gadouble aheight,awidth,swidth,fontsize=100.0;
char str[2];
char *astr = "A";
  gxCgridabort();
  gxCshadeflush();

  if (drawing) cairo_stroke(cr);
  drawing = 0;
  gxCselfont (fn);

  /* get the scale factor based on size of "A" */
  cairo_set_font_size (cr, fontsize);
  cairo_text_extents (cr, astr, &te);
  awidth  = fontsize/te.width;
  aheight = fontsize/te.height;
  usize = w * xscl * awidth;
  vsize = h * yscl * aheight;
  if (brdrflg) {
    usize *= borderscale;
    vsize *= borderscale;
  }
  /* Convert the position coordinates and adjust the rotation if necessary */
  gxCxycnv (x,y,&xpage,&ypage);
  if (rotate) rot = rot + M_PI/2;

  /* get the text extents of the character we want to draw */
  str[0] = ch;
  str[1] = '\0';
  cairo_text_extents (cr, str, &te);

  /* draw a character */
  cairo_save(cr);                                    /* save the untranslated, unrotated, unclipped context */
  cairo_rectangle(cr,clx,cly,clw,clh);               /* set the clipping area */
  cairo_clip (cr);                                   /* clip it */
  cairo_translate(cr,xpage,ypage);                   /* translate to location of character */
  cairo_rotate(cr,-1.0*rot);                         /* rotate if necessary */
  cairo_move_to (cr, 0.0, 0.0);                      /* move to (translated) origin */
  cairo_scale (cr, usize/fontsize, vsize/fontsize);  /* apply the scale factor right before drawing */
  cairo_show_text (cr, str);                         /* finally, draw the darned thing */
  cairo_restore(cr);                                 /* restore the saved graphics context */

  /* return the scaled width of the character */
  swidth = te.x_advance*usize/(fontsize*xscl);
  return(swidth);
}


/* Set the Cairo font based on the font number and the settings 
   obtained from the backend database settings */

void gxCselfont (gaint fn) {
  struct gxdbquery dbq;
  cairo_font_face_t *cf_face=NULL;           
  FT_Face newface;
  gaint dflt,rc;
  gaint cbold,citalic;
  
  /* get font info from graphics database */
  gxdbqfont (fn, &dbq);

  /* font 0-5 (but not 3) and hershflag=1 in gxmeta.c, so we use cairo to draw something hershey-like */
  if (fn<6) {
    cbold = CAIRO_FONT_WEIGHT_NORMAL;
    if (dbq.fbold == 1) cbold = CAIRO_FONT_WEIGHT_BOLD;
    citalic = CAIRO_FONT_SLANT_NORMAL;
    if (dbq.fitalic == 1) citalic = CAIRO_FONT_SLANT_ITALIC;
    if (dbq.fitalic == 2) citalic = CAIRO_FONT_SLANT_OBLIQUE;
  
    if (dbq.fname == NULL) {
      /* we should never have fn<6 and fname==NULL, but just in case... */
      cairo_select_font_face (cr, "sans-serif", citalic, cbold); 
    } else {
      cairo_select_font_face (cr, dbq.fname, citalic, cbold);
    }
  }
  else {
    /* font>=10 */
    dflt=0;
    if (library == NULL) dflt=1;      /* use default fonts */  
    if (dbq.fname == NULL) dflt=1;    /* make sure we have a font filename */

    if (!dflt) {
      if (face[fn]==NULL) {
        /* try to open user-provided font file */
        rc = FT_New_Face(library, dbq.fname, 0, &newface);
        if (rc) { 
  	  printf("Error: Unable to open font file \"%s\"\n",dbq.fname);
          printf(" Will use a default \"sans-serif\" font instead\n");
          dflt=1;
	  /* update the data base so this error message only appears once */
	  gxdbsetfn(fn, NULL);
        } 
        else {
	  /* we succeeded, so save the face and update the font status */
          face[fn] = newface; 
	}          
      }
      else {
        /* this font has already been opened, so we use the saved face */
        newface = face[fn];
      }
    }

    if (!dflt) {
	/* create a new font face  */
	cf_face = cairo_ft_font_face_create_for_ft_face (newface, 0);
	cairo_set_font_face(cr,cf_face);
    }
    else {
      /* set up a default font with the Cairo "Toy" interface */
      cairo_select_font_face (cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    }
  }
}

/* Create a tile pattern on the fly.  Get all the info on the 
   pattern from the backend db.   */

void gxCpattc (gaint pnum) {
struct gxdbquery dbq;
cairo_surface_t *surfacep1;
cairo_t *crpatt;
gadouble xx,yy,x1,y1;
gaint pt,alph,status;

  if (pnum<0 || pnum>=COLORMAX) return;

  gxdbqpatt (pnum, &dbq);
  gxdbqwid (dbq.pthick, &dbq);

  if (dbq.ptype==0) {
    /* create tile from user-provided filename */
    surfacep1 = cairo_image_surface_create_from_png (dbq.fname);
    status = cairo_surface_status (surfacep1);
    if (status) {
      printf("Error in gxCpattc: unable to import tile image %s \n",dbq.fname);
      printf("Cairo status: %s\n",cairo_status_to_string(status));
      cairo_surface_finish(surfacep1);
      cairo_surface_destroy(surfacep1); 
      return; 
    }
    crpatt = cairo_create(surfacep1);
  }
  else {
    /* create the tile on-the-fly */
    surfacep1 = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, dbq.pxs, dbq.pys);
    crpatt = cairo_create(surfacep1);
    
    if (dbq.ptype==1) {
      /* Type is a solid -- paint entire surface with foreground color */
      if (dbq.pfcol < 0 ) {
	/* use default foreground color, opaque white */
	cairo_set_source_rgba(crpatt, 1.0, 1.0, 1.0, 1.0);
      } else {
	gxdbqcol (dbq.pfcol, &dbq);
	if (dbq.red < 0) {                
	  /* this color cannot be assigned to a tile, use default */
	  cairo_set_source_rgba(crpatt, 1.0, 1.0, 1.0, 1.0);
	} else {
	  alph = dbq.alpha;
	  if (alph<0) alph = -1*alph;
	  cairo_set_source_rgba(crpatt, (double)(dbq.red)/255.0, (double)(dbq.green)/255.0,
				(double)(dbq.blue)/255.0, (double)(alph)/255.0);
	}
      }
    } else {
      /* Type is not a solid -- paint entire surface with background color */
      if (dbq.pbcol < 0 ) {
	/* use default background color, which is completely transparent */
	cairo_set_source_rgba(crpatt, 0.0, 0.0, 0.0, 0.0);
      } else {
	gxdbqcol (dbq.pbcol, &dbq);
	if (dbq.red < 0) {
	  /* this color cannot be assigned to a tile, use default */
	  cairo_set_source_rgba(crpatt, 0.0, 0.0, 0.0, 0.0);
	} else {
	  alph = dbq.alpha;
	  if (alph<0) alph = -1*alph;
	  cairo_set_source_rgba(crpatt, (double)(dbq.red)/255.0, (double)(dbq.green)/255.0,
				(double)(dbq.blue)/255.0, (double)(alph)/255.0);
	}
      }
    }
    cairo_paint(crpatt);

    /* now paint the dots or lines */
    if (dbq.ptype>1) {     /* Not solid */
      if (dbq.pfcol < 0 ) {
	/* use default foreground color, opaque white */
	cairo_set_source_rgba(crpatt, 1.0, 1.0, 1.0, 1.0);
      } else {
	gxdbqcol (dbq.pfcol, &dbq);
	if (dbq.red < 0) {
	  /* this color cannot be assigned to a tile, use default */
	  cairo_set_source_rgba(crpatt, 1.0, 1.0, 1.0, 1.0);
	} else {
	  alph = dbq.alpha;
	  if (alph<0) alph = -1*alph;
	  cairo_set_source_rgba(crpatt, (double)(dbq.red)/255.0, (double)(dbq.green)/255.0,
				(double)(dbq.blue)/255.0, (double)(alph)/255.0);
	}
      }
      cairo_set_line_width(crpatt,dbq.wid);  
      xx = (gadouble)dbq.pxs;
      yy = (gadouble)dbq.pys;
      pt = dbq.ptype;
      if (pt==2) {         /* dot */
	x1 = (xx/2.0)-dbq.wid/2.0; y1 = (yy/2.0);
	cairo_arc (crpatt, x1, y1, dbq.wid, 0.0, 2.0*3.1416);
	cairo_fill (crpatt);
      } else if (pt>=3 && pt<=5) {   /* diagonals */
	if (pt==3 || pt==5) {
	  cairo_move_to (crpatt, 0.0, 0.0);
	  cairo_line_to (crpatt, xx, yy);
	  cairo_stroke (crpatt);
	  cairo_move_to (crpatt, 0.0, -1.0*yy);
	  cairo_line_to (crpatt, 2.0*xx, yy);
	  cairo_stroke (crpatt);
	  cairo_move_to (crpatt, -1.0*xx, 0.0);
	  cairo_line_to (crpatt, xx, 2.0*yy);
	  cairo_stroke (crpatt);
	}
	if (pt==4 || pt==5) {
	  cairo_move_to (crpatt, 0.0, yy);
	  cairo_line_to (crpatt, xx, 0.0);
	  cairo_stroke (crpatt);
	  cairo_move_to (crpatt, 0.0, 2.0*yy);
	  cairo_line_to (crpatt, 2.0*xx, 0.0);
	  cairo_stroke (crpatt);
	  cairo_move_to (crpatt, -1.0*xx, yy);
	  cairo_line_to (crpatt, xx, -1.0*yy);
	  cairo_stroke (crpatt);
	}
      } else if (pt>=6 && pt<=8) {   /* up/down accross */
	x1 = xx/2.0; y1 = yy/2.0;
	if (pt==6 || pt==8) {
	  cairo_move_to (crpatt, x1, 0.0);
	  cairo_line_to (crpatt, x1, yy);
	  cairo_stroke (crpatt);
	}
	if (pt==7 || pt==8) {
	  cairo_move_to (crpatt, 0.0, y1);
	  cairo_line_to (crpatt, xx, y1);
	  cairo_stroke (crpatt);
	}
      } 
    }
  }

  cairo_surface_show_page (surfacep1);
  pattern[pnum] = cairo_pattern_create_for_surface(surfacep1);
  pattsurf[pnum] = surfacep1; 
  cairo_destroy(crpatt);
}

/* Here we get notified that a pattern has been reset/changed.  
   This will trigger re-creating the pattern if it is used. */

void gxCpattrset (gaint pnum) {
  if (pnum<0 || pnum>=2048) return;
  cairo_pattern_destroy (pattern[pnum]);
  cairo_surface_finish (pattsurf[pnum]);
  cairo_surface_destroy (pattsurf[pnum]);
  pattern[pnum] = NULL;
  pattsurf[pnum] = NULL;
}

/* initialize the FreeType library */
void gxCftinit (void) {
  gaint rc=1;
  if (library == NULL) {
    rc = FT_Init_FreeType(&library);
    if (rc) library=NULL;
  }
}

/* close the FreeType library */
void gxCftend (void) {
  FT_Done_FreeType(library);
}

void gxCpush (void) {
  gxCgridlineflush();
  gxCcharacterflush();
  gxCgridabort();
  gxCshadeflush();
  cairo_push_group(cr);
}

void gxCpop (void) {
  gxCgridlineflush();
  gxCcharacterflush();
  gxCgridabort();
  gxCshadeflush();
  cairo_pop_group_to_source(cr);
  cairo_paint(cr);
}

/* report on configuration */
void gxCcfg (void) {
  printf("cairo-%d.%d.%d ",CAIRO_VERSION_MAJOR,CAIRO_VERSION_MINOR,CAIRO_VERSION_MICRO);
}

