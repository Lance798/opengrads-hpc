/* Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
   Time workers exec a fresh, headless instance of this executable. No parser,
   allocator, file-library state or OpenMP runtime is used between fork/exec.
   Only the parent touches graphics and progress. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include "grads.h"
#include "gaomp.h"
#include "gatime.h"
#if USEOPENMP == 1
#include <omp.h>
#endif

#if !defined(_WIN32) && !defined(__GO32__) && !defined(SHRDOBJ)
#define TIME_POSIX 1
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <spawn.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#ifdef __linux__
#include <sched.h>
#endif
extern char **environ;
#else
#define TIME_POSIX 0
#endif

#define TIME_MAX_WORKERS 128
#define TIME_TEXT 4096
#define TIME_CONFIG 0x47544331
#define TIME_JOB 0x47544a31
struct time_config {
  int operation,z1,z2,e,idim,jdim,ri,rj,nx,ny,lo[5],hi[5],reads;
  int cachesf,opener;
  double dmin[5],dmax[5],undef,cache_bytes;
  char path[TIME_TEXT],expression[TIME_TEXT];
};
struct time_job { int operation,t; };
struct time_reply { int rc,t; uint64_t size; };
static char time_executable[TIME_TEXT];
static char time_loader[TIME_TEXT],time_library_path[TIME_TEXT];
static int time_child;

/* Linux's free-page count excludes reclaimable file cache. Use the kernel's
   available-memory estimate so warm input files do not artificially shrink
   worker pools. Keep the conservative free-page fallback on other hosts. */
static double time_available_memory(void) {
#ifdef __linux__
  FILE *file=fopen("/proc/meminfo","r");char line[256];unsigned long long kib;
  if(file) {
    while(fgets(line,sizeof(line),file)) {
      if(sscanf(line,"MemAvailable: %llu kB",&kib)==1 && kib) {
        fclose(file);return (double)kib*1024;
      }
    }
    fclose(file);
  }
#endif
#if TIME_POSIX && defined(_SC_AVPHYS_PAGES)
  {
    long pages=sysconf(_SC_AVPHYS_PAGES),page=sysconf(_SC_PAGESIZE);
    if(pages>0 && page>0)return (double)pages*page;
  }
#endif
  return 32.0*1024*1024*1024;
}

/* Whole expressions may use read-only built-ins, arithmetic and unmodified
   fields of the default descriptor; no particular outer function is required.
   Defined variables, file qualifiers, plug-ins, strings and unknown syntax
   retain the ordinary evaluator. Never serialize pointers/session objects. */
static int time_expression(char *expr, struct gastat *pst) {
  static const char *functions[]={"ave","mean","sum","sumg","aave","amean",
    "asum","atot","maskout","zint","abs","sqrt","exp","log","log10",
    "sin","cos","tan","asin","acos","atan2","pow","mag","const",
    "esat","qvsat",NULL};
  static const char *words[]={"x","y","z","t","e","lon","lat","lev","ens","auto","global",NULL};
  char name[32],*p=expr,*end; int n,i,field=0; struct gavar *v;
  while (*p) {
    if (isdigit((unsigned char)*p) || (*p=='.' && isdigit((unsigned char)p[1]))) {
      (void)strtod(p,&end); if(end==p)return 0; p=end; continue;
    }
    if (isalpha((unsigned char)*p)) {
      n=0;
      while(isalnum((unsigned char)*p) || *p=='_') {
        if(n==(int)sizeof(name)-1)return 0;
        name[n++]=*p++;
      }
      name[n]=0;
      if(*p=='.')return 0;
      if(*p=='(') {
        for(i=0;functions[i] && strcmp(name,functions[i]);i++);
        if(!functions[i])return 0;
      } else {
        if(getdfn(name,pst))return 0;
        v=pst->pfid->pvar1;
        for(i=0;i<pst->pfid->vnum && strcmp(name,v[i].abbrv);i++);
        if(i<pst->pfid->vnum)field=1;
        else { for(i=0;words[i] && strcmp(name,words[i]);i++); if(!words[i])return 0; }
      }
    } else {
      if(!strchr("()+-*/<>=!,&|",*p))return 0;
      p++;
    }
  }
  return field;
}

/* Each process owns its NetCDF/HDF5 handles and library state. */
static int time_file(struct gafile *f) {
  return f && f->type==1 && (f->ncflg==1 || f->ncflg==3) &&
         !f->ppflag && !f->idxflg && !f->climo;
}

static int time_supported(char *expr, struct gastat *pst) {
  struct gafile *f=pst->pfid;
  return !time_child && time_file(f) && strlen(expr)<TIME_TEXT &&
         !strchr(f->dnam,'\n') && time_expression(expr,pst);
}

/* One owner per complete time. Spatial functions keep their original order,
   including SIMD row grouping and per-column Z addition. */
