/*
** Zabbix module for Docker container monitoring
** Copyright (C) 2014-2016 Jan Garaj - www.monitoringartist.com
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

#include "common.h"
#include "log.h"
#include "comms.h"
#include "module.h"
#include "sysinc.h"
#include "zbxjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <grp.h>

// request parameters
#include "common/common.h"
#include "sysinfo.c"

struct inspect_result
{
   char  *value;
   int   return_code;
};

char    *m_version = "v0.5.3";
static int item_timeout = 1, buffer_size = 1024, cid_length = 66;
char    *stat_dir = NULL, *driver, *c_prefix = NULL, *c_suffix = NULL, *cpu_cgroup;
static int socket_api;
int     zbx_module_docker_discovery(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_inspect(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_cstatus(AGENT_REQUEST *request, AGENT_RESULT *result);
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
        {"docker.discovery", CF_HAVEPARAMS, zbx_module_docker_discovery,    "<parameter 1>, <parameter 2>, <parameter 3>"},
        {"docker.inspect", CF_HAVEPARAMS, zbx_module_docker_inspect, "full container id, parameter 1, <parameter 2>"},
        {"docker.cstatus", CF_HAVEPARAMS, zbx_module_docker_cstatus, "status"},
        {"docker.info", CF_HAVEPARAMS,  zbx_module_docker_info, "full container id, info"},
        {"docker.stats",CF_HAVEPARAMS,  zbx_module_docker_stats,"full container id, parameter 1, <parameter 2>, <parameter 3>"},
        {"docker.up",   CF_HAVEPARAMS,  zbx_module_docker_up,   "full container id"},
        {"docker.mem",  CF_HAVEPARAMS,  zbx_module_docker_mem,  "full container id, memory metric name"},
        {"docker.cpu",  CF_HAVEPARAMS,  zbx_module_docker_cpu,  "full container id, cpu metric name"},
        {"docker.xnet", CF_HAVEPARAMS,  zbx_module_docker_net,  "full container id, interface, network metric name"},
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
 * Purpose: quering details via Docker socket API (permission is needed)      *
 *                                                                            *
 * Return value: empty string - function failed                               *
 *               string - response from Docker's socket API                   *
 *                                                                            *
 * Notes: https://docs.docker.com/reference/api/docker_remote_api/            *
 *        echo -e "GET /containers/json?all=1 HTTP/1.0\r\n" | \               *
 *        nc -U /var/run/docker.sock                                          *
 ******************************************************************************/
const char*  zbx_module_docker_socket_query(char *query, int stream)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_socket_query()");

        struct sockaddr_un address;
        int sock, nbytes, tbytes = 0;
        size_t addr_length;
        char buffer[buffer_size+1];
        char *response_substr, *response, *empty="", *message = NULL, *temp1, *temp2;
        if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot create socket for docker's communication");
            return empty;
        }
        address.sun_family = AF_UNIX;
        zbx_strlcpy(address.sun_path, "/var/run/docker.sock", strlen("/var/run/docker.sock")+1);
        addr_length = sizeof(address.sun_family) + strlen(address.sun_path);
        if (connect(sock, (struct sockaddr *) &address, addr_length))
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot connect to standard docker's socket /var/run/docker.sock");
            return empty;
        }
        temp1 = string_replace(query, "\n", "");
        temp2 = string_replace(temp1, "\r", "");
        free(temp1);
        zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket query: %s", temp2);
        free(temp2);
        write(sock, query, strlen(query));
        message = realloc(NULL, 1);
        if (message == NULL)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Problem with allocating memory for Docker answer");
            return empty;
        }
        strcat(message, "");
        while ((nbytes = read(sock, buffer, buffer_size)) > 0 )
        {
            buffer[nbytes] = 0;
            message = realloc(message, (strlen(message) + nbytes + 1));
            if (message == NULL)
            {
                zabbix_log(LOG_LEVEL_WARNING, "Problem with allocating memory");
                return empty;
            }
            strcat(message, buffer);
            // wait only for first chunk of (stats) stream
            if (stream == 1)
            {
                if ((strstr(message, "}\n")) != NULL)
                {
                    break;
                }
            }
        }
        close(sock);
        // remove http header
        if (response_substr = strstr(message, "\r\n\r\n"))
        {
            response_substr += 4;
            size_t response_size = strlen(response_substr) + 1;
            response = malloc(response_size);
            zbx_strlcpy(response, response_substr, response_size);
        } else {
            response = malloc(5);
            zbx_strlcpy(response, "[{}]", 5);
        }
        free(message);

        temp1 = string_replace(response, "\n", "");
        temp2 = string_replace(temp1, "\r", "");
        free(temp1);
        zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket response: %s", temp2);
        free(temp2);
        return response;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_inspect_exec                                   *
 *                                                                            *
 * Purpose: container inspection                                              *
 *                                                                            *
 * Return value: inspect_result structure                                     *
 *                                                                            *
 ******************************************************************************/
