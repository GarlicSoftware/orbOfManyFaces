"""Installer safety and USB failure cases; no physical hardware required."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import install
from tools import upload_groups as ug


class FakeClock:
    def __init__(self, files=None, available=True, corrupt=False, lost_finish=False, fail_chunk=False, lose_chunk=False):
        self.files = dict(files or {})
        self.available = available
        self.corrupt = corrupt
        self.lost_finish = lost_finish
        self.fail_chunk = fail_chunk
        self.lose_chunk = lose_chunk
        self.chunk_acks = set()
        self.serial = self
        self.closed = False
        self.begins = []
        self.pending = bytearray()
        self.current = None
        self.offset = None

    def close(self):
        self.closed = True

    def command(self, cmd, timeout=20):
        if cmd == 'sd status':
            return dict(ok=True, sd_protocol=1, available=self.available)
        if cmd == 'sd abort':
            self.pending.clear()
            return dict(ok=True)
        if cmd.startswith('sd info '):
            name = cmd[8:]
            data = self.files.get(name)
            return dict(ok=True, file=name, exists=data is not None, valid=True,
                        bytes=len(data or b''), sha256=hashlib.sha256(data or b'').hexdigest())
        if cmd.startswith('sd begin '):
            _, _, self.current, size, digest = cmd.split()
            if self.current in self.files:
                raise RuntimeError('Refuse overwrite')
            self.pending.clear()
            self.begins.append(self.current)
            return dict(ok=True)
        if cmd == 'sd finish':
            self.files[self.current] = bytes(self.pending) + (b'bad' if self.corrupt else b'')
            if self.lost_finish:
                raise TimeoutError('Lost final acknowledgement')
            return dict(ok=True)
        raise AssertionError(cmd)

    def write(self, data):
        _, command, chunk = data.split(b'\n', 2)
        *_, offset, size = command.split()
        self.offset = int(offset)
        if len(self.pending) == self.offset:
            self.pending.extend(chunk)

    def response(self, timeout, cmd, matches):
        if self.fail_chunk:
            raise TimeoutError('USB disconnected')
        if self.lose_chunk and self.offset not in self.chunk_acks:
            self.chunk_acks.add(self.offset)
            raise TimeoutError('Lost chunk acknowledgement')
        reply = dict(ok=True, offset=self.offset)
        assert matches(reply)
        return reply


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'sd-groups').mkdir()
        self.packs = []
        for n, faces in enumerate((12, 9, 10, 3)):
            data = bytes([n]) * 2500
            path = self.root / 'sd-groups' / f'library-{n}.ocg'
            path.write_bytes(data)
            self.packs.append(dict(file='sd-groups/' + path.name, bytes=len(data),
                                   sha256=hashlib.sha256(data).hexdigest(), face_count=faces))
        self.images = []
        for name, addr, limit in [('bootloader.bin', 0, 0x8000), ('partitions.bin', 0x8000, 0x9000), ('firmware.bin', 0x10000, 0x650000)]:
            data = bytes(100)
            (self.root / name).write_bytes(data)
            self.images.append(dict(file=name, address=hex(addr), limit=limit, bytes=100, sha256=hashlib.sha256(data).hexdigest()))
        self.manifest = dict(release='test', board='test', chip='esp32s3', flash_bytes=0x1000000,
                             contains_nvs=False, images=self.images, sd_packs=self.packs)
        self.save_manifest()
        self.messages = []

    def save_manifest(self):
        (self.root / 'manifest.json').write_text(json.dumps(self.manifest))

    def run_upload(self, clock):
        return ug.install_packs('TEST', self.packs, self.root, self.messages.append, lambda port: clock)

    def test_fresh_sd_installs_all_and_preserves_other_files(self):
        clock = FakeClock({'unrelated.ocg': b'user artwork', 'themes/file': b'keep'})
        result = self.run_upload(clock)
        self.assertEqual([r['status'] for r in result], ['installed'] * 4)
        self.assertEqual(clock.files['unrelated.ocg'], b'user artwork')
        self.assertEqual(clock.files['themes/file'], b'keep')
        self.assertTrue(clock.closed)
        self.assertTrue(any('42/42' in m for m in self.messages))

    def test_reinstall_verifies_and_skips_all(self):
        clock = FakeClock()
        self.run_upload(clock)
        clock.begins.clear()
        result = self.run_upload(clock)
        self.assertEqual([r['status'] for r in result], ['verified existing'] * 4)
        self.assertFalse(clock.begins)

    def test_conflict_is_not_overwritten(self):
        clock = FakeClock({'library-0.ocg': b'user data'})
        result = self.run_upload(clock)
        self.assertEqual(clock.files['library-0.ocg'], b'user data')
        self.assertEqual(result[0]['status'], 'failed')
        self.assertTrue(any('30/42' in m for m in self.messages))

    def test_missing_sd(self):
        clock = FakeClock(available=False)
        self.assertEqual(self.run_upload(clock), [])
        self.assertFalse(clock.begins)
        self.assertTrue(any('8/42' in m for m in self.messages))

    def test_lost_finish_ack_is_verified(self):
        result = self.run_upload(FakeClock(lost_finish=True))
        self.assertTrue(all(r['status'] == 'installed' for r in result))

    def test_lost_chunk_ack_retries_idempotently(self):
        result = self.run_upload(FakeClock(lose_chunk=True))
        self.assertTrue(all(r['status'] == 'installed' for r in result))

    def test_corrupt_readback_is_not_counted(self):
        self.assertTrue(all(r['status'] == 'failed' for r in self.run_upload(FakeClock(corrupt=True))))

    def test_interruption_and_retry(self):
        clock = FakeClock(fail_chunk=True)
        self.assertTrue(all(r['status'] == 'failed' for r in self.run_upload(clock)))
        self.assertFalse(clock.files)
        clock.fail_chunk = False
        self.assertTrue(all(r['status'] == 'installed' for r in self.run_upload(clock)))

    def test_local_corruption_never_uploaded(self):
        (self.root / self.packs[0]['file']).write_bytes(b'corrupt')
        clock = FakeClock()
        self.assertEqual(self.run_upload(clock)[0]['status'], 'failed')
        self.assertNotIn('library-0.ocg', clock.begins)

    def test_manifest_checks_and_protected_regions(self):
        with patch.object(install, 'ROOT', self.root):
            install.validate()
            for field, value in [('address', '0x9000'), ('limit', 0x1000000)]:
                original = self.images[2][field]
                self.images[2][field] = value
                self.save_manifest()
                with self.assertRaises(ValueError):
                    install.validate()
                self.images[2][field] = original
            self.images[2]['file'] = '../outside.bin'
            self.save_manifest()
            with self.assertRaises(ValueError):
                install.validate()

    def run_installer(self, extra=(), short_backup=False, sd_error=False):
        self.commands = []
        def run(cmd, check):
            self.commands.append(cmd)
            if 'read_flash' in cmd:
                Path(cmd[-1]).write_bytes(bytes(100 if short_backup else 0x1000000))
        def sd(*args):
            if sd_error:
                raise OSError('USB disappeared')
            return []
        with patch.object(install, 'ROOT', self.root), patch.object(sys, 'argv', ['install.py', '--port', 'TEST', *extra]), patch.object(install.Path, 'cwd', return_value=self.root), patch.object(install.subprocess, 'run', side_effect=run), patch.object(ug, 'install_packs', side_effect=sd) as upload, patch('builtins.print'):
            install.main()
            return upload

    def test_flash_then_sd_and_no_erase(self):
        upload = self.run_installer()
        self.assertEqual(len(self.commands), 2)
        self.assertIn('read_flash', self.commands[0])
        self.assertIn('write_flash', self.commands[1])
        self.assertNotIn('erase_flash', str(self.commands))
        upload.assert_called_once()
        self.assertTrue(list(self.root.glob('orb-backups/*/*.sha256')))

    def test_incomplete_backup_blocks_flash(self):
        with self.assertRaises(RuntimeError):
            self.run_installer(short_backup=True)
        self.assertEqual(len(self.commands), 1)

    def test_optional_failure_does_not_fail_firmware_install(self):
        self.run_installer(sd_error=True)
        self.assertEqual(len(self.commands), 2)

    def test_sd_only_never_flashes_or_backs_up(self):
        with self.assertRaises(RuntimeError):
            self.run_installer(extra=['--sd-only'])
        self.assertEqual(self.commands, [])

    def test_skip_sd_never_calls_uploader(self):
        upload = self.run_installer(extra=['--skip-sd'])
        upload.assert_not_called()
        self.assertEqual(len(self.commands), 2)

    def test_reboot_wait_retries_connection(self):
        clock = FakeClock()
        with patch.object(ug, 'Clock', side_effect=[OSError('rebooting'), TimeoutError(), clock]) as connect, patch.object(ug.time, 'sleep'):
            self.assertIs(ug.connect('TEST'), clock)
            self.assertEqual(connect.call_count, 3)

    def test_failed_flash_never_uploads(self):
        def fail_write(cmd, check):
            if 'read_flash' in cmd:
                Path(cmd[-1]).write_bytes(bytes(0x1000000))
            else:
                raise install.subprocess.CalledProcessError(1, cmd)
        with patch.object(install, 'ROOT', self.root), patch.object(sys, 'argv', ['install.py', '--port', 'TEST']), patch.object(install.Path, 'cwd', return_value=self.root), patch.object(install.subprocess, 'run', side_effect=fail_write), patch.object(ug, 'install_packs') as upload, patch('builtins.print'):
            with self.assertRaises(install.subprocess.CalledProcessError):
                install.main()
            upload.assert_not_called()

    def test_dry_run_never_accesses_device(self):
        upload = self.run_installer(extra=['--dry-run'])
        self.assertEqual(self.commands, [])
        upload.assert_not_called()

    def test_check_never_accesses_device(self):
        upload = self.run_installer(extra=['--check'])
        self.assertEqual(self.commands, [])
        upload.assert_not_called()


if __name__ == '__main__':
    unittest.main()
