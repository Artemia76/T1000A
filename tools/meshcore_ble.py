#!/usr/bin/env python3
"""Test minimal du protocole compagnon MeshCore par BLE (service Nordic UART), depuis le PC.

Envoie DEVICE_QUERY puis APP_START et décode les réponses ; avec --advert, demande au nœud d'émettre
son advert en LoRa (SEND_SELF_ADVERT, diffusion flood). Une écriture = une trame, une notification = une trame.

Usage : meshcore_ble.py [--name MeshCore-] [--advert] [-t 120]
"""
import argparse, asyncio, struct, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from t1000_ble import connect, disconnecting   # noqa: E402

NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'   # PC -> nœud
NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'   # nœud -> PC (notifications)


def decode(f):
    code = f[0]
    if code == 13:                                    # RESP_CODE_DEVICE_INFO
        return f'DEVICE_INFO ver={f[1]} max_contacts={f[2] * 2} max_canaux={f[3]} ' \
               f'fabricant/version={f[8:].split(b"\\0")[0][:60]!r}'
    if code == 5:                                     # RESP_CODE_SELF_INFO
        typ, txp, maxp = f[1], f[2], f[3]
        pub = f[4:36].hex()
        lat, lon = struct.unpack_from('<ii', f, 36)
        freq, bw = struct.unpack_from('<II', f, 48)
        sf, cr = f[56], f[57]
        name = f[58:].decode('utf-8', 'replace')
        return (f'SELF_INFO nom="{name}" type={typ} tx={txp}/{maxp} dBm clé={pub[:16]}… '
                f'radio={freq / 1000:.3f} MHz BW={bw / 1000} SF{sf} CR{cr}')
    if code == 0:
        return 'OK'
    if code == 1:
        return f'ERREUR {f[1:].hex()}'
    if code >= 0x80:
        return f'PUSH 0x{code:02x} {f[1:].hex()}'
    return f'code {code} : {f.hex()}'


async def main(a):
    c, adv = await connect(a.name, a.t)
    q = asyncio.Queue()
    async with disconnecting(c):
        print(f'MTU={c.mtu_size}')
        await c.start_notify(NUS_TX, lambda _, d: q.put_nowait(bytes(d)))

        async def cmd(frame, label, wait=3.0):
            await c.write_gatt_char(NUS_RX, frame, response=True)
            print(f'> {label}')
            end = asyncio.get_running_loop().time() + wait
            got = False
            while True:
                try:
                    f = await asyncio.wait_for(q.get(), max(0.1, end - asyncio.get_running_loop().time()))
                except asyncio.TimeoutError:
                    break
                print(f'< {decode(f)}')
                got = True
            if not got:
                print('< (pas de réponse)')

        await cmd(bytes([22, 3]), 'DEVICE_QUERY')
        await cmd(bytes([1, 3]) + bytes(6) + b'pc-test', 'APP_START')
        if a.advert:
            await cmd(bytes([7, 1]), 'SEND_SELF_ADVERT (flood)')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--name', default='MeshCore-')
    ap.add_argument('--advert', action='store_true')
    ap.add_argument('-t', type=float, default=120)
    asyncio.run(main(ap.parse_args()))
