import time
import httplib

def stop(server, port, job):
    conn = httplib.HTTPConnection(server, port)
    stop = "/stop/%s" % job
    conn.request("GET", stop)
    resp = conn.getresponse()
    if resp.status == 200:
        data = resp.read ()
        print data
    conn.close()

def start(server, port, jobfile):
    conn = httplib.HTTPConnection(server, port)
    headers = {"Content-Type": "application/json"}
    job = open(jobfile).read()
    conn.request("POST", "/start", job, headers)
    resp = conn.getresponse()
    if resp.status == 200:
        data = resp.read ()
        print "start job response:", data
    conn.close()

while True:
    start ("localhost", 20118, "examples/test.job")
    time.sleep(10)
    stop ("localhost", 20118, "test")
    time.sleep(10)
