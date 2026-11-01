"""Windows flasher safety tests; exercise the shared installer without hardware."""
from pathlib import Path
import contextlib
import io
import sys
import types
import unittest
from unittest.mock import Mock,patch

import install
from tools import flasher_backend as backend
import test_install as fixture_module


class FlasherTests(unittest.TestCase):
    setUp=fixture_module.Fixture.setUp
    save_manifest=fixture_module.Fixture.save_manifest

    def execute(self,operation='install',short=False,wrong_chip=False,upload_error=None):
        self.commands=[];self.phases=[]
        def runner(command,check=True):
            self.commands.append(command)
            if command[-1]=='flash_id':print('Detected flash size: '+('8MB' if wrong_chip else '16MB'))
            elif 'read_flash' in command:Path(command[-1]).write_bytes(bytes(100 if short else 0x1000000))
            elif 'write_flash' in command:
                backups=list(self.root.glob('orb-backups/*/orb-before-install.bin'))
                self.assertEqual(len(backups),1)
                self.assertEqual(backups[0].stat().st_size,0x1000000)
                self.assertTrue(backups[0].with_suffix('.sha256').exists())
        self.uploader=Mock(side_effect=upload_error,return_value=[dict(file=Path(p['file']).name,faces=p['face_count'],status='installed') for p in self.packs])
        with patch.object(install,'ROOT',self.root),contextlib.redirect_stdout(io.StringIO()):
            return backend.execute(operation,'COM99',self.root,self.root,self.phases.append,runner,self.uploader)

    def test_full_flow_verifies_before_backup_then_flash_and_sd(self):
        result=self.execute();self.assertTrue(result['firmware_installed']);self.assertEqual(result['available_faces'],42)
        self.assertTrue(result['sd_complete']);self.assertEqual(len(self.commands),3)
        self.assertEqual(self.commands[0][-1],'flash_id');self.assertIn('read_flash',self.commands[1]);self.assertIn('write_flash',self.commands[2])
        self.assertEqual(self.phases,['connect','backup','flash','firmware-ready','sd','complete'])
        self.assertNotIn('erase_flash',str(self.commands))

    def test_short_backup_stops_before_writing(self):
        with self.assertRaises(RuntimeError):self.execute(short=True)
        self.assertEqual(len(self.commands),2);self.uploader.assert_not_called()

    def test_wrong_flash_size_never_backs_up_or_writes(self):
        with self.assertRaises(RuntimeError):self.execute(wrong_chip=True)
        self.assertEqual(len(self.commands),1);self.uploader.assert_not_called()

    def test_invalid_payload_never_accesses_hardware(self):
        (self.root/'firmware.bin').write_bytes(b'corrupted')
        with self.assertRaises(ValueError):self.execute()
        self.assertEqual(self.commands,[])

    def test_sd_failure_retains_firmware_success(self):
        result=self.execute(upload_error=OSError('Card unavailable'))
        self.assertTrue(result['firmware_installed']);self.assertFalse(result['sd_complete']);self.assertEqual(result['available_faces'],8)
        self.assertIn('Card unavailable',result['sd_error']);self.assertTrue(Path(result['backup']).exists())

    def test_repair_never_flashes_or_backs_up(self):
        result=self.execute('repair');self.assertEqual(self.commands,[]);self.assertFalse(result['firmware_installed'])
        self.assertEqual(result['available_faces'],42);self.assertFalse((self.root/'orb-backups').exists())

    def test_verify_never_accesses_device(self):
        self.assertTrue(self.execute('verify')['verified']);self.assertEqual(self.commands,[]);self.uploader.assert_not_called()

    def test_in_process_esptool_dispatch_and_nonzero_exit(self):
        module=types.SimpleNamespace(__version__='4.8.1',main=Mock())
        command=[sys.executable,'-m','esptool','--port','COM99','flash_id']
        with patch.dict(sys.modules,{'esptool':module}):
            backend.run_esptool(command);module.main.assert_called_once_with(command[3:])
            module.main.side_effect=SystemExit(2)
            with self.assertRaises(RuntimeError):backend.run_esptool(command)
            with self.assertRaises(ValueError):backend.run_esptool(['unexpected-command'])

    def test_setup_closes_serial_on_success_and_error(self):
        clock=Mock();clock.command.return_value={'active':True}
        with patch.object(install,'ROOT',self.root):
            result=backend.execute('setup','COM99',self.root,self.root,connector=lambda port:clock)
            self.assertTrue(result['setup_started']);clock.serial.close.assert_called_once()
            clock.serial.close.reset_mock();clock.command.side_effect=RuntimeError('Old firmware')
            with self.assertRaises(RuntimeError):backend.execute('setup','COM99',self.root,self.root,connector=lambda port:clock)
            clock.serial.close.assert_called_once()


if __name__=='__main__':unittest.main()
