
# python postlivejobs.py http://localhost:20118/admin/start segmentjobs.txt

import sys
import fileinput
import requests

if __name__ == "__main__":
    server = sys.argv[1]
    jobs = sys.argv[2]
    for job in fileinput.input(jobs):
        name, source, temp = job.split()
        tempfile = temp + ".temp"
        print name, source, tempfile
        template = open(tempfile).read()
        jsonjob = template % (name, source)
        r = requests.post(server, jsonjob)
        print r.text