struct inspect_result     zbx_module_docker_inspect_exec(AGENT_REQUEST *request)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_inspect_exec()");
        struct inspect_result iresult;

        if (socket_api != 1)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not available");
            iresult.value = zbx_strdup(NULL, "Docker's socket API is not available");
            iresult.return_code = SYSINFO_RET_FAIL;
            return iresult;
        }

        if (1 >= request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                iresult.value = zbx_strdup(NULL, "Invalid number of parameters");
                iresult.return_code = SYSINFO_RET_FAIL;
                return iresult;
        }

        char    *container, *query;
        container = get_rparam(request, 0);
        // skip leading '/' in case of human name or short container id
        if (container[0] == '/')
        {
            container++;
        }

        size_t s_size = strlen("GET /containers/ /json HTTP/1.0\r\n\n") + strlen(container);
        query = malloc(s_size);
        zbx_strlcpy(query, "GET /containers/", s_size);
        zbx_strlcat(query, container, s_size);
        zbx_strlcat(query, "/json HTTP/1.0\r\n\n", s_size);

        const char *answer = zbx_module_docker_socket_query(query, 0);
        free(query);
        if(strcmp(answer, "") == 0)
        {
            free((void*) answer);
            zabbix_log(LOG_LEVEL_DEBUG, "docker.inspect is not available at the moment - some problem with Docker's socket API");
            iresult.value = zbx_strdup(NULL, "docker.inspect is not available at the moment - some problem with Docker's socket API");
            iresult.return_code = SYSINFO_RET_FAIL;
            return iresult;
        }

	      struct zbx_json_parse jp_data2, jp_data3, jp_row;
        char api_value[buffer_size];

        struct zbx_json_parse jp_data = {&answer[0], &answer[strlen(answer)]};

        if (request->nparam > 1)
        {
            char *param1;
            param1 = get_rparam(request, 1);
            // 1st level - plain value search
            if (SUCCEED != zbx_json_value_by_name(&jp_data, param1, api_value, buffer_size))
            {
                // 1st level - json object search
                if (SUCCEED != zbx_json_brackets_by_name(&jp_data, param1, &jp_data2))
                {
                    free((void*) answer);
                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s] item in the received JSON object", param1);
                    iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s] item in the received JSON object", param1);
                    iresult.return_code = SYSINFO_RET_FAIL;
                    return iresult;
                } else {
                    // 2nd level
                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object", param1);
                    if (request->nparam > 2)
                    {
                        char *param2, api_value2[buffer_size];
                        param2 = get_rparam(request, 2);
                        if (SUCCEED != zbx_json_value_by_name(&jp_data2, param2, api_value2, buffer_size))
                        {
                            struct zbx_json_parse jp_data_array;
                            if (SUCCEED != zbx_json_brackets_by_name(&jp_data2, param2, &jp_data_array))
                            {
                                free((void*) answer);
                                zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s] item in the received JSON object", param1, param2);
                                iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s] item in the received JSON object", param1, param2);
                                iresult.return_code = SYSINFO_RET_FAIL;
                                return iresult;
                            } else {
                                if (request->nparam < 4)
                                {
                                    free((void*) answer);
                                    char *values;
                                    s_size = jp_data_array.end - jp_data_array.start + 2;
                                    values = malloc(s_size);
                                    zbx_strlcpy(values, jp_data_array.start, s_size);
                                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object: %s", param1, param2, values);
                                    iresult.value = zbx_strdup(NULL, values);
                                    iresult.return_code = SYSINFO_RET_OK;
                                    free((void*) values);
                                    return iresult;
                                } else {
                                    // find item in array - selector is param3
    	                              const char	*p_array = NULL;
                                    char *result_array, *value;
                                    while (NULL != (p_array = zbx_json_next(&jp_data_array, p_array)))
   	                              {
                                       if ((result_array = strchr(p_array+1, '"')) != NULL)
                                       {
                                           s_size = strlen(p_array+1) - strlen(result_array) + 1;
                                           value = malloc(s_size);
                                           zbx_strlcpy(value, p_array+1, s_size);
                                           zabbix_log(LOG_LEVEL_DEBUG, "Array item: %s", value);

                                           // if start of value match with array selector return without selector
                                           if (strncmp(value, get_rparam(request, 3), strlen(get_rparam(request, 3))) == 0)
                                           {
                                                // remove selector from returned value
                                                value += strlen(get_rparam(request, 3));

                                                zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s][%s] found in the received JSON object: %s", param1, param2, get_rparam(request, 3), value);
                                                iresult.value = zbx_strdup(NULL, value);
                                                iresult.return_code = SYSINFO_RET_OK;
                                                value -= strlen(get_rparam(request, 3));
                                                free((void*) value);
                                                free((void*) answer);
                                                return iresult;
                                           }
                                       } else {
                                           free((void*) value);
                                           free((void*) answer);
                                           zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object (non standard JSON array)", param1, param2, get_rparam(request, 3));
                                           iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object (non standard JSON array)", param1, param2, get_rparam(request, 3));
                                           iresult.return_code = SYSINFO_RET_FAIL;
                                           return iresult;
                                       }
                                    }
                                    free((void*) value);
                                    free((void*) answer);
                                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object (selector - param3 doesn't match any value)", param1, param2, get_rparam(request, 3));
                                    iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object (selector - param3 doesn't match any value)", param1, param2, get_rparam(request, 3));
                                    iresult.return_code = SYSINFO_RET_FAIL;
                                    return iresult;
                                 }
                            }
                        } else {
                            // 3rd level
                            zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object", param1, param2);
                            if (request->nparam > 3)
                            {
                               char *param3, api_value3[buffer_size];
                               param3 = get_rparam(request, 3);
                               struct zbx_json_parse jp_data3 = {&api_value2[0], &api_value2[strlen(api_value2)]};
                               if (SUCCEED != zbx_json_value_by_name(&jp_data3, param3, api_value3, buffer_size))
                               {
                                    free((void*) answer);
                                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3);
                                    iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3);
                                    iresult.return_code = SYSINFO_RET_FAIL;
                                    return iresult;
                                } else {
                                    free((void*) answer);
                                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s][%s] found in the received JSON object: %s", param1, param2, param3, api_value3);
                                    iresult.value = zbx_strdup(NULL, api_value3);
                                    iresult.return_code = SYSINFO_RET_OK;
                                    return iresult;
                                }
                            } else {
                                free((void*) answer);
                                zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object: %s", param1, param2, api_value2);
                                iresult.value = zbx_strdup(NULL, api_value2);
                                iresult.return_code = SYSINFO_RET_OK;
                                return iresult;
                            }
                        }
                    } else {
                        free((void*) answer);
                        zabbix_log(LOG_LEVEL_WARNING, "Item [%s] found in the received JSON object, but it's not plain value object", param1);
                        iresult.value = zbx_dsprintf(NULL, "Can find the [%s] item in the received JSON object, but it's not plain value object", param1);
                        iresult.return_code = SYSINFO_RET_FAIL;
                        return iresult;
                    }
                }
            } else {
                free((void*) answer);
                zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object: %s", param1, api_value);
                iresult.value = zbx_strdup(NULL, api_value);
                iresult.return_code = SYSINFO_RET_OK;
                return iresult;
            }
        }
        free((void*) answer);
        iresult.value = zbx_strdup(NULL, "");
        iresult.return_code = SYSINFO_RET_OK;
        return iresult;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_get_fci                                        *
 *                                                                            *
 * Purpose: get full container ID, if container name is specified             *
 *                                                                            *
 * Return value: empty string - function failed                               *
 *               string - full container ID                                   *
 ******************************************************************************/
