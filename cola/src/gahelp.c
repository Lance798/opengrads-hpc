/* Added in 2026: usage hints at the prompt. GPLv2; see COPYING.

   What commands, set and query options, and functions take, in the words
   of the GrADS documentation index (doc/gadocindex.html): a line of syntax
   and, where it helps, an example. When a command typed at the prompt
   fails, the hint for it follows the error; a name that is not known gets
   the nearest known ones. Tab completion (grads.c) offers the same names,
   and after a set option its keywords (on|off, the kinds of gxout, ...).

   Nothing is added for commands run by a script or through the Python
   interface: their output goes to the script's result, which scripts
   read, and it stays as it was. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "grads.h"
#include "gahelp.h"

struct gahelp {
  const char *name;           /* as typed */
  const char *usage;          /* the syntax */
  const char *example;        /* an example, or NULL */
  const char *words;          /* keywords Tab offers after it, or NULL */
};

static const struct gahelp cmds[] = {
  {"bpopen",   "bpopen path.bp", "bpopen ~/run/output.bp", NULL},
  {"c",        "clear <norset|graphics|events|hbuff|button n|rband n|dropmenu n|sdfwrite|mask|shp>", NULL,
               "norset graphics events hbuff button rband dropmenu sdfwrite mask shp"},
  {"clear",    "clear <norset|graphics|events|hbuff|button n|rband n|dropmenu n|sdfwrite|mask|shp>", NULL,
               "norset graphics events hbuff button rband dropmenu sdfwrite mask shp"},
  {"close",    "close file#", "close 2", NULL},
  {"collect",  "collect number expression | free", "collect 1 ts", NULL},
  {"d",        "display expression", "d ts    d u;v    d ave(ts,t=1,t=12)", NULL},
  {"define",   "define name = expression", "define tm = ave(ts,t=1,t=12)", NULL},
  {"disable",  "disable print | fwrite", NULL, "print fwrite"},
  {"display",  "display expression", "d ts    d u;v    d ave(ts,t=1,t=12)", NULL},
  {"draw",     "draw string|title|xlab|ylab|line|rec|recf|mark|polyf|map|button|dropmenu|wxsym|shp ...",
               "draw title Surface temperature",
               "string title xlab ylab line rec recf mark polyf map button dropmenu wxsym shp"},
  {"enable",   "enable print filename", NULL, "print"},
  {"exec",     "exec filename <arg1 ... arg9>", NULL, NULL},
  {"flush",    "flush", NULL, NULL},
  {"gxprint",  "gxprint filename <x# y#> <white|black> <-b bgimage> <-f fgimage> <-t color#> <-e width>",
               "gxprint ~/plot.png x1600 y1200 white", "white black"},
  {"help",     "help", NULL, NULL},
  {"history",  "history <n1 <n2>> <file>    (his for short)", "history 10", NULL},
  {"modify",   "modify name seasonal|diurnal", NULL, NULL},
  {"open",     "open descriptor.ctl", "open ~/data/model.ctl", NULL},
  {"pdefwrite","pdefwrite filename", NULL, NULL},
  {"print",    "print <filename>", NULL, NULL},
  {"printim",  "printim filename <x# y#> <white|black> <-b bgimage> <-f fgimage> <-t color#>",
               "printim ~/plot.png x1600 y1200 white", "white black"},
  {"q",        "query option    (q alone lists the options)", "q dims    q file 2", NULL},
  {"query",    "query option    (q alone lists the options)", "q dims    q file 2", NULL},
  {"quit",     "quit", NULL, NULL},
  {"redraw",   "redraw button number 0|1", NULL, "button"},
  {"reinit",   "reinit", NULL, NULL},
  {"repeat",   "repeat <n1 <n2>>    (r for short: repeat commands from the history)", "repeat 12", NULL},
  {"reset",    "reset <events|graphics|hbuff|norset>", NULL, "events graphics hbuff norset"},
  {"run",      "run script.gs <arguments>", "run cbarn.gs", NULL},
  {"screen",   "screen save|show|free number", NULL, "save show free"},
  {"sdfopen",  "sdfopen file.nc <template #timesteps>", "sdfopen ~/data/t2m.nc", NULL},
  {"sdfwrite", "sdfwrite name", "define tm = ave(ts,t=1,t=12)  then  sdfwrite tm", NULL},
  {"set",      "set option values    (Tab after \"set \" lists the options)", "set gxout shaded    set lon 100 150", NULL},
  {"swap",     "swap", NULL, NULL},
  {"undefine", "undefine name", NULL, NULL},
  {"undo",     "undo <steps>", "undo 2", NULL},
  {"xdfopen",  "xdfopen descriptor.ctl", NULL, NULL},
  {NULL,NULL,NULL,NULL}
};

