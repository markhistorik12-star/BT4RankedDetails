import os, shutil
from PIL import Image, ImageDraw, ImageFont

SZ_CHARS = r'G:\Programacion\SparkingRankedDetails-v1.11.0-AllInOne\Mods\SparkingRankedDetails\history\chars'
BT4_TSV  = r'G:\Jueguitos\BT4decomp-windows-x64\BT4RankedDetails\data\characters.tsv'
CUSTOM   = r'G:\Jueguitos\BT4decomp-windows-x64\BT4RankedDetails\custom_chars'
OUT_REPO = r'G:\Jueguitos\BT4decomp-windows-x64\BT4RankedDetails\history\chars'
OUT_GAME = r'G:\Jueguitos\BT4decomp-windows-x64\BT4decomp\ranked_details\history\chars'

# BT4 chara_id -> Sparking! ZERO portrait stem (Spike Chunsoft shared numbering)
BT4_TO_SZ = {
    0: '0000_00', 1: '0000_10', 2: '0000_11', 3: '0000_20', 4: '0000_21', 5: '0000_22', 6: '0000_23',
    7: '0002_30', 8: '0002_31', 9: '0002_32', 10: '0000_33', 11: '0002_50',
    13: '0030_00', 14: '0031_00', 15: '0031_01', 16: '0031_02',
    17: '0032_00', 18: '0032_01', 19: '0032_02', 20: '0032_10', 21: '0032_20',
    22: '0040_00', 23: '0040_10', 24: '0040_20',
    25: '0050_00', 26: '0060_00', 27: '0070_00', 28: '0190_00',
    29: '0020_00', 30: '0023_00', 31: '0020_10', 32: '0020_11', 33: '0021_20',
    34: '0020_30', 35: '0020_31', 36: '0020_32', 37: '0020_40', 38: '0020_50',
    39: '0080_00', 40: '0080_01', 41: '0080_10', 42: '0080_11', 43: '0081_20',
    44: '0082_00', 45: '0082_01', 46: '0090_00', 47: '0090_01',
    48: '0100_00', 49: '0100_01', 50: '0100_02',
    51: '0110_00', 52: '0110_01', 53: '0120_00', 54: '0120_02',
    55: '0130_00', 56: '0140_00', 60: '0210_00', 62: '0660_00',
    63: '0260_00', 64: '0240_00', 65: '0240_01', 66: '0220_00', 67: '0230_00',
    73: '0270_00', 77: '0280_00', 79: '0360_00', 80: '0350_00',
    82: '0310_00', 83: '0310_01', 84: '0320_00', 85: '0330_00',
    86: '0380_00', 87: '0390_00', 88: '0400_00', 89: '0410_00', 90: '0420_00',
    91: '0150_00', 92: '0151_00', 93: '0152_00', 94: '0153_00', 95: '0154_00', 96: '0153_10',
    97: '0480_00', 99: '0440_00',
    100: '0430_00', 101: '0450_00', 102: '0460_00', 103: '0470_00', 104: '0451_00',
    105: '0160_00', 106: '0161_00', 107: '0162_00', 108: '0162_01', 109: '0163_00',
    110: '0490_00', 111: '0500_00',
    112: '0170_00', 113: '0171_00', 114: '0172_00', 115: '0172_10', 116: '0172_11', 117: '0173_00',
    118: '0561_00', 119: '0561_00', 120: '0570_00', 121: '0580_00', 123: '0590_00', 124: '0591_00',
    126: '0600_00', 127: '0600_10', 128: '0601_00', 129: '0610_00', 130: '0620_00',
    131: '0550_00', 132: '0551_00', 133: '0552_00',
    135: '0621_00', 136: '0630_00', 137: '0640_00', 138: '0650_00', 139: '0651_00',
    140: '0680_00', 141: '0680_01', 142: '0680_02', 143: '0681_00',
    146: '0700_00', 147: '0700_01',
    158: '0510_00', 159: '0032_30', 160: '0032_31',
}

def make_fallback_badge(name, out_path):
    img = Image.new('RGBA', (96, 96), (18, 26, 44, 255))
    d = ImageDraw.Draw(img)
    # Color accent from name hash
    h = sum(ord(c) * (i + 7) for i, c in enumerate(name)) % 360
    pal = [(58, 130, 246), (234, 148, 36), (168, 85, 247), (34, 197, 94), (239, 68, 68), (20, 184, 166)]
    col = pal[h % len(pal)]
    d.rounded_rectangle([3, 3, 92, 92], radius=14, fill=(20, 28, 46, 255), outline=col + (220,), width=3)
    words = [w for w in name.replace('(', '').replace(')', '').split() if w]
    initials = (words[0][0] + (words[1][0] if len(words) > 1 else words[0][1:2])).upper()
    try:
        font = ImageFont.truetype('segoeui.ttf', 34)
    except Exception:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), initials, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((96 - tw) / 2, (96 - th) / 2 - 4), initials, fill=(240, 246, 255, 255), font=font)
    img.save(out_path, 'WEBP', quality=85)

os.makedirs(OUT_REPO, exist_ok=True)
os.makedirs(OUT_GAME, exist_ok=True)

mapped = 0
custom = 0
fallback = 0

for line in open(BT4_TSV, encoding='utf-8'):
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    cid_s, name = line.split('\t', 1)
    cid = int(cid_s)
    key = f'{cid:03d}'
    dst_repo = os.path.join(OUT_REPO, f'{key}.webp')
    dst_game = os.path.join(OUT_GAME, f'{key}.webp')

    # Priority 1: user custom icon in custom_chars/<000>.png or .webp
    used = False
    if os.path.isdir(CUSTOM):
        for ext in ('.webp', '.png', '.jpg'):
            cp = os.path.join(CUSTOM, f'{key}{ext}')
            if os.path.isfile(cp):
                Image.open(cp).convert('RGBA').resize((96, 96), Image.Resampling.LANCZOS).save(dst_repo, 'WEBP', quality=88)
                custom += 1
                used = True
                break

    # Priority 2: matched HD portrait from Sparking! ZERO
    if not used and cid in BT4_TO_SZ:
        sp = os.path.join(SZ_CHARS, BT4_TO_SZ[cid] + '.webp')
        if os.path.isfile(sp):
            shutil.copy2(sp, dst_repo)
            mapped += 1
            used = True

    # Priority 3: clean styled fallback badge
    if not used:
        make_fallback_badge(name, dst_repo)
        fallback += 1

    shutil.copy2(dst_repo, dst_game)

print(f'Generated 161 BT4 character icons: {mapped} HD portraits, {custom} custom, {fallback} styled badges')
