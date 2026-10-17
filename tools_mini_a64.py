# Mini-desassembler AArch64 (so o que precisa para ler CRope): imprime acessos a campos, ADRP/ADD, BL, desvios.
import struct, subprocess, bisect, sys
SO='/home/claude/proj/TesteBR-free/app/src/main/jniLibs/arm64-v8a/libGTASA.so'
data=open(SO,'rb').read()
out=subprocess.run(['nm','-D','--defined-only',SO],capture_output=True,text=True).stdout
syms=sorted((int(l.split()[0],16),l.split()[2]) for l in out.splitlines() if len(l.split())==3)
addrs=[s[0] for s in syms]
def name(a):
    i=bisect.bisect_right(addrs,a)-1
    if i>=0 and a-syms[i][0]<0x4000: return syms[i][1]+("+0x%x"%(a-syms[i][0]) if a!=syms[i][0] else "")
    return "?"
rel=subprocess.run(['readelf','-rW',SO],capture_output=True,text=True).stdout
slots={}
for l in rel.splitlines():
    p=l.split()
    if len(p)>=5 and 'JUMP_SLOT' in p[2]: slots[int(p[0],16)]=p[4]
def plt(a):
    w0,w1=struct.unpack_from('<II',data,a)
    if (w0&0x9f000000)==0x90000000 and (w1&0xffc00000)==0xf9400000:
        imm=(((w0>>5)&0x7ffff)<<2|((w0>>29)&3))<<12
        if imm&(1<<32): imm-=1<<33
        return slots.get(((a&~0xfff)+imm)+(((w1>>10)&0xfff)<<3))
def sx(v,b): return v-(1<<b) if v&(1<<(b-1)) else v
def R(n,sf=1,sp=False):
    if n==31: return 'sp' if sp else ('xzr' if sf else 'wzr')
    return ('x%d' if sf else 'w%d')%n
