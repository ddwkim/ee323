# Source Generated with Decompyle++
# File: proxy_tester.pyc (Python 3.8)

import os
import random
import sys
import signal
import socket
import telnetlib
import time
import threading
from urllib import parse as urlparse

use_private = False
proxy_grade_private = None
pub_urls = [
    "http://www.testingmcafeesites.com/",
    "http://help.websiteos.com/websiteos/example_of_a_simple_html_page.htm",
    "http://neverssl.com",
    "http://otl.kaist.ac.kr/timetable/",
]
timeout_secs = 30


def main():
    try:
        proxy_bin = sys.argv[1]
    except IndexError:
        usage()
        sys.exit(2)

    try:
        port = sys.argv[2]
    except IndexError:
        port = str(random.randint(1025, 49151))

    print("Binary: {}".format(proxy_bin))
    print("Running on port {}".format(port))
    cid = os.spawnl(os.P_NOWAIT, proxy_bin, proxy_bin, port)
    time.sleep(2)
    basic_score = 0
    print("#############################\n")
    print("Basic Functionality Test\n")
    print("#############################\n")
    for url in pub_urls:
        print("### Testing: " + url)
        passed = run_test(compare_url, (url, port), cid)
        if not live_process(cid):
            print("!!!Proxy process experienced abnormal termination during test- restarting proxy!")
            (cid, port) = restart_proxy(proxy_bin, port)
            passed = False
        if passed:
            print("{}: [PASSED] (5/5 points)\n".format(url))
            basic_score += 5
            continue
        print("{}: [FAILED] (0/5 points)\n".format(url))
    if use_private:
        (priv_passed, test_count, cid) = proxy_grade_private.runall(port, cid, proxy_bin)
    print("#############################\n")
    print("Extended Functionality Test\n")
    print("#############################\n")
    test_url = "http://www.example.com"
    extended_score = run_test(error_handling, (test_url, port), cid)
    if not live_process(cid):
        print("!!!Proxy process experienced abnormal termination during test- restarting proxy!")
        (cid, port) = restart_proxy(proxy_bin, port)
        extended_score = 0
    terminate(cid)
    print("Total Score:\t{} / {}".format(basic_score + extended_score, 60))
    if use_private:
        print("{} of {} extended tests passed".format(priv_passed, test_count))


def usage():
    print("Usage: proxy_tested.py path/to/proxy/binary port")
    print("Omit the port argument for a randomly generated port.")


def run_test(test, args, childid):
    """
    Run a single test function, monitoring its execution with a timer thread.

    * test: A function to execute.  Should take a tuple as its sole
    argument and return True for a passed test, and False otherwise.
    * args: Tuple that contains arguments to the test function
    * childid: Process ID of the running proxy

    The amount of time that the monitor waits before killing
    the proxy process can be set by changing timeout_secs at the top of this
    file.

    Returns True for a passed test, False otherwise.
    """
    monitor = threading.Timer(timeout_secs, do_timeout, [childid])
    monitor.start()
    passed = test(args)
    monitor.cancel()
    return passed


def compare_url(argtuple):
    """
    Compare proxy output to the output from a direct server transaction.

    A simple sample test: download a web page via the proxy, and then fetch the
    same page directly from the server.  Compare the two pages for any
    differences, ignoring the Date header field if it is set.

    Argument tuples is in the form (url, port), where url is the URL to open, and
    port is the port the proxy is running on.
    """
    (url, port) = argtuple
    urldata = urlparse.urlparse(url)

    try:
        (host, hostport) = urldata[1].split(":")
    except ValueError:
        host = urldata[1]
        hostport = 80

    try:
        proxy_data = get_data("localhost", port, url, host)
    except socket.error:
        print("!!!! Socket error while attempting to talk to proxy!")
        return False
    else:
        direct_data = get_data(host, hostport, url, host)
        passed = True
        for proxy, direct in zip(proxy_data, direct_data):
            if proxy != direct or proxy.startswith(b"Date"):
                if not direct.startswith(b"Date"):
                    print("Proxy: {}".format(proxy))
                    print("Direct: {}".format(direct))
                    passed = False
                    continue

    return passed


