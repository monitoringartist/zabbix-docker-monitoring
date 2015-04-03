# Compiling https://github.com/jangaraj/Zabbix-Docker-Monitoring for centos6

FROM centos:centos6
MAINTAINER "Jan Garaj" <jan.garaj@gmail.com>
RUN yum -y install git subversion automake autoconf gcc make
RUN git clone https://github.com/jangaraj/Zabbix-Docker-Monitoring
RUN mkdir ~/zabbix/
RUN svn co svn://svn.zabbix.com/branches/2.2 ~/zabbix/
RUN cd ~/zabbix/; ./bootstrap.sh
RUN cd ~/zabbix/; ./configure --enable-agent
RUN cp -R Zabbix-Docker-Monitoring/src/modules/zabbix_module_docker/ ~/zabbix/src/modules/
RUN cd ~/zabbix/src/modules/zabbix_module_docker; make

# docker build --rm=true -t local/zabbix-module-docker-centos6 .
# docker run -v /tmp:/tmp local/zabbix-module-docker-centos6 cp ~/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker_centos6.so
# docker rmi -f local/zabbix-module-docker-centos6
# # use /tmp/zabbix_module_docker_centos6.so in your Zabbix
