/* Independent Cairo fill oracle for exact vector boundary union. GPLv2. */
#include <assert.h>
#include <cairo.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fail_allocation,allocations;
static void *checked_malloc(size_t n) {
  if (++allocations==fail_allocation) return NULL;
  return malloc(n);
}
#define malloc checked_malloc
#include "gxCunite.h"
#undef malloc

static uint32_t seed=1729;
static int cases,compacted;
static uint32_t random_word(void) {
  seed=seed*1664525u+1013904223u;
  return seed;
}

static cairo_surface_t *render(cairo_path_t *path,int clip,int alpha) {
  cairo_surface_t *s=cairo_image_surface_create(CAIRO_FORMAT_A8,96,96);
  cairo_t *c=cairo_create(s);
  cairo_set_antialias(c,CAIRO_ANTIALIAS_NONE);
  if (clip) { cairo_rectangle(c,11.125,9.75,67.5,70.375); cairo_clip(c); }
  cairo_append_path(c,path);
  cairo_set_source_rgba(c,1,1,1,alpha/255.0);
  cairo_fill(c);
  assert(cairo_status(c)==CAIRO_STATUS_SUCCESS);
  cairo_destroy(c);
  cairo_surface_flush(s);
  return s;
}

static void same_path(cairo_path_t *a,cairo_path_t *b) {
  int i,j;
  assert(a->num_data==b->num_data);
  for (i=0;i<a->num_data;i+=a->data[i].header.length) {
    assert(a->data[i].header.type==b->data[i].header.type);
    assert(a->data[i].header.length==b->data[i].header.length);
    for (j=1;j<a->data[i].header.length;j++) {
      assert(a->data[i+j].point.x==b->data[i+j].point.x);
      assert(a->data[i+j].point.y==b->data[i+j].point.y);
    }
  }
}

static void check(cairo_t *c,int clip,int alpha) {
  cairo_path_t *a=cairo_copy_path(c),*b;
  cairo_surface_t *s,*t;
  int changed=gxCunite(c);
  b=cairo_copy_path(c);
  assert(cairo_status(c)==CAIRO_STATUS_SUCCESS);
  if (changed) { assert(b->num_data<a->num_data); compacted++; }
  else same_path(a,b);
  s=render(a,clip,alpha); t=render(b,clip,alpha);
  assert(cairo_image_surface_get_stride(s)==cairo_image_surface_get_stride(t));
  if (memcmp(cairo_image_surface_get_data(s),cairo_image_surface_get_data(t),
             96*(size_t)cairo_image_surface_get_stride(s))) {
    fprintf(stderr,"Unite coverage differs in case %d seed %u\n",cases,seed);
    cairo_surface_write_to_png(s,"unite-reference.png");
    cairo_surface_write_to_png(t,"unite-failed.png");
    abort();
  }
  cases++;
  cairo_surface_destroy(s); cairo_surface_destroy(t);
  cairo_path_destroy(a); cairo_path_destroy(b);
}

static void point(cairo_t *c,double x,double y,int skew,int move) {
  double xx=skew ? x+y*.375:x,yy=skew ? y+x*.125:y;
  if (move) cairo_move_to(c,xx,yy); else cairo_line_to(c,xx,yy);
}

static void polygon(cairo_t *c,double x,double y,int triangle,int reverse,int skew) {
  double xx[4]={x,x+7,x+7,x},yy[4]={y,y,y+7,y+7};
  int j,index,n=triangle ? 3:4;
  if (triangle==2) { xx[1]=x+7; yy[1]=y+7; xx[2]=x; yy[2]=y+7; }
  for (j=0;j<n;j++) {
    index=reverse ? n-1-j:j;
    point(c,xx[index],yy[index],skew,j==0);
  }
  cairo_close_path(c);
}

int main(void) {
  cairo_surface_t *s=cairo_image_surface_create(CAIRO_FORMAT_A8,1,1);
  cairo_t *c=cairo_create(s);
  int i,x,y,color,cell[64],reverse,triangles,skew,j;
  const char *version=cairo_version_string();
  for (i=0;i<800;i++) {
    for (j=0;j<64;j++) cell[j]=(int)(random_word()%5);
    reverse=i%2; triangles=(i/2)%2; skew=(i/4)%2;
    for (color=0;color<3;color++) {
      cairo_new_path(c);
      for (y=0;y<8;y++) for (x=0;x<8;x++) if (cell[y*8+x]==color) {
        polygon(c,3+7*x,3+7*y,triangles ? 1:0,reverse,skew);
        if (triangles && (random_word()%3)) polygon(c,3+7*x,3+7*y,2,reverse,skew);
      }
      check(c,i%3==0,i%2 ? 125:255);
    }
  }
  /* T-junctions: one long edge meets several shorter opposite edges. */
  cairo_new_path(c);
  cairo_rectangle(c,5,5,14,28);
  cairo_rectangle(c,19,5,14,7);
  cairo_rectangle(c,19,12,14,21);
  check(c,0,255);
  /* A hole joined to the outside by a retraced, zero-area cut. */
  cairo_new_path(c);
  cairo_move_to(c,5,5); cairo_line_to(c,75,5); cairo_line_to(c,75,75);
  cairo_line_to(c,5,75); cairo_line_to(c,5,40); cairo_line_to(c,25,40);
  cairo_line_to(c,25,55); cairo_line_to(c,55,55); cairo_line_to(c,55,25);
  cairo_line_to(c,25,25); cairo_line_to(c,25,40); cairo_line_to(c,5,40);
  cairo_close_path(c);
  check(c,0,125);
  /* Point-touching islands must not turn the space between them into fill. */
  cairo_new_path(c); cairo_rectangle(c,5,5,20,20); cairo_rectangle(c,25,25,20,20);
  check(c,1,125);
  /* Allocation failures leave the original path available for fallback. */
  for (i=1;i<=3;i++) {
    cairo_path_t *a,*b;
    cairo_new_path(c); cairo_rectangle(c,5,5,20,20); cairo_rectangle(c,25,5,20,20);
    a=cairo_copy_path(c); allocations=0; fail_allocation=i;
    assert(!gxCunite(c)); b=cairo_copy_path(c); same_path(a,b);
    cairo_path_destroy(a); cairo_path_destroy(b); fail_allocation=0;
  }
  /* Unsupported curves and coordinates must remain untouched. */
  cairo_new_path(c); cairo_move_to(c,1,1); cairo_curve_to(c,2,30,40,20,60,60);
  check(c,0,255);
  cairo_new_path(c); cairo_rectangle(c,300000,300000,20,20); check(c,0,255);
  /* A long run exercises a single line with many collinear endpoint events. */
  cairo_new_path(c);
  for (i=0;i<10000;i++) cairo_rectangle(c,5+i,5,1,20);
  check(c,0,125);
  /* Large paths hit the working-array limit before making any allocations. */
  {
    cairo_path_t *a,*b;
    cairo_new_path(c);
    for (i=0;i<100000;i++) cairo_rectangle(c,(i%500)*2,(i/500)*2,1,1);
    a=cairo_copy_path(c); allocations=0;
    assert(!gxCunite(c) && allocations==0);
    b=cairo_copy_path(c); same_path(a,b);
    cairo_path_destroy(a); cairo_path_destroy(b);
  }
  assert(compacted>100);
  printf("Unite topology passed: %d Cairo fill comparisons, %d compacted, holes/T-junctions/point contacts, allocation and size-limit fallback; Cairo %s\n",cases,compacted,version);
  cairo_destroy(c); cairo_surface_destroy(s);
  return 0;
}