static int time_series(struct time_config *c, struct gastat *model,
                       struct gagrid **shape, int t, double *values, char *mask) {
  struct gastat sample=*model;
  struct gafile *f=model->pfid;
  struct gagrid *g;
  int k,rc,fresh=0; size_t size=(size_t)c->nx*c->ny,j,at=0;
  sample.result.pgr=NULL;
  sample.dmin[2]=sample.dmax[2]=f->gr2ab[2](f->grvals[2],c->z1);
  sample.dmin[4]=sample.dmax[4]=f->gr2ab[4](f->grvals[4],c->e);
  gr2t(f->grvals[3],t,&sample.tmin); sample.tmax=sample.tmin;
  if(!*shape) {
    if(gaexpr(c->expression,&sample))return 1;
    if(sample.type!=1) { gafree(&sample);return 1; }
    *shape=sample.result.pgr; fresh=1;
    if((*shape)->idim!=c->ri || (*shape)->jdim!=c->rj ||
       (*shape)->isiz!=c->nx || (*shape)->jsiz!=c->ny)return 1;
    if((c->ri>=0 && ((*shape)->dimmin[c->ri]!=c->lo[c->ri] || (*shape)->dimmax[c->ri]!=c->hi[c->ri])) ||
       (c->rj>=0 && ((*shape)->dimmin[c->rj]!=c->lo[c->rj] || (*shape)->dimmax[c->rj]!=c->hi[c->rj])))return 1;
    *model=sample; model->result.pgr=NULL;
  }
  if(c->z2>c->z1) {
    rc=gadefseries(c->expression,&sample,c->z1,c->z2,*shape,values,mask,c->undef);
    if(rc>=0)return rc;
  }
  for(k=c->z1;k<=c->z2;k++) {
    sample=*model; sample.result.pgr=NULL;
    sample.dmin[2]=sample.dmax[2]=f->gr2ab[2](f->grvals[2],k);
    sample.dmin[4]=sample.dmax[4]=f->gr2ab[4](f->grvals[4],c->e);
    gr2t(f->grvals[3],t,&sample.tmin);sample.tmax=sample.tmin;
    if(fresh && k==c->z1)g=*shape;
    else {
      if(gaexpr(c->expression,&sample))return 1;
      if(sample.type!=1) { gafree(&sample);return 1; }
      g=sample.result.pgr;
    }
    if(g->idim!=c->ri || g->jdim!=c->rj || g->isiz!=c->nx || g->jsiz!=c->ny ||
       (c->ri>=0 && gagchk(g,*shape,c->ri)) || (c->rj>=0 && gagchk(g,*shape,c->rj))) {
      if(g!=*shape)gagfre(g);
      return 1;
    }
    for(j=0;j<size;j++,at++) { mask[at]=g->umask[j]; values[at]=mask[at] ? g->grid[j] : c->undef; }
    if(g!=*shape)gagfre(g);
    if(gaqsig())return 1;
  }
  return 0;
}

/* Input reductions evaluate the whole spatial grid at each time. Unlike
   output profiles, varying Z belongs to this grid and must not be collapsed. */
static int time_input_grid(struct time_config *c, struct gastat *model,
                           struct gagrid **shape, int t, double *values, char *mask) {
  struct gastat sample=*model;struct gagrid *g;size_t j,size=(size_t)c->nx*c->ny;
  sample.result.pgr=NULL;
  gr2t(model->pfid->grvals[3],t,&sample.tmin);sample.tmax=sample.tmin;
  if(gaexpr(c->expression,&sample))return 1;
  if(sample.type!=1) { gafree(&sample);return 1; }
  g=sample.result.pgr;
  if(g->idim!=c->ri || g->jdim!=c->rj || g->isiz!=c->nx || g->jsiz!=c->ny ||
     (c->ri>=0 && (g->dimmin[c->ri]!=c->lo[c->ri] || g->dimmax[c->ri]!=c->hi[c->ri])) ||
     (c->rj>=0 && (g->dimmin[c->rj]!=c->lo[c->rj] || g->dimmax[c->rj]!=c->hi[c->rj])) ||
     (*shape && ((c->ri>=0 && gagchk(g,*shape,c->ri)) ||
                 (c->rj>=0 && gagchk(g,*shape,c->rj))))) {
    gagfre(g);return 1;
  }
  for(j=0;j<size;j++) { mask[j]=g->umask[j];values[j]=mask[j] ? g->grid[j] : c->undef; }
  if(*shape)gagfre(g);else *shape=g;
  return gaqsig() ? 1 : 0;
}

#if TIME_POSIX
struct time_slot { pid_t pid; int fd,t; size_t received; struct time_reply reply; };
static struct time_slot slots[TIME_MAX_WORKERS];
static int pool_count;
static int pool_active;
gaint ga_time_active_workers(void) { return pool_active; }
static double time_now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+1e-9*t.tv_nsec; }
static int time_write(int fd,const void *p,size_t n) {
  const char *s=p;ssize_t k;
  while(n) {
#ifdef MSG_NOSIGNAL
    k=send(fd,s,n,MSG_NOSIGNAL);
#else
    k=send(fd,s,n,0);
#endif
    if(k<0 && errno==EINTR)continue;
    if(k<=0)return 1;
    s+=k;n-=k;
  }
  return 0;
}
static int time_read(int fd,void *p,size_t n) {
  char *s=p;ssize_t k;
  while(n) { k=read(fd,s,n);if(k<0 && errno==EINTR)continue;if(k<=0)return 1;s+=k;n-=k; }
  return 0;
}

