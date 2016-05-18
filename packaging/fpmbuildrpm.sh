#!/bin/sh

yum install -y git wget make autoconf automake gcc svn rpm-build gem ruby-devel
mkdir /tmp/zabbix30git
cd /tmp/zabbix30git
git clone https://github.com/monitoringartist/Zabbix-Docker-Monitoring .
git checkout devel

rm -rf /tmp/zabbix30
mkdir /tmp/zabbix30
cd /tmp/zabbix30
svn co svn://svn.zabbix.com/branches/3.0 .
ls -lah
./bootstrap.sh
./configure --enable-agent

rm -rf src/modules/zabbix_module_docker
mkdir -p src/modules/zabbix_module_docker
cd src/modules/zabbix_module_docker
wget https://raw.githubusercontent.com/monitoringartist/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/zabbix_module_docker.c
wget https://raw.githubusercontent.com/monitoringartist/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/Makefile
make

gem install fpm
mkdir -p usr/lib/modules
cp zabbix_module_docker.so usr/lib/modules 
fpm -s dir -t rpm -n "zabbix_module_docker" -v 0.1 usr

pwd
ls -lah

# rm -rf /tmp/zabbix30git
# rm -rf /tmp/zabbix30
