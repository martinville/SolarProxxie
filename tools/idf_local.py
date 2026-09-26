"""Use the optional project-local ESP-IDF installation on Windows."""
from pathlib import Path
import os, subprocess, sys
root=Path(__file__).resolve().parents[1]
sdk=root/'.tools/esp-idf-v5.4.2'
tools=root/'.tools/espressif'
env=os.environ.copy()
env['IDF_PATH']=str(sdk)
env['IDF_TOOLS_PATH']=str(tools)
env['ESP_ROM_ELF_DIR']=str(tools/'tools/esp-rom-elfs/20241011')
env['IDF_COMPONENT_MANAGER']='0'
env['GIT_CONFIG_COUNT']='1'
env['GIT_CONFIG_KEY_0']='safe.directory'
env['GIT_CONFIG_VALUE_0']=sdk.as_posix()
pyenv=next((tools/'python_env').glob('idf5.4*'))
env['IDF_PYTHON_ENV_PATH']=str(pyenv)
bins=[str(pyenv/'Scripts'),str(root/'.tools/mingit/cmd')]
bins.extend(str(p) for p in (tools/'tools').rglob('bin') if p.is_dir())
bins.extend(str(p.parent) for p in (tools/'tools').rglob('ninja.exe'))
env['PATH']=os.pathsep.join(bins+[env.get('PATH','')])
if 'build' in sys.argv[1:]:
    # ESP-IDF embeds __DATE__/__TIME__ in this object. Ninja otherwise reuses an
    # older object on incremental builds, even when the application has changed.
    app_desc=root/'build/esp-idf/esp_app_format/CMakeFiles/__idf_esp_app_format.dir/esp_app_desc.c.obj'
    if app_desc.is_file():
        app_desc.unlink()
sys.exit(subprocess.call([str(pyenv/'Scripts/python.exe'),str(sdk/'tools/idf.py'),*sys.argv[1:]],env=env,cwd=root))
