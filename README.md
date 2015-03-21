Zabbix Docker Monitoring - beta version
========================================

This is beta version. Project is developed with Zabbix 2.4 and Docker 1.4. It should works also with another Zabbix/Docker versions.
Please feel free to test and provide feedback. Systemd and LXC execution driver is also supported.

Build
=====

[Download latest build](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/files/zabbix24/src/modules/zabbix_module_docker/zabbix_module_docker.so)
[![Build Status](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/status.png)](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/latest) Please see section Compilation, if provided build doesn't work on your system.

Available keys
==============

Note: fci - full container ID

| Key | Description | Comments |
| --- | ----------- | -------- |
| **docker.discovery** | LLD discovering | Only running containers are discovered.<br>Additional Docker permissions are needed, when you want to see container name (human name) in metrics/graphs instead of short container ID. |  
| **docker.mem[fci,mmetric]** | Memory metrics | **mmetric** - any available memory metric in the pseudo-file memory.stat, e.g.: *cache, rss, mapped_file, pgpgin, pgpgout, swap, pgfault, pgmajfault, inactive_anon, active_anon, inactive_file, active_file, unevictable, hierarchical_memory_limit, hierarchical_memsw_limit, total_cache, total_rss, total_mapped_file, total_pgpgin, total_pgpgout, total_swap, total_pgfault, total_pgmajfault, total_inactive_anon, total_active_anon, total_inactive_file, total_active_file, total_unevictable* |
| **docker.cpu[fci,cmetric]** | CPU metrics | **cmetric** - any available CPU metric in the pseudo-file cpuacct.stat, e.g.: *system, user*<br>Jiffy CPU counter is recalculated to % value by Zabbix. | 
| **docker.dev[fci,cfile,cmetric]** | IO metrics | **cfile** - container blkio pseudo-file, e.g.:*blkio.io_merged, blkio.io_queued, blkio.io_service_bytes, blkio.io_serviced, blkio.io_service_time, blkio.io_wait_time, blkio.sectors, blkio.time, blkio.avg_queue_size, blkio.idle_time, blkio.dequeue, ...*<br>**cmetric** - any available blkio metric in selected pseudo-file, e.g.: *Total*. Option for selected block device only is also available e.g. *'8:0 Sync'* (quotes must be used in key parameter in this case)<br>\*Note: Some pseudo blkio files are available only if kernel config *CONFIG_DEBUG_BLK_CGROUP=y*, see recommended docs. |
| **docker.inspect[fci,param1,\<param2\>]** | Docker inspection | Requested value from Docker inspect structure is returned.<br>**param1** - name of first level JSON property<br>**param2** - optional name of second level JSON property<br>For example: *docker.inspect[fid,NetworkSettings,IPAddres], docker.inspect[fid,State,StartedAt], docker.inspect[fid,Name]*<br>Note 1: requested value must be plain text/numeric value. JSON objects/arrays are not supported.<br>Note 2: Additional Docker permissions are needed. |
| **docker.up[fci]** | Running state check | 1 if container is running, otherwise 0 |
 
Not available at the moment, probably in the (near) future:

* docker.net - tricky metrics
* docker.stat - stat about number of available images, running/crashed/stopped containers
* Docker API metrics/details queries (when zabbix-agent has root or docker permissions)

Recommended docs:

- https://docs.docker.com/articles/runmetrics/
- https://www.kernel.org/doc/Documentation/cgroups/blkio-controller.txt
- https://www.kernel.org/doc/Documentation/cgroups/memory.txt
- https://www.kernel.org/doc/Documentation/cgroups/cpuacct.txt

Images
======

