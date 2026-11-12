#!/usr/bin/env python3
"""Outil BLE pour le SenseCAP T1000-A (PC Linux / BlueZ, bibliothèque bleak).

Sous-commandes :
  scan [-t s]                    liste les appareils BLE visibles (nom, adresse, RSSI)
  gatt [--name N] [-t s]         attend l'appareil (fenêtre courte après appui long), s'y connecte
                                 immédiatement et liste services / caractéristiques
  flash FICHIER [--name N]       DFU Goodix vers le bootloader (SENSECAP_GR_DFU_xxxx)
  at CMD… [--name N]             envoie des commandes AT au firmware Seeed (UART transparent BLE)
  console [--name N]             console texte du firmware de test (service GUS)

Exemples :
  t1000_ble.py gatt -t 120       puis appui long sur le bouton du T1000-A
  t1000_ble.py flash firmware/hello/build/t1000a_hello_dfu.bin
"""
import argparse, asyncio, struct, sys, time
from bleak import BleakClient, BleakScanner

GOODIX = '-d344-460a-8075-b9e8ec90d71b'
GUS_TX   = 'a6ed0202' + GOODIX   # notifications appareil -> PC
GUS_RX   = 'a6ed0203' + GOODIX   # écriture PC -> appareil
OTA_TX   = 'a6ed0402' + GOODIX
OTA_RX   = 'a6ed0403' + GOODIX
OTA_CTRL = 'a6ed0404' + GOODIX

# Firmware Seeed d'origine : service « Transparent UART » Microchip/ISSC, canal des commandes AT
SEEED_SVC = '49535343-fe7d-4ae5-8fa9-9fafd205e455'
SEEED_TX  = '49535343-1e4d-4bd9-ba61-23c647249616'   # notifications appareil -> PC
SEEED_RX  = '49535343-8841-43f4-a8d4-ecbe34729bb3'   # écriture PC -> appareil

APP_LOAD_ADDR = 0x0101B000      # seule adresse acceptée pour une application T1000-A


async def wait_for(name_prefix, timeout):
    """Attend un appareil dont le nom commence par name_prefix ; renvoie (device, adv)."""
    print(f'attente de « {name_prefix}… » ({timeout:.0f} s)…', flush=True)
    found = asyncio.get_running_loop().create_future()

    def cb(dev, adv):
        n = adv.local_name or dev.name or ''
        if name_prefix.lower() in n.lower() and not found.done():
            found.set_result((dev, adv))

    async with BleakScanner(cb):
        try:
            return await asyncio.wait_for(found, timeout)
        except asyncio.TimeoutError:
            sys.exit('introuvable')


import contextlib


@contextlib.asynccontextmanager
async def disconnecting(c):
    """Garantit la déconnexion d'un client déjà connecté (BleakClient en `async with` reconnecterait)."""
    try:
        yield c
    finally:
        with contextlib.suppress(Exception):
            await c.disconnect()


async def connect(name, timeout):
    """Attend l'appareil puis s'y connecte, en réessayant tant que le délai n'est pas écoulé :
    BlueZ échoue souvent au premier essai (br-connection-canceled) sur une fenêtre d'annonce courte."""
    deadline = time.time() + timeout
    while True:
        dev, adv = await wait_for(name, max(1.0, deadline - time.time()))
        print(f'trouvé : {adv.local_name} {dev.address} rssi={adv.rssi} services_pub={adv.service_uuids}',
              flush=True)
        c = BleakClient(dev, timeout=10)
        try:
            await c.connect()
            print(f'connecté, MTU={c.mtu_size}', flush=True)
            return c, adv
        except Exception as e:
            print(f'échec de connexion ({e}), nouvel essai…', flush=True)
            if time.time() > deadline:
                sys.exit('abandon : délai écoulé')
            await asyncio.sleep(0.3)


async def cmd_scan(a):
    seen = {}

    def cb(dev, adv):
        seen[dev.address] = (adv.local_name or dev.name or '?', adv.rssi, adv.service_uuids)

    async with BleakScanner(cb):
        await asyncio.sleep(a.t)
    for addr, (n, rssi, uu) in sorted(seen.items(), key=lambda x: -x[1][1]):
        print(f'{addr}  {rssi:4d} dBm  {n}  {" ".join(uu)}')


async def cmd_gatt(a):
    c, adv = await connect(a.name, a.t)
    try:
        for s in c.services:
            print(f'service {s.uuid}  {s.description}')
            for ch in s.characteristics:
                val = ''
                if 'read' in ch.properties:
                    try:
                        v = await c.read_gatt_char(ch)
                        val = f'  = {v.hex()} {v!r}'
                    except Exception as e:
                        val = f'  (lecture : {e})'
                print(f'   car {ch.uuid}  h={ch.handle}  {",".join(ch.properties)}{val}')
    finally:
        await c.disconnect()


