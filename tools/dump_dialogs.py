import pefile, struct, sys
p = pefile.PE(sys.argv[1])
ORDINALS = {0x80:'Button',0x81:'Edit',0x82:'Static',0x83:'ListBox',0x84:'ScrollBar',0x85:'ComboBox'}
def word(b,o): return struct.unpack_from('<H',b,o)[0]
def dword(b,o): return struct.unpack_from('<I',b,o)[0]
def item(b,o):
    w=word(b,o); o+=2
    if w==0: return '',o
    if w==0xffff: return f'#{word(b,o)}',o+2
    chars=[]
    while w:
        chars.append(chr(w)); w=word(b,o); o+=2
    return ''.join(chars),o
def align4(o): return (o+3)&~3
for typ in p.DIRECTORY_ENTRY_RESOURCE.entries:
    if typ.id != 5: continue
    for dlg in typ.directory.entries:
        lang=dlg.directory.entries[0]
        data=lang.data.struct
        b=p.get_data(data.OffsetToData,data.Size)
        ext=(word(b,0)==1 and word(b,2)==0xffff)
        if ext:
            helpid=dword(b,4); exstyle=dword(b,8); style=dword(b,12); n=word(b,16); x,y,cx,cy=struct.unpack_from('<hhhh',b,18); o=26
        else:
            style=dword(b,0); exstyle=dword(b,4); n=word(b,8); x,y,cx,cy=struct.unpack_from('<hhhh',b,10); o=18
        menu,o=item(b,o); klass,o=item(b,o); title,o=item(b,o)
        font=''
        if style & 0x40:
            if ext:
                point=word(b,o); weight=word(b,o+2); italic=b[o+4]; charset=b[o+5]; font,o=item(b,o+6)
            else:
                point=word(b,o); o+=2; font,o=item(b,o)
        print(f'DIALOG id={dlg.id} extended={ext} caption={title!r} size={cx}x{cy} controls={n} font={font!r}')
        for i in range(n):
            o=align4(o)
            if ext:
                help=dword(b,o); cex=dword(b,o+4); cs=dword(b,o+8); xx,yy,ww,hh=struct.unpack_from('<hhhh',b,o+12); cid=dword(b,o+20); o+=24
            else:
                cs=dword(b,o); cex=dword(b,o+4); xx,yy,ww,hh=struct.unpack_from('<hhhh',b,o+8); cid=word(b,o+16); o+=18
            c,o=item(b,o); t,o=item(b,o); extra=word(b,o); o+=2+extra
            c=ORDINALS.get(int(c[1:]),c) if c.startswith('#') else c
            print(f'  id={cid:>5} class={c!r} text={t!r} rect=({xx},{yy},{ww},{hh}) style=0x{cs:08x}')
