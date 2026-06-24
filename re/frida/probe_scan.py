import frida, sys, time

# LOW-FREQUENCY hooks only — these fire just when a scan/credit/event happens, never per-frame.
# Offsets = decompile addr - 0x140000000.
JS = r'''
let base = null;
for (const m of Process.enumerateModules()) {
  if (m.name.toLowerCase() === 'starfield.exe') { base = m.base; break; }
}
send({t:'base', v: base ? base.toString() : 'NOT FOUND'});

const fns = {
  'ID_90506_realScanHandler': 0x1599f70,   // the hand-scanner scan handler (once per scan)
  'ID_83008_scanInner':       0x1307910,   // the fn ID_90506 calls directly (-> ID_83038 + ID_52157)
  'ID_83038_byteWrite':       0x130a600,   // writes 939118+0x28
  'ID_52157_credit':          0x7b7fa0,    // per-planet progress credit
  'ID_52158_durableWrite':    0x7b81c0,    // durable 938333 +0x21/+0x20
  'ID_83025_identityReveal':  0x309730,    // known-set identity write
  'ID_101322_scanEvent':      0x1962310,   // scan-complete / "surveyed" event
  'ID_90507_earlyCredit':     0x159a7f0,   // early survey-credit path
  'ID_90513_sfxComplete':     0x159ae80,   // scan-complete SFX
  'ID_90517_repaint':         0x159b730,   // monocle repaint (recomputes panel?)
  'ID_90530_monocleReeval':   0x15a08c0,   // monocle re-evaluate
};

let hooked = [];
for (const name in fns) {
  try {
    Interceptor.attach(base.add(fns[name]), {
      onEnter(a) {
        let a1='?', a2='?', a3='?';
        try { a1 = a[1].toInt32(); } catch(e){}
        try { a2 = a[2].toInt32(); } catch(e){}
        try { a3 = a[3].toInt32(); } catch(e){}
        send({t:'call', fn:name, arg1:a1, arg2:a2, arg3:a3});
      }
    });
    hooked.push(name);
  } catch (e) {
    send({t:'hookfail', fn:name, err: e.message});
  }
}
send({t:'ready', hooked: hooked});
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
    print('[frida] scan-path hooks installed (6 low-frequency fns) -- DO A REAL HAND-SCAN NOW', flush=True)
    window = int(sys.argv[1]) if len(sys.argv) > 1 else 90
    time.sleep(window)
    session.detach()
    print('[frida] detached', flush=True)

main()
