# -*- coding: utf-8 -*-

"""
向gstreamill提交转码任务，当转码任务多于n个的时候等待，否则提交。
可用于批量对电视剧或者电影进行无人值守的转码。
"""

import glob
import httplib
import json
import time

movies = glob.glob("*.ts")
index = 1

for movie in movies:

    gstreamill = httplib.HTTPConnection("localhost:20118")
    gstreamill.request("GET", "/stat/gstreamill")
    response = gstreamill.getresponse()
    status = json.loads(response.read())

    while len(status["job"]) >= 5: # 当前运行的job数达到了4个，等待10s
        print "%d job, waitting ..." % len(status["job"])
        time.sleep(10)
        gstreamill = httplib.HTTPConnection("localhost:20118")
        gstreamill.request("GET", "/stat/gstreamill")
        response = gstreamill.getresponse()
        status = json.loads(response.read())
        continue

    # 当前运行的job数少于4个，提交一个job
    name = movie[0:-3] #去掉 .ts 扩展名
    template = open("template.job").read()
    job = template % (index, index, name, name)
    gstreamill.request("POST", "/start", job)
    response = gstreamill.getresponse()
    print "post job %d, return %s" % (index, response.reason)
    index += 1
