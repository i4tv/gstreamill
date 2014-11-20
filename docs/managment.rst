Management Interfaces
*********************

Gstreamill managment API subject to RESTful, allowing easy integration into operator environment.

State Interface
===============

gstreamill stat
---------------

HTTP Request:

    GET /stat/gstreamill HTTP/1.1
    Host: 192.168.7.60:20118
    Connection: keep-alive
    Accept: */*
    X-Requested-With: XMLHttpRequest
    User-Agent: Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/35.0.1916.114 Safari/537.36
    Referer: http://192.168.7.60:20118/admin/gstreamill.html
    Accept-Encoding: gzip,deflate,sdch
    Accept-Language: zh-CN,zh;q=0.8
    Cookie: i18next=en

HTTP Response:

    HTTP/1.1 200 Ok
    Server: gstreamill-0.5.3
    Content-Type: application/json
    Content-Length: 152
    Access-Control-Allow-Origin: *
    Cache-Control: no-cache
    Connection: Close
    
    {
        "version": "0.5.3",
        "builddate": "Nov 20 2014",
        "buildtime": "13:27:11",
        "starttime": "2014-11-20T13:30:13+0800",
        "jobcount": 5
    }

Administrator Interface
======================

Media Managment Interface
=========================
