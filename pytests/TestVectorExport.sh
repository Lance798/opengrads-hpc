#!/usr/bin/env bash
# Vector page geometry, background isolation and graphics output regressions.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python3 - "$repo_root" <<'PY'
import os,shlex,subprocess,sys,tempfile
from pathlib import Path
root=Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='opengrads-unite-') as directory:
    binary=Path(directory)/'unite'
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','cairo'],text=True))
    command=shlex.split(os.environ.get('CC','cc'))+['-std=c99','-O2','-I'+str(root/'cola/src'),
             str(root/'pytests/vector_unite.c'),'-o',str(binary)]+flags+['-lm']
    subprocess.run(command,check=True)
    environment=dict(os.environ)
    environment.pop('OPENGRADS_VECTOR_UNITE',None)
    subprocess.run([str(binary)],check=True,cwd=directory,env=environment)
PY
python3 "$repo_root/pytests/vector_export.py"
