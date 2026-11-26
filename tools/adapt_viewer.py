import os, shutil, re

SRC_HIST = r'G:\Programacion\SparkingRankedDetails-v1.11.0-AllInOne\Mods\SparkingRankedDetails\history'
DST_HIST = r'G:\Jueguitos\BT4decomp-windows-x64\BT4RankedDetails\history'

os.makedirs(os.path.join(DST_HIST, 'vendor'), exist_ok=True)
os.makedirs(os.path.join(DST_HIST, 'chars'), exist_ok=True)

for fname in ['app.css', 'build-history.py', 'refresh.bat']:
    shutil.copy2(os.path.join(SRC_HIST, fname), os.path.join(DST_HIST, fname))
for fname in os.listdir(os.path.join(SRC_HIST, 'vendor')):
    shutil.copy2(os.path.join(SRC_HIST, 'vendor', fname), os.path.join(DST_HIST, 'vendor', fname))

# 1. Adapt app.js:
# - Rebrand to "BT4 Rankeds Details"
# - Remove Steam links (BT4 has no Steam)
# - Remove DP/PD labels and replace with Team / Equipo
app_js = open(os.path.join(SRC_HIST, 'app.js'), 'r', encoding='utf-8').read()
app_js = app_js.replace(
    'var ha=e=>typeof e==`string`&&/^7656119\\d{10}$/.test(e)?e:null',
    'var ha=e=>null'
)
app_js = app_js.replace('Sparking Ranked Details', 'BT4 Rankeds Details')
app_js = app_js.replace('Sparking! ZERO', 'DBZ BT4 Decomp')
# Map both "team" and legacy "dp" to Team battle (no DP/PD)
app_js = app_js.replace('e===`dp`', '(e===`dp`||e===`team`)')
app_js = app_js.replace('t.format===`dp`', '(t.format===`dp`||t.format===`team`)')
app_js = app_js.replace('e.format===`dp`', '(e.format===`dp`||e.format===`team`)')
app_js = app_js.replace('`DP battle (team)`', '`Team battle`')
app_js = app_js.replace('`DP battles`', '`Team battles`')
app_js = app_js.replace('`DP`', '`Team`')
app_js = app_js.replace('`DP limit {0}`', '`Team`')
open(os.path.join(DST_HIST, 'app.js'), 'w', encoding='utf-8').write(app_js)

# 2. Adapt index.html:
# - Rebrand to "BT4 Rankeds Details"
# - Remove all DP/PD references
# - Remove Steam references
idx_html = open(os.path.join(SRC_HIST, 'index.html'), 'r', encoding='utf-8').read()
replacements = [
    ('Sparking Ranked Details', 'BT4 Rankeds Details'),
    ('SparkingRankedDetails.ini', 'BT4RankedDetails.ini'),
    (r'Mods\SparkingRankedDetails\history\index.html', r'ranked_details\history\index.html'),
    ('SteamID', 'ID de cuenta'),
    ('Steam profile (opens in the browser)', 'Cuenta BT4'),
    ('Perfil de Steam (se abre en el navegador)', 'Cuenta BT4'),
    ('Sparking! activations', 'Max Power activations'),
    ('Time in Sparking!', 'Time in Max Power'),
    ('Activaciones de Sparking!', 'Activaciones de Max Power'),
    ('Tiempo en Sparking!', 'Tiempo en Max Power'),
    ('DP battle (team)|Batalla PD (equipo)', 'Team battle|Batalla en equipo'),
    ('DP battles|Batallas PD', 'Team battles|Batallas en equipo'),
    ('DP|PD', 'Team|Equipo'),
    ('Characters in your DP teams|Personajes en tus equipos PD', 'Characters in your teams|Personajes en tus equipos'),
    ('DP teams', 'teams'),
    ('equipos PD', 'equipos'),
    ('Batalla PD', 'Batalla en equipo'),
    ('DP battle', 'Team battle'),
    ('DP ', 'Team '),
    (' PD', ''),
]
for old, new in replacements:
    idx_html = idx_html.replace(old, new)
open(os.path.join(DST_HIST, 'index.html'), 'w', encoding='utf-8').write(idx_html)

# 3. Adapt build-history.py
bh = open(os.path.join(DST_HIST, 'build-history.py'), 'r', encoding='utf-8').read()
bh = bh.replace('Sparking Ranked Details', 'BT4 Rankeds Details')
bh = bh.replace(r'Mods\SparkingRankedDetails', r'ranked_details')
bh = bh.replace('"packet_loss"):', '"packet_loss", "region", "transport", "game_speed", "rollback"):')
open(os.path.join(DST_HIST, 'build-history.py'), 'w', encoding='utf-8').write(bh)

# 4. Initial placeholder data.js
data_js = (
    '// Initial placeholder - overwritten automatically by BT4 Rankeds Details after your first online match.\n'
    'window.SZ_GENERATED = "";\n'
    'window.SZ_META = {"generated":"","writer":"BT4 Rankeds Details 1.1.0","source":"jsonl","log_missing":false,"csv_present":false,"filtered_no_samples":0,"malformed":0,"other_lines":0,"ignored_keys":[],"crosscheck":[]};\n'
    'window.SZ_INCIDENTS = [];\n'
    'window.SZ_MATCHES = [];\n'
)
open(os.path.join(DST_HIST, 'data.js'), 'w', encoding='utf-8').write(data_js)

print('Adapted history viewer for BT4 Rankeds Details (no Steam, no DP/PD) OK')
