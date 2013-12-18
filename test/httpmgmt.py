import time
import httplib

def request(server, port, channelid, command):
    conn = httplib.HTTPConnection(server, port)
    channel = "/channel/%d/%s" % (channelid, command)
    conn.request("GET", channel)
    resp = conn.getresponse()
    if resp.status == 200:
        data = resp.read (16)
    conn.close()

def save (server, port):
    conn = httplib.HTTPConnection(server, port)
    headers = {
        "Content-Type": "application/xml",
        "Content-Length": 4586
    }
    conn.request ("GET", "/configure")
    response = conn.getresponse()
    if response.status == 200:
        params = response.read()
    conn.request("POST", "/configure", params, headers)
    response = conn.getresponse()
    print response.status, response.reason

while True:
    save ("192.168.2.10", 20220)
    time.sleep(1)
