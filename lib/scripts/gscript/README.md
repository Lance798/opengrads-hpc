# gscript

Scripts and script functions for GrADS by Chihiro Kodama, from
<https://github.com/kodamail/gscript> (commit 88939ab, 2025-08-20),
distributed under the GNU General Public License version 3 (`LICENSE`).
The files are as upstream has them.

The launchers put this directory on `GASCRP` after `lib/scripts`, so the
scripts run by name (`color`, `xcbar`, `run hatch.gs`) and their functions
(`.gsf`) load with `gsfallow('on')`. A script here finds the functions it
calls in this directory first.

Your own scripts come first: one by the same name in the current directory,
or in a directory on your own `GASCRP`, is the one that runs. A function of
your own by the same name is the one your scripts get, while the scripts
here keep using theirs.

Scripts with help print it when run without arguments (`xcbar`), and
`color` is the usual start:

```text
ga-> set gxout shaded
ga-> color 220 280 -kind bluered -xcbar
ga-> d ts
```

## Not included

Four files have the same names as scripts from bGASL that already ship in
`lib/scripts`, and are different scripts:

- `drawline.gs`, `drawmark.gs`, `save.gs`
- `qdims.gsf`: here a function that returns one value
  (`qdims('tmin')`); bGASL's sets global variables
  (`qdims(snap_to_grid, outname)`)

Seven call gscript's `qdims()`, and would get bGASL's, so they are left out
with it: `lreg.gs`, `saveanim.gs`, `setlabs.gs`, `setshift.gs`, `shift.gs`,
`tile.gs` and `time_avail.gsf`.