static const struct gahelp draws[] = {
  {"button",   "draw button number x y width height text", "draw button 1 9 8 1 0.5 Next", NULL},
  {"dropmenu", "draw dropmenu number x y width height text1 | text2 | ...", NULL, NULL},
  {"line",     "draw line x1 y1 x2 y2", "draw line 1 1 10 7.5", NULL},
  {"map",      "draw map", NULL, NULL},
  {"mark",     "draw mark marktype x y size", "draw mark 3 5.5 4.25 0.2", NULL},
  {"polyf",    "draw polyf x1 y1 x2 y2 x3 y3 ... xn yn", NULL, NULL},
  {"rec",      "draw rec xlo ylo xhi yhi", "draw rec 1 1 5 4", NULL},
  {"recf",     "draw recf xlo ylo xhi yhi", "draw recf 1 1 5 4", NULL},
  {"shp",      "draw shp shapefile <start> <end>", NULL, NULL},
  {"string",   "draw string x y text", "draw string 5.5 8 Surface temperature", NULL},
  {"title",    "draw title text", "draw title Surface temperature", NULL},
  {"wxsym",    "draw wxsym symbol x y size <color <thickness>>", NULL, NULL},
  {"xlab",     "draw xlab text", "draw xlab Longitude", NULL},
  {"ylab",     "draw ylab text", "draw ylab Pressure (hPa)", NULL},
  {NULL,NULL,NULL,NULL}
};

