========================================
Zabbix Docker Monitoring - alpha version
========================================

About
=====

This is alpha version. I started it, because Zabbix doesn't support container monitoring.
Project is tested with Zabbix 2.2 and Docker 1.1. It should works also with another Zabbix/Docker versions.
Please feel free to test and comment it.

What is done
============

* docker.discovery - Zabbix LLD discovering of running containers
* docker.mem - memory metrics (memory.stat file)
* docker.up - check if container is running
 
TODO
----
* docker.cpu - cpu usage metric; it will need to implement some jiffy calculations
* docker.net - tricky metrics, because it will need root permissions
* docker.dev - blkio metrics 


Installation
============

* Import provided template Zabbix-Template-App-Docker.xml. 
* Configure your Zabbix agent(s)- load zabbix-module-docker.so
https://www.zabbix.com/documentation/2.2/manual/config/items/loadablemodules


Links
=====

Author: Jan Garaj - www.jangaraj.com
Zabbix: www.zabbix.com