def error_handling(argtuple):
    """
    Check if the proxy outputs correct error messages in some corner cases.
    Argtuple is in the form of (url, port), where url is the URL to open, and
    port is the port the proxy is running on.
    """
    passed_count = 0
    answer = b"HTTP/1.0 400 Bad Request\r\n"
    (url, port) = argtuple
    urldata = urlparse.urlparse(url)

    try:
        (host, hostport) = urldata[1].split(":")
    except ValueError:
        host = urldata[1]
        hostport = 80

    print("### Testing error case #1:")
    error_1 = "GET {} HTTP/1.0\r\n\r\n"

    try:
        proxy_data = http_exchange("localhost", port, error_1.format(url))
        if proxy_data == answer:
            passed_count += 6
            print("Request without host header field: [PASSED] (6/6 points)\n")
        else:
            print("Request without host header field: [FAILED] (0/6 points)\n")
    except socket.error:
        print("!!!! Socket error while attempting to talk to proxy!")

    print("### Testing error case #2:")
    error_2_1 = "POST {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    error_2_2 = "PUT {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    error_2_3 = "PATCH {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    error_2_4 = "DELETE {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    error_2_5 = "MYMETHOD {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    method_error_list = [error_2_1, error_2_2, error_2_3, error_2_4, error_2_5]
    for midx, method_error in enumerate(method_error_list):
        try:
            proxy_data = http_exchange("localhost", port, method_error.format(url, host))
            if proxy_data == answer:
                passed_count += 2
                print("Request with method other than GET (case {}): [PASSED] (2/2 points)".format(midx + 1))
            else:
                print("Request with method other than GET (case {}): [FAILED] (0/2 points)".format(midx + 1))

        except socket.error:
            print("!!!! Socket error while attempting to talk to proxy!")
            continue

    print("")
    print("### Testing error case #3:")
    error_3 = "GET {} HTTP/1.1\r\nHost: {}\r\n\r\n"

    try:
        proxy_data = http_exchange("localhost", port, error_3.format(url, host))
        if proxy_data == answer:
            passed_count += 6
            print("Request with different HTTP version (other than v1.0): [PASSED] (6/6 points)\n")
        else:
            print("Request with different HTTP version (other than v1.0): [FAILED] (0/6 points)\n")
    except socket.error:
        print("!!!! Socket error while attempting to talk to proxy!")

    print("### Testing error case #4:")
    error_4 = "GET {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    url_4 = "http://www.sjisthanosifyoudidntknow.com"
    host_4 = "www.sjisthanosifyoudidntknow.com"

    try:
        proxy_data = http_exchange("localhost", port, error_4.format(url_4, host_4))
        if proxy_data == answer:
            passed_count += 6
            print("Invalid host header: [PASSED] (6/6 points)\n")
        else:
            print("Invalid host header: [FAILED] (0/6 points)\n")
    except socket.error:
        print("!!!! Socket error while attempting to talk to proxy!")

    print("### Testing error case #5:")
    error_5 = "GET {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    url_5 = "http://www.example.com"
    host_5 = "www.google.com"

    try:
        proxy_data = http_exchange("localhost", port, error_5.format(url_5, host_5))
        if proxy_data == answer:
            passed_count += 6
            print("Different URI and host: [PASSED] (6/6 points)\n")
        else:
            print("Different URI and host: [FAILED] (0/6 points)\n")
    except socket.error:
        print("!!!! Socket error while attempting to talk to proxy!")

    print("### Testing error case #5:")
    error_6 = "GET {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    url_6 = "http://www.google.com/giveme404"
    host_6 = "www.google.com"
    answer = b"HTTP/1.0 404 Not Found\r\n"

    try:
        proxy_data = http_exchange("localhost", port, error_6.format(url_6, host_6))
        if proxy_data[: len(answer)] == answer:
            passed_count += 6
            print("No resource on server: [PASSED] (6/6 points)\n")
        else:
            print("No resource on server: [FAILED] (0/6 points)\n")
    except socket.error:
        print("!!!! Socket error while attempting to talk to proxy!")

    return passed_count


def get_data(host, port, url, origin):
    """Retrieve a URL using HTTP/1.0 GET."""
    getstring = "GET {} HTTP/1.0\r\nHost: {}\r\n\r\n"
    data = http_exchange(host, port, getstring.format(url, origin))
    return data.split(b"\n")


def http_exchange(host, port, data):
    conn = telnetlib.Telnet()
    conn.open(host, port)
    conn.write(data.encode("ascii"))
    ret_data = conn.read_all()
    conn.close()
    return ret_data


def live_process(pid):
    """Check that a process is still running."""

    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def do_timeout(id):
    """Callback function run by the monitor threads to kill a long-running operation."""
    print("!!!! Proxy transaction timed out after {} seconds".format(timeout_secs))
    terminate(id)


def terminate(id):
    """Stops and cleans up a running child process."""
    if not live_process(id):
        raise AssertionError
    None.kill(id, signal.SIGINT)
    os.kill(id, signal.SIGKILL)

    try:
        os.waitpid(id, 0)
    except OSError:
        pass


def restart_proxy(binary, oldport):
    """Restart the proxy on a new port number."""
    newport = str(int(oldport) + 1)
    cid = os.spawnl(os.P_NOWAIT, binary, binary, newport)
    time.sleep(3)
    return (cid, newport)


if __name__ == "__main__":
    main()
