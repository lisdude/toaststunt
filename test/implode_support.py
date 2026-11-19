"""Private disposable MOO server for implode tests and ToastCore benchmarks."""
import json
from pathlib import Path
import secrets
import signal
import socket
import subprocess
import time

REFERENCE = [
    '{thelist, ?separator = ""} = args;',
    'if (separator == "")',
    '  return tostr(@thelist);',
    'elseif (thelist)',
    '  result = tostr(thelist[1]);',
    '  for elt in (thelist[2..$])',
    '    result = tostr(result, separator, elt);',
    '  endfor',
    '  return result;',
    'else',
    '  return "";',
    'endif',
]


def moo_strings(lines):
    return '{' + ','.join(json.dumps(line) for line in lines) + '}'


class Server:
    def __init__(self, binary, database, directory):
        self.directory = Path(directory)
        self.directory.mkdir()
        self.log = (self.directory / 'server.log').open('wb')
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            self.port = probe.getsockname()[1]
        self.process = subprocess.Popen(
            [str(Path(binary).resolve()), '-e', '-O', '-p', '0', '-4', '127.0.0.1', '--no-ipv6',
             str(Path(database).resolve()), str(self.directory / 'output.db'), '0'],
            cwd=self.directory, stdin=subprocess.PIPE, stdout=self.log, stderr=self.log)
        self.token = secrets.token_hex(32)
        offset = len(self.token) + 1
        handler = [f'if (length(argstr) <= {offset} || !equal(argstr[1..{offset}], "{self.token} "));',
                   '  return 0;', 'endif', f'code = argstr[{offset + 1}..$];',
                   'notify(player, "RESULT:" + generate_json(eval(code), "common-subset", 1));', 'return 0;']
        command = (';; o=create(#-1); add_verb(o,{player,"xd","do_login_command"},'
                   '{"this","none","this"}); set_verb_code(o,"do_login_command",' +
                   moo_strings(handler) + f'); listen(o,{self.port},["interface"->"127.0.0.1"]); return o;\ncontinue\n')
        try:
            self.process.stdin.write(command.encode())
            self.process.stdin.flush()
            deadline = time.monotonic() + 120
            while True:
                if self.process.poll() is not None:
                    raise RuntimeError('MOO startup failed: ' + (self.directory / 'server.log').read_text()[-4000:])
                try:
                    self.connect()
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise RuntimeError('MOO startup timeout')
                    time.sleep(.05)
            self.obj = self.eval('return tostr(create(#-1));')
            self.code('reference', REFERENCE)
        except BaseException:
            try:
                self.close()
            except AssertionError:
                pass
            raise

    def connect(self):
        self.sock = socket.create_connection(('127.0.0.1', self.port), timeout=60)
        self.stream = self.sock.makefile('rw', buffering=1)

    def eval(self, code):
        self.stream.write(self.token + ' ' + code + '\n')
        self.stream.flush()
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError('MOO connection closed')
            if line.startswith('RESULT:'):
                result = json.loads(line[len('RESULT:'):])
                if result[0] != 1:
                    raise AssertionError((code, result))
                return result[1]

    def outcome(self, expression):
        return self.eval('try r=' + expression + '; '
                         'if (typeof(r)==STR && length(r)>200); '
                         'return {\"long-string\",length(r),string_hash(r)}; endif; '
                         'return r; except e (ANY) return e[1]; endtry')

    def code(self, name, lines):
        self.eval(f'add_verb({self.obj},{{caller_perms(),"xd",{json.dumps(name)}}},'
                  '{"this","none","this"}); return 1;')
        result = self.eval(f'return set_verb_code({self.obj},{json.dumps(name)},' +
                           moo_strings(lines) + ');')
        assert result == [], (name, result)

    def option(self, name, value):
        # Only alters the disposable database; callers retain normal quotas unless requested.
        self.eval(f'if ("{name}" in properties($server_options)); '
                  f'$server_options.{name}={value}; else; '
                  f'add_property($server_options,"{name}",{value},{{caller_perms(),""}}); '
                  'endif; load_server_options(); return 1;')

    def close(self):
        if hasattr(self, 'stream'):
            self.stream.close()
            self.sock.close()
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdin.close()
        self.log.close()
        assert self.process.returncode == 0, self.directory / 'server.log'
