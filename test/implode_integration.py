"""Differential implode tests. Usage: python3 test/implode_integration.py MOO DATABASE
DATABASE is read only; all changes live in a temporary server/output database.
"""
import hashlib
from pathlib import Path
import socket
import sys
import tempfile
from implode_support import Server


def main():
    binary, database = map(Path, sys.argv[1:])
    before = hashlib.sha256(database.read_bytes()).hexdigest()
    checks = 0
    with tempfile.TemporaryDirectory(prefix='implode-tests-') as directory:
        server = Server(binary, database, Path(directory) / 'server')
        try:
            rollout = Path(__file__).resolve().parents[1] / 'docs/Features/implode_from_list_compat.moo'
            server.code('rollout', rollout.read_text().splitlines())

            def equal(actual, expected, label):
                nonlocal checks
                assert actual == expected, (label, expected, actual)
                checks += 1

            def compare(arguments, expected=None):
                native = server.outcome(f'implode({arguments})')
                reference = server.outcome(f'{server.obj}:reference({arguments})')
                equal(native, reference, arguments)
                equal(server.outcome(f'{server.obj}:rollout({arguments})'), reference,
                      'rollout: ' + arguments)
                if expected is not None:
                    equal(native, expected, arguments + ' expected value')

            equal(server.eval('return function_info("implode")[1..3];'), ['implode', 1, 2],
                  'builtin registered with its public name and arity')
            for args, expected in [
                ('{}', ''), ('{}, "-"', ''), ('{"one"}', 'one'),
                ('{"one"}, ","', 'one'), ('{"a","b"}', 'ab'),
                ('{"a","b","c"}, " | "', 'a | b | c'),
                ('{"a","b"}, ""', 'ab'), ('{"a","b"}, 42', 'a42b'),
                ('{"","b",""}, ","', ',b,'), ('{"",""}, "-"', '-'),
                ('{"A a"," B ","~00~0D~7e"}, "~FF"', 'A a~FF B ~FF~00~0D~7e'),
            ]:
                compare(args, expected)
            values = '{"MiXeD ",42,-7,1.25,#0,#-1,E_TYPE,true,false,{1,{2}},["A"->1]}'
            for sep in ['""', '", "', '0', '42', '1.5', '#0', 'E_TYPE', 'true',
                        'false', '{}', '{1,2}', '[]', '["x"->42]']:
                compare(f'{values}, {sep}')
            compare(values)
            # Temporary complex elements/separators exercise ownership of non-scalars too.
            compare('{create(#-1,1), $waif:new()}, {create(#-1,1), $waif:new()}')
            for args in ['', '{}, "-", 1']:
                compare(args, 'E_ARGS')
            for args in ['"ab"', '"ab", "-"', '0', '[]', '#0', 'true']:
                equal(server.outcome(f'implode({args})'), 'E_TYPE', args)
                equal(server.outcome(f'{server.obj}:rollout({args})'),
                      server.outcome(f'{server.obj}:reference({args})'),
                      'non-list rollout: ' + args)
            equal(server.outcome(f'{server.obj}:reference("ab","-")'), 'a-b', 'legacy string input')
            equal(server.outcome(f'{server.obj}:reference("ab")'), 'E_TYPE', 'legacy string without separator')
            # Keep oversized source values in the disposable database before lowering the limit.
            server.eval(f'add_property({server.obj},"big","",'
                        '{caller_perms(),""}); return 1;')
            server.eval(f'x="x"; for i in [1..11]; x=tostr(x,x); endfor; '
                        f'{server.obj}.big=x; return 1;')
            server.eval(f'add_property({server.obj},"boundary",{server.obj}.big[1..1025],'
                        '{caller_perms(),""}); return 1;')
            server.option('max_concat_catchable', 1)
            server.option('max_string_concat', 1024)
            for n in [1023,1024]:
                for suffix in ['', ', ""', ', ","']:
                    arguments = f'{{{server.obj}.big[1..{n}]}}{suffix}'
                    compare(arguments)
                    equal(server.outcome(f'implode({arguments})')[1], n, "exact boundary length")
            server.eval(f'add_property({server.obj},"values",{{{server.obj}.big}},'
                        '{caller_perms(),""}); return 1;')
            for suffix in ['', ', ""', ', ","']:
                compare(f'{server.obj}.values{suffix}', 'E_QUOTA')
                compare(f'{{{server.obj}.boundary}}{suffix}', 'E_QUOTA')
            for n in [1022,1023,1024]:
                for sep in ['""', '","', '42']:
                    arguments = f'{{{server.obj}.big[1..{n}],"x"}}, {sep}'
                    compare(arguments)
                    expected_size = n + 1 + (0 if sep == '""' else 1 if sep == '","' else 2)
                    actual = server.outcome(f'implode({arguments})')
                    equal(actual if expected_size > 1024 else actual[1],
                          'E_QUOTA' if expected_size > 1024 else expected_size,
                          'combined boundary length/error')
            # A separator that would exceed the limit must never be converted if unused.
            compare(f'{{}}, {server.obj}.big', '')
            compare(f'{{"ok"}}, {server.obj}.big', 'ok')
            compare(f'{{"a","b"}}, {server.obj}.big', 'E_QUOTA')
            equal(server.eval('return implode({"still","working"}," ");'), 'still working',
                  'stream exception state restored after overflow')
            # max_concat_catchable=0 must abort the task instead of returning E_QUOTA.
            server.option('max_concat_catchable', 0)
            for expression in [f'implode({server.obj}.values)',
                               f'{server.obj}:reference({server.obj}.values)']:
                server.sock.settimeout(1)
                try:
                    server.outcome(expression)
                except (TimeoutError, socket.timeout):
                    checks += 1
                else:
                    raise AssertionError('oversize must abort: ' + expression)
                server.stream.close()
                server.sock.close()
                server.connect()
                equal(server.eval('return implode({"alive"});'), 'alive', 'server survived task abort')
        finally:
            server.close()
    assert hashlib.sha256(database.read_bytes()).hexdigest() == before
    print(f'PASS: {checks} checks; clean shutdown; input database unchanged')


if __name__ == '__main__':
    main()
