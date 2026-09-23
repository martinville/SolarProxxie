"""Download a pinned local SDK. Run explicitly; never part of a firmware build."""
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / '.tools'
DEST.mkdir(exist_ok=True)
url = 'https://github.com/espressif/esp-idf/releases/download/v5.4.2/esp-idf-v5.4.2.zip'
archive = DEST / 'esp-idf-v5.4.2.zip'
if not archive.exists():
    print('Downloading ESP-IDF 5.4.2', flush=True)
    urllib.request.urlretrieve(url, archive)
if not (DEST / 'esp-idf-v5.4.2' / 'tools' / 'idf.py').exists():
    print('Extracting SDK', flush=True)
    with zipfile.ZipFile(archive) as z:
        z.extractall(DEST)
print('SDK ready', flush=True)
