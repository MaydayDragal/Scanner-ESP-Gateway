"""Exercise the pinned component and application driver at a stateful RMT boundary."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--upstream', action='store_true', help='reproduce defects against unmodified installed component')
    args = parser.parse_args()
    component = ROOT / ('managed_components/espressif__led_strip' if args.upstream else 'components/led_strip')
    with tempfile.TemporaryDirectory(prefix='scanner-led-transport-') as temp:
        binary = Path(temp) / 'test.exe'
        command = [sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror']
        for directory in ('tests/rmt_stubs', 'main'):
            command += ['-I', str(ROOT / directory)]
        for directory in ('include', 'interface', 'src'):
            command += ['-I', str(component / directory)]
        command += [str(ROOT / path) for path in ('tests/test_scanner_led_transport.c', 'main/scanner_led.c', 'main/scanner_led_model.c', 'main/scanner_idle_model.c')]
        command += [str(component / path) for path in ('src/led_strip_api.c', 'src/led_strip_rmt_dev.c')]
        subprocess.run(command + ['-o', str(binary)], check=True)
        failed = []
        for case in ('transmit', 'wait', 'disable', 'persistent', 'bounded', 'init', 'create'):
            result = subprocess.run([str(binary), case])
            if result.returncode:
                failed.append(case)
        if failed:
            raise SystemExit('LED transport failures: ' + ', '.join(failed))

if __name__ == '__main__':
    main()
