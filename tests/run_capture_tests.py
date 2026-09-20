"""Production capture with real temporary files and network/SDK boundary stubs."""
from pathlib import Path
import re, subprocess, sys, tempfile
from PIL import Image, ImageChops
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="capture-tests-") as directory:
    work=Path(directory)
    objects=[]
    sources=["main/scanner_capture.c", "main/jpeg_crop.c", "main/jpeg_width_crop.c", "tests/test_scanner_capture.c", "main/esci_scan.c"]
    for extra in ["main/scan_files.c", "main/jpeg_stream.c"]:
        if (root/extra).exists(): sources.append(extra)
    for i,source in enumerate(sources):
        obj=work/f"{i}.o"
        flags=[]
        if source in ("main/scanner_capture.c","main/scan_files.c","main/jpeg_stream.c"):
            flags=["-include",str(root/"tests/capture_stubs/boundary.h")]
        subprocess.run([sys.executable,"-m","ziglang","cc","-std=c11","-Wall","-Wextra","-Werror",
            "-I",str(root/"tests/capture_stubs"),"-I",str(root/"main"),*flags,
            "-c",str(root/source),"-o",str(obj)],check=True)
        objects.append(str(obj))
    binary=work/"capture.exe"
    subprocess.run([sys.executable,"-m","ziglang","cc",*objects,"-o",str(binary)],check=True)
    fixtures={}
    for color in ('white','dark'):
        image=Image.new('RGB',(2550,384),'white')
        if color=='dark':
            image.paste((8,8,8),(1536,0,2550,384));image.paste('white',(2000,8,2400,16))
        fixture=work/f'{color}.jpg';image.save(fixture,quality=75,subsampling=1,restart_marker_rows=1)
        clean=fixture.read_bytes();sof=clean.index(b'\xff\xc0')
        fixture.write_bytes(clean[:sof+5]+(4200).to_bytes(2,'big')+clean[sof+7:])
        fixtures[color]=(fixture,clean)
    corrupt=work/'invalid.jpg';data=fixtures['white'][0].read_bytes()
    start=data.index(b'\xff\xd0')+2;end=data.index(b'\xff\xd1',start)
    corrupt.write_bytes(data[:start]+data[end:])
    wrong_canvas=work/'wrong-canvas.jpg'
    Image.new('RGB',(5100,384),'white').save(wrong_canvas,quality=75,subsampling=1,restart_marker_rows=1)
    data=wrong_canvas.read_bytes();sof=data.index(b'\xff\xc0')
    wrong_canvas.write_bytes(data[:sof+5]+(8400).to_bytes(2,'big')+data[sof+7:])
    cases=['success','white','ownership','connect','recv','send','open1','fdopen1','write1','flush1','sync1','close1',
        'fopen1','alloc1','alloc2','write2','write3','flush2','sync2','close2','fopen2','close3','rename1','collision1','stat1','stat6',
        'open2','alloc3','alloc4','corrupt_crop','fdopen2','fopen3','put1','close4','flush3','sync3','close5','fopen4','close6','rename2','collision2','stat7','invalid','wrong_canvas']
    cases += [f'reserve_{p}' for p in ('SCAN0001.JPG','SCAN0001.TMP','SCAN0001.CRP','CROP0001.JPG','CROP0001.TMP')]
    cases += ['stop_close1','stop_write3','stop_close2','stop_close3','stop_published1','stop_open2','stop_close5','stop_close6','stop_published2']
    expected_stages={}
    for stage,names in {
        1:'ownership',2:'open1 fdopen1 stat1',3:'connect',4:'recv send write1 write2',
        5:'flush1 sync1 close1',6:'fopen1 alloc1 alloc2 fopen2 close3 invalid wrong_canvas',
        7:'write3 flush2 sync2 close2',8:'rename1 collision1 stat6',9:'open2 fdopen2',
        10:'alloc3 fopen3 put1 close4 flush3 sync3 close5',11:'alloc4 corrupt_crop fopen4 close6',
        12:'rename2 collision2 stat7'
    }.items():
        expected_stages.update({name:stage for name in names.split()})
    cases += ['phase_order']
    original_failures=set(cases[:cases.index('open2')]) - {'success','white','stat6'}
    for index,case in enumerate(cases):
        run=work/f'case-{index}';run.mkdir()
        color='white' if case=='white' or case.startswith('reserve_') else 'dark'
        fixture,clean=fixtures[color]
        if case=='invalid': fixture=corrupt
        if case=='wrong_canvas': fixture=wrong_canvas
        result=subprocess.run([str(binary),case,str(fixture)],cwd=run,capture_output=True,text=True)
        assert result.returncode==(77 if case.startswith('stop_') else 0),(case,result.stdout,result.stderr)
        original=run/('SCAN0002.JPG' if case.startswith('reserve_') else 'SCAN0001.JPG')
        if case.startswith('reserve_'):
            assert (run/case[8:]).read_bytes()==b'PREEXISTING'
        if case in ('collision1','collision2'):
            assert (run/('SCAN0001.JPG' if case=='collision1' else 'CROP0001.JPG')).read_bytes()==b'PREEXISTING'
        if original.exists() and case!='collision1':
            assert original.read_bytes()==clean,(case,'original changed or lost content')
            with Image.open(original) as img:
                img.load();assert img.size==(2550,384)
        if case in original_failures or case in ('invalid','wrong_canvas'):
            assert not original.exists() or case=='collision1',case
        if case in ('open2','alloc3','alloc4','corrupt_crop','fdopen2','fopen3','put1','close4','flush3','sync3','close5','fopen4','close6','rename2','collision2','stat7','stat6'):
            assert original.exists(),case
        if case=='success':
            derived=run/'CROP0001.JPG';assert derived.exists()
            with Image.open(original) as before,Image.open(derived) as after:
                before.load();after.load();assert ImageChops.difference(before.crop((0,0,*after.size)),after).getbbox() is None
        if case=='white' or case.startswith('reserve_'):
            number='0001' if case=='white' else '0002'
            assert not (run/f'CROP{number}.TMP').exists() and not (run/f'CROP{number}.JPG').exists()
        if case.startswith('stop_'):
            before_publish=case in ('stop_close1','stop_write3','stop_close2','stop_close3')
            assert original.exists()!=before_publish,case
            assert (run/'CROP0001.JPG').exists()==(case=='stop_published2'),case
        if case in expected_stages:
            assert int(re.search(r'stage=(\d+)',result.stdout)[1])==expected_stages[case],(case,result.stdout)
    print(f'Capture pipeline passed: {len(cases)} real-filesystem/protocol boundary and interrupted-publication scenarios')
