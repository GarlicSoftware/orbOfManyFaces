"""Install bundled SD libraries over USB without replacing existing files."""
from pathlib import Path
import argparse
import hashlib
import json
import time
try:
    from .device import Clock
except ImportError:
    from device import Clock

ROOT = Path(__file__).resolve().parents[1]


def connect(port, timeout=60):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            return Clock(port)
        except (OSError, TimeoutError) as error:
            last = error
            time.sleep(1)
    raise RuntimeError(f'Orb did not become ready on {port}: {last}. Check its port and retry.')


def bundled_packs(root=ROOT):
    result = []
    for group in json.loads((root / 'sd-groups/index.json').read_text(encoding='utf-8')):
        path = root / 'sd-groups' / group['file']
        if path.parent.resolve() != (root / 'sd-groups').resolve():
            raise ValueError('Invalid pack filename')
        result.append(dict(file='sd-groups/' + group['file'], bytes=path.stat().st_size,
                           sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                           face_count=len(group['faces'])))
    return result


def matches(info, pack):
    return (info.get('exists') and info.get('valid') and
            info.get('bytes') == pack['bytes'] and info.get('sha256') == pack['sha256'])


def upload(clock, name, data, digest, report=print):
    clock.command(f'sd begin {name} {len(data)} {digest}')
    reported = -1
    for offset in range(0, len(data), 1024):
        progress = offset * 4 // len(data)
        if progress > reported:
            report(f'{name}: {progress * 25}%')
            reported = progress
        chunk = data[offset:offset + 1024]
        for attempt in range(4):
            clock.serial.write(f'\n?clock sd chunk {offset} {len(chunk)}\n'.encode() + chunk)
            try:
                clock.response(timeout=3, cmd='SD chunk', matches=lambda reply: reply.get('offset') == offset)
                break
            except TimeoutError:
                if attempt == 3:
                    raise RuntimeError(f'{name} at {offset}/{len(data)}: no acknowledgement')
    try:
        clock.command('sd finish', timeout=20)
    except TimeoutError:
        # A lost final acknowledgement may follow a successful atomic rename.
        pass


def install_packs(port, packs, root=ROOT, report=print, connector=connect):
    results = []
    clock = None
    try:
        report('Waiting for the Orb to reboot and open its USB connection...')
        clock = connector(port)
        status = clock.command('sd status', timeout=10)
        if status.get('sd_protocol') != 1:
            raise RuntimeError('Update firmware with install.py before uploading SD libraries.')
        if not status.get('available'):
            raise RuntimeError('SD card unavailable. Use a compatible FAT32 card; built-in clocks still work.')
        clock.command('sd abort')
        for pack in packs:
            name = Path(pack['file']).name
            outcome = dict(file=name, faces=pack['face_count'], status='failed')
            results.append(outcome)
            try:
                data = (root / pack['file']).read_bytes()
                if len(data) != pack['bytes'] or hashlib.sha256(data).hexdigest() != pack['sha256']:
                    raise ValueError('Local pack hash mismatch')
                info = clock.command('sd info ' + name, timeout=15)
                if matches(info, pack):
                    outcome['status'] = 'verified existing'
                elif info.get('exists'):
                    raise RuntimeError('Filename already exists with different or invalid contents; left untouched')
                else:
                    report('Uploading: ' + name)
                    upload(clock, name, data, pack['sha256'], report)
                    if not matches(clock.command('sd info ' + name, timeout=15), pack):
                        raise RuntimeError('Stored pack failed SHA256 verification')
                    outcome['status'] = 'installed'
                report(outcome['status'] + ': ' + name)
            except (OSError, ValueError, RuntimeError, TimeoutError) as error:
                outcome['error'] = str(error)
                report(f'Could not install {name}: {error}')
                # Partial files are never visible as libraries. Abort only this transfer.
                try:
                    clock.command('sd abort', timeout=2)
                except (OSError, RuntimeError, TimeoutError):
                    break
    except (OSError, ValueError, RuntimeError, TimeoutError, KeyboardInterrupt) as error:
        report('Optional SD libraries unavailable: ' + (str(error) or 'Interrupted by user'))
    finally:
        if clock is not None:
            clock.serial.close()
    complete = {r['file']: r for r in results if r['status'] != 'failed'}
    available = 8 + sum(r['faces'] for r in complete.values())
    report(f'Clock faces verified available from this release: {available}/42 '
           f'(8 built-in, {available - 8} on SD; {len(complete)}/4 packs).')
    if len(complete) != len(packs):
        report(f'Retry without flashing: python install.py --port {port} --sd-only')
        report('Manual alternative: python tools/upload_groups.py --port ' + port)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    args = parser.parse_args()
    results = install_packs(args.port, bundled_packs())
    return 0 if len(results) == 4 and all(r['status'] != 'failed' for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
