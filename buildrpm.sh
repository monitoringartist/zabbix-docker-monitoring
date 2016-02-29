#!/bin/sh

PACKAGE=libzbxdocker
PACKAGE_VERSION=0.5.0
MAKE=make
RPMBASE=~/rpmbuild
RPMBUILD=rpmbuild
RPMBUILD_FLAGS=-ba
build_cpu=x86_64
RPMFILE=$PACKAGE-$PACKAGE_VERSION-*.$build_cpu.rpm

yum install -y git wget make autoconf automake gcc svn rpm-build
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
rm -rf *
wget https://raw.githubusercontent.com/monitoringartist/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/zabbix_module_docker.c
wget https://raw.githubusercontent.com/monitoringartist/Zabbix-Docker-Monitoring/master/src/modules/zabbix_module_docker/Makefile
tar -pczf $PACKAGE-$PACKAGE_VERSION.tar.gz .
make
ls -lah
pwd

cd /tmp/zabbix30git
sed -i 's/^Version\s*:.*/Version     : $PACKAGE_VERSION/' packaging/rpmbuild/$PACKAGE.spec
mkdir -p $RPMBASE/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
cp /tmp/zabbix30/src/modules/zabbix_module_docker/$PACKAGE-$PACKAGE_VERSION.tar.gz $RPMBASE/SOURCES/$PACKAGE-$PACKAGE_VERSION.tar.gz
cp packaging/rpmbuild/$PACKAGE.spec $RPMBASE/SPECS/$PACKAGE.spec
echo 'rpmbuild:'
pwd
echo "$RPMBUILD $RPMBUILD_FLAGS packaging/rpmbuild/$PACKAGE.spec"
$RPMBUILD $RPMBUILD_FLAGS packaging/rpmbuild/$PACKAGE.spec
cp $RPMBASE/RPMS/$build_cpu/$RPMFILE .