void ga_time_shutdown(void) {
  int i,live;double until;
  for(i=0;i<pool_count;i++) { shutdown(slots[i].fd,SHUT_RDWR);close(slots[i].fd); }
  until=time_now()+0.2;
  do {
    live=0;
    for(i=0;i<pool_count;i++)if(slots[i].pid>0) {
      if(waitpid(slots[i].pid,NULL,WNOHANG)==slots[i].pid)slots[i].pid=0; else live++;
    }
    if(live) { struct timespec nap={0,10000000};nanosleep(&nap,NULL); }
  } while(live && time_now()<until);
  for(i=0;i<pool_count;i++)if(slots[i].pid>0)kill(slots[i].pid,SIGTERM);
  until=time_now()+1.0;
  do {
    live=0;
    for(i=0;i<pool_count;i++)if(slots[i].pid>0) {
      if(waitpid(slots[i].pid,NULL,WNOHANG)==slots[i].pid)slots[i].pid=0; else live++;
    }
    if(live) { struct timespec nap={0,10000000};nanosleep(&nap,NULL); }
  } while(live && time_now()<until);
  for(i=0;i<pool_count;i++)if(slots[i].pid>0) {
    kill(slots[i].pid,SIGKILL);
    while(waitpid(slots[i].pid,NULL,0)<0 && errno==EINTR);
  }
  pool_count=0;
  pool_active=0;
}

static int time_spawn(int n) {
  posix_spawn_file_actions_t actions;posix_spawnattr_t attr;
  char *argv[]={time_executable,"-bl","-d","gxdummy","-h","gxdummy","-j","1","--time-worker",NULL};
  char *loader_argv[]={time_loader,"--library-path",time_library_path,time_executable,
    "-bl","-d","gxdummy","-h","gxdummy","-j","1","--time-worker",NULL};
  DIR *dir;struct dirent *entry;int pair[2],i,fd,rc,old,j,k,total; sigset_t empty;
  char **worker_env,*prefixes[]={"OMP_PROC_BIND=","OMP_NUM_THREADS=","GA_NUM_THREADS=","GOMP_CPU_AFFINITY=","GA_TIME_AFFINITY=","GA_PROGRESS=",NULL};
  char affinity[TIME_TEXT]="GA_TIME_AFFINITY=";
  if(pool_count>=n)return 0;
  ga_time_shutdown();
#ifdef __linux__
  {
    cpu_set_t allowed;size_t at=strlen(affinity);int cpu;
    CPU_ZERO(&allowed);
#if USEOPENMP == 1 && _OPENMP >= 201307
    /* The calling OpenMP thread may be pinned to one place. Restore the
       allocation represented by all places, not just that thread's mask. */
    for(j=0;j<omp_get_num_places();j++) {
      int m=omp_get_place_num_procs(j),*ids=malloc((size_t)m*sizeof(*ids));
      if(!ids)continue;
      omp_get_place_proc_ids(j,ids);
      for(k=0;k<m;k++)if(ids[k]>=0 && ids[k]<CPU_SETSIZE)CPU_SET(ids[k],&allowed);
      free(ids);
    }
#endif
    if(!CPU_COUNT(&allowed))sched_getaffinity(0,sizeof(allowed),&allowed);
    for(cpu=0;cpu<CPU_SETSIZE;cpu++)if(CPU_ISSET(cpu,&allowed) && at<sizeof(affinity)-16)
      at+=(size_t)snprintf(affinity+at,sizeof(affinity)-at,"%s%d",at>strlen("GA_TIME_AFFINITY=") ? "," : "",cpu);
  }
#endif
  for(total=0;environ[total];total++);
  worker_env=calloc((size_t)total+7,sizeof(*worker_env));if(!worker_env)return 1;
  for(j=0,k=0;j<total;j++) {
    int p;
    for(p=0;prefixes[p] && strncmp(environ[j],prefixes[p],strlen(prefixes[p]));p++);
    if(!prefixes[p])worker_env[k++]=environ[j];
  }
  worker_env[k++]="OMP_PROC_BIND=false";worker_env[k++]="OMP_NUM_THREADS=1";
  worker_env[k++]="GA_NUM_THREADS=1";worker_env[k++]="GA_PROGRESS=off";worker_env[k++]=affinity;
  for(i=0;i<n;i++) {
    if(socketpair(AF_UNIX,SOCK_STREAM,0,pair))goto error;
    fcntl(pair[0],F_SETFD,FD_CLOEXEC);fcntl(pair[1],F_SETFD,FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    old=1;setsockopt(pair[0],SOL_SOCKET,SO_NOSIGPIPE,&old,sizeof(old));
    setsockopt(pair[1],SOL_SOCKET,SO_NOSIGPIPE,&old,sizeof(old));
#endif
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_adddup2(&actions,pair[1],3);
    posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addopen(&actions,1,"/dev/null",O_WRONLY,0);
    if(!getenv("GA_TIME_TRACE"))posix_spawn_file_actions_addopen(&actions,2,"/dev/null",O_WRONLY,0);
    dir=opendir("/proc/self/fd");if(!dir)dir=opendir("/dev/fd");
    if(!dir) { close(pair[0]);close(pair[1]);posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attr);goto error; }
    while((entry=readdir(dir))) { fd=atoi(entry->d_name);if(fd>3 && fd!=dirfd(dir))posix_spawn_file_actions_addclose(&actions,fd); }
    closedir(dir);
    sigemptyset(&empty);posix_spawnattr_setsigmask(&attr,&empty);
    posix_spawnattr_setpgroup(&attr,0);
    posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK);
    rc=posix_spawn(&slots[i].pid,time_loader[0] ? time_loader : time_executable,
                   &actions,&attr,time_loader[0] ? loader_argv : argv,worker_env);
    posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attr);
    close(pair[1]);
    if(rc) { close(pair[0]);goto error; }
    slots[i].fd=pair[0];slots[i].t=0;pool_count++;
    old=fcntl(pair[0],F_GETFL);
    if(old<0 || fcntl(pair[0],F_SETFL,old|O_NONBLOCK)<0)goto error;
  }
  free(worker_env);return 0;