static const struct gahelp sets[] = {
  {"annot",    "set annot color <thickness>", "set annot 1 6", NULL},
  {"antialias","set antialias on|off", NULL, "on off"},
  {"arrlab",   "set arrlab on|off", NULL, "on off"},
  {"arrowhead","set arrowhead size", "set arrowhead 0.05", NULL},
  {"arrscl",   "set arrscl size <magnitude>", "set arrscl 0.5 20", NULL},
  {"background","set background color#    (0 black, 1 white)", "set background 1", "0 1"},
  {"barbase",  "set barbase value|bottom|top", "set barbase 0", "bottom top"},
  {"barbopts", "set barbopts outline|filled", NULL, "outline filled"},
  {"bargap",   "set bargap percent", "set bargap 20", NULL},
  {"baropts",  "set baropts outline|filled", NULL, "outline filled"},
  {"black",    "set black value1 value2 | off", "set black -1 1", "off"},
  {"button",   "set button on_fc on_bc on_oc1 on_oc2 off_fc off_bc off_oc1 off_oc2 thickness", NULL, NULL},
  {"cachesf",  "set cachesf factor", "set cachesf 2", NULL},
  {"ccolor",   "set ccolor color#|rainbow|revrain", "set ccolor 2", "rainbow revrain"},
  {"ccols",    "set ccols color1 color2 ... colorN    (with set clevs)", "set ccols 9 4 11 5 13 3 10 7 12 8 2", NULL},
  {"chunksize","set chunksize Xsize Ysize <Zsize> <Tsize> <Esize>", NULL, NULL},
  {"cint",     "set cint interval", "set cint 2", NULL},
  {"clab",     "set clab on|off|forced|masked|auto|format", "set clab %.1f", "on off forced masked auto"},
  {"clevs",    "set clevs level1 level2 ... levelN", "set clevs 270 280 290 300", NULL},
  {"clip",     "set clip xlo xhi ylo yhi", NULL, NULL},
  {"clopts",   "set clopts color <thickness <size>>", "set clopts -1 3 0.09", NULL},
  {"clskip",   "set clskip number <spacing>", "set clskip 2", NULL},
  {"cmark",    "set cmark marktype    (0 none, 1 cross, 2 open circle, 3 closed circle, ...)", "set cmark 0", NULL},
  {"cmax",     "set cmax value", NULL, NULL},
  {"cmin",     "set cmin value", NULL, NULL},
  {"coslat",   "set coslat on|off", NULL, "on off"},
  {"csmooth",  "set csmooth on|off|linear", NULL, "on off linear"},
  {"cstyle",   "set cstyle style    (1 solid, 2 long dash, 3 short dash, 5 dots, 0 none)", "set cstyle 3", NULL},
  {"cterp",    "set cterp on|off", NULL, "on off"},
  {"cthick",   "set cthick thickness    (1 to 12)", "set cthick 6", NULL},
  {"datawarn", "set datawarn on|off", NULL, "on off"},
  {"dbuff",    "set dbuff on|off", NULL, "on off"},
  {"defval",   "set defval name i j value", "set defval tm 10 20 0", NULL},
  {"dfile",    "set dfile file#", "set dfile 2", NULL},
  {"dialog",   "set dialog pc fc bc oc thickness <numeric|n>", NULL, NULL},
  {"dignum",   "set dignum digits", "set dignum 2", NULL},
  {"digsiz",   "set digsiz size", "set digsiz 0.1", NULL},
  {"display",  "set display color|grey|greyscale <black|white>", "set display color white", "color grey greyscale black white"},
  {"dropmenu", "set dropmenu fc bc oc1 oc2 tfc tbc toc1 toc2 bfc bbc boc1 boc2 soc1 soc2 thickness", NULL, NULL},
  {"e",        "set e e1 <e2>", "set e 1", NULL},
  {"ens",      "set ens name1 <name2>", NULL, NULL},
  {"fgvals",   "set fgvals value color <value color> ...", "set fgvals 1 2 2 4", NULL},
  {"font",     "set font number <file filename>", "set font 1", NULL},
  {"frame",    "set frame on|off|circle", NULL, "on off circle"},
  {"fwrite",   "set fwrite <-be|-le> <-sq|-st> <-ap|-cl> filename", "set fwrite -le ~/out.dat", "-be -le -sq -st -ap -cl"},
  {"geotiff",  "set geotiff <-flt|-dbl> filename_root", "set geotiff ~/map", "-flt -dbl"},
  {"grads",    "set grads on|off", NULL, "on off"},
  {"grid",     "set grid on|off|horizontal|vertical <style> <color#>", "set grid on 3 15", "on off horizontal vertical"},
  {"gridln",   "set gridln auto|off|color#", NULL, "auto off"},
  {"gxout",    "set gxout contour|shaded|grfill|line|bar|vector|stream|barb|grid|print|stat|...", "set gxout shaded",
               "contour shaded shade1 shade2 shade2b grfill fgrid grid line bar errbar linefill scatter "
               "vector stream barb print stat fwrite geotiff kml shp imap findstn model stnmark "
               "tserbarb tserwx value wxsym"},
  {"hempref",  "set hempref auto|nhem|shem", NULL, "auto nhem shem"},
  {"hershey",  "set hershey on|off", NULL, "on off"},
  {"imprun",   "set imprun script <arguments>", NULL, NULL},
  {"kml",      "set kml <-img|-ln|-poly> filename_root", "set kml -ln ~/contours", "-img -ln -poly"},
  {"lat",      "set lat lat1 <lat2>", "set lat -30 30", NULL},
  {"lev",      "set lev lev1 <lev2>", "set lev 1000 100", NULL},
  {"lfcols",   "set lfcols color1 color2    (gxout linefill)", "set lfcols 2 4", NULL},
  {"line",     "set line color# <style> <thickness>", "set line 2 1 6", NULL},
  {"log1d",    "set log1d loglog|logh|logv|off", NULL, "loglog logh logv off"},
  {"lon",      "set lon lon1 <lon2>", "set lon 100 150", NULL},
  {"loopdim",  "set loopdim x|y|z|t|e", NULL, "x y z t e"},
  {"looping",  "set looping on|off", NULL, "on off"},
  {"lwid",     "set lwid thickness width    (thickness 13 to 256)", "set lwid 13 2.5", NULL},
  {"map",      "set map auto | color <style> <thickness>", "set map 15 1 3", "auto"},
  {"mdlopts",  "set mdlopts noblank|blank|dig3|nodig3", NULL, "noblank blank dig3 nodig3"},
  {"missconn", "set missconn on|off", NULL, "on off"},
  {"misswarn", "set misswarn on|off file#", NULL, "on off"},
  {"mpdraw",   "set mpdraw on|off", NULL, "on off"},
  {"mpdset",   "set mpdset lowres|mres|hires|nmap", "set mpdset mres", "lowres mres hires nmap"},
  {"mproj",    "set mproj latlon|scaled|nps|sps|robinson|mollweide|orthogr|lambert|off", NULL,
               "latlon scaled nps sps robinson mollweide orthogr lambert off"},
  {"mpt",      "set mpt type off | color <style <thickness>>", "set mpt 1 15 1 3", NULL},
  {"mpvals",   "set mpvals lonmin lonmax latmin latmax | off", "set mpvals -10 40 30 60", "off"},
  {"parea",    "set parea xmin xmax ymin ymax | off", "set parea 1 10 1 7.5", "off"},
  {"poli",     "set poli on|off", NULL, "on off"},
  {"prnopts",  "set prnopts format numl numb <u>", "set prnopts %8.2f 10 1", NULL},
  {"rband",    "set rband number mode x1 y1 x2 y2", NULL, NULL},
  {"rbcols",   "set rbcols color1 color2 ... colorN | auto", "set rbcols 4 11 5 13 3 10 7 12 8 2", "auto"},
  {"rbrange",  "set rbrange low high", "set rbrange 270 310", NULL},
  {"rgb",      "set rgb color# red green blue <alpha>", "set rgb 16 255 128 0", NULL},
  {"sdfattr",  "set sdfattr name|global type attribute_name value", "set sdfattr tm String units K", NULL},
  {"sdfwrite", "set sdfwrite <-3dz|-3dt|-4d|-5d> <-rt|-re> <-flt|-dbl> <-nc3|-nc4> <-chunk> <-zip> filename",
               "set sdfwrite -nc4 ~/tm.nc",
               "-3dz -3dt -4d -5d -rt -re -flt -dbl -nc3 -nc4 -chunk -zip"},
  {"shp",      "set shp <-pt|-ln|-poly> <-fmt n m> filename_root", "set shp -ln ~/contours", "-pt -ln -poly -fmt"},
  {"shpattr",  "set shpattr name type value", NULL, NULL},
  {"shpopts",  "set shpopts fill_color <marktype> <marksize>", NULL, NULL},
  {"stat",     "set stat on|off", NULL, "on off"},
  {"stid",     "set stid on|off", NULL, "on off"},
  {"stnprint", "set stnprint on|off", NULL, "on off"},
  {"string",   "set string color <justification <thickness <rotation>>>    (justification tl tc tr l c r bl bc br)",
               "set string 1 c 6 0", NULL},
  {"strmden",  "set strmden density <spacing <size <type>>>    (density 1 to 10)", "set strmden 5", NULL},
  {"strmopts", "set strmopts density <spacing <size <type>>>    (density 1 to 10)", "set strmopts 5", NULL},
  {"strsiz",   "set strsiz width <height>", "set strsiz 0.15", NULL},
  {"t",        "set t t1 <t2>", "set t 1 12", NULL},
  {"threads",  "set threads N", "set threads 8", NULL},
  {"tile",     "set tile number type <width <height <thickness <fgcolor <bgcolor>>>>>", NULL, NULL},
  {"time",     "set time time1 <time2>", "set time 00z01jan2000 00z31dec2000", NULL},
  {"timelab",  "set timelab on|off", NULL, "on off"},
  {"tlsupp",   "set tlsupp year|month", NULL, "year month"},
  {"undef",    "set undef value | file file# | dfile", "set undef -9.99e8", "file dfile"},
  {"undo",     "set undo steps|on|off", "set undo 20", "on off"},
  {"vpage",    "set vpage xmin xmax ymin ymax | off", "set vpage 0 5.5 0 4.25", "off"},
  {"vrange",   "set vrange y1 y2", "set vrange 0 100", NULL},
  {"vrange2",  "set vrange2 x1 x2", NULL, NULL},
  {"warn",     "set warn on|off", NULL, "on off"},
  {"wxcols",   "set wxcols c1 c2 c3 c4 c5", NULL, NULL},
  {"wxopt",    "set wxopt wxsym|mark|char", NULL, "wxsym mark char"},
  {"x",        "set x x1 <x2>", "set x 1 72", NULL},
  {"xaxis",    "set xaxis start end <increment>", "set xaxis 0 360 30", NULL},
  {"xflip",    "set xflip on|off", NULL, "on off"},
  {"xlab",     "set xlab on|off|auto|format", "set xlab %.0f", "on off auto"},
  {"xlabs",    "set xlabs label1 | label2 | ...", "set xlabs Jan | Feb | Mar", NULL},
  {"xlevs",    "set xlevs value1 value2 ... valueN", NULL, NULL},
  {"xlint",    "set xlint interval", "set xlint 30", NULL},
  {"xlopts",   "set xlopts color <thickness <size>>", "set xlopts 1 4 0.15", NULL},
  {"xlpos",    "set xlpos offset b|t", "set xlpos 0 b", NULL},
  {"xsize",    "set xsize xpixels ypixels", "set xsize 800 600", NULL},
  {"xyrev",    "set xyrev on|off", NULL, "on off"},
  {"y",        "set y y1 <y2>", "set y 1 46", NULL},
  {"yaxis",    "set yaxis start end <increment>", "set yaxis 1000 100 100", NULL},
  {"yflip",    "set yflip on|off", NULL, "on off"},
  {"ylab",     "set ylab on|off|auto|format", "set ylab %.1f", "on off auto"},
  {"ylabs",    "set ylabs label1 | label2 | ...", NULL, NULL},
  {"ylevs",    "set ylevs value1 value2 ... valueN", NULL, NULL},
  {"ylint",    "set ylint interval", "set ylint 10", NULL},
  {"ylopts",   "set ylopts color <thickness <size>>", "set ylopts 1 4 0.15", NULL},
  {"ylpos",    "set ylpos offset l|r", "set ylpos 0 l", NULL},
  {"z",        "set z z1 <z2>", "set z 1 10", NULL},
  {"zlog",     "set zlog on|off", NULL, "on off"},
  {NULL,NULL,NULL,NULL}
};

