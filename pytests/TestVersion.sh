#!/usr/bin/env bash
# Launcher identities follow source/package metadata, even without a build.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python3 - "$repo_root" <<'PY'
import os,shutil,subprocess,sys,tempfile
from pathlib import Path
repo=Path(sys.argv[1]);current=(repo/'release/VERSION').read_text().strip();base=(repo/'cola/src/VERSION').read_text().strip()
env=dict(os.environ,OPENGRADS_BUILD_ROOT='/nonexistent',OPENGRADS_UPDATE_CHECK='0',LD_LIBRARY_PATH='/nonexistent')
with tempfile.TemporaryDirectory(prefix='opengrads-version-') as tmp:
 for kind in ('source','linux-archive','macos-archive'):
  root=Path(tmp)/kind;(root/'libexec').mkdir(parents=True)
  (root/'etc').mkdir();(root/'etc/udpt-local').touch()
  shutil.copy2(repo/'libexec/opengrads-version',root/'libexec/opengrads-version')
  shutil.copy2(repo/('release/opengrads-macos' if kind=='macos-archive' else 'opengrads'),root/'opengrads')
  marker=root/'update-called'
  update=root/'libexec/opengrads-update';update.write_text('#!/bin/bash\nprintf update > "'+str(marker)+'"\nprintf "UPDATE_ONLY\\n"\n');update.chmod(0o755)
  def metadata(version):
   if kind=='source':
    (root/'release').mkdir(exist_ok=True);(root/'release/VERSION').write_text(version+'\n')
    (root/'cola/src').mkdir(parents=True,exist_ok=True);(root/'cola/src/VERSION').write_text(base+'\n')
   else:(root/'VERSION').write_text(f'opengrads-hpc {version}\nGrADS base {base}\n')
  def run(args,settings=env):
   p=subprocess.run([str(root/'opengrads'),*args],env=settings,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
   assert p.returncode==0,(kind,p.stdout,p.stderr)
   return p.stdout
  for version in (current,'73.21.9'):
   metadata(version)
   assert run(['--version'])==f'opengrads-hpc {version}\nGrADS base {base}\n'
   assert not marker.exists(),'version lookup invoked the updater'
  if kind=='macos-archive':binary=root/'bin/grads'
  else:
   binary=root/'build/src/grads';(root/'build/src/.libs').mkdir(parents=True)
   (root/'build/src/.libs/libgxdummy.so').touch()
  binary.parent.mkdir(parents=True,exist_ok=True)
  binary.write_text('#!/bin/bash\nprintf "GRADS_STARTED\\n"\n');binary.chmod(0o755)
  settings=dict(env,OPENGRADS_BUILD_ROOT=str(root/'build'))
  out=run(['-bl','-d','gxdummy','-h','gxdummy'],settings)
  assert out.count(f'opengrads-hpc 73.21.9 (GrADS {base})')==1 and 'GRADS_STARTED' in out,(kind,out)
  assert run(['--update','--check'])=='UPDATE_ONLY\n'
  print('Version identity passed:',kind,flush=True)
print('Version regression passed: automatic metadata updates, no-build CLI, startup banner and update forwarding')
PY