error:
  free(worker_env);ga_time_shutdown();return 1;
}

void ga_time_prepare_worker(void) {
#ifdef __linux__
  const char *p=getenv("GA_TIME_AFFINITY");char *end;long cpu;cpu_set_t allowed;
  CPU_ZERO(&allowed);
  while(p && *p) {
    cpu=strtol(p,&end,10);if(end==p || cpu<0 || cpu>=CPU_SETSIZE)return;
    CPU_SET((int)cpu,&allowed);p=*end==',' ? end+1 : end;
  }
  if(CPU_COUNT(&allowed))sched_setaffinity(0,sizeof(allowed),&allowed);
#endif
}

static int time_workers(struct time_config *c,int nt) {
  double available=8.0*1024*1024*1024,memory=time_available_memory()/4,per;
  int n=ga_omp_get_threads();const char *v=getenv("GA_TIME_WORKERS");
  if(time_child || !time_executable[0] || n<2 || nt<(c->reads ? 2 : 32) ||
     (v && (!strcmp(v,"off") || !strcmp(v,"0"))))return 0;
  if(memory<available)available=memory;
  per=32.0*1024*1024+4.0*c->nx*c->ny*(c->z2-c->z1+1)*9;
  /* Include two worst-case dataset caches; all estimates are command-local. */
  per+=2.0*c->cache_bytes;
  if(n>TIME_MAX_WORKERS)n=TIME_MAX_WORKERS;
  if(n>nt)n=nt;
  if(n>available/per)n=(int)(available/per);
  return n>1 ? n : 0;
}

static int time_parallel(struct time_config *c,int t1,int t2,int incr,double *values,char *mask) {
  struct pollfd polls[TIME_MAX_WORKERS];struct time_job job;
  size_t count=(size_t)c->nx*c->ny*(c->z2-c->z1+1),payload=count*(sizeof(double)+1),part,offset;
  int nt=(t2-t1)/incr+1,n=time_workers(c,nt),i,done=0,rc;int64_t next=t1;
  ssize_t got;char *dest;char trace[4096];
  if(!n)return -1;
  if(time_spawn(n))return -1;
  pool_active=n;
  if(getenv("GA_TIME_TRACE")) {
    offset=(size_t)snprintf(trace,sizeof(trace),"Time workers: workers=%d inner=1 times=%d levels=%d pids=",n,nt,c->z2-c->z1+1);
    for(i=0;i<n && offset<sizeof(trace);i++)offset+=(size_t)snprintf(trace+offset,sizeof(trace)-offset,"%s%ld",i ? "," : "",(long)slots[i].pid);
    if(offset<sizeof(trace)-1)strcat(trace,"\n");
    gaprnt(2,trace);
    if(c->reads==2) {
      snprintf(trace,sizeof(trace),"Time input expression: first=%d times=%d increment=%d grid=%dx%d axes=%d,%d\n",
               t1,nt,incr,c->nx,c->ny,c->ri,c->rj);
      gaprnt(2,trace);
    }
  }
  for(i=0;i<n;i++) {
    int flags=fcntl(slots[i].fd,F_GETFL);fcntl(slots[i].fd,F_SETFL,flags&~O_NONBLOCK);
    rc=time_write(slots[i].fd,c,sizeof(*c));fcntl(slots[i].fd,F_SETFL,flags);
    if(rc)goto error;
    job.operation=TIME_JOB;job.t=next;next+=incr;slots[i].t=job.t;slots[i].received=0;
    if(time_write(slots[i].fd,&job,sizeof(job)))goto error;
  }
  while(done<nt) {
    if(gaqsig())goto error;
    for(i=0;i<n;i++) { polls[i].fd=slots[i].t ? slots[i].fd : -1;polls[i].events=POLLIN;polls[i].revents=0; }
    rc=poll(polls,n,100);
    if(rc<0 && errno!=EINTR)goto error;
    for(i=0;i<n;i++)if(slots[i].t && polls[i].revents) {
      for(;;) {
        offset=(size_t)((slots[i].t-t1)/incr)*count;
        if(slots[i].received<sizeof(struct time_reply)) {
          part=sizeof(struct time_reply)-slots[i].received;dest=(char *)&slots[i].reply+slots[i].received;
        } else if(slots[i].received<sizeof(struct time_reply)+count*sizeof(double)) {
          part=sizeof(struct time_reply)+count*sizeof(double)-slots[i].received;
          dest=(char *)(values+offset)+slots[i].received-sizeof(struct time_reply);
        } else {
          part=sizeof(struct time_reply)+payload-slots[i].received;
          dest=mask+offset+slots[i].received-sizeof(struct time_reply)-count*sizeof(double);
        }
        got=read(slots[i].fd,dest,part);
        if(got<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))break;
        if(got<=0)goto error;
        slots[i].received+=(size_t)got;
        if(slots[i].received==sizeof(struct time_reply) &&
           (slots[i].reply.rc || slots[i].reply.t!=slots[i].t || slots[i].reply.size!=count))goto error;
        if(slots[i].received==sizeof(struct time_reply)+payload) {
          done++;slots[i].t=0;
          if(next<=t2) {
            job.operation=TIME_JOB;job.t=next;next+=incr;slots[i].t=job.t;slots[i].received=0;
            if(time_write(slots[i].fd,&job,sizeof(job)))goto error;
          }
          break;
        }
      }
    }
    if(c->reads)gaprogpulse();
    else gaprogstep(done*(c->z2-c->z1+1));
  }
  pool_active=0;return 0;
