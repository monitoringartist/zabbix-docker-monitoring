ZABBIX_SOURCE = ../../..

zabbix_module_docker: zabbix_module_docker.c
	gcc -fPIC -shared -o zabbix_module_docker.so zabbix_module_docker.c -I$(ZABBIX_SOURCE)/include `pkg-config --cflags --libs jansson`
