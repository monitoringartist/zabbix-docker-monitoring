Zabbix Docker Monitoring - beta version
========================================

About
=====

This is beta version. I've started it, because Zabbix doesn't support container monitoring.
Project is developed with Zabbix 2.4 and Docker 1.4. It should works also with another Zabbix/Docker versions.
Please feel free to test and provide feedback.

Build
=====

[Download latest build](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/files/zabbix24/src/modules/zabbix_module_docker/zabbix_module_docker.so)
[![Build Status](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/status.png)](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/latest)

Available keys
==============

Note: fci - full container ID

| Key | Description | Comments |
| --- | ----------- | -------- |
| **docker.discovery** | LLD discovering | Only running containers are discovered<br>Additional Docker permissions are needed, when you want to see container name (human name) in metrics/graphs instead of short container ID |  
| **docker.mem[fci,mmetric]** | Memory metrics | mmetric - any available memory metric in the pseudo-file memory.stat, e.g.: *cache, rss, mapped_file, pgpgin, pgpgout, swap, pgfault, pgmajfault, inactive_anon, active_anon, inactive_file, active_file, unevictable, hierarchical_memory_limit, hierarchical_memsw_limit, total_cache, total_rss, total_mapped_file, total_pgpgin, total_pgpgout, total_swap, total_pgfault, total_pgmajfault, total_inactive_anon, total_active_anon, total_inactive_file, total_active_file, total_unevictable* |
| **docker.cpu[fci,cmetric]** | CPU metrics | cmetric - any available CPU metric in the pseudo-file cpuacct.stat, e.g.: *system, user*<br>Counter is recalculated to % value by Zabbix | 
| **docker.up[fci]** | Running state check | 1 if container is running, otherwise 0 | 
 
Not available at the moment, maybe in the (near) future:

* docker.net - tricky metrics
* docker.dev - blkio metrics
* docker.stat - stat about number of available images, running/crashed/stopped containers
* Docker API metrics/details queries (when zabbix-agent has root permission)

How to get additional Docker permission
=======================================

You have two options, how to get additional Docker permission:

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

You have to compile module, if provided binary is not working on your system.
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

Troubleshooting
===============

Edit your zabbix_agentd.conf and set DebugLevel:

    DebugLevel=4
    
Debug messages from this module will be available in standard zabbix_agentd.log.      

Author
======

[Devops Monitoring zExpert](http://www.jangaraj.com), who loves monitoring systems, which start with letter Z. Those are Zabbix and Zenoss. [LinkedIn] (http://uk.linkedin.com/in/jangaraj/).
