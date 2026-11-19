"""Compare native implode and the exact MOO reference on a disposable ToastCore.
Usage: python3 test/benchmark_implode.py MOO TOASTCORE_DB RESULTS.json [--elements 10000]
CPU and MOO ticks are reported independently; no live callers are changed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import tempfile
import time
from implode_support import Server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('database', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--elements', type=int, nargs='+', default=[5, 1000])
    parser.add_argument('--widths', type=int, nargs='+', help='characters per string element')
    args = parser.parse_args()
    if any(length < 1 for length in args.elements):
        parser.error('element counts must be positive')
    if args.widths is not None and any(width < 1 for width in args.widths):
        parser.error('element widths must be positive')
    binary, database, output = args.binary, args.database, args.output
    if output.exists() and (output.samefile(database) or output.samefile(binary)):
        parser.error('results output must be separate from the input database and executable')
    affinity = min(os.sched_getaffinity(0))
    report = {'database_sha256': hashlib.sha256(database.read_bytes()).hexdigest(),
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'cpu_affinity': affinity, 'settle_seconds': 60, 'samples_per_path': 16,
              'target_cpu_seconds': .15, 'element_widths': args.widths or [6], 'cases': []}
    with tempfile.TemporaryDirectory(prefix='implode-benchmark-') as directory:
        server = Server(binary, database, Path(directory) / 'server')
        try:
            os.sched_setaffinity(server.process.pid, {affinity})
            ready = time.monotonic()
            print('ToastCore loaded; settling for 60 seconds', flush=True)
            while time.monotonic() - ready < 60:
                time.sleep(min(15, 60 - (time.monotonic() - ready)))
            report['actual_settle_seconds'] = time.monotonic() - ready
            for name, value in [('fg_ticks', 2147483647), ('fg_seconds', 60),
                                ('max_string_concat', 10000000), ('max_concat_catchable', 1)]:
                server.option(name, value)
            for name in ['values', 'separator']:
                server.eval(f'add_property({server.obj},"{name}",0,{{caller_perms(),""}}); return 1;')
            for path in ['reference', 'native']:
                call = 'this:reference(values, separator)' if path == 'reference' else 'implode(values, separator)'
                server.code('bench_' + path, [
                    'values=this.values; separator=this.separator;',
                    'before=usage(); start=ftime(1); ticks=ticks_left();',
                    'for i in [1..args[1]]', 'result=' + call + ';', 'endfor',
                    'ticks=ticks-ticks_left(); elapsed=ftime(1)-start; after=usage();',
                    'return {after[2]+after[3]-before[2]-before[3],elapsed,ticks,length(result)};',
                ])
            for width in args.widths or [6]:
                element = 'x' * width if args.widths is not None else 'Ab~00 '
                for length in args.elements:
                    for separator in [', ', '']:
                        server.eval('values={}; for i in [1..' + str(length) +
                                    ']; values={@values,' + json.dumps(element) + '}; endfor; ' +
                                    f'{server.obj}.values=values; {server.obj}.separator=' +
                                    json.dumps(separator) + '; return 1;')
                        assert server.eval(f'return equal(implode({server.obj}.values,{server.obj}.separator),'
                                           f'{server.obj}:reference({server.obj}.values,{server.obj}.separator));') == 1
                        record = {'elements': length, 'element_width': width, 'separator': separator, 'paths': {}}
                        iterations = {}
                        def sample(path, n):
                            cpu, wall, ticks, size = server.eval(f'return {server.obj}:bench_{path}({n});')
                            return {'cpu_seconds': cpu, 'wall_seconds': wall, 'ticks': ticks, 'bytes': size}
                        for path in ['reference', 'native']:
                            n = 1
                            while True:
                                calibration = sample(path, n)
                                if calibration['cpu_seconds'] >= .02:
                                    break
                                n *= 10
                            iterations[path] = max(1, int(n * .15 / calibration['cpu_seconds']))
                            record['paths'][path] = {'iterations': iterations[path], 'samples': []}
                            for _ in range(3):
                                sample(path, iterations[path])
                        # One MOO process, alternate timed path order; no parallel CPU load.
                        for pair in range(16):
                            for path in (['reference', 'native'] if pair % 2 == 0 else ['native', 'reference']):
                                record['paths'][path]['samples'].append(sample(path, iterations[path]))
                        for path, data in record['paths'].items():
                            n = data['iterations']
                            data['median_cpu_us_per_call'] = statistics.median(s['cpu_seconds'] / n * 1e6 for s in data['samples'])
                            data['median_ticks_per_call'] = statistics.median(s['ticks'] / n for s in data['samples'])
                        a = record['paths']['reference']['median_cpu_us_per_call']
                        b = record['paths']['native']['median_cpu_us_per_call']
                        record['cpu_percent_change'] = 100 * (b / a - 1)
                        record['cpu_speedup'] = a / b
                        report['cases'].append(record)
                        output.write_text(json.dumps(report, indent=2) + '\n')
                        print(json.dumps({k: v for k, v in record.items() if k != 'paths'}), flush=True)
        finally:
            server.close()
    assert hashlib.sha256(database.read_bytes()).hexdigest() == report['database_sha256']
    report['input_unchanged'] = True
    report['stopped_cleanly'] = True
    output.write_text(json.dumps(report, indent=2) + '\n')
    print('Complete; clean shutdown; input unchanged', flush=True)


if __name__ == '__main__':
    main()
