"""Synthetic process-group tests only: no GPU or kernel log writes."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from kstb_fault_guard import KernelFaultGuard

class GuardTest(unittest.TestCase):
    def test_kills_blocked_test_and_child(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp)/'fake-kernel.txt'
            log.write_text('')
            guard = KernelFaultGuard(log).start()
            proc = subprocess.Popen([sys.executable, '-c',
                'import subprocess,time; p=subprocess.Popen(["sleep","60"]); '
                'print(p.pid,flush=True); time.sleep(60)'],
                stdout=subprocess.PIPE, text=True, start_new_session=True)
            child = int(proc.stdout.readline())
            try:
                guard.attach(proc)
                start = time.monotonic()
                log.write_text('synthetic test: Resetting GPU for hang.\n')
                # Main test thread blocks here; independent guard must still fire.
                self.assertEqual(proc.wait(timeout=2), -signal.SIGKILL)
                self.assertIsNotNone(guard.fault)
                path = Path('/proc')/str(child)/'stat'
                if path.exists():
                    self.assertEqual(path.read_text().rsplit(')',1)[1].split()[0], 'Z')
                print('Synthetic first-fault kill latency: %.3f s' % (time.monotonic()-start))
                with self.assertRaises(RuntimeError):guard.check()
            finally:
                guard.close()
                if proc.poll() is None:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
                proc.stdout.close()

    def test_fault_before_attach_stops_new_test(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp)/'fake-kernel.txt'
            log.write_text('synthetic test: MMU error\n')
            guard = KernelFaultGuard(log).start()
            proc = None
            try:
                end = time.monotonic()+2
                while guard.fault is None and time.monotonic()<end:time.sleep(.01)
                self.assertIsNotNone(guard.fault)
                proc = subprocess.Popen(['sleep','60'],start_new_session=True)
                guard.attach(proc)
                self.assertEqual(proc.wait(timeout=2),-signal.SIGKILL)
            finally:
                guard.close()
                if proc and proc.poll() is None:os.killpg(proc.pid,signal.SIGKILL);proc.wait()

if __name__ == '__main__':unittest.main()