error:
  ga_time_shutdown();
  if(!gaqsig())gaprnt(0,"Time evaluation failed; incomplete results discarded.\n");
  return 1;
}

void ga_time_init(const char *executable) {
  static int registered;
  const char *loader=getenv("GA_TIME_LOADER"),*libraries=getenv("GA_TIME_LIBRARY_PATH");
  time_executable[0]=time_loader[0]=time_library_path[0]=0;
  /* An explicitly invoked Linux loader owns /proc/self/exe. The release
     launcher supplies its matched loader/library path; argv[0] names GrADS.
     Re-exec that same combination so older hosts never use their own loader
     against the bundled libc. Invalid/incomplete context disables the pool. */
  if(loader || libraries) {
    if(loader && *loader && libraries && strlen(libraries)<sizeof(time_library_path) &&
       realpath(loader,time_loader) && realpath(executable,time_executable))
      snprintf(time_library_path,sizeof(time_library_path),"%s",libraries);
    else time_executable[0]=time_loader[0]=0;
  } else {
#ifdef __linux__
    ssize_t n=readlink("/proc/self/exe",time_executable,sizeof(time_executable)-1);
    if(n>0)time_executable[n]=0;else
#endif
    if(!realpath(executable,time_executable))time_executable[0]=0;
  }
  if(!registered) { atexit(ga_time_shutdown);registered=1; }
}

gaint ga_time_worker(struct gacmn *pcm,int fd) {
  struct time_config c;struct time_job job;struct time_reply reply;
  struct gastat *pst=NULL,model;struct gagrid *shape=NULL;
  double *values=NULL;char *mask=NULL;size_t count;int operation,rc=0,d;
  time_child=1;ga_omp_set_threads(1);
  for(;;) {
    if(time_read(fd,&operation,sizeof(operation)))break;
    if(operation==TIME_CONFIG) {
      c.operation=operation;if(time_read(fd,(char *)&c+sizeof(operation),sizeof(c)-sizeof(operation)))break;
      if(shape) { gagfre(shape);shape=NULL; }free(values);free(mask);values=NULL;mask=NULL;
      gacmd("reinit",pcm,0);pcm->sig=0;
      if(!memchr(c.path,0,sizeof(c.path)) || !memchr(c.expression,0,sizeof(c.expression)) ||
         c.z1<1 || c.z2<c.z1 || c.nx<1 || c.ny<1)break;
      pcm->cachesf=c.cachesf;setcachesf(pcm->cachesf);
      if(c.opener==0) { if(gaopen(c.path,pcm))break; }
#if USENETCDF==1
      else if(c.opener==1) { if(gasdfopen(c.path,pcm))break; }
      else if(c.opener==2) { if(gaxdfopen(c.path,pcm))break; }
#endif
      else break;
      setcachesf(pcm->cachesf);
      pst=getpst(pcm);if(!pst)break;model=*pst;gree(pst,"timepst");pst=NULL;
      for(d=0;d<5;d++) { model.dmin[d]=c.dmin[d];model.dmax[d]=c.dmax[d]; }
      model.idim=c.idim;model.jdim=c.jdim;model.result.pgr=NULL;
      count=(size_t)c.nx*c.ny*(c.z2-c.z1+1);
      if(count>128*1024*1024/9)break;
      values=malloc(count*sizeof(*values));mask=malloc(count);if(!values || !mask)break;
    } else if(operation==TIME_JOB && values && mask) {
      job.operation=operation;if(time_read(fd,&job.t,sizeof(job.t)))break;
      pcm->sig=0;
      reply.rc=c.reads==2 ? time_input_grid(&c,&model,&shape,job.t,values,mask) :
                           time_series(&c,&model,&shape,job.t,values,mask);
      reply.t=job.t;reply.size=count;
      if(time_write(fd,&reply,sizeof(reply)))break;
      if(reply.rc)break;
      if(time_write(fd,values,count*sizeof(*values)) || time_write(fd,mask,count))break;
    } else break;
  }
  if(shape)gagfre(shape);
  free(values);free(mask);gacmd("reinit",pcm,0);close(fd);
  return rc;
}
#else
void ga_time_init(const char *executable) { (void)executable; }
void ga_time_prepare_worker(void) {}
void ga_time_shutdown(void) {}
gaint ga_time_active_workers(void) { return 0; }
gaint ga_time_worker(struct gacmn *pcm,int fd) { (void)pcm;(void)fd;return 1; }
static int time_parallel(struct time_config *c,int t1,int t2,int incr,double *v,char *m) {
  (void)c;(void)t1;(void)t2;(void)incr;(void)v;(void)m;return -1;
}
#endif

/* Input-time reductions reuse the same isolated workers. These entry points
   only see native variables already accepted by the bounded batch reader;
   no user expression or defined-variable state is reconstructed. */
gaint ga_time_can_read(struct gafile *file) {
#if TIME_POSIX
  const char *v=getenv("GA_TIME_WORKERS");
  return !time_child && time_executable[0] && ga_omp_get_threads()>1 &&
    !(v && (!strcmp(v,"off") || !strcmp(v,"0"))) && file &&
    time_file(file);
#else
  (void)file;return 0;
#endif
}

