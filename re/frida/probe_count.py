import frida, sys, time

# Decompile addrs (preferred base 0x140000000):
#   ID_90521 count-walker entry @ 0x14159f240  -> offset 0x159f240
#   ID_90523 per-entry tally    @ 0x14159f7a0  -> offset 0x159f7a0
JS = r'''
let base = null;
for (const m of Process.enumerateModules()) {
  if (m.name.toLowerCase() === 'starfield.exe') { base = m.base; break; }
}
send({t:'base', v: base ? base.toString() : 'NOT FOUND'});
const ID_90521 = base.add(0x159f240);
const ID_90523 = base.add(0x159f7a0);

let totalPasses = 0;
let tally = 0, nonzero = 0, seen = {};

Interceptor.attach(ID_90521, {
  onEnter(a) { tally = 0; nonzero = 0; seen = {}; },
  onLeave(r) {
    totalPasses++;
    if (nonzero > 0) send({t:'pass', n: totalPasses, tally: tally, nonzero: nonzero, refs: seen});
  }
});

Interceptor.attach(ID_90523, {
  onEnter(a) {
    tally++;
    const byte = a[2].toInt32() & 0xff;
    if (byte !== 0) {
      nonzero++;
      const ref = a[1];
      let fid14 = 0, fid18 = 0;
      try { fid14 = ref.add(0x14).readU32(); } catch(e) {}
      try { fid18 = ref.add(0x18).readU32(); } catch(e) {}
      const key = 'f14=0x'+fid14.toString(16)+' f18=0x'+fid18.toString(16)+' byte='+byte;
      seen[key] = (seen[key]||0)+1;
    }
  }
});

rpc.exports = { stats() { return {totalPasses: totalPasses}; } };
'''

def main():
    session = frida.attach("Starfield.exe")
    script = session.create_script(JS)
    def on_msg(m, d):
        if m['type'] == 'send':
            print('[probe]', m['payload'], flush=True)
        else:
            print('[err]', m, flush=True)
    script.on('message', on_msg)
    script.load()
    print('[frida] hooked ID_90521 (count entry) + ID_90523 (tally) -- AIM AT THE SCAN-TARGET NOW', flush=True)
    window = int(sys.argv[1]) if len(sys.argv) > 1 else 50
    time.sleep(window)
    try:
        print('[frida] total count passes observed:', script.exports_sync.stats(), flush=True)
    except Exception as e:
        print('[frida] stats err', e, flush=True)
    session.detach()
    print('[frida] detached', flush=True)

main()
