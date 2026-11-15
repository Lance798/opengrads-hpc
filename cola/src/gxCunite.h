/* Exact boundary union for Cairo's non-overlapping grfill/shade2 partitions.
   Copyright (C) 2026. GPLv2; see COPYING.

   Work on cairo_copy_path's actual fixed-point vertices, never on rounded
   guesses at input coordinates. Split collinear edges at every endpoint,
   cancel opposite edges, and trace the remaining directed boundary rings.
   This preserves winding, holes, point-touching islands and clipped vertices.
   Unsupported paths or allocation/complexity limits leave the source intact.
   Internal linkage keeps this private to each Cairo rendering plug-in. */
#include <stdint.h>

struct gxCuPoint { int64_t x,y; };
struct gxCuEvent { int64_t dx,dy,c,t,x,y; int delta; };
struct gxCuEdge { struct gxCuPoint a,b; int used; };

static int gxCuPointEq(struct gxCuPoint a,struct gxCuPoint b) {
  return a.x==b.x && a.y==b.y;
}

static int64_t gxCuGcd(int64_t a,int64_t b) {
  int64_t t;
  if (a<0) a=-a;
  if (b<0) b=-b;
  while (b) { t=a%b; a=b; b=t; }
  return a;
}

static int gxCuCoordinate(cairo_path_data_t *p,struct gxCuPoint *v) {
  double x=p->point.x*256.0,y=p->point.y*256.0;
  /* Bound all integer cross/dot products well inside signed 64 bits. */
  if (!isfinite(x) || !isfinite(y) || fabs(x)>67108864.0 || fabs(y)>67108864.0) return 0;
  v->x=llround(x); v->y=llround(y);
  return x==(double)v->x && y==(double)v->y;
}

static void gxCuAdd(struct gxCuEvent *events,size_t *count,
                    struct gxCuPoint a,struct gxCuPoint b) {
  int64_t dx=b.x-a.x,dy=b.y-a.y,g= gxCuGcd(dx,dy);
  struct gxCuEvent e;
  if (!g) return;
  dx/=g; dy/=g;
  if (dx<0 || (!dx && dy<0)) { dx=-dx; dy=-dy; }
  e.dx=dx; e.dy=dy; e.c=dx*a.y-dy*a.x;
  e.x=a.x; e.y=a.y; e.t=dx ? a.x:a.y;
  /* Endpoint order carries the direction: a reverse edge starts at hi. */
  e.delta=1;
  events[(*count)++]=e;
  e.x=b.x; e.y=b.y; e.t=dx ? b.x:b.y; e.delta=-e.delta;
  events[(*count)++]=e;
}

static int gxCuEventCmp(const void *aa,const void *bb) {
  const struct gxCuEvent *a=aa,*b=bb;
#define GXCU_CMP(field) if (a->field!=b->field) return a->field<b->field ? -1:1
  GXCU_CMP(dx); GXCU_CMP(dy); GXCU_CMP(c); GXCU_CMP(t);
#undef GXCU_CMP
  return 0;
}

static int gxCuSameLine(const struct gxCuEvent *a,const struct gxCuEvent *b) {
  return a->dx==b->dx && a->dy==b->dy && a->c==b->c;
}

static int gxCuEdgeCmp(const void *aa,const void *bb) {
  const struct gxCuEdge *a=aa,*b=bb;
#define GXCU_CMP(field) if (a->field!=b->field) return a->field<b->field ? -1:1
  GXCU_CMP(a.x); GXCU_CMP(a.y); GXCU_CMP(b.x); GXCU_CMP(b.y);
#undef GXCU_CMP
  return 0;
}

static size_t gxCuFirst(struct gxCuEdge *edges,size_t count,struct gxCuPoint p) {
  size_t lo=0,hi=count,mid;
  while (lo<hi) {
    mid=lo+(hi-lo)/2;
    if (edges[mid].a.x<p.x || (edges[mid].a.x==p.x && edges[mid].a.y<p.y)) lo=mid+1;
    else hi=mid;
  }
  return lo;
}

static int gxCuStraight(struct gxCuPoint a,struct gxCuPoint b,struct gxCuPoint c) {
  int64_t x=b.x-a.x,y=b.y-a.y,u=c.x-b.x,v=c.y-b.y;
  return x*v-y*u==0 && x*u+y*v>0;
}

