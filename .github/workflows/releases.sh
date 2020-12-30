#!/bin/bash

# compile binary modules version for different systems, zabbix versions for gh-pages branch

rm -rf /tmp/ghpages
mkdir /tmp/ghpages

read -d '' systems << EOF
amazonlinux2,amazonlinux:2
amazonlinux1,amazonlinux:1
centos8,centos:8
centos7,centos:7
debian10,debian:10
debian9,debian:9
debian8,debian:8
fedora33,fedora:33
fedora32,fedora:32
fedora31,fedora:31
opensuse15,opensuse/leap:15
opensuse42,opensuse/leap:42
ubuntu20,ubuntu:20.04
ubuntu18,ubuntu:18.04
ubuntu16,ubuntu:16.04
ubuntu14,ubuntu:14.04
EOF

set -e

versions=(5.2 5.0 4.0)

GREEN='\033[0;32m'
NC='\033[0m'

for system in ${systems[@]}; do

  sourceimage=$(echo ${system} | awk -F',' '{print $2}')
  destionationfolder=$(echo ${system} | awk -F',' '{print $1}')
  sourcefolder=$(echo ${system} | awk -F',' '{print $1}' |  sed 's/[0-9]//g')
  sed -i -E "s#FROM .*#FROM ${sourceimage}#g" dockerfiles/${sourcefolder}/Dockerfile

  for version in ${versions[@]}; do
 
    echo ""
    echo -e "${GREEN}Module compilation for Zabbix ${version} on ${sourceimage}${NC}"
    mkdir -p /tmp/ghpages/${destionationfolder}/${version}
    docker build --rm=false \
      --build-arg ZABBIX_VERSION=release/${version} \
      -t local/zabbix-docker-module-compilation \
      -f dockerfiles/${sourcefolder}/Dockerfile \
      dockerfiles/${sourcefolder}/

    docker run --rm \
      -v /tmp/ghpages/${destionationfolder}/${version}:/tmp \
      local/zabbix-docker-module-compilation \
      cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker.so

  done

  docker rm -f $(docker ps -aq) &>/dev/null
  docker system prune -f &>/dev/null

done

docker rmi -f local/zabbix-docker-module-compilation
git checkout -- dockerfiles/*

# hashing
find /tmp/ghpages -type d \( ! -name . \) -exec bash -c "cd '{}' && find . -type f -name 'zabbix_module_docker.so' -exec bash -c \"md5sum zabbix_module_docker.so > md5sum.txt\" \;" \;
find /tmp/ghpages -type d \( ! -name . \) -exec bash -c "cd '{}' && find . -type f -name 'zabbix_module_docker.so' -exec bash -c \"sha1sum zabbix_module_docker.so > sha1sum.txt\" \;" \;
find /tmp/ghpages -type d \( ! -name . \) -exec bash -c "cd '{}' && find . -type f -name 'zabbix_module_docker.so' -exec bash -c \"sha256sum zabbix_module_docker.so > sha256sum.txt\" \;" \;
find /tmp/ghpages -type f -empty -delete
