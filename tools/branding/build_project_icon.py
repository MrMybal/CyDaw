"""Build the project document icon from vector geometry and the unchanged app logo.
Run with Python + Pillow. Generated assets are committed; no extra build dependency.
"""
import base64
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
BRANDING = ROOT / 'resources/branding'
SCALE = 4

def main():
    vertices = [(52,12),(158,12),(216,70),(216,232),(214,238),(208,244),
                (48,244),(42,238),(40,232),(40,24),(42,18)]
    image = Image.new('RGBA', (256*SCALE,256*SCALE))
    draw = ImageDraw.Draw(image)
    points = [(x*SCALE,y*SCALE) for x,y in vertices]
    draw.polygon(points, fill='#292d35')
    draw.line(points+[points[0]], fill='#a3aab5', width=4*SCALE, joint='curve')
    draw.polygon([(158*SCALE,12*SCALE),(216*SCALE,70*SCALE),(166*SCALE,70*SCALE),(158*SCALE,62*SCALE)], fill='#ff953e')
    draw.rounded_rectangle((60*SCALE,40*SCALE,124*SCALE,46*SCALE), radius=3*SCALE, fill='#ff953e')
    draw.rounded_rectangle((60*SCALE,55*SCALE,100*SCALE,60*SCALE), radius=2*SCALE, fill='#89909b')
    logo = Image.open(BRANDING/'cydaw-logo-512.png').convert('RGBA')
    logo = logo.resize((140*SCALE,140*SCALE), Image.Resampling.LANCZOS)
    image.alpha_composite(logo,(58*SCALE,78*SCALE))
    for left,right in ((64,96),(104,152),(160,192)):
        draw.rounded_rectangle((left*SCALE,226*SCALE,right*SCALE,231*SCALE),radius=2*SCALE,fill='#ff953e')
    image = image.resize((256,256), Image.Resampling.LANCZOS)
    image.save(BRANDING/'cydaw-project.png')
    image.save(BRANDING/'cydaw-project.ico', sizes=[(n,n) for n in (16,20,24,32,40,48,64,96,128,256)])
    logo64=base64.b64encode((BRANDING/'cydaw-logo-512.png').read_bytes()).decode('ascii')
    path='M52 12H158L216 70V232L214 238L208 244H48L42 238L40 232V24L42 18Z'
    svg=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="CyDaw project">
  <path d="{path}" fill="#292d35" stroke="#a3aab5" stroke-width="4" stroke-linejoin="round"/>
  <path d="M158 12L216 70H166L158 62Z" fill="#ff953e"/>
  <rect x="60" y="40" width="64" height="6" rx="3" fill="#ff953e"/>
  <rect x="60" y="55" width="40" height="5" rx="2" fill="#89909b"/>
  <image x="58" y="78" width="140" height="140" href="data:image/png;base64,{logo64}"/>
  <path d="M64 228.5H96M104 228.5H152M160 228.5H192" stroke="#ff953e" stroke-width="5" stroke-linecap="round"/>
</svg>
'''
    (BRANDING/'cydaw-project.svg').write_text(svg,'utf-8')
    print('Generated SVG, PNG and ten-size Windows project icon')

if __name__ == '__main__':
    main()
