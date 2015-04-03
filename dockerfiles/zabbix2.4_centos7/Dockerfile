# Compiling https://github.com/jangaraj/Zabbix-Docker-Monitoring for Centos 7

FROM centos:7
MAINTAINER "Jan Garaj" <jan.garaj@gmail.com>
RUN yum -y install git subversion automake autoconf gcc make
RUN git clone https://github.com/jangaraj/Zabbix-Docker-Monitoring
RUN mkdir ~/zabbix24/
RUN svn co svn://svn.zabbix.com/branches/2.4 ~/zabbix24/
RUN cd ~/zabbix24/; ./bootstrap.sh
RUN cd ~/zabbix24/; ./configure --enable-agent
RUN cp -R Zabbix-Docker-Monitoring/src/modules/zabbix_module_docker/ ~/zabbix24/src/modules/
RUN cd ~/zabbix24/src/modules/zabbix_module_docker; make

# docker build --rm -t local/zabbix24-module-docker-centos7 .
# docker run -v /tmp:/tmp local/zabbix24-module-docker-centos7 cp ~/zabbix24/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker_centos7.so
# docker rmi -f local/zabbix24-module-docker-centos7
# # use /tmp/zabbix_module_docker_centos7.so in your Zabbix