# ---------------------------------------------------------------- DFU Goodix

class DfuError(Exception):
    pass


class GoodixDfu:
    """Maître DFU Goodix (transcription de components/libraries/dfu_master/dfu_master.c),
    mode normal (non « fast ») : blocs de 256 octets acquittés un par un."""
    GET_INFO, PROGRAM_START, PROGRAM_FLASH, PROGRAM_END = 0x01, 0x23, 0x24, 0x25
    SYSTEM_INFO, DFU_MODE_SET, DFU_FW_INFO_GET = 0x27, 0x41, 0x42
    ONCE = 256

    def __init__(self, client):
        self.c = client
        self.rx = bytearray()
        self.frames = asyncio.Queue()

    def _on_notify(self, _h, data):
        self.rx += data
        while True:
            i = self.rx.find(b'\x44\x47')
            if i < 0:
                del self.rx[:-1]
                return
            del self.rx[:i]
            if len(self.rx) < 6:
                return
            typ, ln = struct.unpack('<HH', self.rx[2:6])
            if len(self.rx) < 8 + ln:
                return
            body = bytes(self.rx[6:6 + ln])
            cks = struct.unpack('<H', self.rx[6 + ln:8 + ln])[0]
            del self.rx[:8 + ln]
            if (sum(struct.pack('<HH', typ, ln)) + sum(body)) & 0xFFFF != cks:
                print('trame reçue avec somme invalide, ignorée')
                continue
            self.frames.put_nowait((typ, body))

    async def start(self):
        await self.c.start_notify(OTA_TX, self._on_notify)

    async def send(self, typ, data=b''):
        hdr = struct.pack('<HH', typ, len(data))
        frame = b'\x44\x47' + hdr + data + struct.pack('<H', (sum(hdr) + sum(data)) & 0xFFFF)
        mtu = max(20, self.c.mtu_size - 3)
        for i in range(0, len(frame), mtu):
            await self.c.write_gatt_char(OTA_RX, frame[i:i + mtu], response=False)

    async def request(self, typ, data=b'', timeout=4.0):
        await self.send(typ, data)
        while True:
            try:
                rt, body = await asyncio.wait_for(self.frames.get(), timeout)
            except asyncio.TimeoutError:
                raise DfuError(f'pas de réponse à la commande {typ:#x}')
            if rt == typ:
                if not body or body[0] != 0x01:
                    raise DfuError(f'commande {typ:#x} refusée : {body.hex()}')
                return body
            print(f'trame inattendue {rt:#x} : {body.hex()}')