gadouble ga_time_batch_bytes(struct gafile *file) {
  double bytes=64.0*1024*1024;
  if(ga_time_can_read(file)) {
    double memory=time_available_memory()/4;
    bytes=8.0*1024*1024*ga_omp_get_threads();
    if(bytes<64.0*1024*1024)bytes=64.0*1024*1024;
    if(bytes>1024.0*1024*1024)bytes=1024.0*1024*1024;
    if(bytes>memory)bytes=memory;
  }
  return bytes;
}

gaint ga_time_readsteps(struct gagrid *grid,gaint t0,gaint n,gaint incr,
                        gadouble *values,char *mask,long cachesf) {
  struct gafile *file=grid->pfile;struct time_config c;int d;
  if(!ga_time_can_read(file) || !grid->pvar || grid->toff || n<2 ||
     incr<1 || t0<1 || t0>file->dnum[3] || (n-1)>(file->dnum[3]-t0)/incr ||
     grid->idim>1 || grid->jdim>1 || grid->isiz<1 || grid->jsiz<1 ||
     cachesf<0 || cachesf>INT_MAX ||
     (double)grid->isiz*grid->jsiz*n<GA_PARALLEL_MIN_CELLS ||
     (double)grid->isiz*grid->jsiz>128.0*1024*1024/9 ||
     strchr(file->dnam,'\n'))return -1;
  memset(&c,0,sizeof(c));c.operation=TIME_CONFIG;c.reads=1;
  c.opener=file->sdfopener;
  c.idim=c.ri=grid->idim;c.jdim=c.rj=grid->jdim;
  c.nx=grid->isiz;c.ny=grid->jsiz;c.undef=grid->undef;
  c.z1=c.z2=grid->pvar->levels ? grid->dimmin[2] : 1;
  c.e=grid->dimmin[4];c.cachesf=(int)cachesf;c.cache_bytes=(double)cachesf*file->cachesize;
  if(c.z1<1 || c.z1>file->dnum[2])return -1;
  for(d=0;d<5;d++) {
    c.lo[d]=grid->dimmin[d];c.hi[d]=grid->dimmax[d];
    if(d!=3) {
      c.dmin[d]=file->gr2ab[d](file->grvals[d],grid->dimmin[d]);
      c.dmax[d]=file->gr2ab[d](file->grvals[d],grid->dimmax[d]);
    }
  }
#if TIME_POSIX
  if(file->dnam[0]=='/')snprintf(c.path,sizeof(c.path),"%s",file->dnam);
  else {
    char cwd[TIME_TEXT];int length;
    if(!getcwd(cwd,sizeof(cwd)))return -1;
    length=snprintf(c.path,sizeof(c.path),"%s/%s",cwd,file->dnam);
    if(length<0 || length>=(int)sizeof(c.path))return -1;
  }
#else
  return -1;
#endif
  snprintf(c.expression,sizeof(c.expression),"%s",grid->pvar->abbrv);
  return time_parallel(&c,t0,t0+(n-1)*incr,incr,values,mask);
}

/* Relative spatial bounds can mutate the evaluator environment between
   input times. Nested time bounds also retain the serial evaluator. */
gaint ga_time_can_evaluate(char *expr,struct gastat *pst) {
  char *p=expr,name[8];int n;
  if(!ga_time_can_read(pst->pfid) || !time_supported(expr,pst) ||
     pst->idim>2 || pst->jdim>2)return 0;
  while((p=strchr(p,','))) {
    p++;n=0;
    while(isalpha((unsigned char)*p) && n<7)name[n++]=*p++;
    name[n]=0;
    if((!strcmp(name,"t") && (*p=='=' || *p=='+' || *p=='-')) ||
       ((!strcmp(name,"x") || !strcmp(name,"y") || !strcmp(name,"z") ||
         !strcmp(name,"e") || !strcmp(name,"lon") || !strcmp(name,"lat") ||
         !strcmp(name,"lev") || !strcmp(name,"ens")) && (*p=='+' || *p=='-')))return 0;
  }
  return 1;
}

gaint ga_time_evaluatesteps(char *expr,struct gastat *pst,struct gagrid *shape,
                            gaint t0,gaint n,gaint incr,gadouble *values,char *mask) {
  struct time_config c;struct gafile *f=pst->pfid;int d;long cachesf=getcachesf();
  if(!ga_time_can_evaluate(expr,pst) || n<2 || incr<1 || t0<1 || t0>f->dnum[3] ||
     (n-1)>(f->dnum[3]-t0)/incr || shape->idim>2 || shape->jdim>2 ||
     shape->isiz<1 || shape->jsiz<1 || cachesf<0 || cachesf>INT_MAX ||
     (double)shape->isiz*shape->jsiz>128.0*1024*1024/9)return -1;
  memset(&c,0,sizeof(c));c.operation=TIME_CONFIG;c.reads=2;
  c.opener=f->sdfopener;
  c.idim=pst->idim;c.jdim=pst->jdim;c.ri=shape->idim;c.rj=shape->jdim;
  c.nx=shape->isiz;c.ny=shape->jsiz;c.undef=shape->undef;
  c.z1=c.z2=1;c.e=1;c.cachesf=(int)cachesf;c.cache_bytes=(double)cachesf*f->cachesize;
  for(d=0;d<5;d++) {
    c.lo[d]=shape->dimmin[d];c.hi[d]=shape->dimmax[d];
    c.dmin[d]=pst->dmin[d];c.dmax[d]=pst->dmax[d];
  }
#if TIME_POSIX
  if(f->dnam[0]=='/')snprintf(c.path,sizeof(c.path),"%s",f->dnam);
  else {
    char cwd[TIME_TEXT];int length;
    if(!getcwd(cwd,sizeof(cwd)))return -1;
    length=snprintf(c.path,sizeof(c.path),"%s/%s",cwd,f->dnam);
    if(length<0 || length>=(int)sizeof(c.path))return -1;
  }
#else
  return -1;
#endif
  snprintf(c.expression,sizeof(c.expression),"%s",expr);
  return time_parallel(&c,t0,t0+(n-1)*incr,incr,values,mask);
}

