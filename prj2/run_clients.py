import telnetlib
from joblib import Parallel, delayed

url = "http://www.testingmcafeesites.com/"
msg = "GET {} HTTP/1.0\r\nHost: {}\r\n\r\n"


def worker():
    host = "www.testingmcafeesites.com"
    port = 8000
    data = msg.format(url, host)
    ret_data = http_exchange("localhost", port, data)
    return ret_data.decode("utf-8")


def http_exchange(host, port, data):
    conn = telnetlib.Telnet()
    conn.open(host, port)
    conn.write(data.encode("ascii"))
    ret_data = conn.read_all()
    conn.close()
    return ret_data


def main():
    num_clients = 50
    res = Parallel(n_jobs=num_clients)(delayed(worker)() for _ in range(num_clients))
    for i in range(num_clients):
        if "200 OK" not in res[i]:
            print(res[i])


if __name__ == "__main__":
    main()