static const struct gahelp qrys[] = {
  {"attr",     "q attr <file#>", NULL, NULL},
  {"cache",    "q cache <file#>", NULL, NULL},
  {"cachesf",  "q cachesf", NULL, NULL},
  {"config",   "q config", NULL, NULL},
  {"contours", "q contours", NULL, NULL},
  {"ctlinfo",  "q ctlinfo <file#>", NULL, NULL},
  {"dbf",      "q dbf shapefile <delimiter>", NULL, NULL},
  {"define",   "q define <name>", NULL, NULL},
  {"defval",   "q defval name i j", "q defval tm 10 20", NULL},
  {"dialog",   "q dialog <x y w h> text", NULL, NULL},
  {"dims",     "q dims", NULL, NULL},
  {"ens",      "q ens", NULL, NULL},
  {"fgvals",   "q fgvals", NULL, NULL},
  {"file",     "q file <file#>", "q file 2", NULL},
  {"files",    "q files", NULL, NULL},
  {"font",     "q font number", "q font 1", NULL},
  {"fwrite",   "q fwrite", NULL, NULL},
  {"gr2w",     "q gr2w i j", "q gr2w 10 20", NULL},
  {"gr2xy",    "q gr2xy i j", "q gr2xy 10 20", NULL},
  {"gxconfig", "q gxconfig", NULL, NULL},
  {"gxinfo",   "q gxinfo", NULL, NULL},
  {"gxout",    "q gxout", NULL, NULL},
  {"pos",      "q pos <nowait>", NULL, "nowait"},
  {"pp2xy",    "q pp2xy x y", NULL, NULL},
  {"sdfwrite", "q sdfwrite", NULL, NULL},
  {"shades",   "q shades", NULL, NULL},
  {"shp",      "q shp shapefile", NULL, NULL},
  {"shpopts",  "q shpopts", NULL, NULL},
  {"string",   "q string text", "q string Surface temperature", NULL},
  {"threads",  "q threads", NULL, NULL},
  {"time",     "q time", NULL, NULL},
  {"udpt",     "q udpt", NULL, NULL},
  {"undef",    "q undef", NULL, NULL},
  {"undo",     "q undo", NULL, NULL},
  {"w2gr",     "q w2gr lon lat", "q w2gr 120 25", NULL},
  {"w2xy",     "q w2xy lon lat", "q w2xy 120 25", NULL},
  {"xinfo",    "q xinfo", NULL, NULL},
  {"xy2gr",    "q xy2gr x y", "q xy2gr 5.5 4.25", NULL},
  {"xy2w",     "q xy2w x y", "q xy2w 5.5 4.25", NULL},
  {NULL,NULL,NULL,NULL}
};

