#!/bin/sh

yum install -y wget autoconf automake gcc svn
mkdir /tmp/zabbix30
cd /tmp/zabbix30
svn co svn://svn.zabbix.com/branches/3.0 .
./bootstrap.sh
./configure --enable-agent
mkdir src/modules/zabbix_module_docker
cd src/modules/zabbix_module_docker
wget https://raw.githubusercontent.com/monitoringartist/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/zabbix_module_docker.c
wget https://raw.githubusercontent.com/monitoringartist/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/Makefile
make
