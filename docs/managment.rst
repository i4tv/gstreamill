Management Interfaces
*********************

Gstreamill managment API subject to RESTful, allowing easy integration into operator environment.

State Interface
===============

Gstreamill stat
---------------

Get current stat of gstreamill server.

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/stat/gstreamill

**Response**

On successful, returns a response body with the following structure::

    {
        "version": "0.5.3",
        "builddate": "Nov 20 2014",
        "buildtime": "13:27:11",
        "starttime": "2014-11-20T13:30:13+0800",
        "jobcount": 5
    }

The following table defines the properties

    ============= ====== =================================
    Property name Type   Description
    ============= ====== =================================
    version       string The version of the gstreamill
    buildtime     string The build time of the gstreamill
    ============= ====== ================================

Administrator Interface
======================

Media Managment Interface
=========================