def dis(a,w):
    if (w>>26)==0b100101: t=a+sx(w&0x3ffffff,26)*4; return "bl   %s"%(name(t) if name(t)!='?' else (plt(t) or hex(t)))
    if (w>>26)==0b000101: t=a+sx(w&0x3ffffff,26)*4; return "b    0x%x"%t
    if (w&0xff000010)==0x54000000: t=a+sx((w>>5)&0x7ffff,19)*4; return "b.%s 0x%x"%(['eq','ne','cs','cc','mi','pl','vs','vc','hi','ls','ge','lt','gt','le','al','nv'][w&15],t)
    if (w&0x7e000000)==0x34000000: t=a+sx((w>>5)&0x7ffff,19)*4; return "%s %s, 0x%x"%('cbnz' if w&0x01000000 else 'cbz',R(w&31,w>>31),t)
    if (w&0x7e000000)==0x36000000: t=a+sx((w>>5)&0x3fff,14)*4; return "%s %s, #%d, 0x%x"%('tbnz' if w&0x01000000 else 'tbz',R(w&31,1),((w>>26)&0x20)|((w>>19)&0x1f),t)
    if w==0xd65f03c0: return "ret"
    if (w&0x9f000000)==0x90000000:
        imm=((((w>>5)&0x7ffff)<<2)|((w>>29)&3))<<12; imm=sx(imm,33); return "adrp %s, 0x%x"%(R(w&31),(a&~0xfff)+imm)
    if (w&0x1f000000)==0x11000000:
        sf=w>>31; op='sub' if w&0x40000000 else 'add'; s='s' if w&0x20000000 else ''; imm=((w>>10)&0xfff)<<(12 if (w>>22)&1 else 0)
        if s and (w&31)==31: return "cmp%s %s, #0x%x"%('' if op=='sub' else 'n',R((w>>5)&31,sf,True),imm)
        return "%s%s %s, %s, #0x%x"%(op,s,R(w&31,sf,not s),R((w>>5)&31,sf,True),imm)
    if (w&0x1f800000)==0x12800000 or (w&0x1f800000)==0x52800000 or (w&0x1f800000)==0x72800000:
        opc=(w>>29)&3; sf=w>>31; hw=(w>>21)&3; imm=((w>>5)&0xffff)<<(hw*16)
        return "%s %s, #0x%x"%({0:'movn',2:'movz',3:'movk'}.get(opc,'?'),R(w&31,sf),imm)
    if (w&0x3b000000)==0x39000000:  # ldr/str unsigned imm
        size=w>>30; V=(w>>26)&1; opc=(w>>22)&3; imm=((w>>10)&0xfff)
        if V: 
            sc=size if opc<2 else 4; bits={0:'b',1:'h',2:'s',3:'d'}[size] if opc<2 else 'q'; r='%s%d'%(bits,w&31)
        else: sc=size; r=R(w&31, 1 if size==3 else 0)
        off=imm<<sc
        if V==0 and opc in(2,3): mn='ldrs'+['b','h','w'][size] if size<3 else 'prfm'
        else: mn='ldr' if opc&1 else 'str'
        if V==0 and size==0: mn+='b'
        elif V==0 and size==1: mn+='h'
        return "%s  %s, [%s, #0x%x]"%(mn,r,R((w>>5)&31,1,True),off)
    if (w&0x3e000000)==0x28000000 or (w&0x3e000000)==0x2c000000:
        opc=w>>30; V=(w>>26)&1; L=(w>>22)&1; imm=sx((w>>15)&0x7f,7)*(4<<(opc if not V else opc))
        mode={1:'post',2:'off',3:'pre'}[(w>>23)&3]
        regs=(R(w&31,1 if opc==2 else 0),R((w>>10)&31,1 if opc==2 else 0)) if not V else ('d%d'%(w&31),'d%d'%((w>>10)&31))
        return "%s  %s, %s, [%s, #%d]%s"%('ldp' if L else 'stp',regs[0],regs[1],R((w>>5)&31,1,True),imm,'!' if mode=='pre' else '')
    if (w&0x7fe0ffe0)==0x2a0003e0: return "mov  %s, %s"%(R(w&31,w>>31),R((w>>16)&31,w>>31))
    if (w&0x7f200000)==0x0a000000 or (w&0x7f200000)==0x2a000000 or (w&0x7f200000)==0x4a000000 or (w&0x7f200000)==0x6a000000: return "logic %s, %s, %s"%(R(w&31,w>>31),R((w>>5)&31,w>>31),R((w>>16)&31,w>>31))
    if (w&0x7fe0fc00)==0x1b000000: return "madd %s, %s, %s, %s"%(R(w&31,w>>31),R((w>>5)&31,w>>31),R((w>>16)&31,w>>31),R((w>>10)&31,w>>31))
    if (w&0x1f200000)==0x0b000000: return "%s%s %s, %s, %s"%('sub' if w&0x40000000 else 'add','s' if w&0x20000000 else '',R(w&31,w>>31),R((w>>5)&31,w>>31),R((w>>16)&31,w>>31))
    if (w&0x7f800000)==0x13000000 or (w&0x7f800000)==0x53000000: return "bfm/ubfm/sbfm %s,%s,#%d,#%d"%(R(w&31,w>>31),R((w>>5)&31,w>>31),(w>>16)&0x3f,(w>>10)&0x3f)
    if (w&0xff000000)==0x1e000000 or (w&0x5f000000)==0x1e000000: return "fp-op 0x%08x"%w
    return ".word 0x%08x"%w
def dump(start,size,title=None):
    print("=== %s @0x%x (%d bytes)"%(title or name(start),start,size))
    for a in range(start,start+size,4):
        w=struct.unpack_from('<I',data,a)[0]; print("%x: %s"%(a,dis(a,w)))
if __name__=='__main__':
    for arg in sys.argv[1:]:
        s,z=arg.split(':'); dump(int(s,16),int(z,0))
