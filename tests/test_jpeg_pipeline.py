"""Strict production JPEG pipeline tests; synthetic fixtures only."""
import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image, ImageChops
root=Path(__file__).resolve().parents[1]

def segment(marker,p): return bytes([255,marker])+(len(p)+2).to_bytes(2,'big')+p

def minimal(width=80,rows=2,dc_length=2,dc_symbol=0,ac_symbol=0):
    mcus=(width+15)//16
    counts=bytes([int(i==dc_length) for i in range(1,17)])
    data=b'\xff\xd8'+segment(0xdb,b'\x00'+b'\x01'*64)
    data+=segment(0xc4,b'\x00'+counts+bytes([dc_symbol]))
    data+=segment(0xc4,b'\x10'+b'\x01'+b'\x00'*15+bytes([ac_symbol]))
    data+=segment(0xc0,b'\x08'+(rows*8).to_bytes(2,'big')+width.to_bytes(2,'big')+bytes([3,1,0x21,0,2,0x11,0,3,0x11,0]))
    data+=segment(0xdd,mcus.to_bytes(2,'big'))+segment(0xda,bytes([3,1,0,2,0,3,0,0,63,0]))
    bits=mcus*4*(dc_length+1)
    entropy=b'\x00'*(bits//8)+(bytes([(1<<(8-bits%8))-1]) if bits%8 else b'')
    for row in range(rows): data+=entropy+bytes([255,0xd9 if row+1==rows else 0xd0+(row&7)])
    return data

with tempfile.TemporaryDirectory(prefix='jpeg-pipeline-') as directory:
    work=Path(directory); objects=[]
    for index,source in enumerate(['main/jpeg_stream.c','main/jpeg_crop.c','main/jpeg_width_crop.c','tests/jpeg_test_bridge.c']):
        obj=work/f'{index}.o'; flags=[]
        if source=='main/jpeg_stream.c':
            flags=['-include',str(root/'tests/jpeg_test_allocator.h'),'-Dcalloc=jpeg_test_calloc','-Dfree=jpeg_test_free']
        subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-Wall','-Wextra','-Werror',
            '-I',str(root/'main'),*flags,'-c',str(root/source),'-o',str(obj)],check=True)
        objects.append(str(obj))
    library=work/'pipeline.dll'
    subprocess.run([sys.executable,'-m','ziglang','cc','-shared',*objects,'-o',str(library)],check=True)
    dll=ctypes.CDLL(str(library))
    inspect=dll.jpeg_test_inspect
    inspect.argtypes=[ctypes.c_char_p,ctypes.c_int,*([ctypes.c_uint]*4),ctypes.c_int,ctypes.POINTER(ctypes.c_uint)]
    inspect.restype=ctypes.c_int
    height=dll.jpeg_crop_file;height.argtypes=[ctypes.c_char_p,ctypes.c_uint16,ctypes.c_uint16];height.restype=ctypes.c_bool
    crop=dll.jpeg_width_crop_file;crop.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.POINTER(ctypes.c_uint16)];crop.restype=ctypes.c_int
    source=work/'source.jpg';target=work/'crop.jpg'
    def check(data,width,h,mode=1,pw=0,ph=0,stats=1,status=0):
        source.write_bytes(data);out=(ctypes.c_uint*7)()
        actual=inspect(str(source).encode(),mode,width,h,pw,ph,stats,out)
        assert actual==status,(actual,status,width,h)
        return list(out)
    try:
        base=minimal();info=check(base,80,16)
        assert info[:4]==[80,16,16,2]
        at=info[6];end=base.index(b'\xff\xd0',at)
        corrupt={
            'empty row':base[:at]+base[end:],
            'bad padding':base[:end-1]+b'\x00'+base[end:],
            'extra entropy':base[:end]+b'\x00'+base[end:],
            'stuffing':base[:at]+b'\xff\x01'+base[at+2:],
            'stuffed excess':base[:end]+b'\xff\x00'+base[end:],
            'restart order':base[:end+1]+b'\xd1'+base[end+2:],
            'trailing data':base+b'x',
            'truncated amplitude':base[:at+1]+base[end:],
            'missing EOI':base[:-2],
            'DC category':minimal(dc_symbol=12),
            'truncated DC amplitude':minimal(dc_symbol=11),
            'AC category':minimal(ac_symbol=0x0b),
            'AC zero size':minimal(ac_symbol=0x10),
            'AC run overflow':minimal(ac_symbol=0xf0),
            'zero quantizer':base[:7]+b'\x00'+base[8:],
        }
        dri=base.index(b'\xff\xdd')
        corrupt['wrong MCU count']=base[:dri+4]+b'\x00\x04'+base[dri+6:]
        corrupt['missing quantizer']=base[:2]+base[71:]
        dht=base.index(b'\xff\xc4')
        corrupt['oversubscribed table']=base[:dht+5]+b'\x02'+base[dht+6:]
        for name,data in corrupt.items():
            for stats in (0,1):
                try: check(data,80,16,stats=stats,status=1)
                except AssertionError as e: raise AssertionError(name) from e
        # Header >64KiB is parsed incrementally; metadata crosses reader boundaries.
        huge=base[:2]+segment(0xe1,b'x'*65000)+segment(0xe2,b'y'*65000)+base[2:]
        check(huge,80,16)
        check(base,96,16,status=1);check(base,80,24,status=1)
        check(base,2550,4200,mode=0,pw=2550,ph=16,status=1)
        # Existing unrelated targets survive valid no-crop and malformed input.
        image=Image.new('RGB',(2550,384),'white');image.save(source,quality=75,subsampling=1,restart_marker_rows=1)
        clean=source.read_bytes();target.write_bytes(b'PREEXISTING USER DATA');width=ctypes.c_uint16()
        assert crop(str(source).encode(),str(target).encode(),ctypes.byref(width))==0
        assert target.read_bytes()==b'PREEXISTING USER DATA'
        source.write_bytes(b'invalid');assert crop(str(source).encode(),str(target).encode(),ctypes.byref(width))==-1
        assert target.read_bytes()==b'PREEXISTING USER DATA';target.unlink()
        # Formerly unsampled empty row must fail even when crop statistics are off.
        sof=clean.index(b'\xff\xc0');scanner=clean[:sof+5]+(4200).to_bytes(2,'big')+clean[sof+7:]
        start=scanner.index(b'\xff\xd0')+2;end=scanner.index(b'\xff\xd1',start)
        check(scanner[:start]+scanner[end:],2550,4200,mode=0,pw=2550,ph=384,stats=0,status=1)
        # Scanner declaration, normalized original and derivative each use their own dimensions.
        for canvas in (2550,5100):
            for quality in (50,75):
                image=Image.new('RGB',(canvas,384),'white')
                boundary=(canvas*3//5)//16*16
                image.paste((8,8,8),(boundary,0,canvas,384))
                image.paste('white',(canvas-400,8,canvas-100,16)) # real dark-page content retained in original
                image.save(source,quality=quality,subsampling=1,restart_marker_rows=1)
                clean=source.read_bytes();sof=clean.index(b'\xff\xc0');canvas_h=4200 if canvas==2550 else 8400
                raw=clean[:sof+5]+canvas_h.to_bytes(2,'big')+clean[sof+7:]
                check(raw,canvas,canvas_h,mode=0,pw=canvas,ph=384)
                check(raw,canvas,384,status=1)
                check(raw,canvas,canvas_h,mode=0,pw=canvas,ph=400,status=1)
                source.write_bytes(raw);assert height(str(source).encode(),canvas,384)
                normalized=source.read_bytes();assert normalized==clean
                assert inspect(str(source).encode(),1,canvas,384,0,0,0,(ctypes.c_uint*7)())==0
                assert crop(str(source).encode(),str(target).encode(),ctypes.byref(width))==1
                derived=target.read_bytes();assert source.read_bytes()==clean
                with Image.open(source) as before,Image.open(target) as after:
                    before.load();after.load()
                    assert before.size==(canvas,384) and after.size==(width.value,384)
                    assert ImageChops.difference(before.crop((0,0,width.value,384)),after).getbbox() is None
                check(derived,width.value,384);check(derived,canvas,384,status=1)
                target.unlink()
        # Published heights may end in a partial MCU row; width cropping preserves
        # the declared height and every retained pixel, including the final row.
        image=Image.new('RGB',(2550,385),'white')
        image.paste((8,8,8),(1536,0,2550,385))
        image.save(source,quality=75,subsampling=1,restart_marker_rows=1)
        partial=source.read_bytes()
        partial_info=check(partial,2550,385)
        assert partial_info[:4]==[2550,385,392,49]
        result=crop(str(source).encode(),str(target).encode(),ctypes.byref(width))
        assert result==1, ('partial final MCU row must support width crop',result)
        assert source.read_bytes()==partial
        derived=target.read_bytes()
        with Image.open(source) as before,Image.open(target) as after:
            before.load();after.load()
            assert after.size==(width.value,385)
            assert ImageChops.difference(before.crop((0,0,width.value,385)),after).getbbox() is None
        assert check(derived,width.value,385)[:4]==[width.value,385,392,49]
        target.unlink()
        # Maximum supported frame and noisy, large compressed rows use identical workspace.
        for dimensions in ((2550,4200),(5100,8400)):
            image=Image.new('RGB',dimensions,'white');image.save(source,quality=75,subsampling=1,restart_marker_rows=1)
            check(source.read_bytes(),*dimensions)
        noisy=Image.effect_noise((5100,64),100).convert('RGB')
        noisy.save(source,quality=75,subsampling=1,restart_marker_rows=1)
        check(source.read_bytes(),5100,64)
        dll.jpeg_test_fail_allocation(1);check(base,80,16,status=3);dll.jpeg_test_fail_allocation(0)
        dll.jpeg_workspace_size.restype=ctypes.c_size_t;dll.jpeg_test_peak.restype=ctypes.c_size_t
        workspace=dll.jpeg_workspace_size();assert dll.jpeg_test_peak()==workspace<=32768
        print(f'JPEG pipeline passed: {len(corrupt)} malformed variants, all-row validation, 300/600 dpi 50/75 corpus, max dimensions, allocation failure; workspace {workspace} bytes')
    finally:
        if sys.platform=='win32':
            handle=dll._handle
            del inspect,height,crop,dll
            free=ctypes.WinDLL('kernel32',use_last_error=True).FreeLibrary
            free.argtypes=[ctypes.c_void_p];free.restype=ctypes.c_int
            if not free(handle): raise OSError(ctypes.get_last_error(),'DLL unload failed')
