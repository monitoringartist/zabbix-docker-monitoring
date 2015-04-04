Zabbix Docker Monitoring - beta version
========================================

If you like or use this project, please provide feedback to author - Star it ★. 

Monitoring of Docker container by using Zabbix. 
Available CPU, mem, blkio container metrics and some containers config details e.g. IP, name, ...
Zabbix Docker module has native support for Docker containers (Systemd included) and should support 
also a few other container (e.g. LXC) type out of the box. 
Please feel free to test and provide feedback/open issue. 
Module is mainly focused on performance, see section [Module vs. UserParameter script](#module-vs-userparameter-script).

Build
=====

[Download latest build (RHEL 7, CentOS 7, Debian 7, Ubuntu 12, ...)](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/files/zabbix24/src/modules/zabbix_module_docker/zabbix_module_docker.so)
[![Build Status](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/status.png)](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/latest)<br>
If provided build doesn't work on your system, please see section [Compilation](#compilation). Or you can check [folder dockerfiles] (https://github.com/jangaraj/Zabbix-Docker-Monitoring/tree/master/dockerfiles), where Dockerfiles for various OS/Zabbix versions are prepared.

Available keys
==============

Note: fci - full container ID

| Key | Description |
| --- | ----------- |
| **docker.discovery** | **LLD discovering:**<br>Only running containers are discovered.<br>[Additional Docker permissions](#additional-docker-permissions) are needed, when you want to see container name (human name) in metrics/graphs instead of short container ID. |  
| **docker.mem[fci,mmetric]** | **Memory metrics:**<br>**mmetric** - any available memory metric in the pseudo-file memory.stat, e.g.: *cache, rss, mapped_file, pgpgin, pgpgout, swap, pgfault, pgmajfault, inactive_anon, active_anon, inactive_file, active_file, unevictable, hierarchical_memory_limit, hierarchical_memsw_limit, total_cache, total_rss, total_mapped_file, total_pgpgin, total_pgpgout, total_swap, total_pgfault, total_pgmajfault, total_inactive_anon, total_active_anon, total_inactive_file, total_active_file, total_unevictable* |
| **docker.cpu[fci,cmetric]** | **CPU metrics:**<br>**cmetric** - any available CPU metric in the pseudo-file cpuacct.stat, e.g.: *system, user*<br>Jiffy CPU counter is recalculated to % value by Zabbix. | 
| **docker.dev[fci,bfile,bmetric]** | **IO metrics:**<br>**bfile** - container blkio pseudo-file, e.g.: *blkio.io_merged, blkio.io_queued, blkio.io_service_bytes, blkio.io_serviced, blkio.io_service_time, blkio.io_wait_time, blkio.sectors, blkio.time, blkio.avg_queue_size, blkio.idle_time, blkio.dequeue, ...*<br>**bmetric** - any available blkio metric in selected pseudo-file, e.g.: *Total*. Option for selected block device only is also available e.g. *'8:0 Sync'* (quotes must be used in key parameter in this case)<br>\*Note: Some pseudo blkio files are available only if kernel config *CONFIG_DEBUG_BLK_CGROUP=y*, see recommended docs. |
| **docker.inspect[fci,par1,\<par2\>]** | **Docker inspection:**<br>Requested value from Docker inspect structure is returned.<br>**par1** - name of 1st level JSON property<br>**par2** - optional name of 2nd level JSON property<br>For example:<br>*docker.inspect[fci,NetworkSettings,IPAddress], docker.inspect[fci,State,StartedAt], docker.inspect[fci,Name]*<br>Note 1: Requested value must be plain text/numeric value. JSON objects/arrays are not supported.<br>Note 2: [Additional Docker permissions](#additional-docker-permissions) are needed. |
| **docker.info[info]** | **Docker information:**<br>Requested value from Docker information structure is returned.<br>**info** - name of requested information, e.g. *Containers, Images, NCPU, ...*<br>Note: [Additional Docker permissions](#additional-docker-permissions) are needed. |
| **docker.stats[fci,par1,\<par2\>,\<par3\>]** | **Docker container resource usage statistics:**<br>Docker version 1.5+ is required<br>Requested value from Docker statistic structure is returned.<br>**par1** - name of 1st level JSON property<br>**par2** - optional name of 2nd level JSON property<br>**par3** - optional name of 3rd level JSON property<br>For example:<br>*docker.stats[fci,memory_stats,usage], docker.stats[fci,network,rx_bytes], docker.stats[fci,cpu_stats,cpu_usage,total_usage]*<br>Note 1: Requested value must be plain text/numeric value. JSON objects/arrays are not supported.<br>Note 2: [Additional Docker permissions](#additional-docker-permissions) are needed.<br>Note 3: The most accurate way to get Docker container stats, but it's also the slowest (0.3-0.7s), because data are readed from on demand container stats stream. |
| **docker.up[fci]** | **Running state check:**<br>1 if container is running, otherwise 0 |
 
Not available at the moment, probably in the (near) future:

* docker.net - tricky metrics
* docker.info - running/crashed/stopped containers
* docker.inspect - support bool values
* docker.stats - support 3rd parameter

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

- Add zabbix user to docker group (recommended option):

```
usermod -aG docker zabbix
```

- Or edit zabbix_agentd.conf and set AllowRoot:

```
AllowRoot=1
```  

Installation
============

* Import provided template Zabbix-Template-App-Docker.xml.
* Configure your Zabbix agent(s) - load zabbix-module-docker.so<br>
https://www.zabbix.com/documentation/2.4/manual/config/items/loadablemodules


Compilation
===========

You have to compile module, if provided binary doesn't work on your system.
Basic compilation steps:

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

Output will be binary file (shared library) zabbix_module_docker.so, which can be loaded by zabbix agent.

How it works
============

See https://blog.docker.com/2013/10/gathering-lxc-docker-containers-metrics/
Metrics for containers are read from cgroup file system. 
[Docker API](https://docs.docker.com/reference/api/docker_remote_api) is used 
for discovering and some keys. However root or docker permissions are required 
for communication with Docker via unix socket. You can test API also in your command line:

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

Discovery test:

Part of config in zabbix_agentd.conf:

    UserParameter=xdocker.discovery,/etc/zabbix/scripts/container_discover.sh
    LoadModule=zabbix_module_docker.so

container_discover.sh: https://github.com/bsmile/zabbix-docker-lld/blob/master/usr/lib/zabbix/script/container_discover.sh
    
Test with 237 running containers:

    [root@dev ~]# docker info
    Containers: 237
    Images: 121
    Storage Driver: btrfs
    Execution Driver: native-0.2
    Kernel Version: 3.10.0-229.el7.x86_64
    Operating System: Red Hat Enterprise Linux Server 7.1 (Maipo)
    CPUs: 10
    Total Memory: 62.76 GiB
    Name: dev.local
    ID: AOAM:BO3G:5MCE:5FMM:IWKP:NPM4:PRKV:ZZ34:BYFL:XGAV:SRNJ:LKDH
    Username: username
    Registry: [https://index.docker.io/v1/]
    [root@dev ~]# time zabbix_get -s 127.0.0.1 -k docker.discovery > /dev/null
    
    real    0m0.112s
    user    0m0.000s
    sys     0m0.003s
    [root@dev ~]# time zabbix_get -s 127.0.0.1 -k xdocker.discovery > /dev/null
    
    real    0m5.856s
    user    0m0.000s
    sys     0m0.002s
    [root@dev ~]# ./zabbix-agent-stress-test.py -s 127.0.0.1 -k xdocker.discovery
    Starting 1 threads, host: 127.0.0.1:10050, key: xdocker.discovery
    Success: 0      Errors: 0       Avg rate: 0.00 qps      Execution time: 1.00 sec
    Success: 0      Errors: 0       Avg rate: 0.00 qps      Execution time: 2.00 sec
    Success: 0      Errors: 0       Avg rate: 0.00 qps      Execution time: 3.02 sec
    Success: 0      Errors: 0       Avg rate: 0.00 qps      Execution time: 4.02 sec
    Success: 0      Errors: 0       Avg rate: 0.00 qps      Execution time: 5.02 sec
    Success: 1      Errors: 0       Avg rate: 0.10 qps      Execution time: 6.02 sec
    Success: 1      Errors: 0       Avg rate: 0.10 qps      Execution time: 7.02 sec
    Success: 1      Errors: 0       Avg rate: 0.10 qps      Execution time: 8.02 sec
    Success: 1      Errors: 0       Avg rate: 0.10 qps      Execution time: 9.02 sec
    Success: 1      Errors: 0       Avg rate: 0.10 qps      Execution time: 10.02 sec
    Success: 2      Errors: 0       Avg rate: 0.14 qps      Execution time: 11.02 sec
    Success: 2      Errors: 0       Avg rate: 0.14 qps      Execution time: 12.03 sec
    Success: 2      Errors: 0       Avg rate: 0.14 qps      Execution time: 13.03 sec
    Success: 2      Errors: 0       Avg rate: 0.14 qps      Execution time: 14.03 sec
    Success: 2      Errors: 0       Avg rate: 0.14 qps      Execution time: 15.03 sec
    Success: 3      Errors: 0       Avg rate: 0.16 qps      Execution time: 16.03 sec
    Success: 3      Errors: 0       Avg rate: 0.16 qps      Execution time: 17.03 sec
    Success: 3      Errors: 0       Avg rate: 0.16 qps      Execution time: 18.03 sec
    Success: 3      Errors: 0       Avg rate: 0.16 qps      Execution time: 19.03 sec
    Success: 3      Errors: 0       Avg rate: 0.16 qps      Execution time: 20.03 sec
    Success: 3      Errors: 0       Avg rate: 0.16 qps      Execution time: 21.04 sec
    Success: 4      Errors: 0       Avg rate: 0.17 qps      Execution time: 22.04 sec
    Success: 4      Errors: 0       Avg rate: 0.17 qps      Execution time: 23.04 sec
    Success: 4      Errors: 0       Avg rate: 0.17 qps      Execution time: 24.04 sec
    Success: 4      Errors: 0       Avg rate: 0.17 qps      Execution time: 25.05 sec
    Success: 5      Errors: 0       Avg rate: 0.20 qps      Execution time: 26.05 sec
    Success: 5      Errors: 0       Avg rate: 0.20 qps      Execution time: 27.05 sec
    Success: 5      Errors: 0       Avg rate: 0.20 qps      Execution time: 28.05 sec
    Success: 5      Errors: 0       Avg rate: 0.20 qps      Execution time: 29.05 sec
    Success: 5      Errors: 0       Avg rate: 0.20 qps      Execution time: 30.05 sec
    Success: 5      Errors: 0       Avg rate: 0.20 qps      Execution time: 31.05 sec
    ^C
    Success: 5      Errors: 0       Avg rate: 0.20 qps      Execution time: 31.35 sec
    Avg rate based on total execution time and success connections: 0.16 qps
    [root@dev ~]# ./zabbix-agent-stress-test.py -s 127.0.0.1 -k docker.discovery
    Starting 1 threads, host: 127.0.0.1:10050, key: docker.discovery
    Success: 5      Errors: 0       Avg rate: 6.26 qps      Execution time: 1.00 sec
    Success: 5      Errors: 0       Avg rate: 6.26 qps      Execution time: 2.00 sec
    Success: 12     Errors: 0       Avg rate: 7.45 qps      Execution time: 3.00 sec
    Success: 20     Errors: 0       Avg rate: 6.77 qps      Execution time: 4.00 sec
    Success: 28     Errors: 0       Avg rate: 7.82 qps      Execution time: 5.00 sec
    Success: 36     Errors: 0       Avg rate: 7.21 qps      Execution time: 6.01 sec
    Success: 43     Errors: 0       Avg rate: 10.22 qps     Execution time: 7.01 sec
    Success: 43     Errors: 0       Avg rate: 10.22 qps     Execution time: 8.01 sec
    Success: 50     Errors: 0       Avg rate: 6.79 qps      Execution time: 9.01 sec
    Success: 57     Errors: 0       Avg rate: 6.11 qps      Execution time: 10.01 sec
    Success: 66     Errors: 0       Avg rate: 8.50 qps      Execution time: 11.01 sec
    Success: 73     Errors: 0       Avg rate: 6.51 qps      Execution time: 12.01 sec
    Success: 81     Errors: 0       Avg rate: 7.18 qps      Execution time: 13.01 sec
    Success: 82     Errors: 0       Avg rate: 7.85 qps      Execution time: 14.01 sec
    Success: 87     Errors: 0       Avg rate: 6.54 qps      Execution time: 15.02 sec
    Success: 95     Errors: 0       Avg rate: 7.84 qps      Execution time: 16.02 sec
    Success: 103    Errors: 0       Avg rate: 9.24 qps      Execution time: 17.02 sec
    Success: 111    Errors: 0       Avg rate: 9.94 qps      Execution time: 18.02 sec
    Success: 119    Errors: 0       Avg rate: 7.63 qps      Execution time: 19.02 sec
    Success: 120    Errors: 0       Avg rate: 6.70 qps      Execution time: 20.12 sec
    Success: 121    Errors: 0       Avg rate: 3.61 qps      Execution time: 21.12 sec
    Success: 128    Errors: 0       Avg rate: 8.46 qps      Execution time: 22.12 sec
    Success: 136    Errors: 0       Avg rate: 7.63 qps      Execution time: 23.12 sec
    Success: 144    Errors: 0       Avg rate: 6.21 qps      Execution time: 24.12 sec
    Success: 150    Errors: 0       Avg rate: 6.89 qps      Execution time: 25.12 sec
    Success: 157    Errors: 0       Avg rate: 10.87 qps     Execution time: 26.18 sec
    Success: 160    Errors: 0       Avg rate: 7.52 qps      Execution time: 27.18 sec
    Success: 168    Errors: 0       Avg rate: 9.81 qps      Execution time: 28.18 sec
    Success: 174    Errors: 0       Avg rate: 6.69 qps      Execution time: 29.18 sec
    Success: 181    Errors: 0       Avg rate: 6.35 qps      Execution time: 30.18 sec
    Success: 188    Errors: 0       Avg rate: 7.64 qps      Execution time: 31.19 sec
    ^C
    Success: 193    Errors: 0       Avg rate: 8.83 qps      Execution time: 31.79 sec
    Avg rate based on total execution time and success connections: 6.07 qps
       
Troubleshooting
===============

Edit your zabbix_agentd.conf and set DebugLevel:

    DebugLevel=4
    
Debug messages from this module will be available in standard zabbix_agentd.log.

Issues and feature requests
===========================

Please use Github issue tracker.       

Author
======

[Devops Monitoring zExpert](http://www.jangaraj.com), who loves monitoring systems, which start with letter Z. Those are Zabbix and Zenoss. [LinkedIn] (http://uk.linkedin.com/in/jangaraj/).
