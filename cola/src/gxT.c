/* Added in 2026 for terminal graphics display; see COPYING. */

/* Terminal display interface for Cairo -- draws into an off-screen image and
   shows it in the terminal instead of an X window.

   The picture is rendered by gxC.c into a Cairo image surface. Whenever
   GrADS is about to wait for the user (the command prompt, a script "pull",
   a "q pos"), gxdidle hands a copy of the picture to a worker thread if it
   changed since the last time. The worker encodes it and sends it to the
   terminal, so the prompt comes back without waiting: as an iTerm2 inline
   image, with the kitty graphics protocol, or as sixel, whichever the
   terminal shows (see "which image protocol" below; GA_TERM_PROTOCOL names
   one). In a terminal that shows none, -d Term stops at start-up, unless
   the launcher chose it (GA_TERM_AUTO=1): then the pictures are only
   written, as in file mode. Where the picture goes depends on GA_TERM_MODE:

     tmux    A pane is split off beside GrADS (it runs GA_TERM_VIEWER --hold,
             normally libexec/grads-termview, only to keep the pane open) and
             the worker draws each picture into it.
     inline  The picture is printed into the terminal below the command,
             like a notebook. Needs no tmux, but must be asked for.
     file    The picture is only written; grads-termview can show it
             elsewhere, started by hand with the command printed at start-up.
     auto    tmux, inline when no viewer is available. This is the default.
             Outside tmux, auto and tmux stop at start-up (files instead
             when the launcher chose the display).

   Sending. Inside tmux the image sequence travels in a passthrough sequence
   and carries its own cursor movement, because tmux does not place
   passthrough output at the pane. tmux 3.3 and later keep whatever they are
   given and send it on as fast as the link allows, so before each picture
   the worker waits for the tmux client's terminal to have room: pictures
   queue here, where Ctrl-C can drop them, instead of in tmux. Older tmux
   throws away all it holds for a client once that is more than 8 bytes per
   cell of the client's terminal, passthrough included, and redraws; there
   the picture goes in parts of a fraction of that, each sent once the
   client's terminal has had room for a moment. That is no proof: tmux 3.2a
   can sit on what it holds while the terminal has room, until more comes
   in. Should tmux report a loss (client_discarded), the cut-off sequence is
   ended, tmux redraws, and the picture is sent again in smaller parts.
   tmux also skips a passthrough sequence, without a word, while a redraw
   waits for the terminal to catch up, so GrADS asks for allow-passthrough
   all (tmux 3.4), which passes it on regardless, and holds a picture back
   while its pane is not on screen, sending it when the pane is back. tmux
   counts what it queues for the terminal (client_written): a picture that
   did not add to it is sent again.
   With iTerm2's tmux integration (tmux -CC), tmux hands the pane's output
   to iTerm2 as it is, and iTerm2 draws the pane itself, so the image
   sequence goes into the pane unwrapped, after moving to the pane's own
   top left corner, in parts as iTerm2 asks for there. With iTerm2
   (LC_TERMINAL=iTerm2) a picture goes in parts, which keeps the previous
   picture up until the new one is complete, and when the link is slow or
   the picture large, iTerm2's own progress bar (OSC 9;4) is updated
   between the parts, so it shows what has actually arrived.

   Animation. A frame ends where the picture is replaced: at a "swap" in
   double-buffer mode, or when a drawn page is cleared. Every frame is sent,
   in order, as it is made, as an X window would show it; when the link is
   slower than the drawing, the drawing waits. Ctrl-C stops the command,
   drops the frames not yet sent, and sends nothing more for it.
   GA_TERM_ANIM picks the behaviour:

     live    frames are shown as they are made (the default)
     gif     as live, and a command that swaps two or more frames (a
             "set dbuff on" loop, "set looping on") also leaves a looping
             animated GIF, which iTerm2 plays by itself
     off     only the picture at the prompt is shown

   Nothing here needs an X server, so it works over plain ssh.

   Other settings:
     GA_TERM_DIR        Directory for the pictures (default: a new
                        temporary one)
     GA_TERM_SCALE      Pixel density factor, 1 to 4 (default 2, for Retina)
     GA_TERM_PANE       Width of the tmux viewer pane, e.g. 45% (default 50%)
     GA_TERM_WIDTH      Width of an inline image: a percentage of the
                        terminal, or columns; px too for iTerm2 (default 70%)
     GA_TERM_PROTOCOL   iterm2, kitty or sixel, without asking the terminal
     GA_TERM_PROGRESS   auto (default), on (for every picture), or off (no
                        progress bar, and pictures in one piece when they fit)
     GA_TERM_ANIM_DELAY Seconds per GIF frame (default 0.2)
     GA_TERM_ANIM_MAX   Most frames kept in one GIF (default 300)
     GA_TERM_ANIM_SCALE Size of GIF frames relative to the page, 0.25 to 1
                        (default 1); 0.5 roughly halves the data
     GA_TERM_SYNC       1 waits for each picture to be written and sent
                        before going on, for scripts and tests
     GA_TERM_TMUX_STEP  Bytes handed to tmux before waiting for it to pass
                        them on; 0 for no waiting within a picture (default:
                        worked out from the tmux version and terminal size)
     GA_TERM_LOG        A file to log what tmux reported and how each
                        picture was sent, for tracking down display problems

   The picture size comes from the -g option ("-g 1200x900"), otherwise it
   is 1000 points along the longer side of the page. GIF frames are kept at
   that size; the still picture has GA_TERM_SCALE times as many pixels each
   way.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <math.h>
#include <strings.h>

#include <cairo.h>
#include <zlib.h>

#include "gatypes.h"
#include "gx.h"
#include "gxC.h"
#undef pi                                 /* gx.h names M_PI so; pi is a pane here */

#ifdef __APPLE__
#include <crt_externs.h>                    /* a dylib cannot reach environ itself */
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif

#define TERM_DEFAULT_SIZE 1000       /* points along the longer page side */
#define SEQ_LIMIT 1000000            /* iTerm2 and tmux drop control sequences
                                        over 1 MiB, so larger files go in parts */
#define SEQ_PART  65536              /* base64 characters in each part */
#define PACE_QUIET 0.025             /* seconds the link must stay clear */
#define STEP_QUIET 0.005             /* the same, between the parts of a picture */
#define STEP_MIN  1024               /* smallest step for old tmux, in bytes */
#define CC_PART   4096               /* base64 characters per part under tmux -CC */
#define PROGRESS_MIN 1048576         /* a picture this large always shows progress */
#define FRAMEQ    2                  /* frames waiting for the worker */

void gxdXflush (void);
void gxdidle (void);
void gxdintr (void);

static gaint batch=0;                       /* Batch mode? */
static gadouble xscl,yscl;                  /* Pixels per inch */
static gadouble xsize, ysize;               /* Page size in inches */
static gaint dblmode;                       /* single or double buffering */
static gaint width,height;                  /* Picture size in points */
static gadouble scale=2.0;                  /* Device pixels per point */
static cairo_surface_t *surface=NULL,*surface2=NULL; /* front and back pictures */
static char *ugeom = NULL;                  /* -g geometry string */

/* State of the visible picture; main thread only */
static gaint dirty=0;                       /* changed since it was last sent */
static gaint drawn=0;                       /* something drawn since the last clear */
static gaint backdrawn=0;                   /* drawn on the back buffer since the last swap */
static gaint ngif=0;                        /* frames queued for a GIF in this command */
static gaint gifcut=0;                      /* frames left out past GA_TERM_ANIM_MAX */
static volatile sig_atomic_t intr=0;        /* Ctrl-C interrupted the command */

/* Settings */
static char tdir[512];                      /* where the pictures go */
static gaint ownsdir=0;                     /* we created tdir, remove it at exit */
static char seqpath[600], seqtmp[600], fifopath[600];
static gaint mode=0;                        /* 1=tmux 2=inline 3=file */
static gaint anim=1;                        /* 0=off 1=live 2=gif */
static gaint animdelay=20;                  /* hundredths of a second per GIF frame */
static gaint animmax=300;                   /* most frames in one GIF */
static gadouble animscale=1.0;              /* GIF frame size relative to the page */
static gaint syncwrite=0;                   /* wait for each picture */
static gaint iterm=0;                       /* the terminal is iTerm2 */
static gaint progressopt=1;                 /* 0=off 1=auto 2=on */
static char pane[64];                       /* tmux pane id of the viewer */
static gaint panefd=-1;                     /* the viewer pane's terminal */
static gaint tmuxkeeps=-1;                  /* tmux never drops passthrough (3.3+):
                                               1 yes, 0 no, -1 not asked yet */
static gaint tmuxall=0;                     /* the pane has allow-passthrough all (3.4+) */
static gaint stepopt=-1;                    /* GA_TERM_TMUX_STEP, or -1 */

/* How a picture reaches the terminal */
#define VIA_TERM 0                          /* straight to it */
#define VIA_TMUX 1                          /* through tmux, in passthrough sequences */
#define VIA_CC   2                          /* through tmux -CC, as pane output */

/* ---- work handed to the worker thread ---- */

#define JOB_STILL    1                      /* a still picture; a newer one replaces it */
#define JOB_FRAME    2                      /* an animation frame; never skipped */
#define JOB_GIFFRAME 3                      /* add a frame to the looping GIF */
#define JOB_GIFEND   4                      /* finish the GIF and show it */
#define JOB_GIFDROP  5                      /* forget the GIF */

#define ISFRAME(k) ((k)==JOB_FRAME || (k)==JOB_GIFFRAME)

struct tjob {
  gaint kind;
  unsigned char *px;                        /* copy of the ARGB32 picture */
  gaint w,h,stride;                         /* its size in device pixels */
  gaint lw,lh;                              /* size in points, for GIF frames */
  gaint first;                              /* first frame of a new GIF */
  struct tjob *next;
};

static pthread_t worker;
static gaint workeron=0;
static pthread_mutex_t tlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t twake = PTHREAD_COND_INITIALIZER;  /* work for the worker */
static pthread_cond_t tdone = PTHREAD_COND_INITIALIZER;  /* the worker made progress */
static struct tjob *qhead=NULL,*qtail=NULL; /* work, in order */
static gaint qframes=0;                     /* frames in the queue */
static gaint busy=0;                        /* the worker is working */
static volatile gaint stopping=0;           /* finish the queue and exit */
static volatile gaint quitting=0;           /* GrADS is ending: write, don't send */
static gaint seq=0;                         /* pictures shown */
static unsigned char *lastpic=NULL;         /* and its contents */
static size_t lastpiclen=0;

/* Worker only: what the pane shows, and the link */
static gaint sentrows=0,sentcols=0;         /* pane size the picture was sent for */
static gaint panefresh=1;                   /* the pane needs clearing first */
static double slowuntil=0.0;                /* the link was slow until about then */
static gaint stepshift=0;                   /* steps halved this often after losses */
static gaint linkslow=0;                    /* the last picture mostly waited on the link */
static gaint posted=0;                      /* a picture was handed to the worker; main thread */

/* Problems the worker found, for the main thread to report once */
static volatile gaint warnplace=0;          /* tmux did not say where the pane is */
static volatile gaint warnlost=0;           /* tmux dropped a picture: 1 once, 2 again and again */
static gaint warnedplace=0,warnedlost=0;

static double now (void) {
struct timespec t;
  clock_gettime(CLOCK_MONOTONIC,&t);
  return (t.tv_sec + t.tv_nsec*1e-9);
}

/* GA_TERM_LOG */

static FILE *tlog=NULL;

static void tlogf (const char *fmt, ...) {
va_list ap;
  if (tlog==NULL) return;
  va_start(ap,fmt);
  fprintf(tlog,"%.3f ",now());
  vfprintf(tlog,fmt,ap);
  fputc('\n',tlog);
  fflush(tlog);
  va_end(ap);
}

/* Make a surface of the current picture size */

static cairo_surface_t *newsurface (void) {
cairo_surface_t *s;
  s = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
          (gaint)(width*scale+0.5), (gaint)(height*scale+0.5));
  cairo_surface_set_device_scale (s, scale, scale);
  return (s);
}

/* Quote a string for /bin/sh */

static void shquote (char *out, size_t len, const char *in) {
size_t n=0;
  if (len<3) { *out='\0'; return; }
  out[n++] = '\'';
  while (*in && n+5<len) {
    if (*in=='\'') { memcpy(out+n,"'\\''",4); n+=4; }
    else out[n++] = *in;
    in++;
  }
  out[n++] = '\'';
  out[n] = '\0';
}

static void cloexec (gaint fd) {
  if (fd>=0) fcntl(fd,F_SETFD,FD_CLOEXEC);
}

/* Run a command without a shell. Its first line of output goes into out.
   Returns the exit status, or -1 when it could not be run. posix_spawn,
   not fork, because the worker thread runs commands too. */

static gaint runcmd (char *const argv[], char *out, size_t len) {
gaint fd[2],status,rc;
pid_t pid;
ssize_t n;
size_t got=0;
char buf[256],*nl;
posix_spawn_file_actions_t fa;
posix_spawnattr_t at;
sigset_t none;

  if (out && len) *out = '\0';
  if (pipe(fd)) return (-1);
  cloexec(fd[0]);
  cloexec(fd[1]);
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa,0,"/dev/null",O_RDONLY,0);
  posix_spawn_file_actions_addopen(&fa,2,"/dev/null",O_WRONLY,0);
  posix_spawn_file_actions_adddup2(&fa,fd[1],1);
  posix_spawnattr_init(&at);
  sigemptyset(&none);                       /* the worker blocks every signal */
  posix_spawnattr_setsigmask(&at,&none);
  posix_spawnattr_setflags(&at,POSIX_SPAWN_SETSIGMASK);
  rc = posix_spawnp(&pid,argv[0],&fa,&at,argv,environ);
  posix_spawn_file_actions_destroy(&fa);
  posix_spawnattr_destroy(&at);
  close(fd[1]);
  if (rc) { close(fd[0]); return (-1); }
  while ((n=read(fd[0],buf,sizeof(buf)))!=0) {
    if (n<0) { if (errno==EINTR) continue; break; }
    if (out && got+1<len) {
      if ((size_t)n > len-1-got) n = len-1-got;
      memcpy(out+got,buf,n);
      got += n;
      out[got] = '\0';
    }
  }
  if (out && len && (nl=strchr(out,'\n'))!=NULL) *nl = '\0';
  close(fd[0]);
  while (waitpid(pid,&status,0)<0) if (errno!=EINTR) return (-1);
  return (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
}

/* Is the viewer script there to run? */

static char *viewer (void) {
char *v;
  v = getenv("GA_TERM_VIEWER");
  if (v==NULL || *v=='\0') return (NULL);
  if (access(v,X_OK)) return (NULL);
  return (v);
}

/* Where a tmux pane is on the client's screen, and the client's terminal */

