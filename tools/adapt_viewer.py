import os, shutil, re

SRC_HIST = r'G:\Programacion\SparkingRankedDetails-v1.11.0-AllInOne\Mods\SparkingRankedDetails\history'
DST_HIST = r'G:\Jueguitos\BT4decomp-windows-x64\BT4RankedDetails\history'

os.makedirs(os.path.join(DST_HIST, 'vendor'), exist_ok=True)
os.makedirs(os.path.join(DST_HIST, 'chars'), exist_ok=True)

# Copy core assets
for fname in ['app.css', 'build-history.py', 'refresh.bat']:
    shutil.copy2(os.path.join(SRC_HIST, fname), os.path.join(DST_HIST, fname))
for fname in os.listdir(os.path.join(SRC_HIST, 'vendor')):
    shutil.copy2(os.path.join(SRC_HIST, 'vendor', fname), os.path.join(DST_HIST, 'vendor', fname))

# 1. Adapt app.js: disable Steam profile links (BT4 has no Steam) and rebrand title/strings
app_js = open(os.path.join(SRC_HIST, 'app.js'), 'r', encoding='utf-8').read()
# Replace SteamID64 validator ha so it returns null (hides Steam link icon cleanly, while opponent_id still groups rivals!)
app_js_patched = app_js.replace(
    'var ha=e=>typeof e==`string`&&/^7656119\\d{10}$/.test(e)?e:null',
    'var ha=e=>null'
)
app_js_patched = app_js_patched.replace('Sparking Ranked Details', 'BT4 Ranked Details')
app_js_patched = app_js_patched.replace('Sparking! ZERO', 'DBZ BT4 Decomp')
open(os.path.join(DST_HIST, 'app.js'), 'w', encoding='utf-8').write(app_js_patched)

# 2. Adapt index.html: rebrand title, templates, and instructions for BT4 (non-Steam)
idx_html = open(os.path.join(SRC_HIST, 'index.html'), 'r', encoding='utf-8').read()
idx_html = idx_html.replace('Sparking Ranked Details', 'BT4 Ranked Details')
idx_html = idx_html.replace('SparkingRankedDetails.ini', 'BT4RankedDetails.ini')
idx_html = idx_html.replace(r'Mods\SparkingRankedDetails\history\index.html', r'ranked_details\history\index.html')
idx_html = idx_html.replace('SteamID', 'Account ID')
idx_html = idx_html.replace('Steam profile (opens in the browser)', 'BT4 Account')
idx_html = idx_html.replace('Perfil de Steam (se abre en el navegador)', 'Cuenta BT4')
idx_html = idx_html.replace('Sparking! activations', 'Max Power activations')
idx_html = idx_html.replace('Time in Sparking!', 'Time in Max Power')
open(os.path.join(DST_HIST, 'index.html'), 'w', encoding='utf-8').write(idx_html)

# 3. Adapt build-history.py defaults for BT4RankedDetails
bh = open(os.path.join(DST_HIST, 'build-history.py'), 'r', encoding='utf-8').read()
bh = bh.replace('Sparking Ranked Details', 'BT4 Ranked Details')
bh = bh.replace(r'Mods\SparkingRankedDetails', r'ranked_details')
bh = bh.replace('"packet_loss"):', '"packet_loss", "region", "transport", "game_speed", "rollback"):')
open(os.path.join(DST_HIST, 'build-history.py'), 'w', encoding='utf-8').write(bh)

# 4. Write clean initial placeholder data.js
data_js = (
    '// Initial placeholder - overwritten automatically by BT4RankedDetails after your first online match.\n'
    'window.SZ_GENERATED = "";\n'
    'window.SZ_META = {"generated":"","writer":"BT4RankedDetails 1.0.0","source":"jsonl","log_missing":false,"csv_present":false,"filtered_no_samples":0,"malformed":0,"other_lines":0,"ignored_keys":[],"crosscheck":[]};\n'
    'window.SZ_INCIDENTS = [];\n'
    'window.SZ_MATCHES = [];\n'
)
open(os.path.join(DST_HIST, 'data.js'), 'w', encoding='utf-8').write(data_js)

print('Adapted history viewer OK:', DST_HIST)
print('Steam validator patched:', 'var ha=e=>null' in app_js_patched)
