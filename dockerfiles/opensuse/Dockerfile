# https://github.com/monitoringartist/zabbix-docker-monitoring

# Define required openSuse version by using FROM tag. Avalaible: wheezy/jessie/...
FROM opensuse/leap:latest

MAINTAINER "Jan Garaj" <info@monitoringartist.com>

WORKDIR /root

RUN \
   zypper install --no-confirm --no-recommends git automake autoconf gcc make pkg-config pcre-devel libjansson-devel 1>/dev/null

RUN \
   git clone -q https://github.com/monitoringartist/zabbix-docker-monitoring

# Define required Zabbix version (<version>) or release (release/<version>), e.g. 4.2.2, or release/4.2
ARG ZABBIX_VERSION=release/5.2

RUN \
   git clone -b ${ZABBIX_VERSION} --depth 1 https://github.com/zabbix/zabbix.git ~/zabbix && \
   cd ~/zabbix/ && \
   ./bootstrap.sh 1>/dev/null && \
   ./configure --enable-agent 1>/dev/null && \
   cp -R ~/zabbix-docker-monitoring/src/modules/zabbix_module_docker/ ~/zabbix/src/modules/ && \
   cd ~/zabbix/src/modules/zabbix_module_docker && \
   make

## Dockerized compilation (build from remote URL or local PATH):
# docker build --rm=true -t local/zabbix-docker-module-compilation https://github.com/monitoringartist/zabbix-docker-monitoring.git#master:dockerfiles/debian/
# docker build --rm=true -t local/zabbix-docker-module-compilation .
# docker run  --rm -v /tmp:/tmp local/zabbix-docker-module-compilation cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker.so
# docker rmi -f local/zabbix-docker-module-compilation
## use/copy /tmp/zabbix_module_docker.so in your Zabbix