static const struct gahelp funcs[] = {
  {"aave",     "aave(expr, xdim1, xdim2, ydim1, ydim2)    (weighted by latitude)", "aave(ts, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"abs",      "abs(expr)", "abs(u)", NULL},
  {"acos",     "acos(expr)", NULL, NULL},
  {"amax",     "amax(expr, xdim1, xdim2, ydim1, ydim2)", "amax(ts, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"amaxlocx", "amaxlocx(expr, xdim1, xdim2, ydim1, ydim2)", "amaxlocx(ts, x=1, x=72, y=1, y=46)", NULL},
  {"amaxlocy", "amaxlocy(expr, xdim1, xdim2, ydim1, ydim2)", "amaxlocy(ts, x=1, x=72, y=1, y=46)", NULL},
  {"amean",    "amean(expr, xdim1, xdim2, ydim1, ydim2)    (not weighted by latitude)", "amean(ts, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"amin",     "amin(expr, xdim1, xdim2, ydim1, ydim2)", "amin(ts, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"aminlocx", "aminlocx(expr, xdim1, xdim2, ydim1, ydim2)", "aminlocx(ts, x=1, x=72, y=1, y=46)", NULL},
  {"aminlocy", "aminlocy(expr, xdim1, xdim2, ydim1, ydim2)", "aminlocy(ts, x=1, x=72, y=1, y=46)", NULL},
  {"asin",     "asin(expr)", NULL, NULL},
  {"asum",     "asum(expr, xdim1, xdim2, ydim1, ydim2)", "asum(pr, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"asumg",    "asumg(expr, xdim1, xdim2, ydim1, ydim2)", "asumg(pr, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"atan2",    "atan2(expr1, expr2)", "atan2(v, u)", NULL},
  {"atot",     "atot(expr, xdim1, xdim2, ydim1, ydim2)", "atot(pr, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"ave",      "ave(expr, dim1, dim2 <,tinc> <,-b>)", "ave(ts, t=1, t=12)    ave(ts, lon=0, lon=360, -b)", NULL},
  {"cdiff",    "cdiff(expr, dim)", "cdiff(ts, x)", NULL},
  {"coll2gr",  "coll2gr(collection# <,levels>)", "coll2gr(1, -u)", NULL},
  {"const",    "const(expr, value <,-u|-a>)", "const(pr, 0, -u)", NULL},
  {"cos",      "cos(expr)", "cos(lat*3.1416/180)", NULL},
  {"eloop",    "eloop(expr)", NULL, NULL},
  {"exp",      "exp(expr)", NULL, NULL},
  {"fndlvl",   "fndlvl(expr, expr_to_find, lev1, lev2)", "fndlvl(ta, 273.15, lev=1000, lev=100)", NULL},
  {"gint",     "gint(expr, dim1, dim2)", "gint(pr, t=1, t=12)", NULL},
  {"gr2stn",   "gr2stn(grid_expr, stn_expr <,-n>)  or  gr2stn(grid_expr, lon, lat <,-n>)", NULL, NULL},
  {"hcurl",    "hcurl(uexpr, vexpr)", "hcurl(u, v)", NULL},
  {"hdivg",    "hdivg(uexpr, vexpr)", "hdivg(u, v)", NULL},
  {"if",       "if(logical_expr, then_expr, else_expr)", "if(ts>273.15, 1, 0)", NULL},
  {"log",      "log(expr)", NULL, NULL},
  {"log10",    "log10(expr)", NULL, NULL},
  {"lterp",    "lterp(source, dest)", "lterp(ts, ps.2)", NULL},
  {"mag",      "mag(uexpr, vexpr)", "mag(u, v)", NULL},
  {"maskout",  "maskout(expr, mask)    (missing where mask < 0)", "maskout(ts, ts-273.15)", NULL},
  {"max",      "max(expr, dim1, dim2 <,tinc>)", "max(ts, t=1, t=12)", NULL},
  {"maxloc",   "maxloc(expr, dim1, dim2 <,tinc>)", "maxloc(ts, t=1, t=12)", NULL},
  {"mean",     "mean(expr, dim1, dim2 <,tinc> <,-b>)", "mean(ts, t=1, t=12)", NULL},
  {"min",      "min(expr, dim1, dim2 <,tinc>)", "min(ts, t=1, t=12)", NULL},
  {"minloc",   "minloc(expr, dim1, dim2 <,tinc>)", "minloc(ts, t=1, t=12)", NULL},
  {"oabin",    "oabin(grid_expr, stn_expr <,-flag>)", NULL, NULL},
  {"oacres",   "oacres(grid_expr, stn_expr <,radii>)", NULL, NULL},
  {"pow",      "pow(expr1, expr2)", "pow(u, 2)", NULL},
  {"s2g1d",    "s2g1d(stn_expr)", NULL, NULL},
  {"scorr",    "scorr(expr1, expr2, xdim1, xdim2, ydim1, ydim2)", "scorr(ts, ts.2, lon=0, lon=360, lat=-90, lat=90)", NULL},
  {"sin",      "sin(expr)", NULL, NULL},
  {"skip",     "skip(expr, skipx <,skipy>)", "skip(u, 2, 2)", NULL},
  {"smth9",    "smth9(expr)", "smth9(ts)", NULL},
  {"sqrt",     "sqrt(expr)", "sqrt(u*u+v*v)", NULL},
  {"sregr",    "sregr(expr1, expr2, xdim1, xdim2, ydim1, ydim2)", NULL, NULL},
  {"stnave",   "stnave(stn_expr, dim1, dim2 <,-m count>)", NULL, NULL},
  {"stnmax",   "stnmax(stn_expr, dim1, dim2 <,-m count>)", NULL, NULL},
  {"stnmin",   "stnmin(stn_expr, dim1, dim2 <,-m count>)", NULL, NULL},
  {"sum",      "sum(expr, dim1, dim2 <,tinc> <,-b>)", "sum(pr, t=1, t=12)", NULL},
  {"sumg",     "sumg(expr, dim1, dim2 <,tinc> <,-b>)", "sumg(pr, t=1, t=12)", NULL},
  {"tan",      "tan(expr)", NULL, NULL},
  {"tcorr",    "tcorr(expr1, expr2, tdim1, tdim2)", "tcorr(ts, pr, t=1, t=60)", NULL},
  {"tloop",    "tloop(expr)", "tloop(aave(ts, lon=0, lon=360, lat=-90, lat=90))", NULL},
  {"tmave",    "tmave(mask_expr, expr, tdim1, tdim2)", NULL, NULL},
  {"tregr",    "tregr(expr1, expr2, tdim1, tdim2)", "tregr(ts, pr, t=1, t=60)", NULL},
  {"tvrh2q",   "tvrh2q(tv_expr, rh_expr)", NULL, NULL},
  {"tvrh2t",   "tvrh2t(tv_expr, rh_expr)", NULL, NULL},
  {"vint",     "vint(ps_expr, expr, top)", "vint(ps, q, 300)", NULL},
  {NULL,NULL,NULL,NULL}
};

static const struct gahelp *tables[] = {cmds, sets, qrys, funcs, draws};

static const struct gahelp *find (const struct gahelp *t, const char *name) {
  if (name==NULL) return (NULL);
  for (; t->name; t++) if (!strcmp(t->name,name)) return (t);
  return (NULL);
}

const char *gahelpname (gaint kind, gaint i) {
const struct gahelp *t;
gaint n;
  if (kind<0 || kind>4 || i<0) return (NULL);
  t = tables[kind];
  for (n=0; t[n].name && n<i; n++) ;
  return (t[n].name);
}

const char *gahelpwords (gaint kind, const char *name) {
const struct gahelp *h;
  if (kind<0 || kind>4) return (NULL);
  h = find(tables[kind],name);
  return (h ? h->words : NULL);
}

/* ---- what failed while a command ran ---- */

static char failfn[32];                  /* the first function that failed */
static char badname[32];                 /* a name that is neither variable nor function */
static gaint failed;                     /* the command failed, though it says it did not */

void gahelpclear (void) {
  failfn[0] = '\0';
  badname[0] = '\0';
  failed = 0;
}

/* What went wrong: 1, a name that is neither a variable nor a function;
   2, a function that failed; 3, the command failed but returns success, as
   query and draw do with an unknown option. */

void gahelpnote (gaint what, const char *name) {
char *to = what==1 ? badname : failfn;
size_t i;
  if (what==3) { failed = 1; return; }
  if (*to || name==NULL) return;
  for (i=0; name[i] && i<sizeof(failfn)-1; i++) to[i] = tolower((unsigned char)name[i]);
  to[i] = '\0';
}

/* ---- the hints ---- */

static void say (const struct gahelp *h) {
char line[512];
  snprintf(line,sizeof(line),"  Usage: %s\n",h->usage);
  gaprnt(0,line);
  if (h->example) {
    snprintf(line,sizeof(line),"  e.g.   %s\n",h->example);
    gaprnt(0,line);
  }
}

/* How many edits turn a into b (insertions, deletions, substitutions and
   swaps of neighbours), up to 3; more counts as 3. */

static gaint edits (const char *a, const char *b) {
gaint la,lb,i,j,c,d[33][33];
  la = strlen(a); lb = strlen(b);
  if (la>32 || lb>32) return (3);
  if (la-lb>2 || lb-la>2) return (3);
  for (i=0; i<=la; i++) d[i][0] = i;
  for (j=0; j<=lb; j++) d[0][j] = j;
  for (i=1; i<=la; i++) {
    for (j=1; j<=lb; j++) {
      c = d[i-1][j-1] + (a[i-1]!=b[j-1]);
      if (d[i-1][j]+1<c) c = d[i-1][j]+1;
      if (d[i][j-1]+1<c) c = d[i][j-1]+1;
      if (i>1 && j>1 && a[i-1]==b[j-2] && a[i-2]==b[j-1] && d[i-2][j-2]+1<c) c = d[i-2][j-2]+1;
      d[i][j] = c;
    }
  }
  return (d[la][lb]>3 ? 3 : d[la][lb]);
}

/* The names closest to word: from table t, and with vars from the open
   files' variables and the defined ones too. They follow "Did you mean:";
   when the closest is in t, its usage follows that. Gives how many. */

struct near { char name[40]; gaint d; const struct gahelp *h; };

static void consider (struct near *best, gaint *n, const char *name, const struct gahelp *h,
                      const char *word) {
gaint d,i,j;
  d = edits(word,name);
  if (d==0 || d>=3 || (strlen(word)<=3 && d>1)) return;
  for (i=0; i<*n; i++) if (!strcmp(best[i].name,name)) return;
  for (i=0; i<*n && best[i].d<=d; i++) ;
  if (i>=3) return;
  for (j=(*n<3 ? *n : 2); j>i; j--) best[j] = best[j-1];
  snprintf(best[i].name,sizeof(best[i].name),"%s",name);
  best[i].d = d;
  best[i].h = h;
  if (*n<3) (*n)++;
}

static gaint suggest (const char *word, const struct gahelp *t, gaint vars, struct gacmn *pcm) {
struct near best[3];
struct gafile *pfi;
struct gadefn *pdf;
char line[256];
gaint n=0,i,v;

  for (; t && t->name; t++) consider(best,&n,t->name,t,word);
  if (vars && pcm) {
    for (pfi=pcm->pfi1; pfi; pfi=pfi->pforw)
      for (v=0; v<pfi->vnum; v++) consider(best,&n,pfi->pvar1[v].abbrv,NULL,word);
    for (pdf=pcm->pdf1; pdf; pdf=pdf->pforw) consider(best,&n,pdf->abbrv,NULL,word);
  }
  if (n==0) return (0);
  snprintf(line,sizeof(line),"  Did you mean:");
  for (i=0; i<n; i++) {
    strncat(line,i ? ", " : " ",sizeof(line)-strlen(line)-1);
    strncat(line,best[i].name,sizeof(line)-strlen(line)-1);
  }
  strncat(line,"?\n",sizeof(line)-strlen(line)-1);
  gaprnt(0,line);
  if (best[0].h) say(best[0].h);
  return (n);
}

/* The function whose argument list is open at the end of expr, as in
   "mean(ts,t=1": the one the expression broke off in. */

static gaint openfunc (const char *expr, char *name, size_t n) {
char stack[16][32],word[32];
gaint depth=0,i;
const char *p;
  for (p=expr; *p; ) {
    if (isalpha((unsigned char)*p)) {
      for (i=0; (isalnum((unsigned char)*p) || *p=='_') && i<31; p++) word[i++] = tolower((unsigned char)*p);
      word[i] = '\0';
      while (isalnum((unsigned char)*p) || *p=='_') p++;
      while (*p==' ') p++;
      if (*p=='(') {
        if (depth<16) snprintf(stack[depth],32,"%s",word);
        depth++;
        p++;
      }
      continue;
    }
    if (*p=='(') { if (depth<16) stack[depth][0] = '\0'; depth++; }
    else if (*p==')' && depth>0) depth--;
    p++;
  }
  while (depth>0) {
    depth--;
    if (depth<16 && stack[depth][0] && find(funcs,stack[depth])) {
      snprintf(name,n,"%s",stack[depth]);
      return (1);
    }
  }
  return (0);
}

/* After an expression failed (display, define). */

static void hintexpr (const char *expr, struct gacmn *pcm) {
const struct gahelp *h;
char name[32];

  if (badname[0]) {                 /* neither variable nor function */
    suggest(badname,funcs,1,pcm);
    return;
  }
  if (failfn[0] && (h=find(funcs,failfn))!=NULL) {
    say(h);
    return;
  }
  if (expr && openfunc(expr,name,sizeof(name)) && (h=find(funcs,name))!=NULL) say(h);
}

/* After a command typed at the prompt failed: com is the command line,
   in lower case. */

gaint gahelpfailed (void) {
  return (failed);
}

void gahelpafter (char *com, struct gacmn *pcm) {
char w1[32],w2[32],*p,*e;
const struct gahelp *h;
gaint i;

  for (p=com; *p==' '; p++) ;
  for (i=0; p[i] && p[i]!=' ' && i<31; i++) w1[i] = p[i];
  w1[i] = '\0';
  for (p+=i; *p==' '; p++) ;
  for (i=0; p[i] && p[i]!=' ' && i<31; i++) w2[i] = p[i];
  w2[i] = '\0';

  /* define name = expression, or just name = expression */
  e = strchr(com,'=');
  if (!strcmp(w1,"define") || strchr(w1,'=') || w2[0]=='=') {
    if (e) hintexpr(e+1,pcm);
    else say(find(cmds,"define"));
    return;
  }
  if (!strcmp(w1,"d") || !strcmp(w1,"display")) {
    if (*p) hintexpr(p,pcm);
    else say(find(cmds,"display"));
    return;
  }
  if (!strcmp(w1,"set")) {
    if (w2[0]=='\0') { say(find(cmds,"set")); return; }
    if ((h=find(sets,w2))!=NULL) { say(h); return; }
    if (!suggest(w2,sets,0,NULL)) say(find(cmds,"set"));
    return;
  }
  if (!strcmp(w1,"q") || !strcmp(w1,"query")) {
    if ((h=find(qrys,w2))!=NULL) { say(h); return; }
    suggest(w2,qrys,0,NULL);
    return;
  }
  if (!strcmp(w1,"draw")) {
    if ((h=find(draws,w2))!=NULL) { say(h); return; }
    if (!suggest(w2,draws,0,NULL)) say(find(cmds,"draw"));
    return;
  }
  if (!strcmp(w1,"run") || !strcmp(w1,"exec")) return;   /* the script's own errors */
  if ((h=find(cmds,w1))!=NULL) { say(h); return; }
  suggest(w1,cmds,0,NULL);
}
