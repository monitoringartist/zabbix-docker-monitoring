# https://github.com/monitoringartist/Zabbix-Docker-Monitoring

# Define required CentOS version by using FROM tag. Avalaible: centos7/centos6/...
FROM centos:centos7

MAINTAINER "Jan Garaj" <info@monitoringartist.com>

# Define required Zabbix version (tag/<version>) or branch (branches/<version>), e.g. tags/2.4.5, or branches/2.4  
ENV ZABBIX_VERSION=branches/3.0

WORKDIR /root

RUN \
   yum -y install git subversion automake autoconf gcc make && \
   git clone https://github.com/monitoringartist/zabbix-docker-monitoring && \
   mkdir ~/zabbix/ && \
   svn co svn://svn.zabbix.com/${ZABBIX_VERSION} ~/zabbix/ && \
   cd ~/zabbix/ && \
   ./bootstrap.sh && \
   ./configure --enable-agent && \
   cp -R ~/zabbix-docker-monitoring/src/modules/zabbix_module_docker/ ~/zabbix/src/modules/ && \
   cd ~/zabbix/src/modules/zabbix_module_docker && \
   make

## Docker compilation:
# docker build --rm=true -t local/zabbix-docker-module-compilation .
# docker run --rm -v /tmp:/tmp local/zabbix-docker-module-compilation cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker.so
# docker rmi -f local/zabbix-docker-module-compilation
## use/copy /tmp/zabbix_module_docker.so in your Zabbix