static int gxCunite(cairo_t *target) {
  cairo_path_t *original=NULL,*united=NULL;
  cairo_surface_t *scratch_surface=NULL;
  cairo_t *scratch=NULL;
  struct gxCuEvent *events=NULL;
  struct gxCuEdge *edges=NULL;
  struct gxCuPoint *ring=NULL,a={0,0},b,start={0,0},previous={0,0};
  size_t capacity=0,ne=0,nb=0,i,j,k,next,n,first,steps,selected;
  int opened=0,level,delta,ok=0;
  int64_t x,y,u,v;
  double angle,best;
  long double area=0.0;
  const char *setting=getenv("OPENGRADS_VECTOR_UNITE");
  const size_t limit=(96u*1024u*1024u)/
    (2u*(sizeof(struct gxCuEvent)+sizeof(struct gxCuEdge)+sizeof(struct gxCuPoint)));
  if (setting && (!strcmp(setting,"0") || !strcmp(setting,"off"))) return 0;
  if (cairo_get_fill_rule(target)!=CAIRO_FILL_RULE_WINDING) return 0;
  original=cairo_copy_path(target);
  if (!original || original->status!=CAIRO_STATUS_SUCCESS) goto cleanup;
  for (i=0;i<(size_t)original->num_data;i+=original->data[i].header.length) {
    switch (original->data[i].header.type) {
      case CAIRO_PATH_MOVE_TO: case CAIRO_PATH_LINE_TO: case CAIRO_PATH_CLOSE_PATH:
        capacity++; break;
      default: goto cleanup;
    }
  }
  if (!capacity || capacity>limit) goto cleanup;
  events=malloc(2*capacity*sizeof(*events));
  edges=malloc(2*capacity*sizeof(*edges));
  ring=malloc(2*capacity*sizeof(*ring));
  if (!events || !edges || !ring) goto cleanup;
  for (i=0;i<(size_t)original->num_data;i+=original->data[i].header.length) {
    switch (original->data[i].header.type) {
      case CAIRO_PATH_MOVE_TO:
        if (opened) gxCuAdd(events,&ne,a,start);
        if (!gxCuCoordinate(original->data+i+1,&a)) goto cleanup;
        start=a; opened=1; break;
      case CAIRO_PATH_LINE_TO:
        if (!opened || !gxCuCoordinate(original->data+i+1,&b)) goto cleanup;
        gxCuAdd(events,&ne,a,b); a=b; break;
      case CAIRO_PATH_CLOSE_PATH:
        if (opened) gxCuAdd(events,&ne,a,start);
        opened=0; break;
      default: goto cleanup;
    }
  }
  if (opened) gxCuAdd(events,&ne,a,start);
  if (!ne) goto cleanup;
  qsort(events,ne,sizeof(*events),gxCuEventCmp);
  for (i=0;i<ne;i=j) {
    for (j=i+1;j<ne && gxCuSameLine(events+i,events+j);j++);
    level=0; previous.x=events[i].x; previous.y=events[i].y;
    for (k=i;k<j;k=next) {
      b.x=events[k].x; b.y=events[k].y;
      if (level && !gxCuPointEq(previous,b)) {
        edges[nb].a=level>0 ? previous:b;
        edges[nb].b=level>0 ? b:previous;
        edges[nb].used=0;
        nb++;
      }
      delta=0;
      for (next=k;next<j && events[next].t==events[k].t;next++) delta+=events[next].delta;
      level+=delta;
      /* Input partitions must not have coincident duplicate boundaries. */
      if (level>1 || level< -1) goto cleanup;
      previous=b;
    }
    if (level) goto cleanup;
  }
  free(events); events=NULL;
  qsort(edges,nb,sizeof(*edges),gxCuEdgeCmp);
  for (i=0;i<nb;i++) area+=(long double)edges[i].a.x*edges[i].b.y-(long double)edges[i].b.x*edges[i].a.y;
  scratch_surface=cairo_image_surface_create(CAIRO_FORMAT_A8,1,1);
  scratch=cairo_create(scratch_surface);
  if (cairo_status(scratch)!=CAIRO_STATUS_SUCCESS) goto cleanup;
  for (i=0;i<nb;i++) {
    if (edges[i].used) continue;
    start=edges[i].a; ring[0]=start; n=1; steps=0; selected=i;
    for (;;) {
      a=edges[selected].a; b=edges[selected].b;
      edges[selected].used=1;
      if (++steps>nb) goto cleanup;
      if (gxCuPointEq(b,start)) break;
      while (n>1 && gxCuStraight(ring[n-2],ring[n-1],b)) n--;
      if (n>=2*capacity) goto cleanup;
      ring[n++]=b;
      first=gxCuFirst(edges,nb,b);
      if (first==nb || !gxCuPointEq(edges[first].a,b)) goto cleanup;
      best=10.0; selected=nb;
      for (j=first;j<nb && gxCuPointEq(edges[j].a,b);j++) {
        if (j-first>=64) goto cleanup;
        if (edges[j].used) continue;
        x=a.x-b.x; y=a.y-b.y;
        u=edges[j].b.x-b.x; v=edges[j].b.y-b.y;
        angle=atan2((double)(x*v-y*u),(double)(x*u+y*v));
        if (area>=0.0) angle=-angle;
        if (angle<0.0) angle+=2.0*M_PI;
        if (angle<best) { best=angle; selected=j; }
      }
      if (selected==nb) goto cleanup;
    }
    while (n>2 && gxCuStraight(ring[n-2],ring[n-1],ring[0])) n--;
    first=0;
    while (n-first>2 && gxCuStraight(ring[n-1],ring[first],ring[first+1])) first++;
    if (n-first<3) continue;
    cairo_move_to(scratch,ring[first].x/256.0,ring[first].y/256.0);
    for (j=first+1;j<n;j++) cairo_line_to(scratch,ring[j].x/256.0,ring[j].y/256.0);
    cairo_close_path(scratch);
  }
  united=cairo_copy_path(scratch);
  if (!united || united->status!=CAIRO_STATUS_SUCCESS || united->num_data>=original->num_data) goto cleanup;
  cairo_new_path(target);
  cairo_append_path(target,united);
  ok=1;
cleanup:
  if (united) cairo_path_destroy(united);
  if (original) cairo_path_destroy(original);
  if (scratch) cairo_destroy(scratch);
  if (scratch_surface) cairo_surface_destroy(scratch_surface);
  free(events); free(edges); free(ring);
  return ok;
}
