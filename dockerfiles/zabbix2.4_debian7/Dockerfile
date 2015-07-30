# Compiling https://github.com/jangaraj/Zabbix-Docker-Monitoring for Debian 7 Wheezy

FROM debian:wheezy
MAINTAINER "Jan Garaj" <jan.garaj@gmail.com>
RUN apt-get update
RUN apt-get --assume-yes install git subversion automake autoconf gcc make pkg-config
RUN git clone https://github.com/jangaraj/Zabbix-Docker-Monitoring
RUN mkdir ~/zabbix/
RUN svn co svn://svn.zabbix.com/branches/2.4 ~/zabbix/
RUN cd ~/zabbix/; ./bootstrap.sh
RUN cd ~/zabbix/; ./configure --enable-agent
RUN cp -R Zabbix-Docker-Monitoring/src/modules/zabbix_module_docker/ ~/zabbix/src/modules/
RUN cd ~/zabbix/src/modules/zabbix_module_docker; make

#docker build --rm=true -t local/zabbix-module-docker-debian7 .
#docker run --rm -v /tmp:/tmp local/zabbix-module-docker-debian7 cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker_debian7.so
#docker rmi -f local/zabbix-module-docker-debian7
## use /tmp/zabbix_module_docker_debian7.so in your Zabbix