char*  zbx_module_docker_get_fci(char *fci)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_get_fci()");

        // if first character is '/', then detect fci
        if (fci[0] != '/')
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Original full container id will be used");
            return zbx_strdup(NULL, fci);
        }

        // Docker API query - docker.inspect[fci,Id]
        zabbix_log(LOG_LEVEL_DEBUG, "Translating container name to fci by using docker.inspect");
        AGENT_REQUEST	request;
        init_request(&request);
        add_request_param(&request, zbx_strdup(NULL, fci));
        add_request_param(&request, zbx_strdup(NULL, "Id"));
        // TODO dynamic iresult
        struct inspect_result iresult;
        iresult = zbx_module_docker_inspect_exec(&request);
        free_request(&request);
        if (iresult.return_code == SYSINFO_RET_OK) {
            zabbix_log(LOG_LEVEL_DEBUG, "zbx_module_docker_inspect_exec OK: %s", iresult.value);
            return iresult.value;
        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "Default fci will be used, because zbx_module_docker_inspect_exec FAIL: %s", iresult.value);
            return zbx_strdup(NULL, fci);
        }
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_docker_dir_detect                                            *
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
        char *temp1, *temp2;
        FILE *fp;
        DIR  *dir;

        if ((fp = fopen("/proc/mounts", "r")) == NULL)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot open /proc/mounts: %s", zbx_strerror(errno));
            return SYSINFO_RET_FAIL;
        }

        while (fgets(path, 512, fp) != NULL)
        {
            if ((strstr(path, "cpuset cgroup")) != NULL)
            {
                temp1 = string_replace(path, "cgroup ", "");
                temp2 = string_replace(temp1, strstr(temp1, " "), "");
                free(temp1);
                if (stat_dir != NULL) free(stat_dir);
                stat_dir = string_replace(temp2, "cpuset", "");
                free(temp2);
                zabbix_log(LOG_LEVEL_DEBUG, "Detected docker stat directory: %s", stat_dir);

                pclose(fp);

                char *cgroup = "cpuset/";
                tdriver = drivers;
                size_t  ddir_size;
                char    *ddir;
                while (*tdriver != "")
                {
                    ddir_size = strlen(cgroup) + strlen(stat_dir) + strlen(*tdriver) + 1;
                    ddir = malloc(ddir_size);
                    zbx_strlcpy(ddir, stat_dir, ddir_size);
                    zbx_strlcat(ddir, cgroup, ddir_size);
                    zbx_strlcat(ddir, *tdriver, ddir_size);
                    if (NULL != (dir = opendir(ddir)))
                    {
                        closedir(dir);
                        free(ddir);
                        driver = *tdriver;
                        zabbix_log(LOG_LEVEL_DEBUG, "Detected used docker driver dir: %s", driver);
                        // systemd docker
                        if (strcmp(driver, "system.slice/") == 0)
                        {
                            zabbix_log(LOG_LEVEL_DEBUG, "Detected systemd docker - prefix/suffix will be used");
                            c_prefix = "docker-";
                            c_suffix = ".scope";
                        }
                        // detect cpu_cgroup - JoinController cpu,cpuacct
                        cgroup = "cpu,cpuacct/";
                        ddir_size = strlen(cgroup) + strlen(stat_dir) + 1;
                        ddir = malloc(ddir_size);
                        zbx_strlcpy(ddir, stat_dir, ddir_size);
                        zbx_strlcat(ddir, cgroup, ddir_size);
                        if (NULL != (dir = opendir(ddir)))
                        {
                            closedir(dir);
                            cpu_cgroup = "cpu,cpuacct/";
                            zabbix_log(LOG_LEVEL_DEBUG, "Detected JoinController cpu,cpuacct");
                        } else {
                            cpu_cgroup = "cpuacct/";
                        }
                        free(ddir);
                        return SYSINFO_RET_OK;
                    }
                    *tdriver++;
                    free(ddir);
                }
                driver = "";
                zabbix_log(LOG_LEVEL_DEBUG, "Cannot detect used docker driver");
                return SYSINFO_RET_FAIL;
            }
        }
        pclose(fp);
        zabbix_log(LOG_LEVEL_DEBUG, "Cannot detect docker stat directory");
        return SYSINFO_RET_FAIL;
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
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        if (stat_dir == NULL || driver == NULL)
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.up check is not available at the moment - no stat directory");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.up check is not available at the moment - no stat directory"));
                return SYSINFO_RET_FAIL;
        }

        if(cpu_cgroup == NULL)
        {
                if (zbx_docker_dir_detect() == SYSINFO_RET_FAIL)
                {
                    zabbix_log(LOG_LEVEL_DEBUG, "docker.up check is not available at the moment - no cpu_cgroup directory");
                    SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.up check is not available at the moment - no cpu_cgroup directory"));
                    return SYSINFO_RET_FAIL;
                }
        }

        container = zbx_module_docker_get_fci(get_rparam(request, 0));
        metric = get_rparam(request, 1);
        char    *stat_file = "/cpuacct.stat";
        char    *cgroup = cpu_cgroup;
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
        free(container);
        if (c_suffix != NULL)
        {
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_DEBUG, "Cannot open Docker container metric file: '%s', container doesn't run", filename);
                free(filename);
                SET_DBL_RESULT(result, 0);
                return SYSINFO_RET_OK;
        }
        zbx_fclose(file);
        zabbix_log(LOG_LEVEL_DEBUG, "Can open Docker container metric file: '%s', container is running", filename);
        free(filename);
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
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        if (stat_dir == NULL || driver == NULL)
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.dev metrics are not available at the moment - no stat directory");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.dev metrics are not available at the moment - no stat directory"));
                return SYSINFO_RET_FAIL;
        }

        container = zbx_module_docker_get_fci(get_rparam(request, 0));
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
                zabbix_log(LOG_LEVEL_ERR, "Cannot open Docker container metric file: '%s'", filename);
                free(container);
                free(stat_file);
                free(filename);
                SET_MSG_RESULT(result, strdup("Cannot open Docker container stat file, maybe CONFIG_DEBUG_BLK_CGROUP is not enabled"));
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

        free(container);
        free(stat_file);
        free(filename);
        free(metric2);

        if (SYSINFO_RET_FAIL == ret)
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in Docker container blkio file"));

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
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        if (stat_dir == NULL || driver == NULL)
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.mem metrics are not available at the moment - no stat directory");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.mem metrics are not available at the moment - no stat directory"));
                return SYSINFO_RET_FAIL;
        }

        container = zbx_module_docker_get_fci(get_rparam(request, 0));
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
                zabbix_log(LOG_LEVEL_ERR, "Cannot open Docker container metric file: '%s'", filename);
                free(container);
                free(filename);
                SET_MSG_RESULT(result, strdup("Cannot open Docker container memory.stat file"));
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

        free(container);
        free(filename);
        free(metric2);

        if (SYSINFO_RET_FAIL == ret)
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in Docker container memory.stat file"));

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
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        if (stat_dir == NULL || driver == NULL)
        {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.cpu metrics are not available at the moment - no stat directory");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.cpu metrics are not available at the moment - no stat directory"));
                return SYSINFO_RET_FAIL;
        }

        if(cpu_cgroup == NULL)
        {
                if (zbx_docker_dir_detect() == SYSINFO_RET_FAIL)
                {
                    zabbix_log(LOG_LEVEL_DEBUG, "docker.cpu check is not available at the moment - no cpu_cgroup directory");
                    SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.cpu check is not available at the moment - no cpu_cgroup directory"));
                    return SYSINFO_RET_FAIL;
                }
        }

        container = zbx_module_docker_get_fci(get_rparam(request, 0));
        metric = get_rparam(request, 1);
        char    *stat_file = "/cpuacct.stat";
        char    *cgroup = cpu_cgroup;
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
                zabbix_log(LOG_LEVEL_ERR, "Cannot open Docker container metric file: '%s'", filename);
                free(filename);
                free(container);
                SET_MSG_RESULT(result, strdup("Cannot open Docker container cpuacct.stat file"));
                return SYSINFO_RET_FAIL;
        }

        char    line[MAX_STRING_LEN];
        char    *metric2 = malloc(strlen(metric)+1);
        zbx_uint64_t cpu_num;
        memcpy(metric2, metric, strlen(metric));
        memcpy(metric2 + strlen(metric), " ", 2);
        zbx_uint64_t    value = 0;
        zabbix_log(LOG_LEVEL_DEBUG, "Looking metric %s in cpuacct.stat file", metric);
        while (NULL != fgets(line, sizeof(line), file))
        {
                if (0 != strncmp(line, metric2, strlen(metric2)))
                        continue;
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value))
                {
                        zabbix_log(LOG_LEVEL_ERR, "sscanf failed for matched metric line");
                        continue;
                }
                // normalize CPU usage by using number of online CPUs
                if (1 < (cpu_num = sysconf(_SC_NPROCESSORS_ONLN)))
                {
                    value /= cpu_num;
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Container: %s; metric: %s; value: %d", container, metric, value);
                SET_UI64_RESULT(result, value);
                ret = SYSINFO_RET_OK;
                break;
        }
        zbx_fclose(file);

        free(container);
        free(filename);
        free(metric2);

        if (SYSINFO_RET_FAIL == ret)
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in Docker container cpuacct.stat file"));

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
        char    *container, *metric, *interfacec;

        if(geteuid() != 0)
        {
            zabbix_log(LOG_LEVEL_ERR, "docker.net metrics requires root permissions");
            SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.net metrics requires root permissions"));
            return SYSINFO_RET_FAIL;
        }
        struct stat st = {0};

        if(stat("/var/run/netns", &st) == -1)
        {
            if(mkdir("/var/run/netns", 0755) != 0)
            {
                zabbix_log(LOG_LEVEL_ERR, "Cannot create /var/run/netns");
                SET_MSG_RESULT(result, strdup("Cannot create /var/run/netns"));
                return SYSINFO_RET_FAIL;
            }
        }

        container = zbx_module_docker_get_fci(get_rparam(request, 0));
        interfacec = get_rparam(request, 1);
        metric = get_rparam(request, 2);
        const char* znetns_prefix = "zabbix_module_docker_";

        size_t  filename_size = strlen("/var/run/netns/") + strlen(znetns_prefix) + strlen(container) + 2;
        char    *filename = malloc(filename_size);
        char    *netns = malloc(filename_size - strlen("/var/run/netns/") + 2);
        zbx_strlcpy(filename, "/var/run/netns/", filename_size);
        zbx_strlcat(filename, znetns_prefix, filename_size);
        zbx_strlcpy(netns, znetns_prefix, filename_size);
        zbx_strlcat(filename, container, filename_size);
        zbx_strlcat(netns, container, filename_size);

        zabbix_log(LOG_LEVEL_DEBUG, "netns file: %s", filename);
        if(access(filename, F_OK ) == -1)
        {
            // create netns
            // get first task
            char    *stat_file = "/tasks";
            char    *cgroup = "devices/";
            filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
            if (c_prefix != NULL)
            {
                filename_size += strlen(c_prefix);
            }
            if (c_suffix != NULL)
            {
                filename_size += strlen(c_suffix);
            }
            char* filename2 = malloc(filename_size);
            zbx_strlcpy(filename2, stat_dir, filename_size);
            zbx_strlcat(filename2, cgroup, filename_size);
            zbx_strlcat(filename2, driver, filename_size);
            if (c_prefix != NULL)
            {
                zbx_strlcat(filename2, c_prefix, filename_size);
            }
            zbx_strlcat(filename2, container, filename_size);
            if (c_suffix != NULL)
            {
                zbx_strlcat(filename2, c_suffix, filename_size);
            }
            zbx_strlcat(filename2, stat_file, filename_size);
            zabbix_log(LOG_LEVEL_DEBUG, "Tasks file: %s", filename2);
            FILE    *file;
            if (NULL == (file = fopen(filename2, "r")))
            {
                zabbix_log(LOG_LEVEL_ERR, "Cannot open Docker tasks file: '%s'", filename2);
                free(container);
                free(filename);
                free(netns);
                free(filename2);
                SET_MSG_RESULT(result, strdup("Cannot open Docker tasks file"));
                return SYSINFO_RET_FAIL;
            }

            char    line[MAX_STRING_LEN];
            char* first_task;
            while (NULL != fgets(line, sizeof(line), file))
            {
                first_task = string_replace(line, "\n", "");
                zabbix_log(LOG_LEVEL_DEBUG, "First task for container %s: %s", container, first_task);
                break;
            }
            zbx_fclose(file);
            free(container);
            free(filename2);

            // soft link - new netns
            filename_size = strlen("/proc//ns/net") + strlen(first_task) + 2;
            char* netns_source = malloc(filename_size);
            zbx_strlcpy(netns_source, "/proc/", filename_size);
            zbx_strlcat(netns_source, first_task, filename_size);
            free(first_task);
            zbx_strlcat(netns_source, "/ns/net", filename_size);

            // remove broken link - container has been restarted
            if(access(filename, F_OK ) == -1)
            {
                if(unlink(filename) != 0)
                {
                    zabbix_log(LOG_LEVEL_WARNING, "%s: %s", filename, zbx_strerror(errno));
                }
            }

            if(symlink(netns_source, filename) != 0) {
                zabbix_log(LOG_LEVEL_ERR, "Cannot create netns symlink: %s -> %s", filename, netns_source);
                free(netns_source);
                free(filename);
                free(netns);
                SET_MSG_RESULT(result, strdup("Cannot create netns symlink"));
                return SYSINFO_RET_FAIL;
            }
            free(netns_source);
        }

        // execute ip netns exec filename netstat -i
        FILE *fp;
        char line[MAX_STRING_LEN];
        filename_size = strlen("ip netns exec  netstat -i") + strlen(netns) + 2;
        char* command = malloc(filename_size);
        zbx_strlcpy(command, "ip netns exec ", filename_size);
        zbx_strlcat(command, netns, filename_size);
        zbx_strlcat(command, " netstat -i", filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "netns command: %s", command);

        fp = popen(command, "r");
        free(command);
        free(filename);
        free(netns);
        if (fp == NULL)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot execute netns command: %s");
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot execute netns command"));
            return SYSINFO_RET_FAIL;
        }

        /*
        [root@dev ~]# ip netns exec $CID netstat -i
        Kernel Interface table
        Iface      MTU    RX-OK RX-ERR RX-DRP RX-OVR    TX-OK TX-ERR TX-DRP TX-OVR Flg
        eth0      1500  1238410      0      0 0       1836070      0      0      0 BRU
        lo       65536  1856189      0      0 0       1856189      0      0      0 LRU
        */

        int metric_pos = 0;
        int pos = 0;
        char *pch;
        int value = 0;
        while(fgets(line, sizeof(line), fp) != NULL)
        {
            // skip first line
            if (0 == strncmp(line, "Kernel Interface table", strlen("Kernel Interface table")))
                continue;

            if(metric_pos == INT_MAX)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "Not found metric %s", metric);
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Not found net metric"));
                return SYSINFO_RET_FAIL;
            }

            // second line find position
            if(metric_pos == 0)
            {
                int found = 0;
                pch = strtok(line," ");
                while (pch != NULL)
                {
                    if(0 != strncmp(pch, metric, strlen(metric)))
                    {
                       pch = strtok(NULL, " ");
                       metric_pos++;
                       continue;
                    } else {
                       found = 1;
                       break;
                    }
                }
                if(found == 0)
                {
                    metric_pos = INT_MAX;
                    continue;
                }
            }

            // find selected interface and metric
            if(strcmp(interfacec, "all") == 0)
            {
                // find value at metric_pos and sum
                pch = strtok(line," ");
                pos = 0;
                while (pch != NULL)
                {
                    if(pos < metric_pos)
                    {
                       pch = strtok(NULL, " ");
                       pos++;
                       continue;
                    } else {
                       value += atoi(pch);
                       break;
                    }
                }

            } else {
                // skip lines which doesn't match interface
                if (0 != strncmp(line, interfacec, strlen(interfacec)))
                {
                    continue;
                }

                // find value at metric_pos
                pch = strtok(line," ");
                pos = 0;
                while (pch != NULL)
                {
                    if(pos != metric_pos)
                    {
                       pch = strtok(NULL, " ");
                       pos++;
                       continue;
                    } else {
                       value = atoi(pch);
                       zabbix_log(LOG_LEVEL_DEBUG, "found metric %s: %d", metric, value);
                       SET_UI64_RESULT(result, value);
                       pclose(fp);
                       return SYSINFO_RET_OK;
                    }
                }
            }
        }
        zabbix_log(LOG_LEVEL_DEBUG, "found metric %s: %d", metric, value);
        SET_UI64_RESULT(result, value);
        pclose(fp);
        return SYSINFO_RET_OK;
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

        const char* znetns_prefix = "zabbix_module_docker_";
        FILE            *f;
        DIR             *dir;
        struct dirent   *d;

        if (NULL == (dir = opendir("/var/run/netns/")))
        {
            zabbix_log(LOG_LEVEL_WARNING, "/var/run/netns/: %s", zbx_strerror(errno));
            return ZBX_MODULE_FAIL;
        }
        char *file = NULL;
        while (NULL != (d = readdir(dir)))
        {
            if(0 == strcmp(d->d_name, ".") || 0 == strcmp(d->d_name, ".."))
                continue;

            // delete zabbix netns
            if ((strstr(d->d_name, znetns_prefix)) != NULL)
            {
                file = NULL;
                file = zbx_dsprintf(file, "/var/run/netns/%s", d->d_name);
                if(unlink(file) != 0)
                {
                    zabbix_log(LOG_LEVEL_WARNING, "%s: %s", d->d_name, zbx_strerror(errno));
                }
            }
        }
        if(0 != closedir(dir))
        {
            zabbix_log(LOG_LEVEL_WARNING, "/var/run/netns/: %s", zbx_strerror(errno));
        }

        free(stat_dir);

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
        // I hope that zabbix user cannot be member of more than 10 groups
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
             free(groups);
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
                       free(groups);
                       return 1;
                   }
               }
        }
        free(groups);
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
        zabbix_log(LOG_LEVEL_DEBUG, "zabbix_module_docker %s, compilation time: %s %s", m_version, __DATE__, __TIME__);
        zbx_docker_dir_detect();
        // test root or docker permission
        if (geteuid() != 0 && zbx_docker_perm() != 1 )
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Additional permission of Zabbix Agent are not detected - only basic docker metrics are available");
            socket_api = 0;
        } else {
            // test Docker's socket connection
            const char *echo = zbx_module_docker_socket_query("GET /_ping HTTP/1.0\r\n\n", 0);
            if (strcmp(echo, "OK") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket works - extended docker metrics are available");
                socket_api = 1;
            } else {
                zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket doesn't work - only basic docker metrics are available");
                socket_api = 0;
            }
            free((void*) echo);
        }
        return ZBX_MODULE_OK;
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
        struct inspect_result iresult;
        iresult = zbx_module_docker_inspect_exec(request);
        if (iresult.return_code == SYSINFO_RET_OK) {
            zabbix_log(LOG_LEVEL_DEBUG, "zbx_module_docker_inspect_exec OK: %s", iresult.value);
            SET_STR_RESULT(result, iresult.value);
            return iresult.return_code;
        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "zbx_module_docker_inspect_exec FAIL: %s", iresult.value);
            SET_MSG_RESULT(result, iresult.value);
            return iresult.return_code;
        }
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_discovery_basic                                *
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
            return SYSINFO_RET_FAIL;
        }

        char            line[MAX_STRING_LEN], *p, *mpoint, *mtype, container, *containerid;
        FILE            *f;
        DIR             *dir;
        zbx_stat_t      sb;
        char            *file = NULL, scontainerid[13];
        struct dirent   *d;
        char    *cgroup = "cpuset/";
        size_t  ddir_size = strlen(cgroup) + strlen(stat_dir) + strlen(driver) + 2;
        char    *ddir = malloc(ddir_size);
        zbx_strlcpy(ddir, stat_dir, ddir_size);
        zbx_strlcat(ddir, cgroup, ddir_size);
        zbx_strlcat(ddir, driver, ddir_size);

        if (NULL == (dir = opendir(ddir)))
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s: %s", ddir, zbx_strerror(errno));
            free(ddir);
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
                if (c_prefix != NULL)
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
                zbx_json_addstring(&j, "{#SCONTAINERID}", scontainerid, ZBX_JSON_TYPE_STRING);
                zbx_json_close(&j);

        }

        if(0 != closedir(dir))
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s: %s\n", ddir, zbx_strerror(errno));
        }

        zbx_json_close(&j);

        SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

        zbx_json_free(&j);

        free(ddir);

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
        const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n", 0);
        if(strcmp(answer, "") == 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.discovery is not available at the moment - some problem with Docker's socket API");
            zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
            zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
            zbx_json_close(&j);
            SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
            zbx_json_free(&j);
            return SYSINFO_RET_FAIL;
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
                zabbix_log(LOG_LEVEL_WARNING, "Expected brackets, but zbx_json_brackets_open failed");
                continue;
            }

            if (SUCCEED != zbx_json_brackets_by_name(&jp_row, "Names", &jp_data2))
            {
                zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Names\" array in the received JSON object");
                continue;
            } else {
                // HCONTAINERID - human name - first name in array
                // TODO issue #32
                // "Names": ["/redisclient1/redis", "/redis1"],
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
                    continue;
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Parsed container id: %s", cid);

                zbx_json_addobject(&j, NULL);
                zbx_json_addstring(&j, "{#FCONTAINERID}", cid, ZBX_JSON_TYPE_STRING);
                zbx_strlcpy(scontainerid, cid, 13);
                zbx_json_addstring(&j, "{#SCONTAINERID}", scontainerid, ZBX_JSON_TYPE_STRING);

                if (request->nparam > 1) {
                    // custom item for HCONTAINERID
                    AGENT_REQUEST	request2;
                    init_request(&request2);
                    add_request_param(&request2, zbx_strdup(NULL, cid));
                    int n;
                    for(n = 0; n < request->nparam; n++)
                    {
                       add_request_param(&request2, zbx_strdup(NULL, (request)->params[n]));
                    }
                    struct inspect_result iresult;
                    iresult = zbx_module_docker_inspect_exec(&request2);
                    free_request(&request2);
                    if (iresult.return_code == SYSINFO_RET_OK) {
                        zabbix_log(LOG_LEVEL_DEBUG, "zbx_module_docker_inspect_exec OK: %s", iresult.value);
                        free(names);
                        names = iresult.value;
                        zbx_json_addstring(&j, "{#HCONTAINERID}", names, ZBX_JSON_TYPE_STRING);
                    } else {
                        zabbix_log(LOG_LEVEL_DEBUG, "Default HCONTAINERID is used, because zbx_module_docker_inspect_exec FAIL: %s", iresult.value);
                        zbx_json_addstring(&j, "{#HCONTAINERID}", names, ZBX_JSON_TYPE_STRING);
                    }
                } else {
                    zbx_json_addstring(&j, "{#HCONTAINERID}", names, ZBX_JSON_TYPE_STRING);
                }
                zbx_json_close(&j);
                free(names);
           }
        }

        zbx_json_close(&j);
        SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
        zbx_json_free(&j);
        free((void*) answer);

        return SYSINFO_RET_OK;
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
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        char    *info;
        info = get_rparam(request, 0);
        const char *answer = zbx_module_docker_socket_query("GET /info HTTP/1.0\r\n\n", 0);
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
            free((void*) answer);
            return SYSINFO_RET_FAIL;
        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object: %s", info, api_value);
            SET_STR_RESULT(result, zbx_strdup(NULL, api_value));
            free((void*) answer);
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
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        char    *container, *query;
        container = get_rparam(request, 0);
        // skip leading '/' in case of human name or short container id
        if (container[0] == '/')
        {
            container++;
        }

        size_t s_size = strlen("GET /containers/ /stats HTTP/1.0\r\n\n") + strlen(container);
        query = malloc(s_size);
        zbx_strlcpy(query, "GET /containers/", s_size);
        zbx_strlcat(query, container, s_size);
        zbx_strlcat(query, "/stats HTTP/1.0\r\n\n", s_size);
        // stats output is stream
        const char *answer = zbx_module_docker_socket_query(query, 1);
        free(query);
        if(strcmp(answer, "") == 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.stats is not available at the moment - some problem with Docker's socket API");
            SET_MSG_RESULT(result, strdup("docker.stats is not available at the moment - some problem with Docker's socket API"));
            return SYSINFO_RET_FAIL;
        }

	    struct zbx_json_parse jp_data2, jp_data3, jp_row;
        char api_value[buffer_size];

        struct zbx_json_parse jp_data = {&answer[0], &answer[strlen(answer)]};

        if (request->nparam > 1)
        {
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
                    free((void*) answer);
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
                                free((void*) answer);
                                return SYSINFO_RET_FAIL;
                            } else {
                                // 3rd level
                                if (request->nparam > 3)
                                {
                                    char *param3, api_value3[buffer_size];
                                    param3 = get_rparam(request, 3);
                                    if (SUCCEED != zbx_json_value_by_name(&jp_data3, param3, api_value3, buffer_size))
                                    {
                                        zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3);
                                        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3));
                                        free((void*) answer);
                                        return SYSINFO_RET_FAIL;
                                    } else {
                                        zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s][%s] found in the received JSON object: %s", param1, param2, param3, api_value3);
                                        SET_STR_RESULT(result, zbx_strdup(NULL, api_value3));
                                        free((void*) answer);
                                        return SYSINFO_RET_OK;
                                    }
                                } else {
                                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found the received JSON object: %s", param1, param2, api_value2);
                                    SET_STR_RESULT(result, zbx_strdup(NULL, api_value2));
                                    free((void*) answer);
                                    return SYSINFO_RET_OK;
                                }
                            }
                        } else {
                            zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object: %s", param1, param2, api_value2);
                            SET_STR_RESULT(result, zbx_strdup(NULL, api_value2));
                            free((void*) answer);
                            return SYSINFO_RET_OK;
                        }
                    } else {
                        zabbix_log(LOG_LEVEL_WARNING, "Item [%s] found in the received JSON object, but it's not plain value object", param1);
                        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Item [%s] found in the received JSON object, but it's not plain value object", param1));
                        free((void*) answer);
                        return SYSINFO_RET_FAIL;
                    }
                }
            } else {
                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object: %s", param1, api_value);
                    SET_STR_RESULT(result, zbx_strdup(NULL, api_value));
                    free((void*) answer);
                    return SYSINFO_RET_OK;
            }
        }
        free((void*) answer);
        return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_cstatus                                        *
 *                                                                            *
 * Purpose: count of containers in defined status                             *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_cstatus(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_cstatus()");

        if (socket_api != 1)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not avalaible");
            SET_MSG_RESULT(result, strdup("Docker's socket API is not avalaible"));
            return SYSINFO_RET_FAIL;
        }

        if (1 > request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        char    *state, *query;
        state = get_rparam(request, 0);

        if (strcmp(state, "Up") == 0)
        {
            // Up
            const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n", 0);
            if(strcmp(answer, "") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                return SYSINFO_RET_FAIL;
            }
            struct zbx_json_parse	jp_data;
            jp_data.start = &answer[0];
            jp_data.end = &answer[strlen(answer)];
            int count = zbx_json_count(&jp_data);
            free((void*) answer);
            zabbix_log(LOG_LEVEL_DEBUG, "Count of containers in %s status: %d", state, count);
            SET_UI64_RESULT(result, count);
            return SYSINFO_RET_OK;
        } else {
            if (strcmp(state, "Exited") == 0)
            {
                // Exited = All - Up
                // # All
                const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=1 HTTP/1.0\r\n\n", 0);
                if(strcmp(answer, "") == 0)
                {
                    zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                    SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                    return SYSINFO_RET_FAIL;
                }
                struct zbx_json_parse	jp_data;
                jp_data.start = &answer[0];
                jp_data.end = &answer[strlen(answer)];
                int count = zbx_json_count(&jp_data);
                free((void*) answer);

                // # Up
                const char *answer2 = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n", 0);
                if(strcmp(answer2, "") == 0)
                {
                    zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                    SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                    return SYSINFO_RET_FAIL;
                }
                jp_data.start = &answer2[0];
                jp_data.end = &answer2[strlen(answer2)];
                count = count - zbx_json_count(&jp_data);
                free((void*) answer2);
                zabbix_log(LOG_LEVEL_DEBUG, "Count of containers in %s status: %d", state, count);
                SET_UI64_RESULT(result, count);
                return SYSINFO_RET_OK;
            } else {
                if (strcmp(state, "Crashed") == 0)
                {
                    // Crashed - parsing Exited (x) x!=0
                    // # All
                    const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=1 HTTP/1.0\r\n\n", 0);
                    if(strcmp(answer, "") == 0)
                    {
                        zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                        SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                        return SYSINFO_RET_FAIL;
                    }

                    int count = 0;
            	    struct zbx_json_parse	jp_data2, jp_row;
            	    const char		*p = NULL;
                    char status[cid_length];
                    size_t  s_size;

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
                            zabbix_log(LOG_LEVEL_WARNING, "Expected brackets, but zbx_json_brackets_open failed");
                            continue;
                        }

                        if (SUCCEED != zbx_json_value_by_name(&jp_row, "Status", status, cid_length))
                        {
                            zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Status\" array in the received JSON object");
                            continue;
                        }
                        if(strncmp(status, "Exited (", strlen("Exited (")) == 0)
                        {
                            if(strncmp(status, "Exited (0", strlen("Exited (0")) != 0) {
                                zabbix_log(LOG_LEVEL_DEBUG, "Parsed container %s status: %s", state, status);
                                count++;
                            }
                        }
                    }

                    zabbix_log(LOG_LEVEL_DEBUG, "Count of containers in %s status: %d", state, count);
                    SET_UI64_RESULT(result, count);
                    free((void*) answer);
                    return SYSINFO_RET_OK;
                } else {
                    if (strcmp(state, "All") == 0)
                    {
                        // All
                        const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=1 HTTP/1.0\r\n\n", 0);
                        if(strcmp(answer, "") == 0)
                        {
                            zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                            SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                            return SYSINFO_RET_FAIL;
                        }
                        struct zbx_json_parse	jp_data;
                        jp_data.start = &answer[0];
                        jp_data.end = &answer[strlen(answer)];
                        int count = zbx_json_count(&jp_data);
                        free((void*) answer);
                        zabbix_log(LOG_LEVEL_DEBUG, "Count of containers in %s status: %d", state, count);
                        SET_UI64_RESULT(result, count);
                        return SYSINFO_RET_OK;
                    } else {
                        if(strcmp(state, "Paused") == 0)
                        {
                            // Paused
                            // # Up
                            const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n", 0);
                            if(strcmp(answer, "") == 0)
                            {
                                zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                                SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                                return SYSINFO_RET_FAIL;
                            }

                            int count = 0;
                    	    struct zbx_json_parse	jp_data2, jp_row;
                    	    const char		*p = NULL;
                            char status[cid_length];
                            size_t  s_size;

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
                                    zabbix_log(LOG_LEVEL_WARNING, "Expected brackets, but zbx_json_brackets_open failed");
                                    continue;
                                }

                                if (SUCCEED != zbx_json_value_by_name(&jp_row, "Status", status, cid_length))
                                {
                                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Status\" array in the received JSON object");
                                    continue;
                                }
                                if(strstr(status, "(Paused)") != NULL)
                                {
                                    zabbix_log(LOG_LEVEL_DEBUG, "Parsed container %s status: %s", state, status);
                                    count++;
                                }
                            }

                            free((void*) answer);
                            zabbix_log(LOG_LEVEL_DEBUG, "Count of containers in %s status: %d", state, count);
                            SET_UI64_RESULT(result, count);
                            return SYSINFO_RET_OK;

                        } else {
                            zabbix_log(LOG_LEVEL_WARNING, "Not defined container status: %s",  state);
                            SET_MSG_RESULT(result, strdup("Not defined container status"));
                            return SYSINFO_RET_FAIL;
                        }
                    }
                }
            }
        }
        return SYSINFO_RET_FAIL;
}
