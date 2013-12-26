#!/usr/bin/python
import subprocess
import time
import tarfile
import os
import sys
import shutil


def run_test(path, test):
    os.mkdir(path)
    p = subprocess.Popen([test, '--path', path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=path)
    result = p.communicate()
    print(result[0])
    print(result[1])

    return p.returncode


def main():
    source_dir = sys.argv[1]
    binary_dir = sys.argv[2]

    tests = [
        (binary_dir, 'dnet_cpp_test'),
        (binary_dir, 'dnet_cpp_cache_test'),
        (binary_dir, 'dnet_cpp_srw_test')
    ]
    print('Running {0} tests'.format(len(tests)))

    tests_base_dir = binary_dir + '/result'

    if os.path.exists(tests_base_dir):
        print('Removing path: {0}'.format(tests_base_dir))
        shutil.rmtree(tests_base_dir)

    os.mkdir(tests_base_dir)

    all_ok = True
    for i in xrange(0, len(tests)):
        test = tests[i]
        print('# Start {1} of {2}: {0}: '.format(test[1], i + 1, len(tests)))

        timer_begin = time.time()
        result = run_test(tests_base_dir + '/' + test[1], test[0] + '/' + test[1])
        timer_end = time.time()

        print('# Result: {0}\t{1} sec\n'.format('Passed' if result == 0 else 'Failed ({0})'.format(result), timer_end - timer_begin))

        all_ok &= result == 0

        with tarfile.TarFile.open(tests_base_dir + '/' + test[1] + '.tar.bz2', 'w:bz2') as file:
            file.add(tests_base_dir + '/' + test[1], test[1])

    print('Tests are finised')

    exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()

