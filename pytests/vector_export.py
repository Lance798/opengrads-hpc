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
from collections import Counter

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


def svg_properties(item,parent=None):
    props=dict(parent or {})
    props.update((k,v) for k,v in item.attrib.items() if k!='style')
    props.update(pair.strip().split(':',1) for pair in item.attrib.get('style','').split(';') if ':' in pair)
    return {k:v.strip() for k,v in props.items()}


def svg_polygons(path):
    """Read straight SVG subpaths for independent point-coverage checks."""
    result=[]
    number=r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'
    def visit(item, parent, definition=False):
        props=svg_properties(item,parent)
        tag=item.tag.rsplit('}',1)[-1]
        definition=definition or tag=='defs'
        fill=props.get('fill','black')
        if not definition and fill!='none' and tag in ('rect','path'):
            polygons=[]
            if tag=='rect':
                x,y,w,h=(float(item.attrib.get(k,0)) for k in ('x','y','width','height'))
                polygons=[[(x,y),(x+w,y),(x+w,y+h),(x,y+h)]]
            else:
                tokens=re.findall(r'[A-Za-z]|'+number,item.attrib['d'])
                command=None;points=[];i=0;x=y=0.0
                while i<len(tokens):
                    if tokens[i].isalpha():
                        command=tokens[i];i+=1
                        if command in ('Z','z'):
                            if points: polygons.append(points);points=[]
                            continue
                    assert command in ('M','L','m','l'), (command,item.attrib['d'][:200])
                    dx,dy=map(float,tokens[i:i+2]);i+=2
                    if command.islower(): x+=dx;y+=dy
                    else: x,y=dx,dy
                    if command in ('M','m'):
                        if points: polygons.append(points)
                        points=[];command='l' if command=='m' else 'L'
                    points.append((x,y))
                if points: polygons.append(points)
            result.append((fill,float(props.get('fill-opacity','1')),polygons))
        for child in item:
            visit(child,props,definition)
    visit(ET.parse(path).getroot(),{})
    return result


def svg_strokes(path):
    result=[]
    def visit(item,parent,definition=False):
        props=svg_properties(item,parent)
        tag=item.tag.rsplit('}',1)[-1]
        definition=definition or tag=='defs'
        if not definition and tag=='path' and props.get('stroke','none')!='none':
            result.append(props)
        for child in item:visit(child,props,definition)
    visit(ET.parse(path).getroot(),{})
    return result


def svg_layers_at(shapes,x,y):
    layers=[]
    for fill,alpha,polygons in shapes:
        winding=0
        for points in polygons:
            for (ax,ay),(bx,by) in zip(points,points[1:]+points[:1]):
                cross=(bx-ax)*(y-ay)-(x-ax)*(by-ay)
                if ay<=y<by and cross>0: winding+=1
                elif by<=y<ay and cross<0: winding-=1
        if winding and alpha>0: layers.append((fill,alpha))
    return layers


def svg_color_at(shapes,x,y):
    layers=svg_layers_at(shapes,x,y)
    return layers[-1] if layers else None


def svg_rgba_at(shapes,x,y):
    rgba=(0.,0.,0.,0.)
    for fill,alpha in svg_layers_at(shapes,x,y):
        rgb=tuple(float(v)/100 for v in re.findall(r'([-\d.]+)%',fill))
        assert len(rgb)==3, fill
        rgba=tuple(v*alpha+old*(1-alpha) for v,old in zip(rgb,rgba[:3]))+(alpha+rgba[3]*(1-alpha),)
    return rgba


class VectorExportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='opengrads-vector-')
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)

    def run_grads(self, commands, portrait=False, errors=False, settings=None, batch=True):
        environment = dict(os.environ, OPENGRADS_COLOR='0', OPENGRADS_HISTORY='0',
                           OPENGRADS_UPDATE_CHECK='0', GA_PROGRESS='off')
        environment.update(settings or {})
        flags=('-bp' if portrait else '-bl') if batch else ('-p' if portrait else '-l')
        process = subprocess.run([LAUNCHER, flags+'u', '-d', 'gxdummy', '-h', 'Cairo'],
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

    def grid(self, missing=False, block=4):
        values=[float((x//block)%2+2*((y//block)%2)) for y in range(8) for x in range(8)]
        if missing:
            for y in (3,4):
                for x in (3,4): values[y*8+x]=-9999
        (self.work/'grid.dat').write_bytes(struct.pack('<64f',*values))
        ctl=self.work/'grid.ctl'
        ctl.write_text('dset ^grid.dat\noptions little_endian\nundef -9999\n'
                       'xdef 8 linear -30 5\nydef 8 linear 60 3\nzdef 1 linear 1 1\n'
                       'tdef 1 linear 00z01jan2000 1hr\nvars 1\nf 0 99 Test\nendvars\n')
        return (f'open {ctl}\nset grads off\nset timelab off\nset mproj scaled\nset mpdraw off\n'
                'set grid off\nset frame off\nset xlab off\nset ylab off\n'
                'set clevs .5 1.5 2.5\nset ccols 2 3 4 5\nset gxout grfill\n')

    def test_grfill_compacts_colors_and_preserves_holes(self):
        for missing in (False,True):
            with self.subTest(missing=missing):
                setup=self.grid(missing)
                for name,compact in (('legacy','0'),('compact','1')):
                    self.run_grads(setup+'d f\n'+self.export(name,'svg','white')+
                                   self.export(name,'png','white x1100 y850'),
                                   settings={'OPENGRADS_VECTOR_COMPACT':compact})
                legacy=svg_polygons(self.work/'legacy.svg')
                compact=svg_polygons(self.work/'compact.svg')
                self.assertEqual(len(legacy),61 if missing else 65)
                self.assertEqual(len(compact),5)
                self.assertLess((self.work/'compact.svg').stat().st_size,(self.work/'legacy.svg').stat().st_size/2)
                for x in range(23,790,19):
                    for y in range(17,610,19):
                        self.assertEqual(svg_color_at(legacy,x+.13,y+.21),svg_color_at(compact,x+.13,y+.21),(missing,x,y))
                self.assertEqual((self.work/'legacy.png').read_bytes(),(self.work/'compact.png').read_bytes())

    def test_each_gridline_keeps_dash_geometry_in_one_path(self):
        cases=[(p,255,3) for p in ('scaled','latlon','nps','sps','lambert','orthogr','robinson','mollweide')]
        cases += [('scaled',125,3)]+[('scaled',255,s) for s in (1,2,4,5,6,7)]
        for projection,alpha,style in cases:
            with self.subTest(projection=projection,alpha=alpha,style=style):
                commands=(self.grid()+f'set rgb 30 21 73 119 {alpha}\nset grid on {style} 30\n'
                          f'set mproj {projection}\nset xlab on\nset ylab on\n'
                          'set xlint 10\nset ylint 6\nd f\n')
                if projection=='robinson':
                    commands=commands.replace('d f\n','set lon -180 180\nset lat -90 90\nd f\n')
                elif projection=='sps':
                    commands=commands.replace('d f\n','set lat -81 -60\nd const(f,1,-a)\n')
                elif projection=='orthogr':
                    commands=commands.replace('d f\n','set lon -90 90\nset lat -90 90\nset frame circle\nd f\n')
                for name,compact in (('legacy','0'),('compact','1')):
                    output=self.run_grads(commands+self.export(name,'svg','white')+self.export(name,'png','white'),
                                          settings={'OPENGRADS_VECTOR_COMPACT':compact})
                    self.assertNotIn('Map Projection Error',output)
                def lines(name):
                    return [p for p in svg_strokes(self.work/(name+'.svg')) if '8.23529' in p['stroke']]
                old,new=lines('legacy'),lines('compact')
                self.assertGreater(len(new),0)
                if style==1:self.assertEqual(len(new),len(old))
                else:self.assertLess(len(new),len(old)/3)
                def pieces(paths):
                    return Counter(part.strip() for p in paths for part in p['d'].split('M') if 'L' in part)
                self.assertEqual(pieces(old),pieces(new))
                if projection=='scaled':
                    for p in new:
                        coords=list(map(float,re.findall(r'[-\d.]+',p['d'])))
                        self.assertTrue(len(set(coords[::2]))==1 or len(set(coords[1::2]))==1,p['d'])
                self.assertEqual((self.work/'legacy.png').read_bytes(),(self.work/'compact.png').read_bytes())

    def test_grfill_projection_fallback_keeps_geometry(self):
        setup=self.grid()+'set mproj nps\n'
        for name,compact in (('legacy','0'),('compact','1')):
            self.run_grads(setup+'d f\n'+self.export(name,'svg','white'),
                           settings={'OPENGRADS_VECTOR_COMPACT':compact})
        self.assertEqual((self.work/'legacy.svg').read_bytes(),(self.work/'compact.svg').read_bytes())

    def test_unite_removes_internal_edges_and_keeps_hole_rings(self):
        for mode in ('grfill','shaded','shade2b'):
            for alpha in (255,125):
                with self.subTest(mode=mode,alpha=alpha):
                    commands=self.grid(True).replace('set gxout grfill','set gxout '+mode)
                    values=[1.+.01*x for y in range(8) for x in range(8)]
                    for y in (3,4):
                        for x in (3,4):values[8*y+x]=-9999
                    (self.work/'grid.dat').write_bytes(struct.pack('<64f',*values))
                    commands+=f'set rgb 30 40 80 120 {alpha}\nset ccols 30 30 30 30\nd f\n'
                    for name,unite in (('compound','0'),('united','1')):
                        self.run_grads(commands+self.export(name,'svg','white')+self.export(name,'png','white'),
                                       settings={'OPENGRADS_VECTOR_UNITE':unite})
                    old=svg_polygons(self.work/'compound.svg');new=svg_polygons(self.work/'united.svg')
                    def rings(shapes):
                        return [p for fill,a,polygons in shapes[1:] for p in polygons if len(p)>2]
                    a,b=rings(old),rings(new)
                    self.assertLess(sum(map(len,b)),sum(map(len,a)))
                    self.assertEqual(len(b),2,'Unite must produce the outer ring and the hole')
                    areas=[sum(x1*y2-x2*y1 for (x1,y1),(x2,y2) in zip(p,p[1:]+p[:1])) for p in b]
                    self.assertLess(areas[0]*areas[1],0,'Hole winding must oppose outer winding')
                    for x in range(23,790,13):
                        for y in range(17,610,13):
                            self.assertEqual(svg_rgba_at(old,x+.13,y+.21),svg_rgba_at(new,x+.13,y+.21),(mode,alpha,x,y))
                    self.assertEqual((self.work/'compound.png').read_bytes(),(self.work/'united.png').read_bytes())

    def test_grfill_reversed_axes_and_layer_order(self):
        setups=[self.grid(True)+'set xflip on\nset yflip on\nd f\n',
                self.grid()+'d f\nset line 6\ndraw recf 3 3 7 6\n'
                'set clevs .5 1.5 2.5\nset ccols 2 3 4 5\nd maskout(f,f>1.5)\n']
        for index,commands in enumerate(setups):
            with self.subTest(case=index):
                for name,compact in (('legacy','0'),('compact','1')):
                    self.run_grads(commands+self.export(name,'svg','white'),
                                   settings={'OPENGRADS_VECTOR_COMPACT':compact})
                legacy=svg_polygons(self.work/'legacy.svg')
                compact=svg_polygons(self.work/'compact.svg')
                for x in range(23,790,19):
                    for y in range(17,610,19):
                        self.assertEqual(svg_color_at(legacy,x+.13,y+.21),svg_color_at(compact,x+.13,y+.21),(index,x,y))

    def test_shaded_compounds_preserve_contours_and_holes(self):
        for mode in ('shaded','shade2b'):
            for missing in (False,True):
                with self.subTest(mode=mode,missing=missing):
                    setup=self.grid(missing,block=2).replace('set gxout grfill','set gxout '+mode)
                    for name,compact in (('legacy','0'),('compact','1')):
                        self.run_grads(setup+'d f\n'+self.export(name,'svg','white')+
                                       self.export(name,'png','white x1100 y850'),
                                       settings={'OPENGRADS_VECTOR_COMPACT':compact})
                    legacy=svg_polygons(self.work/'legacy.svg')
                    compact=svg_polygons(self.work/'compact.svg')
                    self.assertLess(len(compact),len(legacy))
                    for x in range(23,790,19):
                        for y in range(17,610,19):
                            self.assertEqual(svg_color_at(legacy,x+.13,y+.21),svg_color_at(compact,x+.13,y+.21),(mode,missing,x,y))
                    self.assertEqual((self.work/'legacy.png').read_bytes(),(self.work/'compact.png').read_bytes())

    def test_partial_alpha_layers_preserve_compositing_and_holes(self):
        for mode in ('grfill','shaded','shade2b'):
            with self.subTest(mode=mode):
                setup=self.grid(True,block=2).replace('set gxout grfill','set gxout '+mode)
                commands=(setup+'set rgb 30 240 30 80 125\nset rgb 31 20 180 40 125\n'
                          'set rgb 32 80 40 200 0\nset rgb 33 120 40 80 255\n'
                          'set ccols 30 31 32 33\nd f\nset rgb 34 10 50 210 125\n'
                          'set clevs .5 1.5 2.5\nset ccols 34 34 34 34\nd maskout(f,f>1.5)\n')
                for name,compact in (('legacy','0'),('compact','1')):
                    self.run_grads(commands+self.export(name,'svg','white')+
                                   self.export(name,'png','white')+self.export(name,'pdf','white'),
                                   settings={'OPENGRADS_VECTOR_COMPACT':compact})
                old=svg_polygons(self.work/'legacy.svg');new=svg_polygons(self.work/'compact.svg')
                self.assertLess(len(new),len(old))
                for x in range(23,790,19):
                    for y in range(17,610,19):
                        self.assertEqual(svg_rgba_at(old,x+.13,y+.21),svg_rgba_at(new,x+.13,y+.21),(mode,x,y))
                self.assertEqual((self.work/'legacy.png').read_bytes(),(self.work/'compact.png').read_bytes())
                self.assertNotRegex(pdf_content(self.work/'compact.pdf'),rb'/Subtype\s*/Image')
                self.assertRegex(pdf_content(self.work/'compact.pdf'),rb'/ca 0\.49019')

    def test_negative_alpha_and_shade1_keep_legacy_rendering(self):
        for mode in ('grfill','shaded','shade1'):
            with self.subTest(mode=mode):
                setup=self.grid(True,block=2).replace('set gxout grfill','set gxout '+mode)
                alpha=255 if mode=='shade1' else -125
                commands=setup+f'set rgb 30 40 80 120 {alpha}\nset ccols 30 30 30 30\nd f\n'
                for name,compact in (('legacy','0'),('compact','1')):
                    self.run_grads(commands+self.export(name,'svg','white')+self.export(name,'png','white'),
                                   settings={'OPENGRADS_VECTOR_COMPACT':compact})
                for suffix in ('svg','png'):
                    self.assertEqual((self.work/('legacy.'+suffix)).read_bytes(),(self.work/('compact.'+suffix)).read_bytes())

    def test_color_script_validates_palette_before_changing_plot_state(self):
        script=ROOT/'lib/scripts/gscript/color.gs'
        self.run_grads(self.grid()+'d f\n'+self.export('before','svg','white'))
        commands=self.grid()+f'run {script} 0 10 -kind shaded -gxout contour -alpha 125\n'
        commands+='d f\n'+self.export('after','svg','white')
        output=self.run_grads(commands)
        self.assertIn('unknown or invalid color in -kind: shaded',output)
        self.assertEqual((self.work/'before.svg').read_bytes(),(self.work/'after.svg').read_bytes())
        for palette in ('black->white','bluered','black-(3)->white','(0,0,0)->(255,255,255)'):
            with self.subTest(palette=palette):
                output=self.run_grads(self.grid()+f'run {script} 0 10 -kind {palette} -gxout grfill -alpha 125\n'
                                      'd f\n'+self.export('palette','svg','white'))
                self.assertNotIn('error in color.gs',output)
                shapes=svg_polygons(self.work/'palette.svg')
                self.assertTrue(any(abs(alpha-125/255)<.000001 for fill,alpha,polygons in shapes))

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

    def test_interactive_export_without_shared_display_context(self):
        commands='set string 2\ndraw string 2 4 A+x\n'
        for suffix in ('svg','pdf','eps','ps','png'):
            commands+=self.export('interactive',suffix,'white')
        commands+='set line 4\ndraw recf 3 1 4 2\n'+self.export('after','svg','transparent')
        self.run_grads(commands,batch=False)
        colors=[fill for fill,alpha,polygons in svg_polygons(self.work/'after.svg')]
        self.assertTrue(any(re.search(r'rgb\(11\.76470\d*%,\s*23\.52941\d*%,\s*100%\)',fill) for fill in colors),colors)

    def test_hershey_character_paths_keep_strokes_and_png(self):
        def strokes(name):
            result=[]
            def visit(item,parent):
                props=svg_properties(item,parent)
                if item.tag.endswith('}path') and props.get('fill')=='none' and props.get('stroke','none')!='none':
                    result.append({k:v for k,v in props.items() if k in ('d','stroke','stroke-width','stroke-opacity',
                                  'stroke-linecap','stroke-linejoin','stroke-dasharray','stroke-dashoffset','transform')})
                for child in item:visit(child,props)
            visit(ET.parse(self.work/(name+'.svg')).getroot(),{})
            return result
        for alpha,angle in ((255,0),(125,0),(255,35)):
            with self.subTest(alpha=alpha,angle=angle):
                commands=f'set rgb 30 40 80 120 {alpha}\nset string 30 l 3 {angle}\nset strsiz .4 .4\ndraw string 2 4 A+x\n'
                for name,compact in (('legacy','0'),('compact','1')):
                    self.run_grads(commands+self.export(name,'svg','white')+self.export(name,'png','white'),
                                   settings={'OPENGRADS_VECTOR_COMPACT':compact})
                old,new=strokes('legacy'),strokes('compact')
                self.assertEqual(len(old),7)
                self.assertEqual(len(new),3)
                def subpaths(paths):
                    return sorted((tuple(sorted((k,v) for k,v in p.items() if k!='d')),part.strip())
                                  for p in paths for part in p['d'].split('M') if part.strip())
                self.assertEqual(subpaths(old),subpaths(new))
                self.assertEqual((self.work/'legacy.png').read_bytes(),(self.work/'compact.png').read_bytes())

    def test_custom_alpha_background_is_painted_once(self):
        commands='set rgb 30 25 60 95 125\nset background 30\nc\n'
        commands+=self.export('alpha','svg')+self.export('alpha','png')
        commands+=self.export('empty','svg','transparent')+self.export('empty','png','transparent')
        self.run_grads(commands)
        shapes=svg_polygons(self.work/'alpha.svg')
        self.assertEqual(len(shapes),1)
        self.assertAlmostEqual(shapes[0][1],125/255,places=6)
        width,height,pixel=png_pixels(self.work/'alpha.png')
        self.assertEqual(pixel(0,0)[3],125)
        self.assertEqual(pixel(width//2,height//2)[3],125)
        self.assertEqual(svg_polygons(self.work/'empty.svg'),[])
        width,height,pixel=png_pixels(self.work/'empty.png')
        self.assertEqual(pixel(0,0)[3],0)
        self.assertEqual(pixel(width//2,height//2)[3],0)

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
