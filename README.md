========================================
Zabbix Docker Monitoring - alpha version
========================================

About
=====

This is alpha version. I started it, because Zabbix doesn't support container monitoring.
Project is tested with Zabbix 2.4 and Docker 1.4. It should works also with another Zabbix/Docker versions.
Please feel free to test and provide feedback.

Build
=====

[Download latest build](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/files/zabbix24/src/modules/zabbix_module_docker/zabbix_module_docker.so)
[![Build Status](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/status.png)](https://drone.io/github.com/jangaraj/Zabbix-Docker-Monitoring/latest)

What is done
============

* docker.discovery - Zabbix LLD discovering of running containers
* docker.mem - memory metrics (memory.stat file)
* docker.up - check if container is running
* docker.cpu - cpu usage metric (still beta, no proper implementation) 
 
TODO
----
* docker.net - tricky metrics, because it will need root permissions
* docker.dev - blkio metrics
* Docker API metrics and details queries (if zabbix-agent has root permissions) 


Installation
============

* Import provided template Zabbix-Template-App-Docker.xml. 
* Configure your Zabbix agent(s)- load zabbix-module-docker.so
https://www.zabbix.com/documentation/2.2/manual/config/items/loadablemodules


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

Author
======
 
[Devops Monitoring zExpert](http://www.jangaraj.com), who loves monitoring systems, which start with letter Z. Those are Zabbix and Zenoss. [LinkeIn] (http://uk.linkedin.com/in/jangaraj/).