static int time_configure(struct time_config *c,char *expr,struct gacmn *pcm,
                          struct gastat *pst,int z1,int z2,int e,struct gagrid *shape) {
  int d;
  memset(c,0,sizeof(*c));c->operation=TIME_CONFIG;c->z1=z1;c->z2=z2;c->e=e;
  c->opener=pst->pfid->sdfopener;
  c->idim=pcm->vdim[0] ? 0 : pcm->vdim[1] ? 1 : -1;
  c->jdim=pcm->vdim[0] && pcm->vdim[1] ? 1 : -1;
  c->ri=shape->idim;c->rj=shape->jdim;c->nx=shape->isiz;c->ny=shape->jsiz;c->undef=pcm->undef;
  c->cachesf=pcm->cachesf;
  c->cache_bytes=(double)pcm->cachesf*pst->pfid->cachesize;
  if(!time_supported(expr,pst) || z1<1 || z2>pst->pfid->dnum[2] ||
     (double)c->nx*c->ny*(z2-z1+1)>128.0*1024*1024/9)return 0;
#if TIME_POSIX
  /* Preserve symlink spelling: ^dataset paths are relative to the descriptor
     path used by the caller, not necessarily its realpath target directory. */
  if(pst->pfid->dnam[0]=='/')snprintf(c->path,sizeof(c->path),"%s",pst->pfid->dnam);
  else {
    char cwd[TIME_TEXT];int length;
    if(!getcwd(cwd,sizeof(cwd)))return 0;
    length=snprintf(c->path,sizeof(c->path),"%s/%s",cwd,pst->pfid->dnam);
    if(length<0 || length>=(int)sizeof(c->path))return 0;
  }
#else
  snprintf(c->path,sizeof(c->path),"%s",pst->pfid->dnam);
#endif
  snprintf(c->expression,sizeof(c->expression),"%s",expr);
  for(d=0;d<5;d++) { c->dmin[d]=pcm->dmin[d];c->dmax[d]=pcm->dmax[d];c->lo[d]=shape->dimmin[d];c->hi[d]=shape->dimmax[d]; }
  return 1;
}

gaint ga_time_define(char *expr,struct gacmn *pcm,gaint z1,gaint z2,gaint t1,gaint t2,
                     gaint e,struct gagrid *shape,gadouble *values,char *mask,gadouble undef) {
  struct time_config c;struct gastat *pst;int rc=-1;
  if(t2-t1+1<32 || ga_omp_get_threads()<2 || time_child)return -1;
  pst=getpst(pcm);if(!pst)return 1;
  if(time_configure(&c,expr,pcm,pst,z1,z2,e,shape) && t1>=1 && t2<=pst->pfid->dnum[3] &&
     (double)pst->pfid->dnum[0]*pst->pfid->dnum[1]*pst->pfid->dnum[2]*(t2-t1+1)>=8388608.0) {
    c.undef=undef;rc=time_parallel(&c,t1,t2,1,values,mask);
  }
  gree(pst,"timepst");return rc;
}

/* Fixing an axis before evaluating a reduction over that varying axis can
   change its result shape or make an invalid relative bound look valid.
   Keep those DISPLAY expressions in the ordinary evaluator. DEFINE already
   fixes Z/T for each evaluation by its established command semantics. */
static int time_display_reduces_axis(const char *expr,int z,int t) {
  const char *p=expr;
  /* These native functions require fixed Z/T. Only the established outer
     profile paths explicitly supply their per-level/per-time environment. */
  if((z || t) && (strstr(expr,"aave(") || strstr(expr,"amean(") ||
     strstr(expr,"asum(") || strstr(expr,"atot(") || strstr(expr,"zint(")))return 1;
  while((p=strchr(p,','))) {
    const char *name=++p;size_t length;
    while(isalpha((unsigned char)*p))p++;
    length=(size_t)(p-name);
    if(!strchr("=+-",*p) || !*p)continue;
    if((t && length==1 && *name=='t') ||
       (z && ((length==1 && *name=='z') || (length==3 && !strncmp(name,"lev",3)))))return 1;
  }
  return 0;
}

static int time_profile_expression(const char *expr) {
  return !strncmp(expr,"ave(",4) || !strncmp(expr,"mean(",5) ||
         !strncmp(expr,"aave(",5) || !strncmp(expr,"amean(",6) ||
         !strncmp(expr,"asum(",5) || !strncmp(expr,"zint(",5);
}

/* DISPLAY uses the same per-time evaluator, with a temporary Z/T grid.
   Coordinate arrays stay owned by the open descriptor, as for TLOOP. */
