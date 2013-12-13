import socket
import cStringIO

def request_segment(host, port, location): 
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    s.sendall('GET %s HTTP/1.1\r\n\r\n' % location)
    response = cStringIO.StringIO()
    while True:
        response.write(s.recv(500000))
        if response.tell() > 400000:
            break;
    print "data len %d" % response.tell()
    response.seek(0)
    head = cStringIO.StringIO()
    while True:
        l = response.readline()
        head.write(l)
        if l == "\r\n":
            break
    head.seek(0)
    for l in head:
        print l

    len = 0
    while True:
        chunklenX = response.readline()
        if chunklenX == "":
            print "error"
            print repr(response.read(100))
            exit(0)
        chunklen = int(chunklenX, 16)
        len += chunklen
        chunk = response.read(chunklen+2)
        print "length: %d, chunk length hex: %s, decimal: %d, %s...%s" % (len, chunklenX[:-2], chunklen, repr(chunk[:10]), repr(chunk[-10:]))
        if len > 200000:
            break
    s.close()

while True:
    """
    request_segment("192.168.2.10", 20129, "/channel/0/encoder/0")
    request_segment("192.168.2.10", 20129, "/channel/0/encoder/1")
    request_segment("192.168.2.10", 20129, "/channel/0/encoder/2")
    request_segment("192.168.2.10", 20129, "/channel/0/encoder/3")
    request_segment("192.168.2.10", 20129, "/channel/1/encoder/0")
    request_segment("192.168.2.10", 20129, "/channel/1/encoder/1")
    request_segment("192.168.2.10", 20129, "/channel/1/encoder/2")
    request_segment("192.168.2.10", 20129, "/channel/1/encoder/3")
    """
    request_segment("192.168.2.9", 20129, "/channel/0/encoder/0")
