/*
** Zabbix module for Docker container monitoring - v 0.1.2
** Copyright (C) 2001-2015 Jan Garaj - www.jangaraj.com
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "sysinc.h"
#include "module.h"
#include "log.h"
#include "common.h"
#include <stdio.h>
#include <unistd.h>
#include <grp.h>
#include "zbxjson.h"
/* docker unix connections */
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include "comms.h"

/* the variable keeps timeout setting for item processing */
static int      item_timeout = 1;
static int buffer_size = 1024, cid_length = 65;
char      *stat_dir, *driver, *c_prefix = NULL, *c_suffix = NULL;
static int socket_api;
int     zbx_module_docker_discovery(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_inspect(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_info(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_stats(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_up(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_mem(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_cpu(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_net(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_dev(AGENT_REQUEST *request, AGENT_RESULT *result);

static ZBX_METRIC keys[] =
/*      KEY                     FLAG            FUNCTION                TEST PARAMETERS */
{
        {"docker.discovery", 0, zbx_module_docker_discovery,    NULL},
        {"docker.inspect", CF_HAVEPARAMS, zbx_module_docker_inspect, "full container id, parameter 1, <parameter 2>"},
        {"docker.info", CF_HAVEPARAMS,  zbx_module_docker_info, "full container id, info"},
        {"docker.stats",CF_HAVEPARAMS,  zbx_module_docker_stats,"full container id, parameter 1, <parameter 2>, <parameter 3>"},
        {"docker.up",   CF_HAVEPARAMS,  zbx_module_docker_up,   "full container id"},
        {"docker.mem",  CF_HAVEPARAMS,  zbx_module_docker_mem,  "full container id, memory metric name"},
        {"docker.cpu",  CF_HAVEPARAMS,  zbx_module_docker_cpu,  "full container id, cpu metric name"},
        {"docker.net",  CF_HAVEPARAMS,  zbx_module_docker_net,  "full container id, network metric name"},
        {"docker.dev",  CF_HAVEPARAMS,  zbx_module_docker_dev,  "full container id, blkio file, blkio metric name"},        
        {NULL}
};

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_api_version                                           *
 *                                                                            *
 * Purpose: returns version number of the module interface                    *
 *                                                                            *
 * Return value: ZBX_MODULE_API_VERSION_ONE - the only version supported by   *
 *               Zabbix currently                                             *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_api_version()
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_api_version()");
        return ZBX_MODULE_API_VERSION_ONE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_item_timeout                                          *
 *                                                                            *
 * Purpose: set timeout value for processing of items                         *
 *                                                                            *
 * Parameters: timeout - timeout in seconds, 0 - no timeout set               *
 *                                                                            *
 ******************************************************************************/
void    zbx_module_item_timeout(int timeout)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_item_timeout()");
        item_timeout = timeout;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_item_list                                             *
 *                                                                            *
 * Purpose: returns list of item keys supported by the module                 *
 *                                                                            *
 * Return value: list of item keys                                            *
 *                                                                            *
 ******************************************************************************/
ZBX_METRIC      *zbx_module_item_list()
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_item_list()");
        return keys;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_socket_query                                   *
 *                                                                            *
 * Purpose: quering details via Docker socket API (root permission is needed) *
 *                                                                            *
 * Return value: empty string - function failed                               *
 *               string - response from Docker's socket API                   *
 *                                                                            *
 * Notes: https://docs.docker.com/reference/api/docker_remote_api/            *
 *        echo -e "GET /containers/json?all=1 HTTP/1.0\r\n" | \               *
 *        nc -U /var/run/docker.sock                                          *
 ******************************************************************************/
const char*  zbx_module_docker_socket_query(char *query)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_socket_query()");

        struct sockaddr_un address;
        int sock, nbytes, tbytes = 0;
        size_t addr_length;
        char buffer[buffer_size];
        char *response, *empty="", *message = NULL;
        if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) 
        {
            zabbix_log(LOG_LEVEL_WARNING, "Can't create socket for docker's communication");
            return empty;
        }
        address.sun_family = AF_UNIX;
        zbx_strlcpy(address.sun_path, "/var/run/docker.sock", strlen("/var/run/docker.sock")+1);
        addr_length = sizeof(address.sun_family) + strlen(address.sun_path);
        if (connect(sock, (struct sockaddr *) &address, addr_length)) 
        {
            zabbix_log(LOG_LEVEL_WARNING, "Can't connect to standard docker's socket /var/run/docker.sock");
            return empty;
        }
        zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket query: %s", string_replace(string_replace(query, "\n", ""), "\r", ""));
        write(sock, query, strlen(query));
        message = realloc(NULL, 1);
        if (message == NULL) 
        {
            zabbix_log(LOG_LEVEL_WARNING, "Problem with allocating memory");
            return empty;
        }
        strcat(message, "");
        while ((nbytes = read(sock, buffer, buffer_size)) > 0 ) 
        {
            // TODO stats is stream - wait only for first chunk
            buffer[nbytes] = 0;
            message = realloc(message, (strlen(message) + nbytes + 1));
            if (message == NULL) 
            {
                zabbix_log(LOG_LEVEL_WARNING, "Problem with allocating memory");
                return empty;
            }
            strcat(message, buffer);
        }
        close(sock);
        // remove http header
        if (response = strstr(message, "\r\n\r\n")) 
        {
            response += 4;
        } else {
            response = "[{}]"; 
        }
        zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket response: %s", string_replace(string_replace(response, "\n", ""), "\r", ""));
        return response;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_up                                             *
 *                                                                            *
 * Purpose: check if container is running                                     *
 *                                                                            *
 * Return value: 1 - is running, 0 - is not running                           *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_up(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_up()");
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (1 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
        
        if (stat_dir == NULL || driver == NULL) 
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.up check is not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.up check is not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }        

        container = get_rparam(request, 0);
        metric = get_rparam(request, 1);
        char    *stat_file = "/memory.stat";
        char    *cgroup = "memory/";
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        if (c_prefix != NULL)
        {
            filename_size += strlen(c_prefix);
        }
        if (c_suffix != NULL)
        {
            filename_size += strlen(c_suffix);
        }
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (c_prefix != NULL)
        {        
            zbx_strlcat(filename, c_prefix, filename_size);
        }
        zbx_strlcat(filename, container, filename_size);
        if (c_suffix != NULL)
        {        
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_DEBUG, "Can't open docker container metric file: '%s', container doesn't run", filename);
                SET_DBL_RESULT(result, 0);
                return SYSINFO_RET_OK;
        }
        zbx_fclose(file);
        zabbix_log(LOG_LEVEL_DEBUG, "Can open docker container metric file: '%s', container is running", filename);
        SET_DBL_RESULT(result, 1);
        return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_dev                                            *
 *                                                                            *
 * Purpose: container device blkio metrics                                    *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 * Notes: https://www.kernel.org/doc/Documentation/cgroups/blkio-controller.txt
 ******************************************************************************/
int     zbx_module_docker_dev(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_dev()");
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (3 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
        
        if (stat_dir == NULL || driver == NULL) 
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.dev metrics are not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.dev metrics are not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }        

        container = get_rparam(request, 0);
        char    *stat_file = malloc(strlen(get_rparam(request, 1)) + 2);
        zbx_strlcpy(stat_file, "/", strlen(get_rparam(request, 1)) + 2);
        zbx_strlcat(stat_file, get_rparam(request, 1), strlen(get_rparam(request, 1)) + 2);        
        metric = get_rparam(request, 2);
        
        char    *cgroup = "blkio/";
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        if (c_prefix != NULL)
        {
            filename_size += strlen(c_prefix);
        }
        if (c_suffix != NULL)
        {
            filename_size += strlen(c_suffix);
        }        
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (c_prefix != NULL)
        {        
            zbx_strlcat(filename, c_prefix, filename_size);
        }        
        zbx_strlcat(filename, container, filename_size);
        if (c_suffix != NULL)
        {        
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_ERR, "Can't open docker container metric file: '%s'", filename);
                SET_MSG_RESULT(result, strdup("Can't open docker container stat file, maybe CONFIG_DEBUG_BLK_CGROUP is not enabled."));
                return SYSINFO_RET_FAIL;
        }

        char    line[MAX_STRING_LEN];
        char    *metric2 = malloc(strlen(metric)+1);
        memcpy(metric2, metric, strlen(metric));
        memcpy(metric2 + strlen(metric), " ", 2);
        zbx_uint64_t    value = 0;
        zabbix_log(LOG_LEVEL_DEBUG, "Looking metric %s in blkio file", metric);
        while (NULL != fgets(line, sizeof(line), file))
        {
                if (0 != strncmp(line, metric2, strlen(metric2)))
                        continue;
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value)) 
                {
                     // maybe per blk device metric, e.g. '8:0 Read'
                     if (1 != sscanf(line, "%*s %*s " ZBX_FS_UI64, &value))
                     {
                         zabbix_log(LOG_LEVEL_ERR, "sscanf failed for matched metric line");
                         break;
                     }
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Container: %s; stat file: %s, metric: %s; value: %d", container, stat_file, metric, value);
                SET_UI64_RESULT(result, value);
                ret = SYSINFO_RET_OK;
                break;
        }
        zbx_fclose(file);

        if (SYSINFO_RET_FAIL == ret)
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in docker container blkio file."));

        return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_mem                                            *
 *                                                                            *
 * Purpose: container memory metrics                                          *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 * Notes: https://www.kernel.org/doc/Documentation/cgroups/memory.txt         * 
 ******************************************************************************/
int     zbx_module_docker_mem(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_mem()");
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (2 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
       
        if (stat_dir == NULL || driver == NULL) 
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.mem metrics are not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.mem metrics are not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }

        container = get_rparam(request, 0);
        metric = get_rparam(request, 1);
        char    *stat_file = "/memory.stat";
        char    *cgroup = "memory/";
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        if (c_prefix != NULL)
        {
            filename_size += strlen(c_prefix);
        }
        if (c_suffix != NULL)
        {
            filename_size += strlen(c_suffix);
        }
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (c_prefix != NULL)
        {        
            zbx_strlcat(filename, c_prefix, filename_size);
        }
        zbx_strlcat(filename, container, filename_size);
        if (c_suffix != NULL)
        {        
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_ERR, "Can't open docker container metric file: '%s'", filename);
                SET_MSG_RESULT(result, strdup("Can't open docker container memory.stat file"));
                return SYSINFO_RET_FAIL;
        }

        char    line[MAX_STRING_LEN];
        char    *metric2 = malloc(strlen(metric)+1);
        memcpy(metric2, metric, strlen(metric));
        memcpy(metric2 + strlen(metric), " ", 2);
        zbx_uint64_t    value = 0;
        zabbix_log(LOG_LEVEL_DEBUG, "Looking metric %s in memory.stat file", metric);
        while (NULL != fgets(line, sizeof(line), file))
        {
                if (0 != strncmp(line, metric2, strlen(metric2)))
                        continue;
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value)) 
                {
                        zabbix_log(LOG_LEVEL_ERR, "sscanf failed for matched metric line");
                        continue;
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Container: %s; metric: %s; value: %d", container, metric, value);
                SET_UI64_RESULT(result, value);
                ret = SYSINFO_RET_OK;
                break;
        }
        zbx_fclose(file);

        if (SYSINFO_RET_FAIL == ret)
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in docker container memory.stat file."));

        return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_cpu                                            *
 *                                                                            *
 * Purpose: container CPU metrics                                             *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 * Notes: https://www.kernel.org/doc/Documentation/cgroups/cpuacct.txt        * 
 ******************************************************************************/
int     zbx_module_docker_cpu(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_cpu()");
                
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (2 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
        
        if (stat_dir == NULL || driver == NULL) 
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.cpu metrics are not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.cpu metrics are not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }        

        container = get_rparam(request, 0);
        metric = get_rparam(request, 1);
        char    *stat_file = "/cpuacct.stat";
        char    *cgroup = "cpuacct/";
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        if (c_prefix != NULL)
        {
            filename_size += strlen(c_prefix);
        }
        if (c_suffix != NULL)
        {
            filename_size += strlen(c_suffix);
        }        
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (c_prefix != NULL)
        {        
            zbx_strlcat(filename, c_prefix, filename_size);
        }        
        zbx_strlcat(filename, container, filename_size);
        if (c_suffix != NULL)
        {        
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_ERR, "Can't open docker container metric file: '%s'", filename);
                SET_MSG_RESULT(result, strdup("Can't open docker container memory.stat file"));
                return SYSINFO_RET_FAIL;
        }

        char    line[MAX_STRING_LEN];
        char    *metric2 = malloc(strlen(metric)+1);
        memcpy(metric2, metric, strlen(metric));
        memcpy(metric2 + strlen(metric), " ", 2);
        zbx_uint64_t    value = 0;
        zabbix_log(LOG_LEVEL_DEBUG, "Looking metric %s in memory.stat file", metric);
        while (NULL != fgets(line, sizeof(line), file))
        {
                if (0 != strncmp(line, metric2, strlen(metric2)))
                        continue;
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value)) 
                {
                        zabbix_log(LOG_LEVEL_ERR, "sscanf failed for matched metric line");
                        continue;
                }
                // TODO normalize with number of online CPU
                /*
                #include <unistd.h>                
                if (0 < (cpu_num = sysconf(_SC_NPROCESSORS_ONLN)))
                {
                	value /= cpu_num;
                }
                */
                zabbix_log(LOG_LEVEL_DEBUG, "Container: %s; metric: %s; value: %d", container, metric, value);
                SET_UI64_RESULT(result, value);
                ret = SYSINFO_RET_OK;
                break;
        }
        zbx_fclose(file);

        if (SYSINFO_RET_FAIL == ret)
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in docker container cpuacct.stat file."));

        return ret;        
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_net                                            *
 *                                                                            *
 * Purpose: container network metrics                                         *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_net(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_net()");
        // TODO
        zabbix_log(LOG_LEVEL_ERR,  "docker.net metrics are not implemented.");
        SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.net metrics are not implemented."));
        return SYSINFO_RET_OK;
        /*        
        if(socket_api == 1) 
        {
            zbx_module_docker_net_extended(request, result);
        } else {
            // TODO
            zabbix_log(LOG_LEVEL_ERR,  "docker.net metrics are not implemented.");
            SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.net metrics are not implemented."));
            return SYSINFO_RET_OK;
        }
        */
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_net_extended                                            *
 *                                                                            *
 * Purpose: container network metrics                                         *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_net_extended(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_net_extended()");
        
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (2 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
        
        container = get_rparam(request, 0);
        metric = get_rparam(request, 1);        
        
        // api v1.17 note: this functionality currently only works when using the libcontainer exec-driver
        /*
        from docker import Client
        cli = Client(base_url='unix://var/run/docker.sock')
        stats_obj = cli.stats('zabbix2')
        for stat in stats_obj:
            print(stat)
        404 - exec driver: native
        GET /info HTTP/1.1
        "ExecutionDriver":"native-0.1",    
        */
        char *query = malloc(104);
        zbx_strlcpy(query, "GET /containers/", 104);
        zbx_strlcat(query, container, 104);
        zbx_strlcat(query, "/stats HTTP/1.0\r\n\n", 104);
        const char *answer;
        answer = zbx_module_docker_socket_query(query);
        if (strcmp(answer, "") == 0) 
        {
            zabbix_log(LOG_LEVEL_ERR, "docker.net is not available at the moment - some problem with Docker's socket API");
            SET_MSG_RESULT(result, strdup("docker.net is not available at the moment - some problem with Docker's socket API"));
            return SYSINFO_RET_FAIL;
        }
        
        // 404 page not found test
        
        zabbix_log(LOG_LEVEL_DEBUG, "docker.net metrics answer: %s", answer);
        
        return SYSINFO_RET_OK;        
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_docker_dir_detect                                           *
 *                                                                            *
 * Purpose: it should find metric folder - it depends on docker version       *
 *            or execution environment                                        *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - stat folder was not found                 *
 *               SYSINFO_RET_OK - stat folder was found                       *
 *                                                                            *
 ******************************************************************************/
int     zbx_docker_dir_detect() 
{
        // TODO logic should be changed for all lxc/docker/systemd container support
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_docker_dir_detect()");

        char *drivers[] = {
            "docker/",        // Non-systemd Docker: docker -d -e native
            "system.slice/",  // Systemd Docker    
            "lxc/",           // Non-systemd LXC: docker -d -e lxc
            "libvirt/lxc/",   // Legacy libvirt-lxc
            // TODO pos = cgroup.find("-lxc\\x2");     // Systemd libvirt-lxc
            // TODO pos = cgroup.find(".libvirt-lxc"); // Non-systemd libvirt-lxc
            ""
        }, **tdriver;
        char path[512];
        char *temp;
        FILE *fp;
        DIR  *dir;

        if ((fp = fopen("/proc/mounts", "r")) == NULL) 
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot open /proc/mounts: %s", zbx_strerror(errno));
            return SYSINFO_RET_FAIL;
        }

        while (fgets(path, 512, fp) != NULL) 
        {
            if ((strstr(path, "memory cgroup")) != NULL) 
            {
                // found line e.g. cgroup /cgroup/cpuacct cgroup rw,relatime,cpuacct 0 0
                temp = string_replace(path, "cgroup ", "");
                temp = string_replace(temp, strstr(temp, " "), "");
                stat_dir = string_replace(temp, "memory", "");
                zabbix_log(LOG_LEVEL_DEBUG, "Detected docker stat directory: %s", stat_dir);
                
                char *cgroup = "memory/";
                tdriver = drivers;
                while (*tdriver != "") 
                {
                    size_t  ddir_size = strlen(cgroup) + strlen(stat_dir) + strlen(*tdriver) + 1;
                    char    *ddir = malloc(ddir_size);
                    zbx_strlcpy(ddir, stat_dir, ddir_size);
                    zbx_strlcat(ddir, cgroup, ddir_size);
                    zbx_strlcat(ddir, *tdriver, ddir_size);
                    if (NULL != (dir = opendir(ddir)))
                    {
                        driver = *tdriver;
                        zabbix_log(LOG_LEVEL_DEBUG, "Detected used docker driver dir: %s", driver);
                        // systemd docker
                        if (strcmp(driver, "system.slice/") == 0)
                        {
                            zabbix_log(LOG_LEVEL_DEBUG, "Detected systemd docker - prefix/suffix will be used");
                            c_prefix = "docker-";
                            c_suffix = ".scope";                        
                        }
                        return SYSINFO_RET_OK; 
                    }
                    *tdriver++;
                    free(ddir);
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Can't detect used docker driver");
                return SYSINFO_RET_FAIL;
            }
        }
        zabbix_log(LOG_LEVEL_DEBUG, "Can't detect docker stat directory");
        return SYSINFO_RET_FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_uninit                                                *
 *                                                                            *
 * Purpose: the function is called on agent shutdown                          *
 *          It should be used to cleanup used resources if there are any      *
 *                                                                            *
 * Return value: ZBX_MODULE_OK - success                                      *
 *               ZBX_MODULE_FAIL - function failed                            *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_uninit()
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_uninit()");
        return ZBX_MODULE_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_docker_perm                                                  *
 *                                                                            *
 * Purpose: test if agent has docker permission (docker group membership)     *
 *                                                                            *
 * Return value: ZBX_MODULE_OK - has docker perm                              *
 *               ZBX_MODULE_FAIL - has not docker perm                        *
 *                                                                            *
 ******************************************************************************/
int     zbx_docker_perm()
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_docker_perm()");
        // I hope that zabbix user can't be member of more than 10 groups
        int j, ngroups = 10;
        gid_t *groups;
        struct passwd *pw;
        struct group *gr;  
        groups = malloc(ngroups * sizeof (gid_t));
        if (groups == NULL) 
        {
            zabbix_log(LOG_LEVEL_WARNING, "Malloc error");
            return 0;
        }
        
        struct passwd *p = getpwuid(geteuid());
        if (getgrouplist(p->pw_name, geteuid(), groups, &ngroups) == -1) 
        {
             zabbix_log(LOG_LEVEL_WARNING, "getgrouplist() returned -1; ngroups = %d\n", ngroups);               
             return 0;
        }

        for (j = 0; j < ngroups; j++) 
        {               
               gr = getgrgid(groups[j]);
               if (gr != NULL) 
               {
                   if (strcmp(gr->gr_name, "docker") == 0) 
                   {
                       zabbix_log(LOG_LEVEL_DEBUG, "zabbix agent user has docker perm");
                       return 1;
                   }                   
               }
        }
        return 0;        
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_init                                                  *
 *                                                                            *
 * Purpose: the function is called on agent startup                           *
 *          It should be used to call any initialization routines             *
 *                                                                            *
 * Return value: ZBX_MODULE_OK - success                                      *
 *               ZBX_MODULE_FAIL - module initialization failed               *
 *                                                                            *
 * Comment: the module won't be loaded in case of ZBX_MODULE_FAIL             *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_init()
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_init()");
        zbx_docker_dir_detect();        
        // test root or docker permission
        if (geteuid() != 0 && zbx_docker_perm() != 1 ) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Additional permission of Zabbix Agent are not detected - only basic docker metrics are availaible");
            socket_api = 0;
        } else {
            // test Docker's socket connection 
            if (strcmp(zbx_module_docker_socket_query("GET /_ping HTTP/1.0\r\n\n"), "OK") == 0) 
            {
                zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket works - extended docker metrics are availaible");
                socket_api = 1;
            } else {
                zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket doesn't work - only basic docker metrics are availaible");
                socket_api = 0;
            }
        }
        return ZBX_MODULE_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_discovery                                      *
 *                                                                            *
 * Purpose: container discovery                                               *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_discovery(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        if (socket_api == 1) 
        {
            zbx_module_docker_discovery_extended(request, result);
        } else {
            zbx_module_docker_discovery_basic(request, result);
        }
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_discovery_basic                                      *
 *                                                                            *
 * Purpose: container discovery                                               *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_discovery_basic(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_discovery_basic()");

        struct zbx_json j;
        if(stat_dir == NULL && zbx_docker_dir_detect() == SYSINFO_RET_FAIL) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.discovery is not available at the moment - no stat directory - empty discovery");
            zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
            zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
            zbx_json_close(&j);
            SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
            zbx_json_free(&j);
            return SYSINFO_RET_OK;
        }

        char            line[MAX_STRING_LEN], *p, *mpoint, *mtype, container, *containerid;
        FILE            *f;
        DIR             *dir;
        zbx_stat_t      sb;
        char            *file = NULL, scontainerid[13];
        struct dirent   *d;
        char    *cgroup = "memory/";
        size_t  ddir_size = strlen(cgroup) + strlen(stat_dir) + strlen(driver) + 2;
        char    *ddir = malloc(ddir_size);
        zbx_strlcpy(ddir, stat_dir, ddir_size);
        zbx_strlcat(ddir, cgroup, ddir_size);
        zbx_strlcat(ddir, driver, ddir_size);

        if (NULL == (dir = opendir(ddir))) 
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s: %s", ddir, zbx_strerror(errno));
            return SYSINFO_RET_FAIL;
        }

        zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
        zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
        
        while (NULL != (d = readdir(dir)))
        {
                if(0 == strcmp(d->d_name, ".") || 0 == strcmp(d->d_name, ".."))
                        continue;

                file = zbx_dsprintf(file, "%s/%s", ddir, d->d_name);

                if (0 != zbx_stat(file, &sb) || 0 == S_ISDIR(sb.st_mode))
                        continue;
                
                // systemd docker: remove suffix (.scope)
                if (c_suffix != NULL)
                {
                    containerid = strtok(d->d_name, ".");
                } else {
                    containerid = d->d_name;
                }
                
                // systemd docker: remove preffix (docker-)
                if (c_suffix != NULL) 
                {
                    containerid = strtok(containerid, "-");
                    containerid = strtok(NULL, "-");
                } else {
                    containerid = d->d_name;
                }

                zbx_json_addobject(&j, NULL);
                zbx_json_addstring(&j, "{#FCONTAINERID}", containerid, ZBX_JSON_TYPE_STRING);
                zbx_strlcpy(scontainerid, containerid, 13);
                zbx_json_addstring(&j, "{#HCONTAINERID}", scontainerid, ZBX_JSON_TYPE_STRING);
                zbx_json_close(&j);

        }

        if (0 != closedir(dir)) 
        {
                zbx_error("%s: %s\n", ddir, zbx_strerror(errno));
        }

        zbx_json_close(&j);

        SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

        zbx_json_free(&j);

        return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_discovery_extended                             *
 *                                                                            *
 * Purpose: container discovery                                               *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_discovery_extended(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_discovery_extended()");

        struct zbx_json j;
        const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n");
        //const char *answer = "[{\"Command\":\"nginx\",\"Created\":1426152751,\"Id\":\"3bae6a35de7f5926162c01498427267ae4523c22914aea8fe97d17337cd0709f\",\"Image\":\"dockerfile/nginx:latest\",\"Names\":[\"/docker_nginx\"],\"Ports\":[{\"IP\":\"0.0.0.0\",\"PrivatePort\":80,\"PublicPort\":80,\"Type\":\"tcp\"},{\"IP\":\"0.0.0.0\",\"PrivatePort\":8001,\"PublicPort\":8001,\"Type\":\"tcp\"},{\"PrivatePort\":443,\"Type\":\"tcp\"}],\"Status\":\"Up About an hour\"}]";
        if(strcmp(answer, "") == 0) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.discovery is not available at the moment - some problem with Docker's socket API");
            zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
            zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
            zbx_json_close(&j);
            SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
            zbx_json_free(&j);
            return SYSINFO_RET_OK;
        }

	    struct zbx_json_parse	jp_data2, jp_row;
	    const char		*p = NULL;
        char cid[cid_length], scontainerid[13];
        size_t  s_size;

        zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
        zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
        // skipped zbx_json_brackets_open and zbx_json_brackets_by_name
    	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
    	/*         ^-------------------------------------------^  */
        struct zbx_json_parse jp_data = {&answer[0], &answer[strlen(answer)]};
    	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
    	/*          ^                                             */
    	while (NULL != (p = zbx_json_next(&jp_data, p)))
    	{
    		/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
    		/*          ^------------------^                          */
    		if (FAIL == zbx_json_brackets_open(p, &jp_row)) 
            {
                zabbix_log(LOG_LEVEL_WARNING, "Expected brackets, but zbx_json_brackets_open failes");
                continue;
            }

            if (SUCCEED != zbx_json_brackets_by_name(&jp_row, "Names", &jp_data2))
            {
                zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Names\" array in the received JSON object");
                continue;            
            } else {
                // HCONTAINERID - human name - first name in array
                jp_data2.start += 3;
                char *result, *names;
                if ((result = strchr(jp_data2.start, '"')) != NULL)
                {
                    s_size = strlen(jp_data2.start) - strlen(result) + 1;
                    names = malloc(s_size);                
                    zbx_strlcpy(names, jp_data2.start, s_size);                        
                } else {
                    s_size = jp_data2.end - jp_data2.start;
                    names = malloc(s_size);                
                    zbx_strlcpy(names, jp_data2.start, s_size-3);                    
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Parsed first container name: %s", names);
 
                // FCONTAINERID - full container id
                if (SUCCEED != zbx_json_value_by_name(&jp_row, "Id", cid, cid_length))
                {
                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Id\" array in the received JSON object");
                    // zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Id\" array in the received JSON object, jp_row: %s", jp_row);
                    continue;
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Parsed container id: %s", cid);

                zbx_json_addobject(&j, NULL);
                zbx_json_addstring(&j, "{#FCONTAINERID}", cid, ZBX_JSON_TYPE_STRING);
                zbx_json_addstring(&j, "{#HCONTAINERID}", names, ZBX_JSON_TYPE_STRING);
                zbx_json_close(&j);
           }
        }
        
        zbx_json_close(&j);
        SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
        zbx_json_free(&j);

        return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_inspect                                        *
 *                                                                            *
 * Purpose: container inspection                                              *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_inspect(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_inspect()");
        
        if (socket_api != 1) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not avalaible");
            SET_MSG_RESULT(result, strdup("Docker's socket API is not avalaible"));
            return SYSINFO_RET_FAIL;        
        }

        if (1 >= request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }           

        char    *container, *query;
        container = get_rparam(request, 0);       
        
        size_t s_size = strlen("GET /containers//json HTTP/1.0\r\n\n") + strlen(container);
        query = malloc(s_size);                
        zbx_strlcpy(query, "GET /containers/", s_size);
        zbx_strlcat(query, container, s_size);
        zbx_strlcat(query, "/json HTTP/1.0\r\n\n", s_size);
        
        const char *answer = zbx_module_docker_socket_query(query);
        if(strcmp(answer, "") == 0) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.inspect is not available at the moment - some problem with Docker's socket API");
            SET_MSG_RESULT(result, strdup("docker.inspect is not available at the moment - some problem with Docker's socket API"));
            return SYSINFO_RET_FAIL;
        }

	    struct zbx_json_parse jp_data2, jp_row;
        char api_value[buffer_size];

        struct zbx_json_parse jp_data = {&answer[0], &answer[strlen(answer)]};
        
        if (request->nparam > 1) {
            char *param1;
            param1 = get_rparam(request, 1);
            // 1st level - plain value search
            if (SUCCEED != zbx_json_value_by_name(&jp_data, param1, api_value, buffer_size))
            {
                 // 1st level - json object search
                if (SUCCEED != zbx_json_brackets_by_name(&jp_data, param1, &jp_data2))
                {
                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s] item in the received JSON object", param1);
                    SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s] item in the received JSON object", param1));
                    return SYSINFO_RET_FAIL;
                } else {
                    // 2nd level
                    if (request->nparam > 2) 
                    {
                        char *param2, api_value2[buffer_size];
                        param2 = get_rparam(request, 2);
                        if (SUCCEED != zbx_json_value_by_name(&jp_data2, param2, api_value2, buffer_size))
                        {
                            zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s] item in the received JSON object", param1, param2);
                            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s][%s] item in the received JSON object", param1, param2));
                            return SYSINFO_RET_FAIL;
                        } else {
                            zabbix_log(LOG_LEVEL_DEBUG, "Finded the [%s][%s] item in the received JSON object: %s", param1, param2, api_value2);
                            SET_STR_RESULT(result, zbx_strdup(NULL, api_value2));
                            return SYSINFO_RET_OK;
                        }                    
                    } else {
                        zabbix_log(LOG_LEVEL_WARNING, "Finded the [%s] item in the received JSON object, but it's not plain value object", param1);
                        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Can find the [%s] item in the received JSON object, but it's not plain value object", param1));
                        return SYSINFO_RET_FAIL;                  
                    }                  
                }             
            } else {
                    zabbix_log(LOG_LEVEL_DEBUG, "Finded the [%s] item in the received JSON object: %s", param1, api_value);
                    SET_STR_RESULT(result, zbx_strdup(NULL, api_value));
                    return SYSINFO_RET_OK;
            }        
        }            
        return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_info                                           *
 *                                                                            *
 * Purpose: docker information                                                *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_info(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_info()");
        
        if (socket_api != 1) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not avalaible");
            SET_MSG_RESULT(result, strdup("Docker's socket API is not avalaible"));
            return SYSINFO_RET_FAIL;        
        }

        if (1 > request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }           

        char    *info;
        info = get_rparam(request, 0);
        // TODO info: specific info for running_containers, crashed_containers - Exited (non 0)
        const char *answer = zbx_module_docker_socket_query("GET /info HTTP/1.0\r\n\n");
        if(strcmp(answer, "") == 0) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.info is not available at the moment - some problem with Docker's socket API");
            SET_MSG_RESULT(result, strdup("docker.info is not available at the moment - some problem with Docker's socket API"));
            return SYSINFO_RET_FAIL;
        }

        char api_value[buffer_size];
        struct zbx_json_parse jp_data = {&answer[0], &answer[strlen(answer)]};
        if (SUCCEED != zbx_json_value_by_name(&jp_data, info, api_value, buffer_size))
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s] item in the received JSON object", info);
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s] item in the received JSON object", info));
            return SYSINFO_RET_FAIL;        
        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "Finded the [%s] item in the received JSON object: %s", info, api_value);
            SET_STR_RESULT(result, zbx_strdup(NULL, api_value));
            return SYSINFO_RET_OK;        
        }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_stats                                          *
 *                                                                            *
 * Purpose: container statistics                                              *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_stats(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_stats()");
        
        if (socket_api != 1) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not avalaible");
            SET_MSG_RESULT(result, strdup("Docker's socket API is not avalaible"));
            return SYSINFO_RET_FAIL;        
        }

        if (1 >= request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }           

        char    *container, *query;
        container = get_rparam(request, 0);       
        
        size_t s_size = strlen("GET /containers//stats HTTP/1.0\r\n\n") + strlen(container);
        query = malloc(s_size);                
        zbx_strlcpy(query, "GET /containers/", s_size);
        zbx_strlcat(query, container, s_size);
        zbx_strlcat(query, "/stats HTTP/1.0\r\n\n", s_size);
        // :156 TODO stats are stream
        const char *answer = zbx_module_docker_socket_query(query);
        if(strcmp(answer, "") == 0) 
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.stats is not available at the moment - some problem with Docker's socket API");
            SET_MSG_RESULT(result, strdup("docker.stats is not available at the moment - some problem with Docker's socket API"));
            return SYSINFO_RET_FAIL;
        }

	    struct zbx_json_parse jp_data2, jp_data3, jp_row;
        char api_value[buffer_size];

        struct zbx_json_parse jp_data = {&answer[0], &answer[strlen(answer)]};
        
        if (request->nparam > 1) {
            char *param1;
            param1 = get_rparam(request, 1);
            // 1st level - plain value search
            if (SUCCEED != zbx_json_value_by_name(&jp_data, param1, api_value, buffer_size))
            {
                 // 1st level - json object search
                if (SUCCEED != zbx_json_brackets_by_name(&jp_data, param1, &jp_data2))
                {
                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s] item in the received JSON object", param1);
                    SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s] item in the received JSON object", param1));
                    return SYSINFO_RET_FAIL;
                } else {
                    // 2nd level
                    if (request->nparam > 2) 
                    {
                        char *param2, api_value2[buffer_size];
                        param2 = get_rparam(request, 2);
                        if (SUCCEED != zbx_json_value_by_name(&jp_data2, param2, api_value2, buffer_size))
                        {
                            // 2nd level - json object search
                            if (SUCCEED != zbx_json_brackets_by_name(&jp_data2, param2, &jp_data3))
                            {
                                zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s] item in the received JSON object", param1, param2);
                                SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s][%s] item in the received JSON object", param1, param2));
                                return SYSINFO_RET_FAIL;
                            } else {
                                // 3rd level
                                if (request->nparam > 3)
                                {
                                    char *param3, api_value3[buffer_size];
                                    param3 = get_rparam(request, 3);
                                    if (SUCCEED != zbx_json_value_by_name(&jp_data2, param2, api_value2, buffer_size))
                                    {
                                        zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3);
                                        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3));
                                        return SYSINFO_RET_FAIL;                                    
                                    } else {
                                        zabbix_log(LOG_LEVEL_DEBUG, "Finded the [%s][%s][%s] item in the received JSON object: %s", param1, param2, param3, api_value3);
                                        SET_STR_RESULT(result, zbx_strdup(NULL, api_value3));
                                        return SYSINFO_RET_OK;                                    
                                    }                                
                                } else {
                                    zabbix_log(LOG_LEVEL_DEBUG, "Finded the [%s][%s] item in the received JSON object: %s", param1, param2, api_value2);
                                    SET_STR_RESULT(result, zbx_strdup(NULL, api_value2));
                                    return SYSINFO_RET_OK;
                                }
                            }
                        } else {
                            zabbix_log(LOG_LEVEL_DEBUG, "Finded the [%s][%s] item in the received JSON object: %s", param1, param2, api_value2);
                            SET_STR_RESULT(result, zbx_strdup(NULL, api_value2));
                            return SYSINFO_RET_OK;
                        }                    
                    } else {
                        zabbix_log(LOG_LEVEL_WARNING, "Finded the [%s] item in the received JSON object, but it's not plain value object", param1);
                        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Finded the [%s] item in the received JSON object, but it's not plain value object", param1));
                        return SYSINFO_RET_FAIL;                  
                    }                  
                }             
            } else {
                    zabbix_log(LOG_LEVEL_DEBUG, "Finded the [%s] item in the received JSON object: %s", param1, api_value);
                    SET_STR_RESULT(result, zbx_strdup(NULL, api_value));
                    return SYSINFO_RET_OK;
            }        
        }            
        return SYSINFO_RET_OK;
}
