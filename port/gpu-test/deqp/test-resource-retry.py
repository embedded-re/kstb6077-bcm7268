#!/usr/bin/env python3
"""Host-only check: retry unexecuted cases after dEQP exits with ResourceError."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile


MOCK_DEQP = """from pathlib import Path
import sys
listfile = next(arg.split('=', 1)[1] for arg in sys.argv[1:]
                if arg.startswith('--deqp-caselist-file='))
for case in Path(listfile).read_text().splitlines():
    if case.endswith('.absent'):
        continue
    print("Test case '" + case + "'..", flush=True)
    if case.endswith('.resource'):
        print('  ResourceError (synthetic allocation failure)', flush=True)
        sys.exit(1)
    print('  Pass (Pass)', flush=True)
"""


def main():
    runner = Path(__file__).resolve().parents[1] / "kstb-deqp"
    cases = ["dEQP-GLES31.mock.resource", "dEQP-GLES31.mock.remaining",
             "dEQP-GLES31.mock.absent"]
    with tempfile.TemporaryDirectory(prefix="kstb-deqp-retry-") as temp:
        directory = Path(temp)
        (directory / "gles31").mkdir()
        casefile = directory / "cases.txt"
        casefile.write_text("\n".join(cases) + "\n")
        fake = directory / "gles31/deqp-gles31"
        fake.write_text("#!" + sys.executable + "\n" + MOCK_DEQP)
        fake.chmod(0o755)
        result = subprocess.run(
            [sys.executable, str(runner), "gles31", "--cases", str(casefile), "--chunk", "3"],
            env=dict(os.environ, DEQP=str(directory)), capture_output=True, text=True, timeout=10)
        print(result.stdout, end="")
        print(result.stderr, end="")
        assert result.returncode == 0, result.returncode
        rows = dict(line.split()[:2] for line in
                    (directory / "results-gles31.txt").read_text().splitlines())
        assert rows == dict(zip(cases, ["ResourceError", "Pass", "Missing"])), rows
        print("PASS: retry after positive ResourceError exit; genuinely absent case stays Missing")


if __name__ == "__main__":
    main()
