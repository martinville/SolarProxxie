"""Compile conditional replay and active-low LED branches using the build database."""
from pathlib import Path
import json,os,re,shlex,subprocess
root=Path(__file__).resolve().parents[1]
entries=json.loads((root/'build/compile_commands.json').read_text())
out=root/'build/host';out.mkdir(exist_ok=True)
for name in ('app_main.c','router.c'):
    entry=next(x for x in entries if Path(x['file']).name==name)
    target=(out/f'replay_{name}.obj').as_posix()
    command=re.sub(r' -o [^ ]+',f' -o "{target}"',entry['command'])
    command=re.sub(r' -MF [^ ]+',f' -MF "{target}.d"',command)
    command=re.sub(r' -MT [^ ]+',f' -MT "{target}"',command)
    command+=' -include "'+(root/'tests/replay_override.h').as_posix()+'"'
    subprocess.run(command if os.name=='nt' else shlex.split(command),cwd=entry['directory'],check=True)
print('PASS compile-only variants: replay parser and active-low GPIO 2 LED')
