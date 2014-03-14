#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use,
# copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following
# conditions:
#
# This permission notice shall be included in all copies or
# substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
# KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHOR(S) BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
# AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
# OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

import errno
import os
import subprocess
import shlex
import time
import sys
import traceback

from .core import testBinDir, TestResult


# Platform global variables
if 'PIGLIT_PLATFORM' in os.environ:
    PIGLIT_PLATFORM = os.environ['PIGLIT_PLATFORM']
else:
    PIGLIT_PLATFORM = ''


class Test(object):
    def __init__(self, runConcurrent=False):
        '''
                'runConcurrent' controls whether this test will
                execute it's work (i.e. __doRunWork) on the calling thread
                (i.e. the main thread) or from the ConcurrentTestPool threads.
        '''
        self.runConcurrent = runConcurrent
        self.skip_test = False

        # This is a hook for doing some testing on execute right before
        # self.run is called.
        self._test_hook_execute_run = lambda: None

    def run(self):
        raise NotImplementedError

    def execute(self, env, path, log, json_writer, dmesg):
        '''
        Run the test.

        :path:
            Fully qualified test name as a string.  For example,
            ``spec/glsl-1.30/preprocessor/compiler/keywords/void.frag``.
        '''
        log_current = log.pre_log(path if env.verbose else None)

        # Run the test
        if env.execute:
            try:
                time_start = time.time()
                dmesg.update_dmesg()
                self._test_hook_execute_run()
                result = self.run(env)
                result = dmesg.update_result(result)
                time_end = time.time()
                if 'time' not in result:
                    result['time'] = time_end - time_start
                if 'result' not in result:
                    result['result'] = 'fail'
                if not isinstance(result, TestResult):
                    result = TestResult(result)
                    result['result'] = 'warn'
                    result['note'] = 'Result not returned as an instance ' \
                                     'of TestResult'
            except:
                result = TestResult()
                result['result'] = 'fail'
                result['exception'] = str(sys.exc_info()[0]) + \
                    str(sys.exc_info()[1])
                result['traceback'] = \
                    "".join(traceback.format_tb(sys.exc_info()[2]))

            log.log(path, result['result'])
            log.post_log(log_current, result['result'])

            if 'subtest' in result and len(result['subtest']) > 1:
                for test in result['subtest']:
                    result['result'] = result['subtest'][test]
                    json_writer.write_dict_item(os.path.join(path, test), result)
            else:
                json_writer.write_dict_item(path, result)
        else:
            log.log(path, 'dry-run')
            log.post_log(log_current, 'dry-run')


