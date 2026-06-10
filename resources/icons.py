
from pathlib import Path
from PIL import Image

root = Path(r'C:\Users\philip\sauce\dlna-server')
source = root / 'Gemini_Generated_Image_kxspoykxspoykxsp copy.png'
resources = root / 'code' / 'resources'
icon = Image.open(source).convert('RGBA')

for size in (48, 120, 256):
    resized = icon.resize((size, size), Image.Resampling.LANCZOS)
    resized.save(resources / f'server_icon_{size}.png', format='PNG', optimize=True)

icon.save(resources / 'app.ico', format='ICO', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (256, 256)])
