#!/usr/bin/env python3
"""Portable vector-output checks; Python 3.11 stdlib only."""
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parent.parent
LAUNCHER = os.environ.get('OPENGRADS_LAUNCHER', str(ROOT/'opengrads'))


def png_pixels(path):
    data = path.read_bytes()
    position, compressed = 8, bytearray()
    while position < len(data):
        size = struct.unpack('>I', data[position:position+4])[0]
        kind = data[position+4:position+8]
        body = data[position+8:position+8+size]
        if kind == b'IHDR':
            width, height, depth, color = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            compressed.extend(body)
        position += size+12
    assert depth == 8 and color in (2, 6), (depth, color)
    channels = 4 if color == 6 else 3
    stride = width*channels
    raw = zlib.decompress(compressed)
    previous = bytearray(stride)
    rows = []
    for y in range(height):
        offset = y*(stride+1)
        filter_type = raw[offset]
        row = bytearray(raw[offset+1:offset+1+stride])
        for x in range(stride):
            a = row[x-channels] if x >= channels else 0
            b = previous[x]
            c = previous[x-channels] if x >= channels else 0
            if filter_type == 1:
                value = a
            elif filter_type == 2:
                value = b
            elif filter_type == 3:
                value = (a+b)//2
            elif filter_type == 4:
                p = a+b-c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                value = a if pa <= pb and pa <= pc else b if pb <= pc else c
            else:
                assert filter_type == 0
                value = 0
            row[x] = (row[x]+value) & 255
        rows.append(row)
        previous = row

    def pixel(x, y):
        values = tuple(rows[y][x*channels:(x+1)*channels])
        return values if channels == 4 else values+(255,)
    return width, height, pixel


def pdf_content(path):
    data = path.read_bytes()
    decoded = [data]
    for match in re.finditer(rb'<<(.*?)>>\s*stream\r?\n(.*?)\r?\nendstream', data, re.S):
        if b'/FlateDecode' in match[1]:
            decoded.append(zlib.decompress(match[2]))
    return b'\n'.join(decoded)


class VectorExportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='opengrads-vector-')
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)

    def run_grads(self, commands, portrait=False, errors=False):
        environment = dict(os.environ, OPENGRADS_COLOR='0', OPENGRADS_HISTORY='0',
                           OPENGRADS_UPDATE_CHECK='0', GA_PROGRESS='off')
        process = subprocess.run([LAUNCHER, '-bpu' if portrait else '-blu', '-d', 'gxdummy', '-h', 'Cairo'],
                                 input=commands+'\nquit\n', text=True, capture_output=True,
                                 env=environment, timeout=60)
        output = process.stdout+process.stderr
        self.assertEqual(process.returncode, 0, output[-4000:])
        if not errors:
            self.assertFalse(any(int(code)>0 for code in re.findall(r'<RC> (-?\d+) </RC>',output)), output[-4000:])
            self.assertNotRegex(output, r'(?i)\b(?:draw|syntax|open|define) error\b')
        return output

    def export(self, name, suffix, options=''):
        return f'gxprint {self.work/name}.{suffix} {suffix} {options}\n'

    def test_page_size_follows_canvas(self):
        for portrait, width, height in ((False, 792, 612), (True, 612, 792)):
            with self.subTest(portrait=portrait):
                commands = 'set line 2\ndraw recf 1 1 2 2\n'
                for suffix in ('svg', 'pdf', 'eps', 'ps'):
                    commands += self.export('page', suffix, 'white')
                self.run_grads(commands, portrait=portrait)
                svg = ET.parse(self.work/'page.svg').getroot()
                self.assertEqual(list(map(float,svg.attrib['viewBox'].split())), [0,0,width,height])
                pdf = pdf_content(self.work/'page.pdf')
                media = re.search(rb'/MediaBox\s*\[([^\]]+)\]',pdf)
                self.assertIsNotNone(media)
                self.assertEqual(list(map(float,media[1].split())), [0,0,width,height])
                for suffix in ('eps','ps'):
                    text = (self.work/('page.'+suffix)).read_text(errors='replace')
                    box = re.search(r'(?m)^%%BoundingBox:\s*(\d+) (\d+) (\d+) (\d+)',text)
                    self.assertIsNotNone(box)
                    self.assertEqual(tuple(map(int,box.groups())), (0,0,width,height))

    def test_transparent_postscript_keeps_canvas_bounds(self):
        for suffix in ('eps','ps'):
            self.run_grads('set line 2\ndraw recf 1 1 2 2\n'+self.export('bounds',suffix,'transparent'))
            text=(self.work/('bounds.'+suffix)).read_text(errors='replace')
            boxes=re.findall(r'(?m)^%%BoundingBox:\s*(\d+) (\d+) (\d+) (\d+)',text)
            self.assertEqual(boxes,[('0','0','792','612')])

    def test_border_preserves_square_geometry(self):
        self.run_grads('set line 2\ndraw recf 1 1 2 2\n'+self.export('square','pdf','white'))
        content = pdf_content(self.work/'square.pdf')
        number = rb'(-?\d+(?:\.\d+)?)'
        rectangles = re.findall(number+rb'\s+'+number+rb'\s+'+number+rb'\s+'+number+rb'\s+re\b',content)
        squares = [(float(w),float(h)) for x,y,w,h in rectangles if 50 < abs(float(w)) < 80 and 50 < abs(float(h)) < 80]
        self.assertTrue(squares, content[:1000])
        for width,height in squares:
            self.assertAlmostEqual(abs(width),abs(height),delta=.005)

    def test_background_override_and_transparent_background(self):
        setup = 'set rgb 30 25 60 95\nset background 30\nc\n'
        drawing = 'set line 0\ndraw recf 1 1 2 2\nset line 1\ndraw recf 3 1 4 2\nset line 30\ndraw recf 5 1 6 2\n'
        commands = setup+drawing
        for name,options in (('custom',''),('white','white'),('black','black'),('transparent','transparent')):
            commands += self.export(name,'png','x1100 y850 '+options)
            commands += self.export(name,'svg',options)
            commands += self.export(name,'pdf',options)
        self.run_grads(commands)
        for name,background in (('custom',(25,60,95,255)),('white',(255,255,255,255)),
                                ('black',(0,0,0,255)),('transparent',(0,0,0,0))):
            width,height,pixel = png_pixels(self.work/(name+'.png'))
            self.assertEqual((width,height),(1100,850))
            self.assertEqual(pixel(0,0),background,(name,pixel(0,0)))
            self.assertEqual(pixel(550,700),(25,60,95,255),'background override recolored data')
            pdf=pdf_content(self.work/(name+'.pdf'))
            painted_page=bool(re.search(rb'\b0 0 792 612 re\s+f\b',pdf))
            self.assertEqual(painted_page,name!='transparent',(name,pdf[:1000]))
            if name=='transparent':
                self.assertEqual(pixel(150,700),(0,0,0,255),'transparent background erased black data')
                self.assertEqual(pixel(350,700),(255,255,255,255),'transparent background erased white data')
                svg = ET.parse(self.work/(name+'.svg')).getroot()
                shapes = [item for item in svg.iter() if item.tag.rsplit('}',1)[-1] in ('path','rect')]
                self.assertEqual(len(shapes),3,'transparent export retained the page background')

    def test_failed_export_does_not_leak_transparency(self):
        drawing = 'set line 2\ndraw recf 1 1 2 2\n'
        commands = drawing+self.export('before','png','x1100 y850')
        commands += f'gxprint {self.work}/missing/output.svg svg -t 0\n'
        commands += self.export('after','png','x1100 y850')
        self.run_grads(commands,errors=True)
        self.assertEqual((self.work/'before.png').read_bytes(),(self.work/'after.png').read_bytes())

    def test_failed_image_import_restores_output_state(self):
        commands='set line 2\ndraw recf 1 1 2 2\n'+self.export('reference','png','x1100 y850')
        commands+=self.export('bad-foreground','png',f'white -f {self.work}/missing.png')
        commands+=self.export('after-foreground','png','x1100 y850')
        commands+=self.export('bad-background','png',f'white -b {self.work}/missing.jpg')
        commands+=self.export('after-background','png','x1100 y850')
        self.run_grads(commands,errors=True)
        for name in ('after-foreground','after-background'):
            self.assertEqual((self.work/'reference.png').read_bytes(),(self.work/(name+'.png')).read_bytes())


if __name__ == '__main__':
    unittest.main()
