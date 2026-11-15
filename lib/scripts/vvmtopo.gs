* VVM terrain obstruction heights on the native default-file X/Y grid.
* Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
* Usage: vvmtopo field [periodic_x] [periodic_y]
* X/Y are periodic by default (1,1); use vvmtopo topo 0 0 for closed edges.
* Results: topou, topov, topow and the corrected working copy toposrc.
* Every input zero is treated as 1; other values are preserved in toposrc.
* topou=max(this,right), topov=max(this,next-row), topow=toposrc.
* Uses the full native default-file grid at Z=1,T=1, then restores X/Y/Z/T/E.
* The original dimension environment is also restored after calculation errors.
* Input must be finite, fully defined terrain elevations/level indices.
* Periodic neighbors are copied exactly by ROLL. Closed boundaries use
* forward CDIFF, validated on small integer VVM terrain indices.
* Missing/nonfinite data and huge magnitude differences with closed edges
* are outside this script's input contract.
* Replaces the four result variables above; the input field is preserved
* unless it has one of those reserved result names.
* Bulk grid operations use OpenMP when available; no per-cell script loops.
function main(args)
  fld = subwrd(args,1)
  px = subwrd(args,2)
  py = subwrd(args,3)
  if (fld = '')
    say 'Usage: vvmtopo field [periodic_x] [periodic_y] (defaults: 1 1)'
    return
  endif
  if (px = '')
    px = 1
  endif
  if (py = '')
    py = 1
  endif
  'q file'
  line = sublin(result,5)
  nx = subwrd(line,3)
  ny = subwrd(line,6)
  if (nx = '' | ny = '')
    say 'Error: could not determine Xsize/Ysize from q file.'
    return
  endif
  'q dims'
  if (rc != 0)
    say 'Error: could not save the dimension environment.'
    return
  endif
  dims = result
  i = 1
  while (i <= 5)
    line = sublin(dims,i+1)
    axis = subwrd(line,1)
    if (subwrd(line,3) = 'varying')
      saved.i = 'set 'axis' 'subwrd(line,11)' 'subwrd(line,13)
    else
      saved.i = 'set 'axis' 'subwrd(line,9)
    endif
    i = i+1
  endwhile
  status = build(fld,px,py,nx,ny)
  i = 1
  while (i <= 5)
    saved.i
    i = i+1
  endwhile
  return

* Keep calculation returns inside this function so main always restores dims.
function build(fld,px,py,nx,ny)
  'set x 1 'nx
  'set y 1 'ny
  'set z 1'
  'set t 1'
  'define toposrc = 'fld
  if (rc != 0)
    say 'Error: could not define toposrc from 'fld
    return
  endif
  'define toposrc = if(toposrc=0,1,toposrc)'
  if (rc != 0)
    say 'Error: could not normalize zero terrain values.'
    return
  endif
  'define topow = toposrc'
  if (rc != 0)
    say 'Error: could not define topow.'
    return
  endif
  if (px = 1)
    'define topou = roll(toposrc,-1,x)'
    if (rc != 0)
      say 'Error: could not roll X neighbors.'
      return
    endif
    'define topou = if(toposrc>topou,toposrc,topou)'
  else
    if (nx > 1)
      'define topou = const(cdiff(toposrc,xp),0,-u)'
      if (rc != 0)
        say 'Error: could not compute X neighbors.'
        return
      endif
      'define topou = toposrc+if(topou>0,topou,0)'
    else
      'define topou = toposrc'
    endif
  endif
  if (rc != 0)
    say 'Error: could not define topou.'
    return
  endif
  if (py = 1)
    'define topov = roll(toposrc,-1,y)'
    if (rc != 0)
      say 'Error: could not roll Y neighbors.'
      return
    endif
    'define topov = if(toposrc>topov,toposrc,topov)'
  else
    if (ny > 1)
      'define topov = const(cdiff(toposrc,yp),0,-u)'
      if (rc != 0)
        say 'Error: could not compute Y neighbors.'
        return
      endif
      'define topov = toposrc+if(topov>0,topov,0)'
    else
      'define topov = toposrc'
    endif
  endif
  if (rc != 0)
    say 'Error: could not define topov.'
    return
  endif
  say 'Created topou, topov, and topow from 'fld' (topo=0 treated as 1)'
  say 'Grid: 'nx' x 'ny' ; periodic_x='px' periodic_y='py
  say 'Display with: d topou; d topov; d topow'
  return
