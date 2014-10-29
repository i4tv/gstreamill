#!/bin/sh

if [ -d /etc/gstreamill.d ]; then
    for job in $(ls /etc/gstreamill.d/jobs.d/*.job); do
        curl -H "Content-Type: application/json" --data @$job http://localhost:20118/admin/start 2>&1 > /dev/null
    done
fi
