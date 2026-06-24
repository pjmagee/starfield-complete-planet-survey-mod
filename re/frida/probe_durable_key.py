import frida, sys, time

# GROUND-TRUTH capture: what KEY (formId) + value does a REAL scan write to the durable 939118 store?
# Hooks are per-scan COLD (fire once per scan, never per-frame) -> safe. Offsets = decompile - 0x140000000.
JS = r'''
let base = null;
for (const m of Process.enumerateModules()) {
  if (m.name.toLowerCase() === 'starfield.exe') { base = m.base; break; }
}
send({t:'base', v: base ? base.toString() : 'NOT FOUND'});

function dump(a, n) {
  let o = {};
  for (let i = 0; i < n; i++) {
    try { o['a'+i] = a[i].toString(); } catch(e){}
    try { o['a'+i+'.u8']  = a[i].readU8(); } catch(e){}
    try { o['a'+i+'.u32'] = '0x'+(a[i].readU32() >>> 0).toString(16); } catch(e){}
  }
  return o;
}

// Per-hook try/catch so one un-interceptable address can't halt the rest.
function hook(name, off, handler) {
  try { Interceptor.attach(base.add(off), handler); send({t:'hooked', fn:name}); }
  catch (e) { send({t:'HOOKFAIL', fn:name, err: e.message}); }
}

// Broad per-scan-COLD net (NONE per-frame — these fire only when a scan/credit/event happens).
// dump(a,4) shows each arg as ptr + deref u8/u32, so ID_83038's formId key surfaces as aN.u32.
const fns = {
  'ID_90506_SCAN':     0x1599f70,  // hand-scanner scan handler
  'ID_83038_DURABLE':  0x130a600,  // writes durable 939118 +0x28 (THE key+flag)
  'ID_52157_CREDIT':   0x7b7fa0,   // per-planet survey credit
  'ID_52158_938333':   0x7b81c0,   // durable 938333 +0x21/+0x20
  'ID_101322_EVENT':   0x1962310,  // scan-complete / "surveyed" event
  'ID_83025_IDENTITY': 0x309730,   // known-set identity write
  'ID_90507_EARLY':    0x159a7f0,  // early survey-credit path
};  // dropped ID_90513 @0x159ae80 — it's a per-object render loop (fired 2451x), not scan SFX
for (const name in fns) {
  hook(name, fns[name], { onEnter(a) { send({t:'CALL', fn:name, args: dump(a, 4)}); } });
}

send({t:'ready'});
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
    print('[frida] durable-key hooks installed -- DO A REAL HAND-SCAN OF THE TRAIT NOW', flush=True)
    window = int(sys.argv[1]) if len(sys.argv) > 1 else 240
    time.sleep(window)
    session.detach()
    print('[frida] detached', flush=True)

main()
