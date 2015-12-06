
import sys
import fileinput
import requests

if __name__ == "__main__":
    server = sys.argv[1]
    jobs = sys.argv[2]
    for job in fileinput.input(jobs):
        name, source, temp = job.split()
        r = requests.get(server + name)
        print r.text