struct paneinfo {
  gaint row,col;                            /* top left, 0-based, on the screen */
  gaint rows,cols;
  char ctty[256];                           /* the tmux client's terminal */
  gaint cw,ch;                              /* and its size; 0 when not known */
  gaint control;                            /* the client is iTerm2's tmux -CC */
  unsigned long lost;                       /* bytes tmux dropped for the client */
  unsigned long written;                    /* bytes tmux queued for the client */
  gaint haswritten;                         /* and tmux said so */
  gaint hidden;                             /* the pane is not on the client's screen */
};

/* Ask tmux about a pane and the client showing it. Fields that an older
   tmux does not know come back empty and read as 0. */

static gaint tmuxinfo (const char *target, struct paneinfo *pi) {
char out[700],*f[16],*p,*argv[8];
gaint i,n,status;

  memset(pi,0,sizeof(*pi));
  i = 0;
  argv[i++] = "tmux"; argv[i++] = "display-message"; argv[i++] = "-p";
  if (target && *target) { argv[i++] = "-t"; argv[i++] = (char *)target; }
  argv[i++] = "#{pane_left}|#{pane_top}|#{pane_width}|#{pane_height}|"
              "#{client_tty}|#{status-position}|#{status}|#{client_width}|"
              "#{client_height}|#{client_control_mode}|#{client_discarded}|"
              "#{client_written}|#{window_active}|#{window_zoomed_flag}|#{pane_active}";
  argv[i] = NULL;
  if (runcmd(argv,out,sizeof(out))) return (1);
  for (n=0, p=out; n<15; ) {                 /* fields may be empty */
    f[n++] = p;
    p = strchr(p,'|');
    if (p==NULL) break;
    *p++ = '\0';
  }
  if (n<7) return (1);
  for (i=n; i<15; i++) f[i] = "";
  pi->col = atoi(f[0]);
  pi->row = atoi(f[1]);
  pi->cols = atoi(f[2]);
  pi->rows = atoi(f[3]);
  snprintf(pi->ctty,sizeof(pi->ctty),"%s",f[4]);
  pi->cw = atoi(f[7]);
  pi->ch = atoi(f[8]);
  pi->control = atoi(f[9])==1;
  pi->lost = strtoul(f[10],NULL,10);
  pi->haswritten = f[11][0]!='\0';
  pi->written = strtoul(f[11],NULL,10);
  /* another window, or another pane zoomed over this one */
  pi->hidden = (f[12][0] && atoi(f[12])==0) || (atoi(f[13])==1 && atoi(f[14])==0);
  if (!strcmp(f[5],"top")) {                 /* the status line comes first */
    if (!strcmp(f[6],"on")) status = 1;
    else if (!strcmp(f[6],"off")) status = 0;
    else status = atoi(f[6]);
    pi->row += status;
  }
  return (pi->cols<=0 || pi->rows<=0);
}

/* Split a tmux pane off for the pictures */

static gaint tmuxsplit (void) {
char cmd[2048],qv[700],qd[700],size[32],pct[32],tty[256];
char *v,*p,*msg;
char *argv[16];
gaint i,rc;

  v = viewer();
  if (v==NULL) return (1);
  shquote(qv,sizeof(qv),v);
  shquote(qd,sizeof(qd),tdir);
  snprintf(cmd,sizeof(cmd),"exec %s --hold %s %d",qv,qd,(gaint)getpid());

  p = getenv("GA_TERM_PANE");
  if (p==NULL || *p=='\0') p = "50%";
  snprintf(size,sizeof(size),"%s",p);
  if (strchr(size,'%')==NULL) strncat(size,"%",sizeof(size)-strlen(size)-1);

  i = 0;
  argv[i++] = "tmux"; argv[i++] = "split-window"; argv[i++] = "-h";
  argv[i++] = "-d"; argv[i++] = "-P"; argv[i++] = "-F"; argv[i++] = "#{pane_id}";
  argv[i++] = "-l"; argv[i++] = size; argv[i++] = cmd; argv[i] = NULL;
  rc = runcmd(argv,pane,sizeof(pane));
  if (rc!=0 || pane[0]!='%') {
    /* tmux before 3.1 only understands a percentage given with -p */
    snprintf(pct,sizeof(pct),"%d",atoi(size));
    argv[7] = "-p"; argv[8] = pct;
    rc = runcmd(argv,pane,sizeof(pane));
  }
  if (rc!=0 || pane[0]!='%') {
    pane[0] = '\0';
    return (1);
  }

  /* the pictures go straight to the pane's terminal */
  i = 0;
  argv[i++] = "tmux"; argv[i++] = "display-message"; argv[i++] = "-p";
  argv[i++] = "-t"; argv[i++] = pane; argv[i++] = "#{pane_tty}"; argv[i] = NULL;
  if (runcmd(argv,tty,sizeof(tty)) || tty[0]!='/' ||
      (panefd=open(tty,O_WRONLY|O_NOCTTY))<0) {
    argv[0] = "tmux"; argv[1] = "kill-pane"; argv[2] = "-t";
    argv[3] = pane; argv[4] = NULL;
    runcmd(argv,NULL,0);
    pane[0] = '\0';
    return (1);
  }
  cloexec(panefd);

  /* Everything shown in the pane comes from here, in order; the viewer
     only keeps the pane open. */
  msg = "\033[?25l\033[H\033[2JWaiting for a GrADS picture...";
  if (write(panefd,msg,strlen(msg))<0) { /* the pictures will tell */ }

  /* tmux 3.3 and later drop passthrough sequences unless the pane allows
     them. Older tmux has no such option, and drops output, passthrough
     included, that the terminal cannot take fast enough. With "on", tmux
     also skips passthrough, without a word, whenever a redraw is waiting
     for the terminal to catch up, which on a slow link is often; "all"
     (tmux 3.4) passes it on regardless, and on screens that do not show
     the pane, so pictures are held back while the pane is hidden. */
  i = 0;
  argv[i++] = "tmux"; argv[i++] = "set-option"; argv[i++] = "-p";
  argv[i++] = "-t"; argv[i++] = pane; argv[i++] = "allow-passthrough";
  argv[i++] = "all"; argv[i] = NULL;
  tmuxall = runcmd(argv,NULL,0)==0;
  argv[6] = "on";
  tmuxkeeps = tmuxall || runcmd(argv,NULL,0)==0;
  return (0);
}

/* Does this tmux pass passthrough on, however slow the terminal? tmux 3.3
   stopped dropping it and is also the first with allow-passthrough, so
   asking for that option tells. */

static gaint tmuxprobe (const char *target) {
char *argv[8];
gaint i;
  i = 0;
  argv[i++] = "tmux"; argv[i++] = "show-options"; argv[i++] = "-p";
  if (target && *target) { argv[i++] = "-t"; argv[i++] = (char *)target; }
  argv[i++] = "allow-passthrough"; argv[i] = NULL;
  return (runcmd(argv,NULL,0)==0);
}

/* ---- which image protocol the terminal shows ---- */

/* A picture reaches the screen one of three ways, the first the terminal
   shows of:

     iTerm2   inline images, OSC 1337: iTerm2, WezTerm, mintty, VS Code
     kitty    the kitty graphics protocol: kitty, Ghostty
     sixel    DEC sixel: foot, mlterm, xterm -ti vt340, Windows Terminal

   In tmux, tmux says which terminal its client is in (client_termtype,
   from the terminal's own XTVERSION), whether it shows sixel
   (client_termfeatures), and from 3.5 whether tmux itself was built to
   draw sixel (sixel_support). Otherwise LC_TERMINAL,
   TERM_PROGRAM and TERM may say, or the terminal is asked: a kitty
   graphics query, XTVERSION, the cell size (CSI 16 t), and Primary Device
   Attributes, which every terminal answers, and last. GA_TERM_PROTOCOL
   names one instead. A terminal that shows none gets no -d Term. */

#define PROTO_NONE  0
#define PROTO_ITERM 1
#define PROTO_KITTY 2
#define PROTO_SIXEL 3

static const char *protonames[4] = {"none","iTerm2","kitty","sixel"};
static gaint proto=PROTO_ITERM;
static char protowhy[300];                  /* how it was found out */
static gaint cellw=0,cellh=0;               /* a character cell in pixels, if known */
static gaint tmuxsixel=0;                   /* tmux draws sixel in the pane itself */

static gaint hasword (const char *s, const char *w) {
size_t n;
  if (s==NULL) return (0);
  n = strlen(w);
  for (; *s; s++) if (!strncasecmp(s,w,n)) return (1);
  return (0);
}

/* What a terminal of this name (XTVERSION, TERM_PROGRAM, TERM) shows best;
   none when the name does not tell */

static gaint protofor (const char *name) {
  if (hasword(name,"iterm") || hasword(name,"wezterm") || hasword(name,"mintty"))
    return (PROTO_ITERM);
  if (hasword(name,"kitty") || hasword(name,"ghostty")) return (PROTO_KITTY);
  if (hasword(name,"foot") || hasword(name,"mlterm") || hasword(name,"contour"))
    return (PROTO_SIXEL);
  return (PROTO_NONE);
}

/* Ask the terminal. name gets its XTVERSION; kitty and sixel say whether
   it takes kitty graphics and shows sixel. Returns -1 when no answer came
   within 2 s, as over a very slow link or from no terminal at all. */

