# compile binary modules version for different systems, zabbix versions for gh-pages branch

rm -rf /tmp/ghpages
mkdir /tmp/ghpages

read -d '' systems << EOF
amazonlinux2,amazonlinux:2
amazonlinux1,amazonlinux:2
centos8,centos:8
centos7,centos:7
debian10,debian:10
debian9,debian:9
fedora33,fedora:33
fedora32,fedora:32
opensuse15,opensuse/leap:15
opensuse42,opensuse/leap:42
ubuntu20,ubuntu:20.04
ubuntu18,ubuntu:18.04
EOF

versions=(5.2 5.0 4.0)

for system in ${systems[@]}; do

  sourceimage=$(echo ${system} | awk -F',' '{print $2}')
  destionationfolder=$(echo ${system} | awk -F',' '{print $1}')
  sourcefolder=$(echo ${system} | awk -F',' '{print $1}' |  sed 's/[0-9]//g')
  sed -i -E "s/FROM .*/FROM ${sourceimage}/g" dockerfiles/${sourcefolder}/Dockerfile

  for version in ${versions[@]}; do

    echo "Compiling ${version} on ${sourceimage}:"
    mkdir -p /tmp/ghpages/${destionationfolder}/${version}
    docker build --rm=true \
      --build-arg ZABBIX_VERSION=release/${version} \
      -t local/zabbix-docker-module-compilation \
      -f dockerfiles/${sourcefolder}/Dockerfile \
      dockerfiles/${sourcefolder}/

    docker run --rm \
      -v /tmp/ghpages/${destionationfolder}/${version}:/tmp \
      local/zabbix-docker-module-compilation \
      cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker.so


  done

done

docker rmi -f local/zabbix-docker-module-compilation
git checkout -- dockerfiles/*

# hashing
find /tmp/ghpages -type f -name 'zabbix_module_docker.so' | while read line; do
    folder=$(dirname ${line})
    echo "Processing file '$line' in '${folder}"
    md5sum ${line} > ${folder}/md5sum.txt
    sha1sum ${line} > ${folder}/sha1sum.txt
    sha256sum ${line} > ${folder}/sha256sum.txt
done