gaint ga_time_display(char *expr,struct gastat *pst,struct gacmn *pcm) {
  struct gastat sample=*pst,model;struct gafile *f=pst->pfid;
  struct gagrid *shape=NULL,*out=NULL;struct time_config c;
  int z1,z2,t1,t2,e,nt,nz,k,rc,lv,generic;size_t size;
  const char *setting=getenv("GA_TIME_WORKERS");
  if(!time_supported(expr,pst) || pcm->vdim[0] || pcm->vdim[1] || pcm->vdim[4] ||
     (pst->idim!=2 && pst->idim!=3) || (pst->jdim!=-1 && pst->jdim!=3))return -1;
  /* Preserve the established profile path. New compound expressions that
     reduce a displayed axis need a separate shape plan, not Z/T expansion. */
  generic=!time_profile_expression(expr);
  if(generic &&
     time_display_reduces_axis(expr,pcm->vdim[2],pcm->vdim[3]))return -1;
  if(pst->idim==2 && pst->jdim<0 && strncmp(expr,"aave(",5) &&
     strncmp(expr,"amean(",6) && strncmp(expr,"asum(",5))return -1;
  z1=(int)floor(f->ab2gr[2](f->abvals[2],pst->dmin[2])+0.001);
  z2=(int)ceil(f->ab2gr[2](f->abvals[2],pst->dmax[2])-0.001);
  if(!pcm->vdim[2])z1=z2=(int)round(f->ab2gr[2](f->abvals[2],pst->dmin[2]));
  t1=(int)floor(t2gr(f->abvals[3],&pst->tmin)+0.001);
  t2=(int)ceil(t2gr(f->abvals[3],&pst->tmax)-0.001);
  if(!pcm->vdim[3])t1=t2=(int)round(t2gr(f->abvals[3],&pst->tmin));
  e=(int)round(f->ab2gr[4](f->abvals[4],pst->dmin[4]));
  if(z1<1 || z2>f->dnum[2] || t1<1 || t2>f->dnum[3] || z2<z1 || t2<t1)return -1;
  if(!strncmp(expr,"zint(",5) && z2>z1)return -1;
  nz=z2-z1+1;nt=t2-t1+1;
  if(generic && (nt<32 || ga_omp_get_threads()<2 ||
     (setting && (!strcmp(setting,"off") || !strcmp(setting,"0")))))return -1;
  if((double)nz*nt>INT_MAX)return -1;
  sample.idim=sample.jdim=-1;sample.result.pgr=NULL;
  sample.dmax[2]=sample.dmin[2]=f->gr2ab[2](f->grvals[2],z1);
  gr2t(f->grvals[3],t1,&sample.tmin);sample.tmax=sample.tmin;
  lv=gaprogbeg("display",nz*nt);
  rc=gaexpr(expr,&sample);
  if(rc || sample.type!=1) { if(!rc)gafree(&sample);gaprogend(lv);return 1; }
  shape=sample.result.pgr;
  if(shape->idim>=0 || !time_configure(&c,expr,pcm,pst,z1,z2,e,shape)) { gagfre(shape);gaprogend(lv);return -1; }
  c.idim=c.jdim=-1;model=sample;model.result.pgr=NULL;size=(size_t)nz*nt;
  out=gagrvl(0);if(!out)goto error;
  memset(out,0,sizeof(*out));
  out->grid=galloc(size*sizeof(*out->grid),"timevalues");out->umask=galloc(size,"timemasks");
  /* Set the non-scalar shape before cleanup so GAGFRE owns both buffers. */
  out->idim=nz>1 ? 2 : 3;out->jdim=nz>1 && nt>1 ? 3 : -1;
  out->isiz=nz>1 ? nz : nt;out->jsiz=nz>1 ? nt : 1;
  if(!out->grid || !out->umask)goto error;
  out->undef=c.undef;
  for(k=0;k<5;k++) { out->dimmin[k]=shape->dimmin[k];out->dimmax[k]=shape->dimmax[k]; }
  out->dimmin[2]=z1;out->dimmax[2]=z2;out->dimmin[3]=t1;out->dimmax[3]=t2;
  out->igrab=out->idim==3 ? NULL : f->gr2ab[2];out->iabgr=out->idim==3 ? NULL : f->ab2gr[2];
  out->ivals=f->grvals[out->idim];out->iavals=f->abvals[out->idim];out->ilinr=f->linear[out->idim];
  out->iwrld=out->jwrld=0;
  if(out->jdim>=0) { out->jgrab=NULL;out->jvals=f->grvals[3];out->jlinr=1; }
  rc=time_parallel(&c,t1,t2,1,out->grid,out->umask);
  if(rc<0) {
    if(generic) { gagfre(shape);gagfre(out);gaprogend(lv);return -1; }
    gagfre(shape);shape=NULL;
    for(k=t1;k<=t2;k++) {
      if(time_series(&c,&model,&shape,k,out->grid+(size_t)(k-t1)*nz,out->umask+(size_t)(k-t1)*nz))goto error;
      gaprogstep((k-t1+1)*nz);
    }
  } else if(rc)goto error;
  if(shape)gagfre(shape);
  gaprogend(lv);pst->result.pgr=out;pst->type=1;return 0;
error:
  if(shape)gagfre(shape);
  if(out)gagfre(out);
  gaprogend(lv);return 1;
}