async def cmd_flash(a):
    img = open(a.file, 'rb').read()
    tail = img[-48:]
    pat, ver, bin_size, cks, load, run = struct.unpack('<HHIIII', tail[:20])
    if pat != 0x4744 or bin_size != len(img) - 48 or (sum(img[:bin_size]) & 0xFFFFFFFF) != cks:
        sys.exit('image invalide (passer par tools/mkimage.py)')
    if load != APP_LOAD_ADDR or run != APP_LOAD_ADDR:
        sys.exit(f'adresse de chargement {load:#x} ≠ {APP_LOAD_ADDR:#x} : refus (protège le bootloader)')
    img_info = tail[:40]          # dfu_image_info_t : pattern, version, boot_info (24), comments (12)
    print(f'image : {len(img)} octets, commentaire « {tail[28:40].split(bytes(1))[0].decode()} »')

    if a.enter_dfu:
        await at_session(a.enter_dfu, ['AT+DFU_ENTER'], a.t)
        print('le boîtier redémarre en bootloader DFU…')
    c, adv = await connect(a.name, a.t)
    async with disconnecting(c):
        d = GoodixDfu(c)
        await d.start()
        info = await d.request(d.GET_INFO)
        dfu_ver = info[17] if len(info) > 17 else 0
        print(f'GET_INFO ok (version DFU {dfu_ver}) : {info.hex()}')

        sysinfo = await d.request(d.SYSTEM_INFO, b'\x00' + struct.pack('<IH', 0x01000000, 48))
        boot_bin, boot_cks, boot_load, boot_run = struct.unpack('<IIII', sysinfo[8:24])
        print(f'bootloader : load={boot_load:#x} taille={boot_bin:#x} sécurité={sysinfo[1] >> 4}')
        if sysinfo[1] & 0xF0:
            sys.exit('le bootloader exige des images signées : abandon')

        save_addr = APP_LOAD_ADDR
        if dfu_ver >= 2:
            fw = await d.request(d.DFU_FW_INFO_GET)
            save_addr = struct.unpack('<I', fw[1:5])[0]
            run_in_app = fw[5]
            print(f'adresse d\'écriture annoncée={save_addr:#x}, exécution en appli={run_in_app}')
            if save_addr != APP_LOAD_ADDR:
                sys.exit(f'adresse annoncée {save_addr:#x} inattendue : abandon par sécurité')
            if run_in_app:
                sys.exit('l\'appareil n\'est pas en mode bootloader DFU : abandon')
            await d.send(d.DFU_MODE_SET, b'\x02')          # non-copy (simple banque), pas de réponse
            await asyncio.sleep(0.2)
        if boot_load + boot_bin + 48 >= save_addr:
            sys.exit('chevauchement avec le bootloader : abandon')

        if a.dry_run:
            print('--dry-run : arrêt avant toute écriture (le bootloader repart seul sur l\'appli après 3 min)')
            return
        info40 = bytearray(img_info)
        struct.pack_into('<I', info40, 12, save_addr)       # boot_info.load_addr
        await d.request(d.PROGRAM_START, b'\x00' + bytes(info40), timeout=20)
        print('effacement fait, écriture…')

        total, addr, sent, t0 = len(img), save_addr, 0, time.time()
        while sent < total:
            chunk = img[sent:sent + d.ONCE]
            await d.request(d.PROGRAM_FLASH, b'\x01' + struct.pack('<IH', addr, len(chunk)) + chunk)
            sent += len(chunk)
            addr += len(chunk)
            print(f'\r  {sent * 100 // total:3d} %  ({sent}/{total})', end='', flush=True)
        print(f'\nécrit en {time.time() - t0:.1f} s')
        await d.request(d.PROGRAM_END, b'\x01' + struct.pack('<I', sum(img) & 0xFFFFFFFF), timeout=10)
        print('PROGRAM_END accepté : l\'appareil redémarre sur le nouveau firmware')


# ---------------------------------------------------------------- AT (firmware Seeed)

async def at_session(name, cmds, timeout):
    c, adv = await connect(name, timeout)
    try:
        await c.start_notify(SEEED_TX, lambda _h, d: print(d.decode(errors='replace'), end='', flush=True))
        for cmd in cmds:
            print(f'>>> {cmd}', flush=True)
            data = cmd.encode() + b'\r\n'
            for i in range(0, len(data), 20):
                await c.write_gatt_char(SEEED_RX, data[i:i + 20], response=True)
            await asyncio.sleep(2.5)
    finally:
        try:
            await c.disconnect()
        except Exception:
            pass                      # normal après AT+DFU_ENTER (redémarrage)


async def cmd_at(a):
    await at_session(a.name, a.cmds, a.t)


# ---------------------------------------------------------------- console GUS

async def cmd_console(a):
    c, adv = await connect(a.name, a.t)
    async with disconnecting(c):
        await c.start_notify(GUS_TX, lambda _h, d: print(d.decode(errors='replace'), end='', flush=True))
        print(f'connecté à {adv.local_name} (Ctrl-D pour quitter)')
        loop = asyncio.get_running_loop()
        while c.is_connected:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            await c.write_gatt_char(GUS_RX, line.rstrip('\n').encode() + b'\r\n', response=False)
            await asyncio.sleep(0.3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('scan'); p.add_argument('-t', type=float, default=10)
    p = sub.add_parser('gatt'); p.add_argument('--name', default='T1000'); p.add_argument('-t', type=float, default=120)
    p = sub.add_parser('flash'); p.add_argument('file'); p.add_argument('--name', default='SENSECAP_GR_DFU')
    p.add_argument('-t', type=float, default=180)
    p.add_argument('--dry-run', action='store_true', help='interroge le bootloader sans rien écrire')
    p.add_argument('--enter-dfu', metavar='NOM', help='passer d\'abord le firmware Seeed NOM en DFU (AT+DFU_ENTER)')
    p = sub.add_parser('at'); p.add_argument('cmds', nargs='+'); p.add_argument('--name', default='T1000')
    p.add_argument('-t', type=float, default=180)
    p = sub.add_parser('console'); p.add_argument('--name', default='T1000A-Test'); p.add_argument('-t', type=float, default=60)
    a = ap.parse_args()
    try:
        asyncio.run({'scan': cmd_scan, 'gatt': cmd_gatt, 'flash': cmd_flash, 'at': cmd_at, 'console': cmd_console}[a.cmd](a))
    except DfuError as e:
        sys.exit(f'DFU : {e}')


if __name__ == '__main__':
    main()