Docker container CPU graph in Zabbix:
![Docker container CPU graph in Zabbix](https://raw.githubusercontent.com/jangaraj/Zabbix-Docker-Monitoring/master/doc/zabbix-docker-container-cpu-graph.png)
Docker container memory graph in Zabbix:
![Docker container memory graph in Zabbix](https://raw.githubusercontent.com/jangaraj/Zabbix-Docker-Monitoring/master/doc/zabbix-docker-container-memory-graph.png)
Docker container state graph in Zabbix:
![Docker container state graph in Zabbix](https://raw.githubusercontent.com/jangaraj/Zabbix-Docker-Monitoring/master/doc/zabbix-docker-container-state-graph.png)

Additional Docker permissions
=============================

You have two options, how to get additional Docker permissions:

- Edit your zabbix_agentd.conf and set AllowRoot:

```
AllowRoot=1
```    

- Or add zabbix user to docker group:

```
usermod -aG docker zabbix
```

Installation
============

* Import provided template Zabbix-Template-App-Docker.xml.
* Configure your Zabbix agent(s) - load zabbix-module-docker.so
https://www.zabbix.com/documentation/2.4/manual/config/items/loadablemodules


Compilation
===========

You have to compile module, if provided binary doesn't work on your system.
Basic steps:

    cd ~
    mkdir zabbix24
    cd zabbix24
    svn co svn://svn.zabbix.com/branches/2.4 .
    ./bootstrap.sh
    ./configure --enable-agent
    mkdir src/modules/zabbix_module_docker
    cd src/modules/zabbix_module_docker
    wget https://raw.githubusercontent.com/jangaraj/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/zabbix_module_docker.c
    wget https://raw.githubusercontent.com/jangaraj/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/Makefile
    make

Output will be binary file zabbix_module_docker.so, which can be loaded by zabbix agent.

How it works
============

See https://blog.docker.com/2013/10/gathering-lxc-docker-containers-metrics/
Metrics for containers are read from cgroup file system. For discovering is used 
also Docker API https://docs.docker.com/reference/api/docker_remote_api. However
for communication with Docker via unix socket is required root permission. You 
can test API also in your command line:

    echo -e "GET /containers/json?all=0 HTTP/1.0\r\n" | nc -U /var/run/docker.sock
    
Module vs. UserParameter script
===============================

Module is ~10x quicker, because it's compiled binary code.
I've used my project https://github.com/jangaraj/zabbix-agent-stress-test for performance tests.

Part of config in zabbix_agentd.conf:

    UserParameter=xdocker.cpu[*],grep $2 /cgroup/cpuacct/docker/$1/cpuacct.stat | awk '{print $$2}'
    LoadModule=zabbix_module_docker.so
    
Tests:

    [root@dev zabbix-agent-stress-test]# ./zabbix-agent-stress-test.py -s 127.0.0.1 -k "xdocker.cpu[d5bf68ec1fb570d8ac3047226397edd8618eed14278ce035c98fbceef02d7730,system]" -t 20
    Warning: you are starting more threads, than your system has available CPU cores (4)!
    Starting 20 threads, host: 127.0.0.1:10050, key: xdocker.cpu[d5bf68ec1fb570d8ac3047226397edd8618eed14278ce035c98fbceef02d7730,system]
    Success: 291    Errors: 0       Avg speed: 279.68 qps   Execution time: 1.00 sec
    Success: 548    Errors: 0       Avg speed: 349.04 qps   Execution time: 2.00 sec
    Success: 803    Errors: 0       Avg speed: 282.72 qps   Execution time: 3.00 sec
    Success: 1060   Errors: 0       Avg speed: 209.31 qps   Execution time: 4.00 sec
    Success: 1310   Errors: 0       Avg speed: 187.14 qps   Execution time: 5.00 sec
    Success: 1570   Errors: 0       Avg speed: 178.80 qps   Execution time: 6.01 sec
    Success: 1838   Errors: 0       Avg speed: 189.36 qps   Execution time: 7.01 sec
    Success: 2106   Errors: 0       Avg speed: 225.68 qps   Execution time: 8.01 sec
    Success: 2382   Errors: 0       Avg speed: 344.51 qps   Execution time: 9.01 sec
    Success: 2638   Errors: 0       Avg speed: 327.88 qps   Execution time: 10.01 sec
    Success: 2905   Errors: 0       Avg speed: 349.93 qps   Execution time: 11.01 sec
    Success: 3181   Errors: 0       Avg speed: 352.23 qps   Execution time: 12.01 sec
    Success: 3450   Errors: 0       Avg speed: 239.38 qps   Execution time: 13.01 sec
    Success: 3678   Errors: 0       Avg speed: 209.88 qps   Execution time: 14.02 sec
    Success: 3923   Errors: 0       Avg speed: 180.30 qps   Execution time: 15.02 sec
    Success: 4178   Errors: 0       Avg speed: 201.58 qps   Execution time: 16.02 sec
    Success: 4434   Errors: 0       Avg speed: 191.92 qps   Execution time: 17.02 sec
    Success: 4696   Errors: 0       Avg speed: 332.06 qps   Execution time: 18.02 sec
    Success: 4968   Errors: 0       Avg speed: 325.55 qps   Execution time: 19.02 sec
    Success: 5237   Errors: 0       Avg speed: 325.61 qps   Execution time: 20.02 sec
    ^C
    Success: 5358   Errors: 0       Avg rate: 192.56 qps    Execution time: 20.53 sec
    Avg rate based on total execution time and success connections: 261.02 qps
    
    [root@dev zabbix-agent-stress-test]# ./zabbix-agent-stress-test.py -s 127.0.0.1 -k "docker.cpu[d5bf68ec1fb570d8ac3047226397edd8618eed14278ce035c98fbceef02d7730,system]" -t 20
    Warning: you are starting more threads, than your system has available CPU cores (4)!
    Starting 20 threads, host: 127.0.0.1:10050, key: docker.cpu[d5bf68ec1fb570d8ac3047226397edd8618eed14278ce035c98fbceef02d7730,system]
    Success: 2828   Errors: 0       Avg speed: 2943.98 qps  Execution time: 1.00 sec
    Success: 5095   Errors: 0       Avg speed: 1975.77 qps  Execution time: 2.01 sec
    Success: 7623   Errors: 0       Avg speed: 2574.55 qps  Execution time: 3.01 sec
    Success: 10098  Errors: 0       Avg speed: 4720.20 qps  Execution time: 4.02 sec
    Success: 12566  Errors: 0       Avg speed: 3423.56 qps  Execution time: 5.02 sec
    Success: 14706  Errors: 0       Avg speed: 2397.01 qps  Execution time: 6.03 sec
    Success: 17128  Errors: 0       Avg speed: 903.63 qps   Execution time: 7.05 sec
    Success: 19520  Errors: 0       Avg speed: 2663.53 qps  Execution time: 8.05 sec
    Success: 21899  Errors: 0       Avg speed: 1516.36 qps  Execution time: 9.07 sec
    Success: 24219  Errors: 0       Avg speed: 3570.47 qps  Execution time: 10.07 sec
    Success: 26676  Errors: 0       Avg speed: 1204.58 qps  Execution time: 11.08 sec
    Success: 29162  Errors: 0       Avg speed: 2719.87 qps  Execution time: 12.08 sec
    Success: 31671  Errors: 0       Avg speed: 2265.67 qps  Execution time: 13.08 sec
    Success: 34186  Errors: 0       Avg speed: 3490.64 qps  Execution time: 14.08 sec
    Success: 36749  Errors: 0       Avg speed: 2094.59 qps  Execution time: 15.09 sec
    Success: 39047  Errors: 0       Avg speed: 3213.35 qps  Execution time: 16.09 sec
    Success: 41361  Errors: 0       Avg speed: 3171.67 qps  Execution time: 17.09 sec
    Success: 43739  Errors: 0       Avg speed: 3946.53 qps  Execution time: 18.09 sec
    Success: 46100  Errors: 0       Avg speed: 1308.88 qps  Execution time: 19.09 sec
    Success: 48556  Errors: 0       Avg speed: 2663.52 qps  Execution time: 20.09 sec
    ^C
    Success: 49684  Errors: 0       Avg rate: 2673.85 qps   Execution time: 20.52 sec
    Avg rate based on total execution time and success connections: 2420.70 qps
    
Results of 20s stress test:

| StartAgent value | Module qps | UserParameter script qps |
| ---------------- | ---------- | ------------------------ |
| 3 | 2420.70 | 261.02 |     
| 10 | 2612.20 | 332.62 |
| 20 | 2487.93 | 348.52 |
       
Troubleshooting
===============

Edit your zabbix_agentd.conf and set DebugLevel:

    DebugLevel=4
    
Debug messages from this module will be available in standard zabbix_agentd.log.

Issues and feature requests
===========================

Please use Github functionality.       

Author
======

[Devops Monitoring zExpert](http://www.jangaraj.com), who loves monitoring systems, which start with letter Z. Those are Zabbix and Zenoss. [LinkedIn] (http://uk.linkedin.com/in/jangaraj/).