# ExecTest: A shared base class for tests that simply runs an executable.
class ExecTest(Test):
    def __init__(self, command):
        Test.__init__(self)
        self.command = command
        self.split_command = os.path.split(self._command[0])[1]
        self.env = {}


        self.skip_test = self.check_for_skip_scenario(command)

    @property
    def command(self):
        return self._command

    @command.setter
    def command(self, value):
        if isinstance(value, basestring):
            self._command = shlex.split(str(value))
            return
        self._command = value

    def interpretResult(self, out, returncode, results):
        raise NotImplementedError
        return out

    def run(self, env):
        """
        Run a test.  The return value will be a dictionary with keys
        including 'result', 'info', 'returncode' and 'command'.
        * For 'result', the value may be one of 'pass', 'fail', 'skip',
          'crash', or 'warn'.
        * For 'info', the value will include stderr/out text.
        * For 'returncode', the value will be the numeric exit code/value.
        * For 'command', the value will be command line program and arguments.
        """
        fullenv = os.environ.copy()
        for e in self.env:
            fullenv[e] = str(self.env[e])

        if self.command is not None:
            command = self.command

            if env.valgrind:
                command[:0] = ['valgrind', '--quiet', '--error-exitcode=1',
                               '--tool=memcheck']

            i = 0
            while True:
                if self.skip_test:
                    out = "PIGLIT: {'result': 'skip'}\n"
                    err = ""
                    returncode = None
                else:
                    out, err, returncode = self.get_command_result(command,
                                                                   fullenv)

                # https://bugzilla.gnome.org/show_bug.cgi?id=680214 is
                # affecting many developers.  If we catch it
                # happening, try just re-running the test.
                if out.find("Got spurious window resize") >= 0:
                    i = i + 1
                    if i >= 5:
                        break
                else:
                    break

            # proc.communicate() returns 8-bit strings, but we need
            # unicode strings.  In Python 2.x, this is because we
            # will eventually be serializing the strings as JSON,
            # and the JSON library expects unicode.  In Python 3.x,
            # this is because all string operations require
            # unicode.  So translate the strings into unicode,
            # assuming they are using UTF-8 encoding.
            #
            # If the subprocess output wasn't properly UTF-8
            # encoded, we don't want to raise an exception, so
            # translate the strings using 'replace' mode, which
            # replaces erroneous charcters with the Unicode
            # "replacement character" (a white question mark inside
            # a black diamond).
            out = out.decode('utf-8', 'replace')
            err = err.decode('utf-8', 'replace')

            results = TestResult()

            if self.skip_test:
                results['result'] = 'skip'
            else:
                results['result'] = 'fail'
                out = self.interpretResult(out, returncode, results)

            crash_codes = [
                # Unix: terminated by a signal
                -5,   # SIGTRAP
                -6,   # SIGABRT
                -8,   # SIGFPE  (Floating point exception)
                -10,  # SIGUSR1
                -11,  # SIGSEGV (Segmentation fault)
                # Windows:
                # EXCEPTION_ACCESS_VIOLATION (0xc0000005):
                -1073741819,
                # EXCEPTION_INT_DIVIDE_BY_ZERO (0xc0000094):
                -1073741676
            ]

            if returncode in crash_codes:
                results['result'] = 'crash'
            elif returncode != 0:
                results['note'] = 'Returncode was {0}'.format(returncode)

            if env.valgrind:
                # If the underlying test failed, simply report
                # 'skip' for this valgrind test.
                if results['result'] != 'pass':
                    results['result'] = 'skip'
                elif returncode == 0:
                    # Test passes and is valgrind clean.
                    results['result'] = 'pass'
                else:
                    # Test passed but has valgrind errors.
                    results['result'] = 'fail'

            env = ''
            for key in self.env:
                env = env + key + '="' + self.env[key] + '" '
            if env:
                results['environment'] = env

            results['info'] = unicode("Returncode: {0}\n\nErrors:\n{1}\n\n"
                                      "Output:\n{2}").format(returncode,
                                                             err, out)
            results['returncode'] = returncode
            results['command'] = ' '.join(self.command)

        else:
            results = TestResult()
            if 'result' not in results:
                results['result'] = 'skip'

        return results

    def check_for_skip_scenario(self, command):
        global PIGLIT_PLATFORM
        if PIGLIT_PLATFORM == 'gbm':
            if 'glean' == self.split_command:
                return True
            if self.split_command.startswith('glx-'):
                return True
        return False

    def get_command_result(self, command, fullenv):
        try:
            proc = subprocess.Popen(command,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    env=fullenv,
                                    universal_newlines=True)
            out, err = proc.communicate()
            returncode = proc.returncode
        except OSError as e:
            # Different sets of tests get built under
            # different build configurations.  If
            # a developer chooses to not build a test,
            # Piglit should not report that test as having
            # failed.
            if e.errno == errno.ENOENT:
                out = "PIGLIT: {'result': 'skip'}\n" \
                    + "Test executable not found.\n"
                err = ""
                returncode = None
            else:
                raise e
        return out, err, returncode


class PlainExecTest(ExecTest):
    """
    PlainExecTest: Run a "native" piglit test executable

    Expect one line prefixed PIGLIT: in the output, which contains a result
    dictionary. The plain output is appended to this dictionary
    """
    def __init__(self, command):
        ExecTest.__init__(self, command)
        # Prepend testBinDir to the path.
        self._command[0] = os.path.join(testBinDir, self._command[0])

    def interpretResult(self, out, returncode, results):
        outlines = out.split('\n')
        outpiglit = map(lambda s: s[7:],
                        filter(lambda s: s.startswith('PIGLIT:'), outlines))

        if len(outpiglit) > 0:
            try:
                for piglit in outpiglit:
                    if piglit.startswith('subtest'):
                        if not 'subtest' in results:
                            results['subtest'] = {}
                        results['subtest'].update(eval(piglit[7:]))
                    else:
                        results.update(eval(piglit))
                out = '\n'.join(filter(lambda s: not s.startswith('PIGLIT:'),
                                       outlines))
            except:
                results['result'] = 'fail'
                results['note'] = 'Failed to parse result string'

        if 'result' not in results:
            results['result'] = 'fail'

        return out