static gaint termask (char *name, size_t nlen, gaint *kitty, gaint *sixel) {
static const char q[] = "\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\"  /* kitty */
                        "\033[>0q"                                    /* XTVERSION */
                        "\033[16t"                                    /* cell size */
                        "\033[c";                                     /* DA1 */
struct termios old,raw;
char buf[2048],*p,*e;
gaint fd,n,got=0,done=0,a,b,c;
fd_set rd;
struct timeval tv;
double t0;
size_t k;

  *name = '\0'; *kitty = 0; *sixel = 0;
  fd = open("/dev/tty",O_RDWR|O_NOCTTY);
  if (fd<0) return (-1);
  if (fd>=FD_SETSIZE) { close(fd); return (-1); }
  if (tcgetattr(fd,&old)) { close(fd); return (-1); }
  raw = old;
  raw.c_lflag &= ~(ICANON|ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(fd,TCSANOW,&raw);
  if (write(fd,q,sizeof(q)-1)<0) done = 1;
  t0 = now();
  while (!done && now()-t0<2.0 && got<(gaint)sizeof(buf)-1) {
    FD_ZERO(&rd);
    FD_SET(fd,&rd);
    tv.tv_sec = 0;
    tv.tv_usec = 50000;
    if (select(fd+1,&rd,NULL,NULL,&tv)<=0) continue;
    n = read(fd,buf+got,sizeof(buf)-1-got);
    if (n<=0) continue;
    got += n;
    buf[got] = '\0';
    /* DA1, the last answer: ESC [ ? digits and ; ... c */
    for (p=buf; (p=strstr(p,"\033[?"))!=NULL; p++) {
      for (e=p+3; (*e>='0' && *e<='9') || *e==';'; e++) ;
      if (*e=='c') { done = 1; break; }
    }
  }
  tcsetattr(fd,TCSANOW,&old);
  close(fd);
  buf[got] = '\0';
  tlogf("terminal answered %d bytes in %.3f s",got,now()-t0);
  if (got==0) return (-1);

  if (strstr(buf,"\033_Gi=31;OK")) *kitty = 1;
  if ((p=strstr(buf,"\033P>|"))!=NULL) {
    p += 4;
    for (k=0; *p && *p!='\033' && k+1<nlen; k++) name[k] = *p++;
    name[k] = '\0';
  }
  if ((p=strstr(buf,"\033[6;"))!=NULL && sscanf(p+4,"%d;%d",&a,&b)==2 && a>0 && b>0) {
    cellh = a;
    cellw = b;
  }
  for (p=buf; (p=strstr(p,"\033[?"))!=NULL; p++) {
    for (e=p+3, c=-1; (*e>='0' && *e<='9') || *e==';'; e++) {
      if (*e>='0' && *e<='9') c = (c<0 ? 0 : c*10) + (*e-'0');
      else { if (c==4) *sixel = 1; c = -1; }
    }
    if (*e=='c') { if (c==4) *sixel = 1; break; }
  }
  return (0);
}

/* The cell size from the kernel, when the terminal has told it */

static void cellsize (gaint fd) {
struct winsize ws;
  if (cellw>0 && cellh>0) return;
  if (ioctl(fd,TIOCGWINSZ,&ws)==0 && ws.ws_col>0 && ws.ws_row>0 &&
      ws.ws_xpixel>0 && ws.ws_ypixel>0) {
    cellw = ws.ws_xpixel/ws.ws_col;
    cellh = ws.ws_ypixel/ws.ws_row;
  }
}

static void termproto (void) {
char out[700],*f[8],*p,*a,*t,*argv[8],name[256];
gaint i,n,kitty,sixel,envp,rc,named=PROTO_NONE;

  tmuxsixel = 0;
  a = getenv("GA_TERM_PROTOCOL");
  if (a && *a && strcasecmp(a,"auto")) {
    if (!strcasecmp(a,"iterm2") || !strcasecmp(a,"iterm")) named = PROTO_ITERM;
    else if (!strcasecmp(a,"kitty")) named = PROTO_KITTY;
    else if (!strcasecmp(a,"sixel")) named = PROTO_SIXEL;
    else printf("Terminal display: unknown GA_TERM_PROTOCOL \"%s\"; use iterm2, kitty, or sixel\n",a);
  }

  t = getenv("TMUX");
  if (t && *t) {
    i = 0;
    argv[i++] = "tmux"; argv[i++] = "display-message"; argv[i++] = "-p";
    if ((p=getenv("TMUX_PANE"))!=NULL && *p) { argv[i++] = "-t"; argv[i++] = p; }
    argv[i++] = "#{client_control_mode}|#{client_termtype}|#{client_termname}|"
                "#{client_termfeatures}|#{client_cell_width}|#{client_cell_height}|"
                "#{sixel_support}";
    argv[i] = NULL;
    /* In a session just started, its client may not be attached yet, or
       its terminal not have answered tmux: give them a second */
    for (rc=0; rc<6; rc++) {
      if (rc) usleep(200000);
      out[0] = '\0';
      runcmd(argv,out,sizeof(out));
      for (n=0, p=out; n<7; ) {
        f[n++] = p;
        p = strchr(p,'|');
        if (p==NULL) break;
        *p++ = '\0';
      }
      for (i=n; i<7; i++) f[i] = "";
      a = getenv("LC_TERMINAL");
      if (*f[1] || atoi(f[0])==1 || named!=PROTO_NONE || (a && !strcmp(a,"iTerm2"))) break;
    }
    cellw = atoi(f[4]);
    cellh = atoi(f[5]);
    a = getenv("LC_TERMINAL");
    proto = protofor(f[1]);
    if (atoi(f[0])==1) {
      proto = PROTO_ITERM;
      snprintf(protowhy,sizeof(protowhy),"tmux -CC, so iTerm2");
    } else if (proto==PROTO_ITERM || proto==PROTO_KITTY) {
      snprintf(protowhy,sizeof(protowhy),"tmux says the terminal is %s",f[1]);
    } else if (hasword(f[3],"sixel")) {     /* the terminal shows sixel */
      proto = PROTO_SIXEL;
      tmuxsixel = atoi(f[6])==1;           /* and tmux can keep it with the pane */
      snprintf(protowhy,sizeof(protowhy),"%s sixel for %s",tmuxsixel ? "tmux draws" : "tmux passes on",
               *f[1] ? f[1] : f[2]);
    } else if (proto==PROTO_SIXEL) {
      snprintf(protowhy,sizeof(protowhy),"tmux says the terminal is %s",f[1]);
    } else if (a && !strcmp(a,"iTerm2")) {
      proto = PROTO_ITERM;
      snprintf(protowhy,sizeof(protowhy),"LC_TERMINAL=iTerm2");
    } else if ((proto=protofor(f[2]))!=PROTO_NONE) {
      snprintf(protowhy,sizeof(protowhy),"tmux client TERM=%s",f[2]);
    } else {
      proto = PROTO_NONE;
      snprintf(protowhy,sizeof(protowhy),"tmux knows the terminal only as %s%sTERM=%s",
               f[1],*f[1] ? ", " : "",*f[2] ? f[2] : "?");
    }
    if (hasword(f[1],"iterm2") || (a && !strcmp(a,"iTerm2")) || atoi(f[0])==1) iterm = 1;
    if (named!=PROTO_NONE) {                 /* tmux still gave the cell size */
      proto = named;
      tmuxsixel = named==PROTO_SIXEL && hasword(f[3],"sixel") && atoi(f[6])==1;
      snprintf(protowhy,sizeof(protowhy),"GA_TERM_PROTOCOL=%s",getenv("GA_TERM_PROTOCOL"));
    }
    return;
  }
  if (named!=PROTO_NONE) {
    proto = named;
    snprintf(protowhy,sizeof(protowhy),"GA_TERM_PROTOCOL=%s",getenv("GA_TERM_PROTOCOL"));
    cellsize(1);
    return;
  }

  /* outside tmux: what the environment says, then the terminal */
  envp = PROTO_NONE;
  a = getenv("LC_TERMINAL");
  p = getenv("TERM_PROGRAM");
  t = getenv("TERM");
  if (a && !strcmp(a,"iTerm2")) {
    envp = PROTO_ITERM;
    snprintf(protowhy,sizeof(protowhy),"LC_TERMINAL=iTerm2");
  } else if ((envp=protofor(p))!=PROTO_NONE) {
    snprintf(protowhy,sizeof(protowhy),"TERM_PROGRAM=%s",p);
  } else if ((envp=protofor(t))!=PROTO_NONE) {
    snprintf(protowhy,sizeof(protowhy),"TERM=%s",t);
  } else if (getenv("KITTY_WINDOW_ID")) {
    envp = PROTO_KITTY;
    snprintf(protowhy,sizeof(protowhy),"KITTY_WINDOW_ID");
  }
  if (hasword(p,"iterm") || (a && !strcmp(a,"iTerm2"))) iterm = 1;
  if (envp==PROTO_ITERM) {                  /* nothing to ask */
    proto = envp;
    return;
  }
  if (!isatty(1) || (t && (!strcmp(t,"dumb") || !strcmp(t,"linux")))) {
    /* pictures go to a file or pipe, or to a terminal that cannot be
       asked: as iTerm2 inline images, unless the environment said */
    proto = envp!=PROTO_NONE ? envp : (isatty(1) ? PROTO_NONE : PROTO_ITERM);
    if (envp==PROTO_NONE)
      snprintf(protowhy,sizeof(protowhy),isatty(1) ? "TERM=%s" : "output is no terminal",
               t ? t : "");
    cellsize(1);
    return;
  }
  rc = termask(name,sizeof(name),&kitty,&sixel);
  cellsize(1);
  if (rc<0) {
    proto = envp;
    if (envp==PROTO_NONE) snprintf(protowhy,sizeof(protowhy),"the terminal did not answer");
    return;
  }
  if (hasword(name,"iterm2")) iterm = 1;
  if (protofor(name)==PROTO_ITERM || (hasword(name,"xterm.js") && sixel)) {
    proto = PROTO_ITERM;
    snprintf(protowhy,sizeof(protowhy),"the terminal is %s",name);
  } else if (kitty) {                       /* the answer, rather than TERM */
    proto = PROTO_KITTY;
    snprintf(protowhy,sizeof(protowhy),"%s takes kitty graphics",*name ? name : "the terminal");
  } else if (sixel) {
    proto = PROTO_SIXEL;
    snprintf(protowhy,sizeof(protowhy),"%s shows sixel",*name ? name : "the terminal");
  } else {
    proto = PROTO_NONE;
    snprintf(protowhy,sizeof(protowhy),"%s shows neither sixel nor kitty graphics",
             *name ? name : "the terminal");
  }
}

/* ---- PNG ---- */

struct mbuf {
  unsigned char *p;
  size_t n,cap;
  gaint failed;
};

static void mput (struct mbuf *b, const void *d, size_t n) {
unsigned char *np;
size_t nc;
  if (b->failed) return;
  if (b->n+n > b->cap) {
    nc = b->cap ? b->cap : 262144;
    while (nc < b->n+n) nc *= 2;
    np = (unsigned char *)realloc(b->p,nc);
    if (np==NULL) { b->failed = 1; return; }
    b->p = np;
    b->cap = nc;
  }
  memcpy(b->p+b->n,d,n);
  b->n += n;
}

static void be32 (unsigned char *p, unsigned long v) {
  p[0] = (v>>24)&255; p[1] = (v>>16)&255; p[2] = (v>>8)&255; p[3] = v&255;
}

static void pngchunk (struct mbuf *b, const char *type, const unsigned char *data, size_t len) {
unsigned char hdr[8],crc[4];
uLong c;
  be32(hdr,(unsigned long)len);
  memcpy(hdr+4,type,4);
  c = crc32(0L,(const Bytef *)type,4);
  if (len) c = crc32(c,data,(uInt)len);
  be32(crc,c);
  mput(b,hdr,8);
  if (len) mput(b,data,len);
  mput(b,crc,4);
}

/* Encode an ARGB32 picture (or with stride 0, packed RGB) as an RGB PNG.
   Rows are not filtered: for plots, which are mostly flat colour, that is
   both faster and smaller than letting the encoder try every filter, as
   cairo_surface_write_to_png does. */

static gaint pngencode (const unsigned char *px, gaint w, gaint h, gaint stride, struct mbuf *b) {
static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
unsigned char ihdr[13],*row,*out;
const unsigned int *p;
z_stream zs;
gaint x,y,zrc,err=0;
const size_t outsz = 65536;

  row = (unsigned char *)malloc(1+3*(size_t)w);
  out = (unsigned char *)malloc(outsz);
  memset(&zs,0,sizeof(zs));
  if (row==NULL || out==NULL || deflateInit(&zs,6)!=Z_OK) {
    free(row); free(out);
    return (1);
  }
  be32(ihdr,w); be32(ihdr+4,h);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  mput(b,sig,8);
  pngchunk(b,"IHDR",ihdr,13);

  zs.next_out = out;
  zs.avail_out = outsz;
  for (y=0; y<=h && !err; y++) {
    if (y<h) {
      row[0] = 0;
      if (stride==0) memcpy(row+1,px+(size_t)y*3*w,3*(size_t)w);   /* already RGB */
      else {
        p = (const unsigned int *)(px + (size_t)y*stride);
        for (x=0; x<w; x++) {
          row[1+3*x] = (p[x]>>16)&255;
          row[2+3*x] = (p[x]>>8)&255;
          row[3+3*x] = p[x]&255;
        }
      }
      zs.next_in = row;
      zs.avail_in = 1+3*w;
    }
    do {
      zrc = deflate(&zs, y<h ? Z_NO_FLUSH : Z_FINISH);
      if (zrc==Z_STREAM_ERROR) { err = 1; break; }
      if (zs.avail_out==0 || (y==h && zrc==Z_STREAM_END)) {
        pngchunk(b,"IDAT",out,outsz-zs.avail_out);
        zs.next_out = out;
        zs.avail_out = outsz;
      }
    } while (y<h ? zs.avail_in>0 : zrc!=Z_STREAM_END);
  }
  deflateEnd(&zs);
  pngchunk(b,"IEND",NULL,0);
  free(row);
  free(out);
  return (err || b->failed);
}

/* ---- animated GIF; worker thread only ---- */

static struct {
  unsigned char *buf;                       /* the GIF so far */
  size_t len,cap;
  unsigned char *prev;                      /* previous frame, RGB */
  gaint w,h;                                /* frame size */
  gaint frames;
  size_t delayat;                           /* where the last frame's delay is */
  gaint failed;
} gif;

static void gput (const void *p, size_t n) {
unsigned char *nb;
size_t nc;
  if (gif.failed) return;
  if (gif.len+n > gif.cap) {
    nc = gif.cap ? gif.cap : 65536;
    while (nc < gif.len+n) nc *= 2;
    nb = (unsigned char *)realloc(gif.buf,nc);
    if (nb==NULL) { gif.failed = 1; return; }
    gif.buf = nb;
    gif.cap = nc;
  }
  memcpy(gif.buf+gif.len,p,n);
  gif.len += n;
}

static void gbyte (gaint b) {
unsigned char c = (unsigned char)b;
  gput(&c,1);
}

static void gword (gaint v) {
  gbyte(v&255);
  gbyte((v>>8)&255);
}

static void gifreset (void) {
  free(gif.buf);
  free(gif.prev);
  memset(&gif,0,sizeof(gif));
}

/* Colour reduction. Colours are counted in 6-bit-per-channel bins; the
   palette takes the most common bins, skipping ones too close to a colour
   already chosen so that small features in a colour of their own (a marker,
   a line) are not swallowed by the shades of anti-aliased edges. */

#define QBINS (1<<18)
static unsigned int *qcount=NULL, *qsum=NULL;
static short *qmap=NULL;
static gaint *qused=NULL;

static gaint qbycount (const void *a, const void *b) {
unsigned int ca = qcount[*(const gaint *)a], cb = qcount[*(const gaint *)b];
  return (ca<cb) - (ca>cb);
}

static gaint quantize (const unsigned char *rgb, gaint w, gaint x0, gaint y0, gaint bw, gaint bh,
                       gaint most, unsigned char *pal, unsigned char *idx) {
gaint x,y,i,j,k,nused=0,npal=0,best,pass;
long d,bd,dr,dg,db;
const unsigned char *p;
unsigned int key,n;

  if (qcount==NULL) {
    qcount = (unsigned int *)calloc(QBINS,sizeof(unsigned int));
    qsum = (unsigned int *)calloc(3*(size_t)QBINS,sizeof(unsigned int));
    qmap = (short *)malloc(QBINS*sizeof(short));
    qused = (gaint *)malloc(QBINS*sizeof(gaint));
    if (!qcount || !qsum || !qmap || !qused) return (1);
  }
  for (y=y0; y<y0+bh; y++) {
    p = rgb + 3*((size_t)y*w + x0);
    for (x=0; x<bw; x++, p+=3) {
      key = ((unsigned int)(p[0]>>2)<<12) | ((unsigned int)(p[1]>>2)<<6) | (p[2]>>2);
      if (qcount[key]++==0) qused[nused++] = key;
      qsum[3*key] += p[0]; qsum[3*key+1] += p[1]; qsum[3*key+2] += p[2];
    }
  }
  memset(pal,0,768);
  if (nused<=most) {
    for (i=0; i<nused; i++) {
      key = qused[i]; n = qcount[key];
      pal[3*i] = qsum[3*key]/n; pal[3*i+1] = qsum[3*key+1]/n; pal[3*i+2] = qsum[3*key+2]/n;
      qmap[key] = i;
    }
  } else {
    qsort(qused,nused,sizeof(gaint),qbycount);
    for (pass=0; pass<2 && npal<most; pass++) {
      for (i=0; i<nused && npal<most; i++) {
        key = qused[i];
        if (qcount[key]==0) continue;          /* taken in the first pass */
        n = qcount[key];
        dr = qsum[3*key]/n; dg = qsum[3*key+1]/n; db = qsum[3*key+2]/n;
        if (pass==0) {
          for (j=0; j<npal; j++) {
            d = (dr-pal[3*j])*(dr-pal[3*j]) + (dg-pal[3*j+1])*(dg-pal[3*j+1])
              + (db-pal[3*j+2])*(db-pal[3*j+2]);
            if (d<300) break;
          }
          if (j<npal) continue;
        }
        pal[3*npal] = dr; pal[3*npal+1] = dg; pal[3*npal+2] = db;
        npal++;
        qsum[3*key] = dr; qsum[3*key+1] = dg; qsum[3*key+2] = db;  /* now the average */
        qcount[key] = 0;                       /* mark as taken */
      }
    }
    /* every bin maps to its nearest palette colour */
    for (i=0; i<nused; i++) {
      key = qused[i];
      n = qcount[key];
      if (n) { dr = qsum[3*key]/n; dg = qsum[3*key+1]/n; db = qsum[3*key+2]/n; }
      else   { dr = qsum[3*key];   dg = qsum[3*key+1];   db = qsum[3*key+2]; }
      best = 0; bd = -1;
      for (k=0; k<npal; k++) {
        d = (dr-pal[3*k])*(dr-pal[3*k]) + (dg-pal[3*k+1])*(dg-pal[3*k+1])
          + (db-pal[3*k+2])*(db-pal[3*k+2]);
        if (bd<0 || d<bd) { bd = d; best = k; if (d==0) break; }
      }
      qmap[key] = best;
    }
  }
  for (y=y0, i=0; y<y0+bh; y++) {
    p = rgb + 3*((size_t)y*w + x0);
    for (x=0; x<bw; x++, p+=3) {
      key = ((unsigned int)(p[0]>>2)<<12) | ((unsigned int)(p[1]>>2)<<6) | (p[2]>>2);
      idx[i++] = (unsigned char)qmap[key];
    }
  }
  for (i=0; i<nused; i++) {
    key = qused[i];
    qcount[key] = 0;
    qsum[3*key] = qsum[3*key+1] = qsum[3*key+2] = 0;
  }
  return (0);
}

/* GIF's LZW, 8-bit codes, written as data sub-blocks */

static struct {
  unsigned long acc;
  gaint nbits,cs;
  unsigned char blk[256];
  gaint blen;
} lz;

static void lzflush (void) {
  if (lz.blen) {
    lz.blk[0] = lz.blen;
    gput(lz.blk,lz.blen+1);
    lz.blen = 0;
  }
}

static void lzput (gaint code) {
  lz.acc |= (unsigned long)code << lz.nbits;
  lz.nbits += lz.cs;
  while (lz.nbits>=8) {
    lz.blk[1+lz.blen++] = lz.acc&255;
    lz.acc >>= 8;
    lz.nbits -= 8;
    if (lz.blen==255) lzflush();
  }
}

#define LZHASH 8192
static gaint lzkey[LZHASH];                 /* key+1, 0 when empty */
static short lzval[LZHASH];

static void lzw (const unsigned char *idx, size_t n) {
size_t i;
gaint next,prefix,key,h;

  memset(&lz,0,sizeof(lz));
  memset(lzkey,0,sizeof(lzkey));
  gbyte(8);                                 /* minimum code size */
  lz.cs = 9;
  next = 258;
  lzput(256);                               /* clear */
  if (n) {
    prefix = idx[0];
    for (i=1; i<n; i++) {
      key = (prefix<<8) | idx[i];
      h = (gaint)(((unsigned int)key*2654435761u)>>19) & (LZHASH-1);
      while (lzkey[h] && lzkey[h]!=key+1) h = (h+1)&(LZHASH-1);
      if (lzkey[h]) { prefix = lzval[h]; continue; }
      lzput(prefix);
      if (next<4096) {
        lzkey[h] = key+1;
        lzval[h] = next++;
        if (next > (1<<lz.cs) && lz.cs<12) lz.cs++;
      } else {
        lzput(256);                         /* table full: start again */
        memset(lzkey,0,sizeof(lzkey));
        lz.cs = 9;
        next = 258;
      }
      prefix = idx[i];
    }
    lzput(prefix);
  }
  lzput(257);                               /* end of information */
  if (lz.nbits) {                           /* the last, partial byte */
    lz.blk[1+lz.blen++] = lz.acc&255;
    lz.acc = 0;
    lz.nbits = 0;
    if (lz.blen==255) lzflush();
  }
  lzflush();
  gbyte(0);                                 /* block terminator */
}

/* Add a frame. Only the rectangle that changed since the previous frame is
   stored; an unchanged frame just lengthens the previous one. */

static void gifframe (const unsigned char *rgb, gaint w, gaint h) {
gaint x,y,x0,y0,x1,y1,bw,bh,d;
const unsigned char *a,*b;
unsigned char pal[768],*idx;

  if (gif.failed) return;
  if (gif.frames==0) {
    gif.w = w; gif.h = h;
    gput("GIF89a",6);
    gword(w); gword(h);
    gbyte(0x70); gbyte(0); gbyte(0);        /* no global colour table */
    gput("\041\377\013NETSCAPE2.0\003\001\000\000\000",19);   /* loop forever */
    gif.prev = (unsigned char *)malloc(3*(size_t)w*h);
    if (gif.prev==NULL) { gif.failed = 1; return; }
    x0 = 0; y0 = 0; x1 = w-1; y1 = h-1;
  } else {
    if (w!=gif.w || h!=gif.h) return;       /* the page was resized mid-way */
    x0 = w; y0 = h; x1 = -1; y1 = -1;
    for (y=0; y<h; y++) {
      a = rgb + 3*(size_t)y*w;
      b = gif.prev + 3*(size_t)y*w;
      if (!memcmp(a,b,3*(size_t)w)) continue;
      if (y<y0) y0 = y;
      y1 = y;
      for (x=0; x<w; x++) {
        if (a[3*x]!=b[3*x] || a[3*x+1]!=b[3*x+1] || a[3*x+2]!=b[3*x+2]) {
          if (x<x0) x0 = x;
          if (x>x1) x1 = x;
        }
      }
    }
    if (x1<0) {                             /* nothing changed */
      d = gif.buf[gif.delayat] | (gif.buf[gif.delayat+1]<<8);
      d += animdelay;
      if (d>65535) d = 65535;
      gif.buf[gif.delayat] = d&255;
      gif.buf[gif.delayat+1] = (d>>8)&255;
      return;
    }
  }
  bw = x1-x0+1;
  bh = y1-y0+1;
  idx = (unsigned char *)malloc((size_t)bw*bh);
  if (idx==NULL || quantize(rgb,w,x0,y0,bw,bh,256,pal,idx)) {
    free(idx);
    gif.failed = 1;
    return;
  }
  gput("\041\371\004\004",4);               /* graphic control: keep previous */
  gif.delayat = gif.len;
  gword(animdelay);
  gbyte(0); gbyte(0);
  gbyte(0x2c);                              /* image descriptor */
  gword(x0); gword(y0); gword(bw); gword(bh);
  gbyte(0x87);                              /* local colour table of 256 */
  gput(pal,768);
  lzw(idx,(size_t)bw*bh);
  free(idx);
  memcpy(gif.prev,rgb,3*(size_t)w*h);
  gif.frames++;
}

/* Shrink an ARGB32 picture to w x h RGB by averaging */

static unsigned char *downsample (const struct tjob *j, gaint w, gaint h) {
unsigned char *out,*o;
const unsigned int *p;
gaint x,y,sx,sy,sx0,sx1,sy0,sy1;
unsigned long r,g,b,n;

  out = (unsigned char *)malloc(3*(size_t)w*h);
  if (out==NULL) return (NULL);
  o = out;
  for (y=0; y<h; y++) {
    sy0 = (gaint)((long)y*j->h/h);
    sy1 = (gaint)((long)(y+1)*j->h/h);
    if (sy1<=sy0) sy1 = sy0+1;
    for (x=0; x<w; x++) {
      sx0 = (gaint)((long)x*j->w/w);
      sx1 = (gaint)((long)(x+1)*j->w/w);
      if (sx1<=sx0) sx1 = sx0+1;
      r = g = b = n = 0;
      for (sy=sy0; sy<sy1; sy++) {
        p = (const unsigned int *)(j->px + (size_t)sy*j->stride);
        for (sx=sx0; sx<sx1; sx++) {
          r += (p[sx]>>16)&255; g += (p[sx]>>8)&255; b += p[sx]&255; n++;
        }
      }
      *o++ = r/n; *o++ = g/n; *o++ = b/n;
    }
  }
  return (out);
}

/* ---- sixel ---- */

/* An RGB picture at another size: each new pixel the average of the old
   ones it covers when smaller, the nearest old one when larger */

static unsigned char *rgbscale (const unsigned char *src, gaint w, gaint h, gaint w2, gaint h2) {
unsigned char *dst,*o;
gaint x,y,x0,x1,y0,y1,i,j,n;
unsigned long sum[3];
const unsigned char *q;

  dst = (unsigned char *)malloc((size_t)w2*h2*3);
  if (dst==NULL) return (NULL);
  o = dst;
  for (y=0; y<h2; y++) {
    y0 = (gaint)((gadouble)y*h/h2);
    y1 = (gaint)((gadouble)(y+1)*h/h2);
    if (y1<=y0) y1 = y0+1;
    if (y1>h) y1 = h;
    for (x=0; x<w2; x++) {
      x0 = (gaint)((gadouble)x*w/w2);
      x1 = (gaint)((gadouble)(x+1)*w/w2);
      if (x1<=x0) x1 = x0+1;
      if (x1>w) x1 = w;
      sum[0] = sum[1] = sum[2] = 0;
      n = 0;
      for (j=y0; j<y1; j++) {
        q = src + ((size_t)j*w+x0)*3;
        for (i=x0; i<x1; i++, q+=3, n++) {
          sum[0] += q[0]; sum[1] += q[1]; sum[2] += q[2];
        }
      }
      *o++ = (unsigned char)(sum[0]/n);
      *o++ = (unsigned char)(sum[1]/n);
      *o++ = (unsigned char)(sum[2]/n);
    }
  }
  return (dst);
}

/* An RGB picture as sixel, into b: in up to most (256 at most) colours,
   chosen as for a GIF frame. Each band of six rows goes colour by colour,
   with runs compressed. */

static gaint sixelencode (const unsigned char *rgb, gaint w, gaint h, gaint most, struct mbuf *b) {
unsigned char *idx,*used,pal[768];
gaint ncol=0,i,x,y,y0,c,run,bits,prev;
char tmp[64];

  idx = (unsigned char *)malloc((size_t)w*h);
  used = (unsigned char *)malloc(256);
  if (idx==NULL || used==NULL || quantize(rgb,w,0,0,w,h,most,pal,idx)) {
    free(idx); free(used);
    return (1);
  }
  for (i=0; i<w*h; i++) if (idx[i]>=ncol) ncol = idx[i]+1;

  mput(b,"\033P0;1;0q",8);
  snprintf(tmp,sizeof(tmp),"\"1;1;%d;%d",w,h);
  mput(b,tmp,strlen(tmp));
  for (i=0; i<ncol; i++) {
    snprintf(tmp,sizeof(tmp),"#%d;2;%d;%d;%d",i,(pal[3*i]*100+127)/255,
             (pal[3*i+1]*100+127)/255,(pal[3*i+2]*100+127)/255);
    mput(b,tmp,strlen(tmp));
  }
  for (y0=0; y0<h; y0+=6) {
    memset(used,0,256);
    for (y=y0; y<y0+6 && y<h; y++)
      for (x=0; x<w; x++) used[idx[(size_t)y*w+x]] = 1;
    for (c=0; c<ncol; c++) {
      if (!used[c]) continue;
      snprintf(tmp,sizeof(tmp),"#%d",c);
      mput(b,tmp,strlen(tmp));
      prev = -1;
      run = 0;
      for (x=0; x<=w; x++) {
        bits = -1;
        if (x<w) {
          bits = 0;
          for (y=y0; y<y0+6 && y<h; y++)
            if (idx[(size_t)y*w+x]==c) bits |= 1<<(y-y0);
        }
        if (bits==prev) { run++; continue; }
        if (bits<0 && prev==0) break;           /* nothing more in this colour */
        if (run>3) {
          snprintf(tmp,sizeof(tmp),"!%d%c",run,63+prev);
          mput(b,tmp,strlen(tmp));
        } else {
          for (i=0; i<run; i++) { tmp[i] = (char)(63+prev); }
          if (run) mput(b,tmp,run);
        }
        prev = bits;
        run = 1;
      }
      mput(b,"$",1);
    }
    mput(b,"-",1);
  }
  mput(b,"\033\\",2);
  free(idx);
  free(used);
  return (b->failed);
}

/* ---- sending pictures to the terminal ---- */

struct wout {
  gaint fd;                                 /* where the bytes go */
  gaint pacefd;                             /* the terminal to pace against, or -1 */
  gaint via;                                /* VIA_TERM, VIA_TMUX or VIA_CC */
  size_t step;                              /* bytes to hand tmux before waiting, or 0 */
  size_t since;                             /* bytes handed over since the last wait */
  gaint steps;                              /* waits within the picture */
  double t0;                                /* when the picture started to go out */
  unsigned char buf[16384];
  size_t n;
  double waited;                            /* time spent waiting on the link */
  gaint err;
};

static void wflush (struct wout *w) {
size_t off=0;
ssize_t r;
double t0;

  t0 = now();
  while (off<w->n && !w->err) {
    r = write(w->fd,w->buf+off,w->n-off);
    if (r<0) {
      if (errno==EINTR) continue;
      if (errno==EAGAIN) { usleep(2000); continue; }
      w->err = 1;
      break;
    }
    off += r;
  }
  w->waited += now()-t0;
  w->n = 0;
}

/* tmux keeps everything it is given and sends it on as fast as the link
   allows. Before a picture, wait until tmux has passed on what it holds,
   so that pictures queue here, where Ctrl-C can drop them: the tmux
   client's terminal then has room, and keeps it. While tmux still has
   more to send it fills that room again within a millisecond or two, so
   the room has to last a while (quiet) to count. */

static void pace (struct wout *w, double quiet) {
fd_set wr;
struct timeval tv;
double t0,clear=-1.0,t;
gaint r;

  w->since = 0;
  if (w->pacefd<0 || w->pacefd>=FD_SETSIZE) return;
  t0 = now();
  while (!stopping) {
    /* select, not poll: macOS poll does not take terminals */
    FD_ZERO(&wr);
    FD_SET(w->pacefd,&wr);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    r = select(w->pacefd+1,NULL,&wr,NULL,&tv);
    if (r<0) {
      if (errno==EINTR) continue;
      break;
    }
    t = now();
    if (r>0 && FD_ISSET(w->pacefd,&wr)) {
      if (clear<0) clear = t;
      if (t-clear >= quiet) break;
    } else clear = -1.0;
    if (t-t0 > 30.0) break;                 /* never hang on a stuck link */
    usleep(quiet<0.01 ? 1000 : 2500);
  }
  w->waited += now()-t0-quiet;
}

/* Older tmux drops what it holds for a client once that is too much, so
   hand it no more than a step at a time: n more bytes are coming. Once
   the link turns out to be what holds the picture up, tmux is holding on
   to more, so only half a step. */

static void room (struct wout *w, size_t n) {
size_t lim;
double t;
  if (quitting) w->err = 1;                 /* the pane is going: stop here */
  if (w->step==0 || w->since==0) return;
  lim = w->step;
  t = now()-w->t0;
  if (linkslow || (t>0.1 && w->waited>0.5*t)) lim = w->step/2;
  if (w->since+n > lim) {
    wflush(w);
    pace(w,STEP_QUIET);
    w->steps++;
  }
}

static void wraw (struct wout *w, const void *p, size_t n) {
const unsigned char *s = (const unsigned char *)p;
size_t k;
  w->since += n;
  while (n && !w->err) {
    k = sizeof(w->buf)-w->n;
    if (k>n) k = n;
    memcpy(w->buf+w->n,s,k);
    w->n += k; s += k; n -= k;
    if (w->n==sizeof(w->buf)) wflush(w);
  }
}

static void wstr (struct wout *w, const char *s) {
  wraw(w,s,strlen(s));
}

/* An escape sequence for the outer terminal. Through tmux it travels in a
   passthrough sequence, where each ESC is doubled; tmux -CC hands the
   pane's output to iTerm2 as it is. */
static void wescn (struct wout *w, const char *s, size_t n) {
  if (w->via!=VIA_TMUX) { wraw(w,s,n); return; }
  for (; n; s++, n--) {
    if (*s=='\033') wraw(w,"\033\033",2);
    else wraw(w,s,1);
  }
}

static void wesc (struct wout *w, const char *s) {
  wescn(w,s,strlen(s));
}

static void ptopen (struct wout *w) {
  if (w->via==VIA_TMUX) wstr(w,"\033Ptmux;");
}

static void ptclose (struct wout *w) {
  if (w->via==VIA_TMUX) wstr(w,"\033\\");
}

static void b64put (struct wout *w, const unsigned char *d, size_t len) {
static const char tab[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
char out[4096];
size_t i,o=0;
unsigned long v;
  for (i=0; i+2<len; i+=3) {
    v = ((unsigned long)d[i]<<16) | ((unsigned long)d[i+1]<<8) | d[i+2];
    out[o++] = tab[(v>>18)&63]; out[o++] = tab[(v>>12)&63];
    out[o++] = tab[(v>>6)&63];  out[o++] = tab[v&63];
    if (o==sizeof(out)) { wraw(w,out,o); o = 0; }
  }
  if (i<len) {
    v = (unsigned long)d[i]<<16;
    if (i+1<len) v |= (unsigned long)d[i+1]<<8;
    out[o++] = tab[(v>>18)&63]; out[o++] = tab[(v>>12)&63];
    out[o++] = i+1<len ? tab[(v>>6)&63] : '=';
    out[o++] = '=';
  }
  wraw(w,out,o);
}

/* Columns for a picture at the cursor (inline): GA_TERM_WIDTH as a
   percentage of the terminal's width, or a number of columns */

static gaint inlinecols (gaint fd, const char *iwidth) {
struct winsize ws;
gaint cols=80,n;
  if (ioctl(fd,TIOCGWINSZ,&ws)==0 && ws.ws_col>0) cols = ws.ws_col;
  n = atoi(iwidth);
  if (n<=0) return (cols);
  if (strchr(iwidth,'%')) n = cols*n/100;
  if (n>cols) n = cols;
  return (n<1 ? 1 : n);
}

/* How many columns and rows a picture of iw x ih pixels takes to fill, but
   not overflow, bc x br cells */

static void picfit (gaint iw, gaint ih, gaint bc, gaint br, gaint *c, gaint *r) {
gadouble ca;
  ca = (cellw>0 && cellh>0) ? (gadouble)cellh/cellw : 2.0;   /* cells are about twice as tall */
  *c = bc;
  *r = (gaint)ceil((gadouble)bc*ih/iw/ca);
  if (*r>br) {
    *r = br;
    *c = (gaint)floor((gadouble)br*ca*iw/ih);
  }
  if (*c<1) *c = 1;
  if (*r<1) *r = 1;
}

/* kitty graphics. Outside tmux the picture goes at the cursor, in parts
   of 4096 characters, each its own sequence. tmux knows nothing of such
   pictures, so would neither move nor hide one with its pane: there the
   picture is only handed to kitty, which then draws it wherever it finds
   its placeholder, U+10EEEE, as text. tmux keeps, moves, hides and
   redraws that text as any other. The placeholder's colour (256 colours,
   which tmux always passes on) and a third mark name the picture; marks
   on the first cell of each row give the row. */

static const unsigned long kittymarks[297] = {
  0x0305,0x030d,0x030e,0x0310,0x0312,0x033d,0x033e,0x033f,0x0346,0x034a,0x034b,0x034c,
  0x0350,0x0351,0x0352,0x0357,0x035b,0x0363,0x0364,0x0365,0x0366,0x0367,0x0368,0x0369,
  0x036a,0x036b,0x036c,0x036d,0x036e,0x036f,0x0483,0x0484,0x0485,0x0486,0x0487,0x0592,
  0x0593,0x0594,0x0595,0x0597,0x0598,0x0599,0x059c,0x059d,0x059e,0x059f,0x05a0,0x05a1,
  0x05a8,0x05a9,0x05ab,0x05ac,0x05af,0x05c4,0x0610,0x0611,0x0612,0x0613,0x0614,0x0615,
  0x0616,0x0617,0x0657,0x0658,0x0659,0x065a,0x065b,0x065d,0x065e,0x06d6,0x06d7,0x06d8,
  0x06d9,0x06da,0x06db,0x06dc,0x06df,0x06e0,0x06e1,0x06e2,0x06e4,0x06e7,0x06e8,0x06eb,
  0x06ec,0x0730,0x0732,0x0733,0x0735,0x0736,0x073a,0x073d,0x073f,0x0740,0x0741,0x0743,
  0x0745,0x0747,0x0749,0x074a,0x07eb,0x07ec,0x07ed,0x07ee,0x07ef,0x07f0,0x07f1,0x07f3,
  0x0816,0x0817,0x0818,0x0819,0x081b,0x081c,0x081d,0x081e,0x081f,0x0820,0x0821,0x0822,
  0x0823,0x0825,0x0826,0x0827,0x0829,0x082a,0x082b,0x082c,0x082d,0x0951,0x0953,0x0954,
  0x0f82,0x0f83,0x0f86,0x0f87,0x135d,0x135e,0x135f,0x17dd,0x193a,0x1a17,0x1a75,0x1a76,
  0x1a77,0x1a78,0x1a79,0x1a7a,0x1a7b,0x1a7c,0x1b6b,0x1b6d,0x1b6e,0x1b6f,0x1b70,0x1b71,
  0x1b72,0x1b73,0x1cd0,0x1cd1,0x1cd2,0x1cda,0x1cdb,0x1ce0,0x1dc0,0x1dc1,0x1dc3,0x1dc4,
  0x1dc5,0x1dc6,0x1dc7,0x1dc8,0x1dc9,0x1dcb,0x1dcc,0x1dd1,0x1dd2,0x1dd3,0x1dd4,0x1dd5,
  0x1dd6,0x1dd7,0x1dd8,0x1dd9,0x1dda,0x1ddb,0x1ddc,0x1ddd,0x1dde,0x1ddf,0x1de0,0x1de1,
  0x1de2,0x1de3,0x1de4,0x1de5,0x1de6,0x1dfe,0x20d0,0x20d1,0x20d4,0x20d5,0x20d6,0x20d7,
  0x20db,0x20dc,0x20e1,0x20e7,0x20e9,0x20f0,0x2cef,0x2cf0,0x2cf1,0x2de0,0x2de1,0x2de2,
  0x2de3,0x2de4,0x2de5,0x2de6,0x2de7,0x2de8,0x2de9,0x2dea,0x2deb,0x2dec,0x2ded,0x2dee,
  0x2def,0x2df0,0x2df1,0x2df2,0x2df3,0x2df4,0x2df5,0x2df6,0x2df7,0x2df8,0x2df9,0x2dfa,
  0x2dfb,0x2dfc,0x2dfd,0x2dfe,0x2dff,0xa66f,0xa67c,0xa67d,0xa6f0,0xa6f1,0xa8e0,0xa8e1,
  0xa8e2,0xa8e3,0xa8e4,0xa8e5,0xa8e6,0xa8e7,0xa8e8,0xa8e9,0xa8ea,0xa8eb,0xa8ec,0xa8ed,
  0xa8ee,0xa8ef,0xa8f0,0xa8f1,0xaab0,0xaab2,0xaab3,0xaab7,0xaab8,0xaabe,0xaabf,0xaac1,
  0xfe20,0xfe21,0xfe22,0xfe23,0xfe24,0xfe25,0xfe26,0x10a0f,0x10a38,0x1d185,0x1d186,
  0x1d187,0x1d188,0x1d189,0x1d1aa,0x1d1ab,0x1d1ac,0x1d1ad,0x1d242,0x1d243,0x1d244
};

static unsigned char *lastrgb=NULL;          /* the latest picture's pixels: kitty, sixel */
static gaint lastrw=0,lastrh=0;

/* A copy of them, w x h, or NULL */

static unsigned char *lastpixels (gaint *w, gaint *h) {
unsigned char *rgb=NULL;
  pthread_mutex_lock(&tlock);
  *w = lastrw;
  *h = lastrh;
  if (lastrgb && lastrw>0 && lastrh>0) {
    rgb = (unsigned char *)malloc((size_t)lastrw*lastrh*3);
    if (rgb) memcpy(rgb,lastrgb,(size_t)lastrw*lastrh*3);
  }
  pthread_mutex_unlock(&tlock);
  return (rgb);
}

static unsigned long kittyid=0;             /* the pane's picture */
static gaint kittyshown=0;
static gaint kittyn=0;                      /* pictures at the cursor in tmux */

static void utf8put (struct wout *w, unsigned long c) {
char o[4];
  if (c<0x80) { o[0] = (char)c; wraw(w,o,1); }
  else if (c<0x800) {
    o[0] = (char)(0xc0|(c>>6)); o[1] = (char)(0x80|(c&0x3f));
    wraw(w,o,2);
  } else if (c<0x10000) {
    o[0] = (char)(0xe0|(c>>12)); o[1] = (char)(0x80|((c>>6)&0x3f));
    o[2] = (char)(0x80|(c&0x3f));
    wraw(w,o,3);
  } else {
    o[0] = (char)(0xf0|(c>>18)); o[1] = (char)(0x80|((c>>12)&0x3f));
    o[2] = (char)(0x80|((c>>6)&0x3f)); o[3] = (char)(0x80|(c&0x3f));
    wraw(w,o,4);
  }
}

/* The PNG, its keys with the first part */

static void kittysend (struct wout *w, const unsigned char *data, size_t len, const char *keys) {
char head[200];
size_t off,n;
gaint more;

  for (off=0; off<len && !w->err; off+=n) {
    n = len-off;
    if (n>3072) n = 3072;
    more = off+n<len;
    if (off) room(w,(n+2)/3*4+64);
    ptopen(w);
    if (off==0) snprintf(head,sizeof(head),"\033_G%s,m=%d;",keys,more);
    else snprintf(head,sizeof(head),"\033_Gm=%d;",more);
    wesc(w,head);
    b64put(w,data+off,n);
    wesc(w,"\033\\");
    ptclose(w);
  }
}

static size_t kittypic (struct wout *w, const struct paneinfo *pi, const unsigned char *data,
                        size_t len, const char *iwidth) {
char keys[200],at[32];
gaint iw,ih,c,r,x,y,lo,hi,rw,rh,tw,th;
unsigned long id;
unsigned char *rgb,*small;
struct mbuf mb;

  if (len<24) return (0);
  iw = (data[16]<<24)|(data[17]<<16)|(data[18]<<8)|data[19];
  ih = (data[20]<<24)|(data[21]<<16)|(data[22]<<8)|data[23];
  if (iw<=0 || ih<=0) return (0);
  if (pi) picfit(iw,ih,pi->cols,pi->rows>1 ? pi->rows-1 : 1,&c,&r);
  else picfit(iw,ih,inlinecols(w->fd,iwidth),100000,&c,&r);

  /* No more pixels than the cells show, when their size is known: a
     tenth as much to send, often, and kitty takes it in 4 KB parts */
  memset(&mb,0,sizeof(mb));
  tw = c*cellw;
  if (cellw>0 && cellh>0 && tw<iw && (rgb=lastpixels(&rw,&rh))!=NULL) {
    th = (gaint)((gadouble)ih*tw/iw+0.5);
    small = (rw==iw && rh==ih && th>0) ? rgbscale(rgb,iw,ih,tw,th) : NULL;
    if (small && !pngencode(small,tw,th,0,&mb)) {
      data = mb.p;
      len = mb.n;
    }
    free(small);
    free(rgb);
  }

  if (w->via!=VIA_TMUX) {                   /* at the cursor, which then moves past it */
    snprintf(keys,sizeof(keys),"a=T,f=100,t=d,q=2,c=%d",c);   /* rows to match */
    kittysend(w,data,len,keys);
    free(mb.p);
    return ((len+2)/3*4);
  }

  /* tmux: the id from the process, and for each picture at the cursor
     another, so those above keep theirs. The low byte is the colour, 16
     to 255, as tmux would write 0 to 15 as other colours; the high byte
     any mark. */
  lo = 16 + (gaint)((getpid() + (pi ? 0 : ++kittyn)) % 240);
  hi = (gaint)((getpid()/255) % 256);
  id = ((unsigned long)hi<<24) | (unsigned long)lo;
  if (r>297) r = 297;
  if (pi) {
    if (kittyshown) {
      snprintf(keys,sizeof(keys),"\033_Ga=d,d=I,i=%lu,q=2\033\\",kittyid);
      ptopen(w); wesc(w,keys); ptclose(w);
    }
    kittyid = id;
    kittyshown = 1;
  }
  snprintf(keys,sizeof(keys),"a=T,U=1,f=100,t=d,q=2,i=%lu,c=%d,r=%d",id,c,r);
  kittysend(w,data,len,keys);
  snprintf(keys,sizeof(keys),"\033[38;5;%dm",lo);
  for (y=0; y<r && !w->err; y++) {
    if (pi) {
      snprintf(at,sizeof(at),"\033[%d;1H",y+1);   /* the pane's own row */
      wstr(w,at);
    } else if (y) wstr(w,"\r\n");
    wstr(w,keys);
    utf8put(w,0x10eeee);
    utf8put(w,kittymarks[y]);
    utf8put(w,kittymarks[0]);
    utf8put(w,kittymarks[hi]);
    for (x=1; x<c; x++) utf8put(w,0x10eeee);
    /* tmux 3.7 draws the marks only when it redraws the line (it looks
       for the cell at the left of the window, not of the pane), and
       deleting a character in a pane that is not the full width makes it
       redraw it: delete the last cell, and put it back */
    if (c>=2) {
      snprintf(at,sizeof(at),"\033[%dG\033[P",c);
      wstr(w,at);
      utf8put(w,0x10eeee);
    }
    wstr(w,"\033[39m");
  }
  free(mb.p);
  return ((len+2)/3*4);
}

/* Take the pane's picture out of kitty, as GrADS ends */

static void kittyclear (void) {
struct wout *w;
char head[100];
  if (!kittyshown || panefd<0) return;
  w = (struct wout *)calloc(1,sizeof(struct wout));
  if (w==NULL) return;
  w->fd = panefd;
  w->pacefd = -1;
  w->via = VIA_TMUX;
  snprintf(head,sizeof(head),"\033_Ga=d,d=I,i=%lu,q=2\033\\",kittyid);
  ptopen(w);
  wesc(w,head);
  ptclose(w);
  wflush(w);
  free(w);
  kittyshown = 0;
}

/* sixel: the picture scaled to the pixels it is to fill, from the cell
   size the terminal or tmux gave. tmux built with sixel (3.5 and later
   say so) takes it into the pane and draws it itself, by the cell size it
   gave the pane. When
   none is known, cells of 6 x 12, smaller than most: a picture too large
   would run over the panes below, or scroll the screen. Unlike the others
   a sixel picture cannot go in parts, as tmux puts its own sequences
   between them: through tmux before 3.3, which drops a sequence of more
   than 8 bytes a cell, it goes in fewer colours, or smaller, until it is
   three quarters of that (three steps). */

static size_t sixelpic (struct wout *w, const struct paneinfo *pi, const char *pos,
                        const char *back, const char *iwidth) {
unsigned char *rgb,*small;
gaint iw,ih,cw,ch,bw,bh,tw,th,tries,most=256;
gadouble f;
struct mbuf mb;
size_t sent=0;

  rgb = lastpixels(&iw,&ih);
  if (rgb==NULL) return (0);
  if (w->via==VIA_TMUX && tmuxsixel) cellsize(w->fd);
  cw = cellw>0 && cellh>0 ? cellw : 6;
  ch = cellw>0 && cellh>0 ? cellh : 12;
  if (pi) { bw = pi->cols*cw; bh = (pi->rows>1 ? pi->rows-1 : 1)*ch; }
  else { bw = inlinecols(w->fd,iwidth)*cw; bh = 1<<20; }
  f = (gadouble)bw/iw;
  if ((gadouble)bh/ih<f) f = (gadouble)bh/ih;
  memset(&mb,0,sizeof(mb));
  small = NULL;
  for (tries=0; tries<8; tries++) {
    tw = (gaint)(iw*f);
    th = (gaint)(ih*f);
    if (tw<1) tw = 1;
    if (th<1) th = 1;
    if (small!=rgb) free(small);
    small = (tw==iw && th==ih) ? rgb : rgbscale(rgb,iw,ih,tw,th);
    free(mb.p);
    memset(&mb,0,sizeof(mb));
    if (small==NULL || sixelencode(small,tw,th,most,&mb)) { mb.n = 0; break; }
    if (!(w->via==VIA_TMUX && w->step && !tmuxsixel) || mb.n<=3*w->step) break;
    tlogf("sixel: %dx%d in %d colours is %lu bytes, too many for tmux before 3.3",
          tw,th,most,(unsigned long)mb.n);
    if (most>16) most /= 4;                  /* fewer colours, then fewer pixels */
    else f *= 0.95*sqrt((gadouble)3*w->step/mb.n);
  }
  if (mb.n) {
    if (w->via==VIA_TMUX && tmuxsixel) {
      if (pi) wstr(w,"\033[H\033[2J");       /* tmux lets go of the old one */
      wraw(w,mb.p,mb.n);
    } else {
      ptopen(w);
      wesc(w,pos);
      wescn(w,(const char *)mb.p,mb.n);
      wesc(w,back);
      ptclose(w);
      sent = mb.n;
    }
  }
  free(mb.p);
  if (small!=rgb) free(small);
  free(rgb);
  return (sent);
}

/* How much to hand tmux at a time. tmux before 3.3 drops all it holds for
   a client once that reaches 8 bytes per cell of the client's terminal,
   so stay at a quarter of that (an eighth on a slow link, see room); each
   loss seen halves it again. */

static size_t tmuxstep (const struct paneinfo *pi) {
long cells,s;
  if (stepopt>=0) return ((size_t)stepopt);
  if (tmuxkeeps==1) return (0);
  cells = (long)(pi->cw>0 ? pi->cw : 80) * (pi->ch>0 ? pi->ch : 24);
  s = (cells*8/4) >> stepshift;
  if (s>SEQ_PART) s = SEQ_PART;
  if (s<STEP_MIN) s = STEP_MIN;
  return ((size_t)s);
}

/* Send a picture. Given a pane, it fills the pane. Through plain tmux it
   is placed by absolute cursor movement inside the image sequence, since
   tmux does not move the terminal's cursor to the pane for passthrough
   output; under tmux -CC iTerm2 draws the pane itself, so the cursor goes
   to the pane's own top left corner. Otherwise the picture goes where the
   cursor is, iwidth wide. */

static size_t sentlen=0;                    /* what of the latest picture tmux is to pass on */

static void sendpic (gaint fd, gaint pacefd, gaint via, size_t step, const struct paneinfo *pi,
                     gaint clear, const unsigned char *data, size_t len, const char *iwidth) {
struct wout *w;
char args[256],pos[64],tmp[64];
const char *back="",*home="";
size_t b64len,off,n,part;
gaint parts,showprog,pct,lastpct=-1;
double t0;

  w = (struct wout *)calloc(1,sizeof(struct wout));
  if (w==NULL) return;
  w->fd = fd;
  w->pacefd = pacefd;
  w->via = via;
  b64len = (len+2)/3*4;
  sentlen = b64len;
  if (pi) snprintf(args,sizeof(args),"inline=1;size=%lu;width=%d;height=%d;preserveAspectRatio=1",
                   (unsigned long)len,pi->cols,pi->rows>1 ? pi->rows-1 : 1);
  else snprintf(args,sizeof(args),"inline=1;size=%lu;width=%s;preserveAspectRatio=1",
                (unsigned long)len,iwidth);
  pos[0] = '\0';
  if (pi && via==VIA_CC) home = "\033[H";
  else if (pi) {
    snprintf(pos,sizeof(pos),"\0337\033[%d;%dH",pi->row+1,pi->col+1);
    back = "\0338";
  }

  t0 = now();
  pace(w,PACE_QUIET);
  tlogf("picture: %lu bytes, via %s, step %lu, paced on fd %d, waited %.3f s for the link",
        (unsigned long)len,via==VIA_CC ? "tmux -CC" : (via==VIA_TMUX ? "tmux" : "terminal"),
        (unsigned long)step,pacefd,now()-t0);
  if (w->waited>0.15) slowuntil = now()+20.0;
  w->waited = 0.0;
  w->step = step;
  w->t0 = now();

  /* iTerm2 takes a picture in parts, which also lets the progress bar
     follow it; under tmux -CC it only takes parts. Elsewhere only a
     picture too large for one sequence, or for one step of an older
     tmux, is split, which needs iTerm2 3.5 anyway. */
  parts = via==VIA_CC || (b64len+200 >= SEQ_LIMIT) || (step && b64len+200 > step) ||
          (iterm && progressopt);
  /* iTerm2's progress bar (OSC 9;4), which elsewhere may come out as a
     notification: only iTerm2's, unless GA_TERM_PROGRESS=on */
  showprog = parts && (progressopt==2 ||
             (progressopt && iterm && (b64len>=PROGRESS_MIN || now()<slowuntil)));
  part = SEQ_PART;
  if (via==VIA_CC) part = CC_PART;
  else if (step) {                          /* half a step, so a slow link can take one */
    part = step/2>320 ? step/2-64 : 256;
    if (part>SEQ_PART) part = SEQ_PART;
  }
  part = part/4*3;                          /* raw bytes per part, whole base64 groups */

  if (pi && clear) wstr(w,"\033[H\033[2J");  /* tmux clears the pane's cells */
  if (proto==PROTO_KITTY && via!=VIA_CC) {
    parts = 1;
    sentlen = kittypic(w,pi,data,len,iwidth);
  } else if (proto==PROTO_SIXEL && via!=VIA_CC) {
    parts = 0;
    sentlen = sixelpic(w,pi,pos,back,iwidth);
  } else if (!parts) {
    wstr(w,home);
    ptopen(w);
    wesc(w,pos);
    wesc(w,"\033]1337;File=");
    wstr(w,args);
    wstr(w,":");
    b64put(w,data,len);
    wesc(w,"\a");
    wesc(w,back);
    ptclose(w);
  } else {
    wstr(w,home);
    ptopen(w);
    wesc(w,pos);
    wesc(w,"\033]1337;MultipartFile=");
    wstr(w,args);
    wesc(w,"\a");
    wesc(w,back);
    ptclose(w);
    for (off=0; off<len && !w->err; off+=n) {
      n = len-off;
      if (n>part) n = part;
      room(w,(n+2)/3*4+32);
      ptopen(w);
      wesc(w,"\033]1337;FilePart=");
      b64put(w,data+off,n);
      wesc(w,"\a");
      ptclose(w);
      /* writing blocks on a slow link outside tmux: show progress */
      if (!showprog && progressopt && iterm && w->waited>0.15) showprog = 1;
      if (showprog) {
        pct = (gaint)((off+n)*100/len);
        if (pct!=lastpct) {
          snprintf(tmp,sizeof(tmp),"\033]9;4;1;%d\a",pct);
          ptopen(w); wesc(w,tmp); ptclose(w);
          lastpct = pct;
        }
      }
    }
    room(w,64);
    wstr(w,home);
    ptopen(w);
    wesc(w,pos);
    wesc(w,"\033]1337;FileEnd\a");
    wesc(w,back);
    ptclose(w);
    if (lastpct>=0) { ptopen(w); wesc(w,"\033]9;4;0\a"); ptclose(w); }
  }
  if (!pi) wstr(w,"\n");
  wflush(w);
  if (step) {
    pace(w,STEP_QUIET);                     /* let tmux pass on the last step */
    linkslow = now()-w->t0>0.1 && w->waited>0.5*(now()-w->t0);
  }
  if (w->waited>0.15) slowuntil = now()+20.0;
  tlogf("picture: sent in %s, %lu-byte parts, %d steps, %.3f s, %.3f s of it waiting%s%s",
        parts ? "parts" : "one piece",(unsigned long)part,w->steps,now()-t0,w->waited,
        step && linkslow ? "; the link is slow" : "",w->err ? "; writing failed" : "");
  free(w);
}

/* The tmux client's terminal, to pace against */

static gaint ctyfd=-1;
static char ctypath[256];

static gaint pacefor (const char *path) {
  if (path==NULL || *path!='/') return (-1);
  if (ctyfd>=0 && !strcmp(path,ctypath)) return (ctyfd);
  if (ctyfd>=0) close(ctyfd);
  ctyfd = open(path,O_WRONLY|O_NOCTTY|O_NONBLOCK);
  cloexec(ctyfd);
  snprintf(ctypath,sizeof(ctypath),"%s",path);
  return (ctyfd);
}

/* After tmux dropped output: end an image sequence it may have cut off,
   which would otherwise swallow what follows, and have tmux redraw the
   screen, since its own redraw may have gone that way. */

static void mend (const struct paneinfo *pi) {
static const char st[] = "\033Ptmux;\033\033\\\033\\";
char *argv[6];
  if (write(panefd,st,sizeof(st)-1)<0) return;
  argv[0] = "tmux"; argv[1] = "refresh-client";
  argv[2] = pi->ctty[0] ? "-t" : NULL; argv[3] = (char *)pi->ctty; argv[4] = NULL;
  runcmd(argv,NULL,0);
  usleep(100000);
}

/* Did tmux pass the picture on? tmux counts what it queues for the client
   (client_written), and a passthrough sequence it skips adds nothing to
   that: tmux 3.2a and 3.3, and 3.4 and later with allow-passthrough on,
   skip it without a word while a redraw waits for the terminal to catch
   up. tmux takes a moment to read the end of it. Returns 1 when it went
   out, 0 when it did not, -1 when tmux does not say; after is filled. */

static gaint passedon (const struct paneinfo *pi, struct paneinfo *after, size_t want) {
gaint i;
useconds_t wait=10000;
  for (i=0; ; i++) {
    if (tmuxinfo(pane,after)) return (-1);
    if (!pi->haswritten || !after->haswritten || strcmp(after->ctty,pi->ctty) ||
        after->written<pi->written) return (-1);      /* another client */
    if (after->lost>pi->lost) return (0);   /* queued, then dropped */
    if (after->written-pi->written >= want) return (1);
    if (i>=6 || stopping || quitting) return (0);
    usleep(wait);                           /* about 1.3 s in all */
    wait *= 2;
  }
}

/* Draw the latest picture into the viewer pane; worker only. A picture
   for a pane that is not on screen waits until it is. tmux may not pass
   a picture on: tmux before 3.3 drops all it holds for a client once that
   is too much, and says so in client_discarded, and tmux skips one while
   a redraw waits (see passedon). Then send it again, in smaller steps
   from now on after a drop, once tmux is done dropping (it looks every
   100 ms) and the screen is mended. */

static gaint panehidden=0;                  /* worker: a picture waits for the pane */

static void panesend (gaint clear) {
struct paneinfo pi,after;
gaint tries,via,got;
size_t want;

  if (panefd<0 || lastpic==NULL || quitting) return;
  for (tries=0; ; tries++) {
    if (tmuxinfo(pane,&pi)) {              /* without its place it would land anywhere */
      tlogf("pane %s: tmux did not say where it is",pane);
      warnplace = 1;
      return;
    }
    if (!pi.hidden || !panehidden)          /* once while it is hidden */
      tlogf("pane %s: %dx%d at row %d column %d; client %s, %dx%d%s%s, %lu bytes dropped so far",
            pane,pi.cols,pi.rows,pi.row,pi.col,pi.ctty[0] ? pi.ctty : "(none)",pi.cw,pi.ch,
            pi.control ? ", tmux -CC" : "",pi.hidden ? ", not on screen" : "",pi.lost);
    if (pi.hidden) {                       /* tmux would drop it, or draw it elsewhere */
      panehidden = 1;
      return;
    }
    panehidden = 0;
    if (pi.rows!=sentrows || pi.cols!=sentcols) clear = 1;
    sentrows = pi.rows;
    sentcols = pi.cols;
    via = pi.control ? VIA_CC : VIA_TMUX;
    sendpic(panefd,pacefor(pi.ctty),via,via==VIA_TMUX ? tmuxstep(&pi) : 0,&pi,
            clear || panefresh,lastpic,lastpiclen,NULL);
    panefresh = 0;
    if (via!=VIA_TMUX || quitting || stopping) return;
    want = sentlen;
    got = passedon(&pi,&after,want);
    if (got<0 && tmuxkeeps!=1 && !tmuxinfo(pane,&after) && after.lost>pi.lost) got = 0;
    if (got!=0) return;
    if (after.lost>pi.lost) {
      tlogf("pane %s: tmux dropped %lu bytes while the picture went out",pane,after.lost-pi.lost);
      if (tmuxkeeps!=1 && stepshift<4) stepshift++;
    } else
      tlogf("pane %s: tmux passed on %lu bytes of the picture's %lu",pane,
            after.written-pi.written,(unsigned long)want);
    if (!warnlost) warnlost = 1;
    usleep(300000);
    if (after.lost>pi.lost || after.written>pi.written) mend(&after);
    if (tries>=2) {
      warnlost = 2;
      return;
    }
    clear = 1;
  }
}

/* The pane changed size, or came back: draw the picture again to fit */

static void checkresize (void) {
struct winsize ws;
static gaint ticks=0;
  if (panefd<0 || lastpic==NULL) return;
  if (panehidden) {                         /* now and then, see if it is back */
    if (++ticks%3==0) panesend(1);
    return;
  }
  if (ioctl(panefd,TIOCGWINSZ,&ws)) return;
  if (ws.ws_row!=sentrows || ws.ws_col!=sentcols) panesend(1);
}

/* ---- worker thread ---- */

/* Record the new picture and wake a viewer started by hand */

static void shown (const char *name) {
FILE *f;
gaint fd,n;

  pthread_mutex_lock(&tlock);
  n = ++seq;
  pthread_mutex_unlock(&tlock);
  f = fopen(seqtmp,"w");
  if (f) {
    fprintf(f,"%d %s\n",n,name);
    fclose(f);
    rename(seqtmp,seqpath);
  }
  /* a viewer keeps the FIFO open; without one, open fails at once */
  fd = open(fifopath,O_WRONLY|O_NONBLOCK);
  if (fd>=0) {
    if (write(fd,"\n",1)<0) { /* full or gone: the viewer catches up anyway */ }
    close(fd);
  }
}

/* Write a picture file, keep it as the latest picture (taking the buffer),
   and show it */

static void publish (const char *name, unsigned char *data, size_t len) {
char fn[700],tmp[700];
FILE *f;
gaint ok;

  snprintf(fn,sizeof(fn),"%s/%s",tdir,name);
  snprintf(tmp,sizeof(tmp),"%s/.%s.tmp",tdir,name);
  f = fopen(tmp,"wb");
  ok = 0;
  if (f) {
    ok = fwrite(data,1,len,f)==len;
    if (fclose(f)) ok = 0;
  }
  if (ok && rename(tmp,fn)) ok = 0;
  if (ok && mode!=3 && proto!=PROTO_ITERM && !strcmp(name,"plot.gif")) {
    free(data);                              /* only iTerm2 plays a GIF; the last frame stays */
  } else if (ok) {
    pthread_mutex_lock(&tlock);
    free(lastpic);
    lastpic = data;
    lastpiclen = len;
    pthread_mutex_unlock(&tlock);
    shown(name);
    panesend(0);
  } else {
    unlink(tmp);
    free(data);
  }
}

/* sixel draws from the pixels, and kitty from them made smaller: keep them
   with the picture */

static void keeprgb (const struct tjob *j) {
unsigned char *rgb;
const unsigned int *p;
gaint x,y;
  rgb = (unsigned char *)malloc((size_t)j->w*j->h*3);
  if (rgb==NULL) return;
  for (y=0; y<j->h; y++) {
    p = (const unsigned int *)(j->px + (size_t)y*j->stride);
    for (x=0; x<j->w; x++) {
      rgb[((size_t)y*j->w+x)*3]   = (p[x]>>16)&255;
      rgb[((size_t)y*j->w+x)*3+1] = (p[x]>>8)&255;
      rgb[((size_t)y*j->w+x)*3+2] = p[x]&255;
    }
  }
  pthread_mutex_lock(&tlock);
  free(lastrgb);
  lastrgb = rgb;
  lastrw = j->w;
  lastrh = j->h;
  pthread_mutex_unlock(&tlock);
}

static void dojob (struct tjob *j) {
struct mbuf mb;
unsigned char *rgb;

  if (j->kind==JOB_STILL || j->kind==JOB_FRAME) {
    if (proto==PROTO_SIXEL || proto==PROTO_KITTY) keeprgb(j);
    memset(&mb,0,sizeof(mb));
    if (pngencode(j->px,j->w,j->h,j->stride,&mb)) free(mb.p);
    else publish("plot.png",mb.p,mb.n);
  }
  else if (j->kind==JOB_GIFFRAME) {
    if (j->first) gifreset();
    rgb = downsample(j,j->lw,j->lh);
    if (rgb) gifframe(rgb,j->lw,j->lh);
    else gif.failed = 1;
    free(rgb);
  }
  else if (j->kind==JOB_GIFEND) {
    if (gif.frames>0 && !gif.failed) gbyte(0x3b);
    if (gif.frames>0 && !gif.failed) {
      publish("plot.gif",gif.buf,gif.len);  /* the buffer is the picture's now */
      gif.buf = NULL;
    }
    gifreset();
  }
  else if (j->kind==JOB_GIFDROP) gifreset();
}

static void freejob (struct tjob *j) {
  if (j) {
    free(j->px);
    free(j);
  }
}

static void *work (void *arg) {
struct tjob *j;
struct timespec ts;

  pthread_mutex_lock(&tlock);
  while (1) {
    if (!qhead) {
      if (stopping) break;
      /* wake now and then to notice the pane being resized */
      clock_gettime(CLOCK_REALTIME,&ts);
      ts.tv_nsec += 300000000L;
      if (ts.tv_nsec>=1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      if (pthread_cond_timedwait(&twake,&tlock,&ts)==ETIMEDOUT && !qhead && !stopping) {
        pthread_mutex_unlock(&tlock);
        checkresize();
        pthread_mutex_lock(&tlock);
      }
      continue;
    }
    j = qhead;
    qhead = j->next;
    if (!qhead) qtail = NULL;
    if (ISFRAME(j->kind)) qframes--;
    busy = 1;
    pthread_cond_broadcast(&tdone);         /* room in the queue */
    pthread_mutex_unlock(&tlock);
    if (!intr || j->kind==JOB_GIFDROP) dojob(j);   /* after Ctrl-C, nothing more is sent */
    freejob(j);
    pthread_mutex_lock(&tlock);
    busy = 0;
    pthread_cond_broadcast(&tdone);
  }
  pthread_mutex_unlock(&tlock);
  gifreset();
  free(qcount); free(qsum); free(qmap); free(qused);
  qcount = NULL; qsum = NULL; qmap = NULL; qused = NULL;
  return (NULL);
}

static void startworker (void) {
sigset_t all,old;
  /* the worker takes no signals: Ctrl-C belongs to GrADS, and a terminal
     that went away must not kill us with SIGPIPE */
  sigfillset(&all);
  pthread_sigmask(SIG_SETMASK,&all,&old);
  workeron = !pthread_create(&worker,NULL,work,NULL);
  pthread_sigmask(SIG_SETMASK,&old,NULL);
  if (!workeron) printf("Terminal display: unable to start the picture writer thread\n");
}

static void stopworker (void) {
  if (!workeron) return;
  pthread_mutex_lock(&tlock);
  stopping = 1;
  pthread_cond_signal(&twake);
  pthread_mutex_unlock(&tlock);
  pthread_join(worker,NULL);
  workeron = 0;
}

/* Wait until everything handed to the worker is written and sent */

static void waitworker (void) {
  if (!workeron) return;
  pthread_mutex_lock(&tlock);
  while (qhead || busy) pthread_cond_wait(&tdone,&tlock);
  pthread_mutex_unlock(&tlock);
}

/* Forget the work not yet started; with tlock held */

static void dropqueue (gaint stillsonly) {
struct tjob **pp,*k;
  for (pp=&qhead; *pp; ) {
    k = *pp;
    if (!stillsonly || k->kind==JOB_STILL) {
      *pp = k->next;
      if (ISFRAME(k->kind)) qframes--;
      freejob(k);
    } else pp = &k->next;
  }
  for (qtail=qhead; qtail && qtail->next; qtail=qtail->next) ;
  pthread_cond_broadcast(&tdone);
}

/* Copy the visible picture for the worker */

static struct tjob *snapshot (gaint kind) {
struct tjob *j;
size_t n;

  j = (struct tjob *)calloc(1,sizeof(struct tjob));
  if (j==NULL) return (NULL);
  j->kind = kind;
  j->lw = (gaint)(width*animscale+0.5);
  j->lh = (gaint)(height*animscale+0.5);
  if (j->lw<16) j->lw = 16;
  if (j->lh<16) j->lh = 16;
  if (kind==JOB_STILL || ISFRAME(kind)) {
    gxCflush(1);
    cairo_surface_flush(surface);
    j->w = cairo_image_surface_get_width(surface);
    j->h = cairo_image_surface_get_height(surface);
    j->stride = cairo_image_surface_get_stride(surface);
    n = (size_t)j->stride*j->h;
    j->px = (unsigned char *)malloc(n);
    if (j->px==NULL) { free(j); return (NULL); }
    memcpy(j->px,cairo_image_surface_get_data(surface),n);
  }
  return (j);
}

/* Hand work to the worker, in order. A still picture replaces one not yet
   started. A frame waits for room, as a slow link holds up an X window;
   Ctrl-C ends the wait and the frame is dropped. Without a worker the work
   is done here and now. */

static void post (struct tjob *j) {
struct timespec ts;

  if (j==NULL) return;
  if (intr && j->kind!=JOB_GIFDROP) { freejob(j); return; }
  if (j->kind!=JOB_GIFDROP) posted = 1;
  if (!workeron) {
    dojob(j);
    freejob(j);
    return;
  }
  pthread_mutex_lock(&tlock);
  if (j->kind==JOB_STILL || j->kind==JOB_GIFEND) dropqueue(1);
  while (ISFRAME(j->kind) && qframes>=FRAMEQ && !intr) {
    clock_gettime(CLOCK_REALTIME,&ts);      /* look at intr now and then */
    ts.tv_nsec += 100000000L;
    if (ts.tv_nsec>=1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    pthread_cond_timedwait(&tdone,&tlock,&ts);
  }
  if (intr && j->kind!=JOB_GIFDROP) {
    pthread_mutex_unlock(&tlock);
    freejob(j);
    return;
  }
  j->next = NULL;
  if (qtail) qtail->next = j;
  else qhead = j;
  qtail = j;
  if (ISFRAME(j->kind)) qframes++;
  pthread_cond_signal(&twake);
  pthread_mutex_unlock(&tlock);
  if (syncwrite) waitworker();
}

/* ---- frames, Ctrl-C and the idle point; main thread ---- */

/* The visible picture is about to be replaced, or has just been by a swap */

static void frameend (gaint swapped) {
struct tjob *j;

  if (batch || surface==NULL || !dirty || !drawn || intr) return;
  if (anim==2 && swapped) {
    if (ngif<animmax) {
      j = snapshot(JOB_GIFFRAME);
      if (j) {
        j->first = (ngif==0);
        post(j);
        ngif++;
      }
    } else gifcut++;
  }
  if (anim!=0 && mode!=2) {                 /* show it now */
    post(snapshot(JOB_FRAME));
    dirty = 0;
  }
}

/* Ctrl-C while a command runs; called from the signal handler */

void gxdintr (void) {
  intr = 1;
}

/* Print the latest picture at the cursor */

static void inlineshow (void) {
unsigned char *d;
size_t n,step=0;
gaint fd,via=VIA_TERM,pacefd=-1;
struct paneinfo pi;
char *w,*t;

  pthread_mutex_lock(&tlock);
  n = lastpiclen;
  d = n ? (unsigned char *)malloc(n) : NULL;
  if (d) memcpy(d,lastpic,n);
  pthread_mutex_unlock(&tlock);
  if (d==NULL) return;
  t = getenv("TMUX");
  if (t && *t) {
    via = VIA_TMUX;
    if (tmuxkeeps<0) tmuxkeeps = tmuxprobe(getenv("TMUX_PANE"));
    if (tmuxinfo(getenv("TMUX_PANE"),&pi)) memset(&pi,0,sizeof(pi));
    else pacefd = pacefor(pi.ctty);
    if (pi.control) via = VIA_CC;
    else step = tmuxstep(&pi);
  }
  w = getenv("GA_TERM_WIDTH");
  if (w==NULL || *w=='\0') w = "70%";
  fd = open("/dev/tty",O_WRONLY|O_NOCTTY);
  fflush(stdout);
  sendpic(fd>=0 ? fd : 1,pacefd,via,step,NULL,0,d,n,w);
  if (fd>=0) close(fd);
  free(d);
}

static gaint shownseq (void) {
gaint n;
  pthread_mutex_lock(&tlock);
  n = seq;
  pthread_mutex_unlock(&tlock);
  return (n);
}

/* GrADS is about to wait for the user: show the picture if it changed */

void gxdidle (void) {
gaint before,frames;

  if (batch || surface==NULL) return;

  if (intr) {                               /* Ctrl-C: send nothing more */
    pthread_mutex_lock(&tlock);
    dropqueue(0);
    pthread_mutex_unlock(&tlock);
    intr = 0;
    if (ngif) post(snapshot(JOB_GIFDROP));
    ngif = 0;
    gifcut = 0;
    dirty = 0;                              /* the screen keeps the last picture sent */
    return;
  }

  before = shownseq();
  frames = ngif;
  if (ngif>=2) {
    post(snapshot(JOB_GIFEND));
    /* only iTerm2 plays the GIF: elsewhere print the last frame */
    if (mode==2 && proto!=PROTO_ITERM) post(snapshot(JOB_STILL));
    dirty = 0;
  } else {
    if (ngif==1) post(snapshot(JOB_GIFDROP));
    /* A blank page is worth sending only to replace a picture; the tmux
       pane says it is waiting until then */
    if (dirty && (drawn || mode==3 || (mode==1 && posted))) post(snapshot(JOB_STILL));
    dirty = 0;
  }
  if (gifcut) {
    printf("Terminal display: the animation keeps its first %d frames; %d more were left out\n",
           animmax,gifcut);
    printf("Terminal display: raise GA_TERM_ANIM_MAX to keep more\n");
  }
  ngif = 0;
  gifcut = 0;

  if (mode==2) {
    waitworker();
    if (shownseq()!=before && (drawn || frames>=2)) inlineshow();
  }
  if (warnplace && !warnedplace) {
    printf("Terminal display: tmux did not say where the picture pane is, so pictures are not shown\n");
    warnedplace = 1;
  }
  if (warnlost>warnedlost) {
    if (warnlost==1)
      printf("Terminal display: tmux dropped a picture the terminal could not take fast enough;\n"
             "Terminal display: it was sent again, and pictures now go in smaller pieces.\n");
    else
      printf("Terminal display: tmux keeps dropping pictures the terminal cannot take fast enough.\n");
    printf("Terminal display: tmux 3.3 or later, or iTerm2's tmux -CC, does not drop them.\n");
    warnedlost = warnlost;
  }
  intr = 0;     /* a Ctrl-C while the picture went out was about that picture */
}

/* Pick the output directory and the display mode */

static void termsetup (void) {
char *d,*m,*t,*v,*a;
gaint rc;
gadouble f;

  d = getenv("GA_TERM_DIR");
  ownsdir = 0;
  if (d && *d) {
    snprintf(tdir,sizeof(tdir),"%s",d);
    if (mkdir(tdir,0700) && errno!=EEXIST) {
      printf("Terminal display: unable to create %s\n",tdir);
    }
  } else {
    t = getenv("TMPDIR");
    if (t==NULL || *t=='\0') t = "/tmp";
    snprintf(tdir,sizeof(tdir),"%s/grads-term-XXXXXX",t);
    if (mkdtemp(tdir)==NULL) {
      snprintf(tdir,sizeof(tdir),"/tmp/grads-term-XXXXXX");
      if (mkdtemp(tdir)==NULL) {
        printf("Terminal display: unable to create a temporary directory\n");
        exit(-1);
      }
    }
    ownsdir = 1;
  }
  snprintf(seqpath,sizeof(seqpath),"%s/seq",tdir);
  snprintf(seqtmp,sizeof(seqtmp),"%s/.seq.tmp",tdir);
  snprintf(fifopath,sizeof(fifopath),"%s/notify",tdir);
  if (mkfifo(fifopath,0600) && errno!=EEXIST) fifopath[0] = '\0';

  a = getenv("GA_TERM_ANIM");
  if (a==NULL || *a=='\0' || !strcmp(a,"live") || !strcmp(a,"auto")) anim = 1;
  else if (!strcmp(a,"gif")) anim = 2;
  else if (!strcmp(a,"off")) anim = 0;
  else printf("Terminal display: unknown GA_TERM_ANIM \"%s\"; using live\n",a);
  a = getenv("GA_TERM_ANIM_DELAY");
  if (a && *a) {
    f = atof(a);
    if (f>=0.02 && f<=600.0) animdelay = (gaint)(f*100.0+0.5);
    else printf("Terminal display: GA_TERM_ANIM_DELAY must be 0.02 to 600 seconds\n");
  }
  a = getenv("GA_TERM_ANIM_MAX");
  if (a && *a) {
    if (atoi(a)>=2) animmax = atoi(a);
    else printf("Terminal display: GA_TERM_ANIM_MAX must be at least 2\n");
  }
  a = getenv("GA_TERM_ANIM_SCALE");
  if (a && *a) {
    f = atof(a);
    if (f>=0.25 && f<=1.0) animscale = f;
    else printf("Terminal display: GA_TERM_ANIM_SCALE must be 0.25 to 1\n");
  }
  a = getenv("GA_TERM_PROGRESS");
  if (a==NULL || *a=='\0' || !strcmp(a,"auto")) progressopt = 1;
  else if (!strcmp(a,"on")) progressopt = 2;
  else if (!strcmp(a,"off")) progressopt = 0;
  else printf("Terminal display: unknown GA_TERM_PROGRESS \"%s\"; using auto\n",a);
  a = getenv("GA_TERM_LOG");
  if (a && *a && tlog==NULL) {
    tlog = fopen(a,"a");
    if (tlog==NULL) printf("Terminal display: unable to write the log %s\n",a);
    else cloexec(fileno(tlog));
  }
  a = getenv("GA_TERM_SYNC");
  syncwrite = (a && !strcmp(a,"1"));
  a = getenv("GA_TERM_TMUX_STEP");
  if (a && *a) {
    if (atol(a)==0 && strcmp(a,"0")) printf("Terminal display: GA_TERM_TMUX_STEP must be a number of bytes\n");
    else if (atol(a)==0) stepopt = 0;
    else stepopt = atol(a)<STEP_MIN ? STEP_MIN : (atol(a)>SEQ_PART ? SEQ_PART : atol(a));
  }
  a = getenv("LC_TERMINAL");
  iterm = (a && !strcmp(a,"iTerm2"));

  m = getenv("GA_TERM_MODE");
  if (m==NULL || *m=='\0') m = "auto";

  /* The pictures go to a tmux pane beside the prompt, so outside tmux
     there is no -d Term unless printing them below each command
     (GA_TERM_MODE=inline) or only writing them (file) is asked for. When
     the launcher chose this display by itself, write them to files
     instead; when asked for by name, say so and stop. */
  t = getenv("TMUX");
  if ((!strcmp(m,"auto") || !strcmp(m,"tmux")) && !(t && *t)) {
    a = getenv("GA_TERM_AUTO");
    if (a && !strcmp(a,"1")) {
      printf("Terminal display: GrADS is not running inside tmux;\n");
      printf("Terminal display: the pictures go to files in %s instead\n",tdir);
      m = "file";
    } else {
      printf("Terminal display: GrADS is not running inside tmux.\n");
      printf("  -d Term shows the pictures in a tmux pane beside the prompt: start tmux\n");
      printf("  (or tmux -CC in iTerm2) first, then GrADS. To print each picture below\n");
      printf("  its command instead, set GA_TERM_MODE=inline; to only write the pictures\n");
      printf("  to files, GA_TERM_MODE=file.\n");
      fflush(stdout);
      if (fifopath[0]) unlink(fifopath);
      if (ownsdir) rmdir(tdir);
      exit(1);
    }
  }

  /* Pictures for the terminal need a terminal that shows them: likewise */
  if (strcmp(m,"file")) {
    termproto();
    tlogf("protocol: %s (%s), cell %dx%d",protonames[proto],protowhy,cellw,cellh);
    if (proto==PROTO_NONE) {
      a = getenv("GA_TERM_AUTO");
      if (a && !strcmp(a,"1")) {
        printf("Terminal display: this terminal shows no pictures (%s);\n",protowhy);
        printf("Terminal display: they go to files in %s instead\n",tdir);
        m = "file";
      } else {
        printf("Terminal display: this terminal shows no pictures (%s).\n",protowhy);
        printf("  -d Term needs a terminal that shows iTerm2 inline images (iTerm2, WezTerm),\n");
        printf("  kitty graphics (kitty, Ghostty) or sixel (foot, mlterm, xterm -ti vt340).\n");
        printf("  Otherwise use another display (-d Cairo or -d X11, with an X server), or\n");
        printf("  write the pictures to files: GA_TERM_MODE=file. If the terminal does show\n");
        printf("  one of them, name it: GA_TERM_PROTOCOL=iterm2, kitty or sixel.\n");
        fflush(stdout);
        if (fifopath[0]) unlink(fifopath);
        if (ownsdir) rmdir(tdir);
        exit(1);
      }
    }
  }
  v = viewer();
  pane[0] = '\0';
  if (!strcmp(m,"file")) mode = 3;
  else if (!strcmp(m,"inline")) mode = 2;
  else if (!strcmp(m,"tmux") || (!strcmp(m,"auto") && v)) {
    rc = tmuxsplit();                       /* inside tmux, as checked above */
    if (rc) {
      if (v==NULL) printf("Terminal display: viewer not found; set GA_TERM_VIEWER.\n");
      else printf("Terminal display: unable to open a tmux pane for the pictures.\n");
      printf("Terminal display: showing pictures inline instead.\n");
      mode = 2;
    } else mode = 1;
  }
  else {
    if (strcmp(m,"auto"))
      printf("Terminal display: unknown GA_TERM_MODE \"%s\"; showing pictures inline.\n",m);
    mode = 2;
  }

  tlogf("start: mode %s, protocol %s, pane %s, tmux %s, iTerm2 %s, GA_TERM_TMUX_STEP %ld",
        mode==1 ? "tmux" : (mode==2 ? "inline" : "file"),mode==3 ? "none needed" : protonames[proto],
        pane[0] ? pane : "(none)",
        tmuxall ? "3.4 or later, passthrough all" :
        (tmuxkeeps==1 ? "3.3" : (tmuxkeeps==0 ? "before 3.3" : "not asked")),
        iterm ? "yes" : "no",(long)stepopt);
  if (mode==3) {
    printf("Terminal display: pictures are written to %s\n",tdir);
    if (v) printf("Terminal display: view them with  %s %s\n",v,tdir);
  }
}

void gxdbat (void) {
  batch = 1;
}

/* User-defined picture size, "WIDTHxHEIGHT"; any "+x+y" part is ignored.
   Must be called before gxdbgn to have any effect. */

void gxdgeo (char *arg) {
  ugeom = arg;
}

void gxdbgn (gadouble xsz, gadouble ysz) {
gaint dw,dh,uw,uh;
char *s;
gadouble f;

  xsize = xsz;
  ysize = ysz;

  if (xsize >= ysize) {
    dw = TERM_DEFAULT_SIZE;
    dh = (gaint)((gadouble)dw*ysz/xsz + 0.5);
  } else {
    dh = TERM_DEFAULT_SIZE;
    dw = (gaint)((gadouble)dh*xsz/ysz + 0.5);
  }
  if (ugeom && sscanf(ugeom,"%dx%d",&uw,&uh)==2 && uw>0 && uh>0) {
    dw = uw;
    dh = uh;
  }

  s = getenv("GA_TERM_SCALE");
  if (s && *s) {
    f = atof(s);
    if (f>=1.0 && f<=4.0) scale = f;
    else printf("Terminal display: GA_TERM_SCALE must be between 1 and 4; using %g\n",scale);
  }

  width = dw;
  height = dh;
  xscl = (gadouble)(dw)/xsize;
  yscl = (gadouble)(dh)/ysize;
  dblmode = 0;

  termsetup();
  startworker();                    /* after the tmux split, which forks */

  surface = newsurface();
  gxCbgn(surface,xsz,ysz,dw,dh);
  gxCfrm();      /* a new image is transparent; an X window starts out painted */
  dirty = 1;
}

void gxdend (void) {
char *argv[8];
char fn[700];

  quitting = 1;                     /* finish writing pictures, send nothing more */
  stopworker();
  if (proto==PROTO_KITTY && mode==1) kittyclear();
  gxCend();
  if (surface) {
    cairo_surface_finish (surface);
    cairo_surface_destroy (surface);
    surface = NULL;
  }
  if (panefd>=0) { close(panefd); panefd = -1; }
  if (ctyfd>=0) { close(ctyfd); ctyfd = -1; }
  if (mode==1 && pane[0]) {
    argv[0] = "tmux"; argv[1] = "kill-pane"; argv[2] = "-t";
    argv[3] = pane; argv[4] = NULL;
    runcmd(argv,NULL,0);
  }
  if (ownsdir) {
    snprintf(fn,sizeof(fn),"%s/plot.png",tdir);      unlink(fn);
    snprintf(fn,sizeof(fn),"%s/.plot.png.tmp",tdir); unlink(fn);
    snprintf(fn,sizeof(fn),"%s/plot.gif",tdir);      unlink(fn);
    snprintf(fn,sizeof(fn),"%s/.plot.gif.tmp",tdir); unlink(fn);
    unlink(seqpath);
    unlink(seqtmp);
    if (fifopath[0]) unlink(fifopath);
    rmdir(tdir);
  }
}

/* Frame action.  Values for action are:
      0 -- new frame (clear display), wait before clearing.
      1 -- new frame, no wait.
      2 -- New frame in double buffer mode.
      7 -- new frame, but just clear graphics.
      8 -- clear only the event queue.
      9 -- flush the request buffer
   There are no events here, so only clearing matters. */

void gxdfrm (gaint iact) {
  if (iact==0 || iact==1 || iact==7) {
    frameend(0);                             /* the drawn page is a finished frame */
    gxCfrm();
    dirty = 1;
    drawn = 0;
  }
}

/* There is no mouse. When asked to wait for a click, show the picture and
   wait for Enter instead, so scripts that pause on "q pos" still pause. */

void gxdbtn (gaint flag, gadouble *xpos, gadouble *ypos,
	     gaint *mbtn, gaint *type, gaint *info, gadouble *rinfo) {
char line[256];
gaint i;

  *xpos = -999.9;
  *ypos = -999.9;
  *mbtn = -1;
  *type = -1;
  for (i=0; i<10; i++) *(info+i) = 0;
  for (i=0; i<4; i++) *(rinfo+i) = 0.0;
  if (batch || !flag) return;
  gxdidle();
  printf("Terminal display has no mouse; press Enter to continue ");
  fflush(stdout);
  if (fgets(line,sizeof(line),stdin)) *mbtn = 1;
}

/* Drawing changes the visible picture, or in double-buffer mode the
   hidden one that the next swap shows */

static void touched (void) {
  if (dblmode) backdrawn = 1;
  else dirty = drawn = 1;
}

gaint gxdacol (gaint clr, gaint red, gaint green, gaint blue, gaint alpha) {
  return(0);
}

void gxdcol (gaint clr) {
  gxCcol(clr);
}

void gxdwid (gaint wid){
  gxCwid(wid);
}

void gxdmov (gadouble x, gadouble y){
  gxCmov(x,y);
}

void gxddrw (gadouble x, gadouble y) {
  gxCdrw (x,y);
  touched();
}

void gxdrec (gadouble x1, gadouble x2, gadouble y1, gadouble y2) {
  gxCrec(x1,x2,y1,y2);
  touched();
}

void gxddbl (void) {                         /* turn on double buffer mode */
  frameend(0);                               /* the page is about to be cleared */
  gxCfrm();                                  /* clear the foreground */
  if (surface2==NULL) surface2 = newsurface();
  gxCsfc(surface2);                          /* draw on the background from now on */
  gxCfrm();
  dblmode = 1;
  backdrawn = 0;
  dirty = 1;
  drawn = 0;
}

void gxdswp (void) {                         /* copy the background to the foreground */
  if (dblmode) {
    gxCflush(1);
    if (!backdrawn) frameend(0);             /* the page goes blank */
    gxCswp(surface,surface2);
    dirty = 1;
    drawn = backdrawn;
    if (backdrawn) frameend(1);              /* a new frame is showing */
    gxCfrm();                                /* clear the background */
    backdrawn = 0;
  }
}

void gxdsgl (void) {                         /* turn off double buffer mode */
  if (dblmode) {
    gxCsfc(surface);                         /* draw on the foreground again */
    cairo_surface_destroy(surface2);
    surface2 = NULL;
  }
  dblmode = 0;
  backdrawn = 0;
}

void gxdfil (gadouble *xy, gaint n) {
  gxCfil (xy,n);
  touched();
}

/* "set xsize": make a picture of the new size and redraw into it */

void gxdxsz (gaint xx, gaint yy) {
  if (batch) return;
  if (xx<=0 || yy<=0 || (xx==width && yy==height)) return;
  if (dblmode) gxdsgl();
  width = xx;
  height = yy;
  xscl = (gadouble)(width)/xsize;
  yscl = (gadouble)(height)/ysize;
  cairo_surface_destroy(surface);            /* the worker has its own copies */
  surface = newsurface();
  gxCsfc(surface);
  gxCrsiz(width,height);
  gxhdrw(0,0);
  dirty = 1;
}

/* widgets need a window; all are no-ops */

void gxdpbn (gaint bnum, struct gbtn *pbn, gaint redraw, gaint btnrel, gaint nstat) {
  printf("Warning: The terminal display does not support buttons\n");
}

void gxdrmu (gaint mnum, struct gdmu *pmu, gaint redraw, gaint nstat) {
  printf("Warning: The terminal display does not support drop menus\n");
}

void gxdrbb (gaint num, gaint type, gadouble xlo, gadouble ylo, gadouble xhi, gadouble yhi, gaint mbc) {
  printf("Warning: The terminal display does not support rubber band widgets\n");
}

char *gxdlg (struct gdlg *qry) {
  printf("Warning: The terminal display does not support dialog boxes\n");
  return (NULL);
}

void gxrs1wd (int wdtyp, int wdnum) {
}

void gxdssv (int frame) {
  printf("Warning: The terminal display does not support the screen command\n");
}
void gxdssh (int cnt) {
  printf("Warning: The terminal display does not support the screen command\n");
}
void gxdsfr (int frame) {
  printf("Warning: The terminal display does not support the screen command\n");
}

void gxdptn (int typ, int den, int ang) {
}

void gxdbb(char *filename) {
}

void gxdfb(char *filename)  {
}

gaint win_data (struct xinfo *xinf) {
  return (0);
}

/* Given x,y page location, return picture pixel location */

void gxdgcoord (gadouble x, gadouble y, gaint *i, gaint *j) {
  if (batch) {
    *i = -999;
    *j = -999;
    return;
  }
  *i = (gaint)(x*xscl+0.5);
  *j = height - (gaint)(y*yscl+0.5);
}

void gxdimg(gaint *im, gaint imin, gaint jmin, gaint isiz, gaint jsiz) {
  printf("Warning: The terminal display does not support 'gxout imap'\n");
}

gadouble gxdqchl (char ch, gaint fn, gadouble w) {
  return (gxCqchl(ch,fn,w));
}

gadouble gxdch (char ch, gaint fn, gadouble x, gadouble y, gadouble w, gadouble h, gadouble rot) {
  touched();
  return (gxCch(ch,fn,x,y,w,h,rot));
}

/* Called by gxC.c when drawing is complete; the picture is written out when
   GrADS next waits for the user (gxdidle), not after every command. */
void gxdXflush (void) {
}

void gxdopt (gaint opt) {
  if (opt==4) gxCflush(1);
}

void gxsetpatt (gaint pnum) {
  gxCpattrset(pnum);
}

void gxdsignal (gaint sig) {
  if (sig==1) gxCflush(1);   /* finish rendering */
  if (sig==2) gxCaa(0);      /* disable anti-aliasing */
  if (sig==3) gxCaa(1);      /* enable anti-aliasing */
  if (sig==4) gxCpush();     /* push */
  if (sig==5) { gxCpop(); touched(); }  /* pop and paint */
}

void gxdclip (gadouble xlo, gadouble xhi, gadouble ylo, gadouble yhi) {
  gxCclip (xlo,xhi,ylo,yhi);
}

void gxdcfg (void) {
  printf("Terminal ");
  gxCcfg();
}

gaint gxdckfont (void) {
  return (1);
}
