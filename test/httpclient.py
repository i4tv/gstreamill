import socket
import httplib
import time
import string

ReadSize = 190000

def patch_http_response_read(func):
    def inner(*args):
        try:
            return func(*args)
        except httplib.IncompleteRead, e:
            return e.partial

    return inner

httplib.HTTPResponse.read = patch_http_response_read(httplib.HTTPResponse.read)

def fetch_data(server, port, location, size):
    readsize = 0
    attempts = 5
    while True:
        try:
            t1 = time.time()
            conn = httplib.HTTPConnection(server, port)
            #conn.set_debuglevel(1)
            conn.request("GET", location)
            resp = conn.getresponse()
            if resp.status == 200:
                data = resp.read (ReadSize)
                t2 = time.time()
                conn.close()
                if not (len(data) == ReadSize):
                    print time.strftime("%b %d - %H:%M:%S", time.gmtime()), "http://%s%s" % (server, location), resp.status, resp.reason, "size", len(data), "time", t2-t1
                    exit (0)
            break
        except socket.error, msg:
            print "socket error %s" % msg
            break
        except httplib.HTTPException, msg:
            print time.strftime("%b %d - %H:%M:%S", time.gmtime()), "read data http://%s:%d%s, http error %s" % (server, port, location, msg)
            exit(0)
            attempts -= 1
            if attempts == 0:
                raise
            else:
                print "try %d times" % (5-attempts)
                time.sleep(1)
                continue

while True:
    """
    fetch_data("192.168.2.11", 20129, "/channel/0/encoder/0", ReadSize)
    fetch_data("192.168.2.11", 20129, "/channel/0/encoder/1", ReadSize)
    fetch_data("192.168.2.11", 20129, "/channel/1/encoder/0", ReadSize)
    fetch_data("192.168.2.11", 20129, "/channel/1/encoder/1", ReadSize)
    fetch_data("192.168.2.11", 20129, "/channel/2/encoder/0", ReadSize)
    fetch_data("192.168.2.11", 20129, "/channel/2/encoder/1", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/0/encoder/0", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/0/encoder/1", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/0/encoder/2", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/0/encoder/3", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/1/encoder/0", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/1/encoder/1", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/1/encoder/2", ReadSize)
    fetch_data("192.168.2.10", 20129, "/channel/1/encoder/3", ReadSize)
    """
    fetch_data("192.168.2.9", 20129, "/channel/0/encoder/0", ReadSize)
