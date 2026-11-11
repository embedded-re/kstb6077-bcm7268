"""Stop explicitly attached test process groups when the kernel log reports a fault.

The watcher runs independently of browser/RPC calls. Attach only subprocesses
started with start_new_session=True; it never signals an existing desktop.
"""
import os
from pathlib import Path
import re
import signal
import threading
import time

FAULT = re.compile(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun')

class KernelFaultGuard:
    def __init__(self, log, interval=0.05):
        self.log = Path(log)
        self.interval = interval
        self.fault = None
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._processes = []
        self._thread = threading.Thread(target=self._watch, daemon=True)

    def start(self):
        self._thread.start()
        return self

    def attach(self, process):
        with self._lock:
            if process.poll() is not None:
                return
            if os.getpgid(process.pid) != process.pid:
                raise ValueError('test process must have its own session/process group')
            self._processes.append(process)
            if self.fault is not None:
                self._kill(process)

    @staticmethod
    def _kill(process):
        # Only groups validated by attach() are ever signalled.
        if process.poll() is not None:
            return
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def _watch(self):
        while not self._stop.is_set():
            try:
                text = self.log.read_text(errors='replace')
            except FileNotFoundError:
                text = ''
            match = FAULT.search(text)
            if match:
                line = text[text.rfind('\n', 0, match.start()) + 1:].split('\n', 1)[0]
                with self._lock:
                    self.fault = dict(monotonic=time.monotonic(), line=line)
                    for process in self._processes:
                        self._kill(process)
                return
            self._stop.wait(self.interval)

    def check(self):
        if self.fault is not None:
            raise RuntimeError('kernel fault: ' + self.fault['line'])

    def close(self):
        self._stop.set()
        self._thread.join()
