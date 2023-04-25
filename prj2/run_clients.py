import telnetlib
from joblib import Parallel, delayed
import random

pub_urls = [
    "http://www.testingmcafeesites.com/",
    "http://help.websiteos.com/websiteos/example_of_a_simple_html_page.htm",
    "http://neverssl.com",
    "http://otl.kaist.ac.kr/timetable/",
]
msg = "GET {} HTTP/1.0\r\nHost: {}\r\n\r\n"


def worker():
    # randomly select url from pub_urls
    url = random.choice(pub_urls)
    # extract host from url
    host = url.split("/")[2]
    data = msg.format(url, host)
    ret_data = http_exchange("localhost", 8000, data)
    ret_data_direct = http_exchange(host, 80, data)
    return ret_data.decode("utf-8"), ret_data_direct.decode("utf-8")


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
    with open("proxy_result.txt", "w") as f, open("direct_result.txt", "w") as g:
        for i in range(num_clients):
            for proxy_line, direct_line in zip(res[i][0].splitline(), res[i][1].splitline()):
                if "Date" not in proxy_line and "Date" not in direct_line:
                    if proxy_line != direct_line:
                        print(res[i][0], file=f)
                        print(file=f)
                        print(res[i][1], file=g)
                        print(file=g)
                        break


if __name__ == "__main__":
    main()
