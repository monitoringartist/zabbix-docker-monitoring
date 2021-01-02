/*
** Zabbix module for Docker container monitoring
** Copyright (C) 2014-2017 Jan Garaj - www.monitoringartist.com
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
#include "zbxregexp.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <grp.h>
#include <jansson.h>

#ifndef ZBX_MODULE_API_VERSION
#       define ZBX_MODULE_API_VERSION   ZBX_MODULE_API_VERSION_ONE
#endif

struct inspect_result
{
   char  *value;
   int   return_code;
};
struct timeval stimeout = { .tv_sec = 30, .tv_usec = 0 };

char    *m_version = "v0.7.0";
char    *stat_dir = NULL, *driver, *c_prefix = NULL, *c_suffix = NULL, *cpu_cgroup = NULL, *hostname = 0;
static int item_timeout = 1, buffer_size = 1024, cid_length = 66, socket_api;
int     zbx_module_docker_discovery(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_port_discovery(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_inspect(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_cstatus(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_istatus(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_vstatus(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_info(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_stats(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_up(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_mem(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_cpu(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_net(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_dev(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_modver(AGENT_REQUEST *request, AGENT_RESULT *result);

static ZBX_METRIC keys[] =
/*      KEY                     FLAG            FUNCTION                TEST PARAMETERS */
{
        {"docker.discovery", CF_HAVEPARAMS, zbx_module_docker_discovery,    "<parameter 1>, <parameter 2>, <parameter 3>"},
        {"docker.port.discovery", CF_HAVEPARAMS, zbx_module_docker_port_discovery, "full container id, <protocol>"},
        {"docker.inspect", CF_HAVEPARAMS, zbx_module_docker_inspect, "full container id, parameter 1, <parameter 2>"},
        {"docker.cstatus", CF_HAVEPARAMS, zbx_module_docker_cstatus, "status"},
        {"docker.istatus", CF_HAVEPARAMS, zbx_module_docker_istatus, "status"},
        {"docker.vstatus", CF_HAVEPARAMS, zbx_module_docker_vstatus, "status"},
        {"docker.info", CF_HAVEPARAMS,  zbx_module_docker_info, "full container id, info"},
        {"docker.stats",CF_HAVEPARAMS,  zbx_module_docker_stats,"full container id, parameter 1, <parameter 2>, <parameter 3>"},
        {"docker.up",   CF_HAVEPARAMS,  zbx_module_docker_up,   "full container id"},
        {"docker.mem",  CF_HAVEPARAMS,  zbx_module_docker_mem,  "full container id, memory metric name"},
        {"docker.cpu",  CF_HAVEPARAMS,  zbx_module_docker_cpu,  "full container id, cpu metric name"},
        {"docker.xnet", CF_HAVEPARAMS,  zbx_module_docker_net,  "full container id, interface, network metric name"},
        {"docker.dev",  CF_HAVEPARAMS,  zbx_module_docker_dev,  "full container id, blkio file, blkio metric name"},
        {"docker.modver",  CF_HAVEPARAMS,  zbx_module_docker_modver},
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
        stimeout.tv_sec = item_timeout = timeout;
        stimeout.tv_usec = 0;
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
 * Purpose: querying details via Docker socket API (permission is needed)      *
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
        int sock, nbytes;
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

        // socket input/output timeout
        if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&stimeout, sizeof(stimeout)) < 0)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot set SO_RCVTIMEO socket timeout: %ld seconds", stimeout.tv_sec);
        }
        if (setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&stimeout, sizeof(stimeout)) < 0)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot set SO_SNDTIMEO socket timeout: %ld seconds", stimeout.tv_sec);
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
        if ((response_substr = strstr(message, "\r\n\r\n")))
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
 * Function: zbx_docker_api_detect                                            *
 *                                                                            *
 * Purpose: the function is trying to test Docker API availability            *
 *                                                                            *
 * Return value: 0 - API not detected                                         *
 *               1 - API detected                                             *
 *                                                                            *
 ******************************************************************************/
int     zbx_docker_api_detect()
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_docker_api_detect()");
        // test root or docker permission
        if (geteuid() != 0 && zbx_docker_perm() != 1 )
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Additional permission of Zabbix Agent are not detected - only basic docker metrics are available");
            socket_api = 0;
            return socket_api;
        } else {
            // test Docker's socket connection
            const char *echo = zbx_module_docker_socket_query("GET /_ping HTTP/1.0\r\n\n", 0);
            if (strcmp(echo, "OK") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket works - extended docker metrics are available");
                socket_api = 1;
                return socket_api;
            } else {
                zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket doesn't work - only basic docker metrics are available");
                socket_api = 0;
                return socket_api;
            }
            free((void*) echo);
        }
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
struct inspect_result     zbx_module_docker_inspect_exec(const char *container, const char *param1, const char *param2, const char *param3)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_inspect_exec()");
        struct inspect_result iresult;

        if (socket_api == 0 && zbx_docker_api_detect() == 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not available");
            iresult.value = zbx_strdup(NULL, "Docker's socket API is not available");
            iresult.return_code = SYSINFO_RET_FAIL;
            return iresult;
        }

        if (container == NULL || param1 == NULL)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  (container != NULL) + (param1 != NULL));
                iresult.value = zbx_strdup(NULL, "Invalid number of parameters");
                iresult.return_code = SYSINFO_RET_FAIL;
                return iresult;
        }

        char    *query;
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
            zabbix_log(LOG_LEVEL_DEBUG, "docker.inspect is not available at the moment - some problem with Docker's socket API");
            iresult.value = zbx_strdup(NULL, "docker.inspect is not available at the moment - some problem with Docker's socket API");
            iresult.return_code = SYSINFO_RET_FAIL;
            return iresult;
        }

        json_t *jp_data = json_loads(answer, 0, NULL);

        if (param1 != NULL)
        {
            // 1st level - plain value search
            json_t *jp_data2 = json_object_get(jp_data, param1);
            if (!json_is_string(jp_data2))
            {
                // 1st level - json object search
                if (NULL == jp_data2)
                {
                    free((void*) answer);
                    json_decref(jp_data);
                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s] item in the received JSON object", param1);
                    iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s] item in the received JSON object", param1);
                    iresult.return_code = SYSINFO_RET_FAIL;
                    return iresult;
                } else {
                    // 2nd level
                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object", param1);
                    if (param2 != NULL)
                    {
                        json_t *jp_data3 = json_object_get(jp_data2, param2);
                        if (!json_is_string(jp_data3))
                        {
                            if (NULL == jp_data3)
                            {
                                free((void*) answer);
                                json_decref(jp_data);
                                zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s] item in the received JSON object", param1, param2);
                                iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s] item in the received JSON object", param1, param2);
                                iresult.return_code = SYSINFO_RET_FAIL;
                                return iresult;
                            } else {
                                if (param3 == NULL)
                                {
                                    free((void*) answer);
                                    char *values = json_dumps(jp_data3, 0);
                                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object: %s", param1, param2, values);
                                    iresult.value = values;
                                    iresult.return_code = SYSINFO_RET_OK;
                                    return iresult;
                                } else {
                                    // find item in array - selector is param3
                                    json_t *element;
                                    size_t index;
                                    json_array_foreach(jp_data3, index, element)
   	                                {
                                       if (json_is_string(element))
                                       {
                                           const char *value = json_string_value(element);
                                           zabbix_log(LOG_LEVEL_DEBUG, "Array item: %s", value);

                                           const char    *arg4 = param3;
                                           char *tofree, *string, *selector;
                                           tofree = string = strdup(arg4);

                                           // hacking: Marathon MESOS_TASK_ID, Chronos - mesos_task_id
                                           // docker.inspect[cid,Config,Env,MESOS_TASK_ID=|mesos_task_id=]
                                           while ((selector = strsep(&string, "|")) != NULL) {
                                               // if start of value match with array selector return without selector
                                               if (strncmp(value, selector, strlen(selector)) == 0)
                                               {
                                                    // remove selector from returned value
                                                    value += strlen(selector);

                                                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s][%s] found in the received JSON object: %s", param1, param2, selector, value);
                                                    iresult.value = zbx_strdup(NULL, value);
                                                    iresult.return_code = SYSINFO_RET_OK;
                                                    free((void*) answer);
                                                    free((void*) tofree);
                                                    return iresult;
                                               }
                                           }
                                           free(tofree);
                                       } else {
                                           free((void*) answer);
                                           zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object (non standard JSON array)", param1, param2, param3);
                                           iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object (non standard JSON array)", param1, param2, param3);
                                           iresult.return_code = SYSINFO_RET_FAIL;
                                           return iresult;
                                       }
                                    }
                                    free((void*) answer);
                                    json_decref(jp_data);
                                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object (selector - param3 doesn't match any value)", param1, param2, param3);
                                    iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object (selector - param3 doesn't match any value)", param1, param2, param3);
                                    iresult.return_code = SYSINFO_RET_FAIL;
                                    return iresult;
                                }
                            }
                        } else {
                            // 3rd level
                            zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object", param1, param2);
                            if (param3 != NULL)
                            {
                                json_t *jp_data4 = json_object_get(jp_data3, param3);
                                if (!json_is_string(jp_data4))
                                {
                                    free((void*) answer);
                                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3);
                                    iresult.value = zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3);
                                    iresult.return_code = SYSINFO_RET_FAIL;
                                    return iresult;
                                } else {
                                    free((void*) answer);
                                    const char *api_value3 = json_string_value(jp_data4);
                                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s][%s] found in the received JSON object: %s", param1, param2, param3, api_value3);
                                    iresult.value = zbx_strdup(NULL, api_value3);
                                    iresult.return_code = SYSINFO_RET_OK;
                                    json_decref(jp_data);
                                    return iresult;
                                }
                            } else {
                                free((void*) answer);
                                const char *api_value2 = json_string_value(jp_data3);
                                zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object: %s", param1, param2, api_value2);
                                iresult.value = zbx_strdup(NULL, api_value2);
                                iresult.return_code = SYSINFO_RET_OK;
                                json_decref(jp_data);
                                return iresult;
                            }
                        }
                    } else {
                        free((void*) answer);
                        json_decref(jp_data);
                        zabbix_log(LOG_LEVEL_WARNING, "Item [%s] found in the received JSON object, but it's not plain value object", param1);
                        iresult.value = zbx_dsprintf(NULL, "Can find the [%s] item in the received JSON object, but it's not plain value object", param1);
                        iresult.return_code = SYSINFO_RET_FAIL;
                        return iresult;
                    }
                }
            } else {
                free((void*) answer);
                const char *api_value = json_string_value(jp_data2);
                zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object: %s", param1, api_value);
                iresult.value = zbx_strdup(NULL, api_value);
                iresult.return_code = SYSINFO_RET_OK;
                json_decref(jp_data);
                return iresult;
            }
        }
        free((void*) answer);
        json_decref(jp_data);
        iresult.value = zbx_strdup(NULL, "");
        iresult.return_code = SYSINFO_RET_OK;
        return iresult;
}

/******************************************************************************
 *                                                                            *
 * Function:  zbx_module_docker_port_discovery                                *
 *                                                                            *
 * Purpose: published container port discovery                                *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int zbx_module_docker_port_discovery(AGENT_REQUEST * request, AGENT_RESULT * result) {
  zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_discover_ports()");

  if (2 < request->nparam || 0 == request->nparam) {
    zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d", request->nparam);
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters"));
    return SYSINFO_RET_FAIL;
  }

  char * container;
  container = get_rparam(request, 0);

  struct inspect_result iresult;
  iresult = zbx_module_docker_inspect_exec(container, "HostConfig", "PortBindings", NULL);
  if (iresult.return_code == SYSINFO_RET_FAIL) {
    zabbix_log(LOG_LEVEL_DEBUG, "zbx_module_docker_inspect_exec FAIL: %s", iresult.value);
    return SYSINFO_RET_FAIL;
  }

  // now parse the value we get back from zbx_module_docker_inspect_exec()
  json_t *jp_data = json_loads(iresult.value, 0, NULL);

  char container_port[6];
  int port_len;
  const char
    *p2 = NULL,
    *proto = NULL,
    *request_proto = NULL;
  json_t *jp_obj;

  json_t *a = json_array();

  if (1 < request->nparam) {
    request_proto = get_rparam(request, 1);
  }
  const char *buf;
  json_t *p;
  json_object_foreach(jp_data, buf, p) {
    // Split "12345/tcp" specifier
    proto = strstr(buf, "/") + 1;
    port_len = proto - buf;
    zbx_strlcpy(container_port, buf, port_len);

    if (NULL != request_proto && '\0' != *request_proto && 0 != strcmp(request_proto, proto)) {
      continue;
    }

    // Open list of HostIP/HostPort dicts
    json_t *jp_obj = json_array_get(p, 0);
    if (NULL == jp_obj) {
      //zabbix_log(LOG_LEVEL_DEBUG, "zbx_json_brackets_open FAIL: %s", zbx_json_strerror());
      continue;
    }

    // Lookup HostPort value
    const char *host_port = json_string_value(json_object_get(jp_obj, "HostPort"));
    if (NULL == host_port) {
      //zabbix_log(LOG_LEVEL_DEBUG, "zbx_json_value_by_name FAIL: %s", zbx_json_strerror());
      continue;
    }

    json_t *o = json_object();
    json_object_set_new(o, "{#HOSTPORT}", json_string(host_port));
    json_object_set_new(o, "{#CONTAINERPORT}", json_string(container_port));
    json_object_set_new(o, "{#PROTO}", json_string(proto));
    json_array_append_new(a, o);

  }
  json_t *j = json_object();
  json_object_set_new(j, "data", a);
  SET_STR_RESULT(result, json_dumps(j, 0));
  json_decref(j);

  return SYSINFO_RET_OK;
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
        // TODO dynamic iresult
        struct inspect_result iresult;
        iresult = zbx_module_docker_inspect_exec(fci, "Id", NULL, NULL);
        if (iresult.return_code == SYSINFO_RET_OK) {
            zabbix_log(LOG_LEVEL_DEBUG, "zbx_module_docker_inspect_exec OK: %s", iresult.value);
            return iresult.value;
        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "Default fci will be used, because zbx_module_docker_inspect_exec FAIL: %s", iresult.value);
            free(iresult.value);
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
            NULL
        }, **tdriver;
        char path[512];
        const char *mounts_regex = "^[^[:blank:]]+[[:blank:]]+(/[^[:blank:]]+/)[^[:blank:]]+[[:blank:]]+cgroup[[:blank:]]+.*$";
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
                if (SUCCEED != zbx_regexp_sub(path, mounts_regex, "\\1", &stat_dir) || NULL == stat_dir)
                {
                    continue;
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Detected docker stat directory: %s", stat_dir);

                pclose(fp);

                char *cgroup = "cpuset/";
                tdriver = drivers;
                size_t  ddir_size;
                char    *ddir;
                while (*tdriver != NULL)
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
                    tdriver++;
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
        char    *container;

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
        char    *stat_file = "/cpuacct.stat";
        char    *cgroup = cpu_cgroup;
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        if (strstr(container, ".") == NULL) {
            if (c_prefix != NULL)
            {
                filename_size += strlen(c_prefix);
            }
            if (c_suffix != NULL)
            {
                filename_size += strlen(c_suffix);
            }
        }
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (strstr(container, ".") == NULL && c_prefix != NULL)
        {
            zbx_strlcat(filename, c_prefix, filename_size);
        }
        zbx_strlcat(filename, container, filename_size);
        if (strstr(container, ".") == NULL && c_suffix != NULL)
        {
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        free(container);
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_DEBUG, "Cannot open metric file: '%s', container doesn't run", filename);
                free(filename);
                SET_UI64_RESULT(result, 0);
                return SYSINFO_RET_OK;
        }
        zbx_fclose(file);
        zabbix_log(LOG_LEVEL_DEBUG, "Can open metric file: '%s', container is running", filename);
        free(filename);
        SET_UI64_RESULT(result, 1);
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
        char    *arg2 = get_rparam(request, 1);
        char    *stat_file = malloc(strlen(arg2) + 2);
        zbx_strlcpy(stat_file, "/", strlen(arg2) + 2);
        zbx_strlcat(stat_file, get_rparam(request, 1), strlen(arg2) + 2);
        metric = get_rparam(request, 2);

        char    *cgroup = "blkio/";
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        if (strstr(container, ".") == NULL) {
            if (c_prefix != NULL)
            {
                filename_size += strlen(c_prefix);
            }
            if (c_suffix != NULL)
            {
                filename_size += strlen(c_suffix);
            }
        }
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (strstr(container, ".") == NULL && c_prefix != NULL)
        {
            zbx_strlcat(filename, c_prefix, filename_size);
        }
        zbx_strlcat(filename, container, filename_size);
        if (strstr(container, ".") == NULL && c_suffix != NULL)
        {
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_ERR, "Cannot open metric file: '%s'", filename);
                free(container);
                free(stat_file);
                free(filename);
                SET_MSG_RESULT(result, strdup("Cannot open stat file, maybe CONFIG_DEBUG_BLK_CGROUP is not enabled"));
                return SYSINFO_RET_FAIL;
        }

        char    line[MAX_STRING_LEN];
        char    *metric2 = malloc(strlen(metric)+3);
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
                zabbix_log(LOG_LEVEL_DEBUG, "Id: %s; stat file: %s, metric: %s; value: %lu", container, stat_file, metric, value);
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
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in blkio file"));

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

        if (strstr(container, ".") == NULL) {
            if (c_prefix != NULL)
            {
                filename_size += strlen(c_prefix);
            }
            if (c_suffix != NULL)
            {
                filename_size += strlen(c_suffix);
            }
        }

        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (strstr(container, ".") == NULL && c_prefix != NULL)
        {
            zbx_strlcat(filename, c_prefix, filename_size);
        }
        zbx_strlcat(filename, container, filename_size);
        if (strstr(container, ".") == NULL && c_suffix != NULL)
        {
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_ERR, "Cannot open metric file: '%s'", filename);
                free(container);
                free(filename);
                SET_MSG_RESULT(result, strdup("Cannot open memory.stat file"));
                return SYSINFO_RET_FAIL;
        }

        char    line[MAX_STRING_LEN];
        char    *metric2 = malloc(strlen(metric)+3);
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
                zabbix_log(LOG_LEVEL_DEBUG, "Id: %s; metric: %s; value: %lu", container, metric, value);
                SET_UI64_RESULT(result, value);
                ret = SYSINFO_RET_OK;
                break;
        }
        zbx_fclose(file);

        free(container);
        free(filename);
        free(metric2);

        if (SYSINFO_RET_FAIL == ret)
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in memory.stat file"));

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
        char    *cgroup = NULL, *stat_file = NULL;
        if(strcmp(metric, "user") == 0 || strcmp(metric, "system") == 0 || strcmp(metric, "total") == 0) {
            stat_file = "/cpuacct.stat";
            cgroup = cpu_cgroup;
        } else {
            stat_file = "/cpu.stat";
            if (strchr(cpu_cgroup, ',') != NULL) {
                cgroup = cpu_cgroup;
            } else {
                cgroup = "cpu/";
            }
        }
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        if (strstr(container, ".") == NULL) {
            if (c_prefix != NULL)
            {
                filename_size += strlen(c_prefix);
            }
            if (c_suffix != NULL)
            {
                filename_size += strlen(c_suffix);
            }
        }
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        if (strstr(container, ".") == NULL && c_prefix != NULL)
        {
            zbx_strlcat(filename, c_prefix, filename_size);
        }
        zbx_strlcat(filename, container, filename_size);
        if (strstr(container, ".") == NULL && c_suffix != NULL)
        {
            zbx_strlcat(filename, c_suffix, filename_size);
        }
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_ERR, "Cannot open metric file: '%s'", filename);
                free(filename);
                free(container);
                SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open %s file", ++stat_file));
                return SYSINFO_RET_FAIL;
        }

        char    line[MAX_STRING_LEN];
        char    *metric2 = malloc(strlen(metric)+3);
        zbx_uint64_t cpu_num;
        memcpy(metric2, metric, strlen(metric));
        memcpy(metric2 + strlen(metric), " ", 2);
        zbx_uint64_t    value = 0;
        zbx_uint64_t    result_value = 0;
        zabbix_log(LOG_LEVEL_DEBUG, "Looking metric %s in cpuacct.stat/cpu.stat file", metric);
        while (NULL != fgets(line, sizeof(line), file))
        {
                if (0 != strcmp("total", metric) && 0 != strncmp(line, metric2, strlen(metric2))) {
                        continue;
                }
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value))
                {
                        zabbix_log(LOG_LEVEL_ERR, "sscanf failed for matched metric line");
                        continue;
                }
                result_value += value;
                ret = SYSINFO_RET_OK;
        }

        zbx_fclose(file);
        free(filename);
        free(metric2);

        if (SYSINFO_RET_FAIL == ret) {
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with requested metric in cpuacct.stat/cpu.stat file"));
        } else {
                // normalize CPU usage by using number of online CPUs - only tick metrics
                if ((strcmp(metric, "user") == 0 || strcmp(metric, "system") == 0 || strcmp(metric, "total") == 0) && (1 < (cpu_num = sysconf(_SC_NPROCESSORS_ONLN))))
                {
                        result_value /= cpu_num;
                }

                zabbix_log(LOG_LEVEL_DEBUG, "Id: %s; metric: %s; value: %lu", container, metric, result_value);
                SET_UI64_RESULT(result, result_value);
        }
        free(container);

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
            zabbix_log(LOG_LEVEL_WARNING, "Cannot execute netns command");
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
        DIR             *dir;
        struct dirent   *d;

        if (NULL == (dir = opendir("/var/run/netns")))
        {
            zabbix_log(LOG_LEVEL_DEBUG, "/var/run/netns: %s", zbx_strerror(errno));
            return ZBX_MODULE_OK;
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
        zbx_docker_api_detect();
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
        iresult = zbx_module_docker_inspect_exec(get_rparam(request, 0), get_rparam(request, 1), get_rparam(request, 2), get_rparam(request, 3));
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

        if(stat_dir == NULL && zbx_docker_dir_detect() == SYSINFO_RET_FAIL)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.discovery is not available at the moment - no stat directory - empty discovery");
            json_t *j = json_object();
            json_object_set_new(j, "data", json_array());
            SET_STR_RESULT(result, json_dumps(j, 0));
            json_decref(j);
            return SYSINFO_RET_FAIL;
        }

        DIR             *dir;
        zbx_stat_t      sb;
        char            *file = NULL, *containerid, scontainerid[13];
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

        json_t *a = json_array();

        size_t hostname_len = 128;
        while (1) {
            char* realloc_hostname = realloc(hostname, hostname_len);
            if (realloc_hostname == 0) {
                free(hostname);
            }
            hostname = realloc_hostname;
            hostname[hostname_len-1] = 0;
            if (gethostname(hostname, hostname_len-1) == 0) {
                size_t count = strlen(hostname);
                if (count < hostname_len-2) {
                    break;
                }
            }
            hostname_len *= 2;
        }

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

                json_t *o = json_object();
                json_object_set_new(o, "{#FCONTAINERID}", json_string(containerid));
                zbx_strlcpy(scontainerid, containerid, 13);
                json_object_set_new(o, "{#HCONTAINERID}", json_string(scontainerid));
                json_object_set_new(o, "{#SCONTAINERID}", json_string(scontainerid));
                json_object_set_new(o, "{#SYSTEM.HOSTNAME}", json_string(hostname));
                json_array_append_new(a, o);

        }

        if(0 != closedir(dir))
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s: %s\n", ddir, zbx_strerror(errno));
        }

        json_t *j = json_object();
        json_object_set_new(j, "data", a);

        SET_STR_RESULT(result, json_dumps(j, 0));

        json_decref(j);

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

        const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n", 0);
        if(strcmp(answer, "") == 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.discovery is not available at the moment - some problem with Docker's socket API");
            json_t *j = json_object();
            json_object_set_new(j, "data", json_array());
            SET_STR_RESULT(result, json_dumps(j, 0));
            json_decref(j);
            return SYSINFO_RET_FAIL;
        }

        // empty response
        if (strcmp(answer, "[]\n") == 0) {
            json_t *j = json_object();
            json_object_set_new(j, "data", json_array());
            SET_STR_RESULT(result, json_dumps(j, 0));
            json_decref(j);
            free((void*) answer);
            return SYSINFO_RET_OK;
        }

        json_t *jp_row;
        size_t index;
        char scontainerid[13];
        size_t  s_size;

        size_t hostname_len = 128;
        while (1) {
            char* realloc_hostname = realloc(hostname, hostname_len);
            if (realloc_hostname == 0) {
                free(hostname);
            }
            hostname = realloc_hostname;
            hostname[hostname_len-1] = 0;
            if (gethostname(hostname, hostname_len-1) == 0) {
                size_t count = strlen(hostname);
                if (count < hostname_len-2) {
                    break;
                }
            }
            hostname_len *= 2;
        }

        json_t *a = json_array();

        // skipped zbx_json_brackets_open and zbx_json_brackets_by_name
    	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
    	/*         ^-------------------------------------------^  */
        json_t *jp_data = json_loads(answer, 0, NULL);
    	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
    	/*          ^                                             */
        json_array_foreach(jp_data, index, jp_row)
    	{
    		/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
    		/*          ^------------------^                          */
    		if (!json_is_object(jp_row))
            {
                zabbix_log(LOG_LEVEL_WARNING, "Array element is not an object");
                continue;
            }

            json_t *jp_data2 = json_object_get(jp_row, "Names");

            if (NULL == jp_data2)
            {
                zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Names\" array in the received JSON object");
                continue;
            } else {
                // HCONTAINERID
                // "Names": ["/redisclient1/redis", "/redis1"],
                const char *names;
                if (json_is_array(jp_data2) || json_array_size(jp_data2) > 0)
                {
                    json_t *element;
                    size_t index;
                    json_array_foreach(jp_data2, index, element) {
                        names = json_string_value(element) + 1;

                        if (strstr(names, "/") != NULL)
                            continue; // linked name - search another one

                        break;
                    }

                    names = zbx_strdup(NULL, names);
                } else {
                    names = json_dumps(jp_data2, 0);
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Parsed container name: %s", names);

                // FCONTAINERID - full container id
                const char *cid = json_string_value(json_object_get(jp_row, "Id"));
                if (NULL == cid)
                {
                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the \"Id\" array in the received JSON object");
                    continue;
                }
                zabbix_log(LOG_LEVEL_DEBUG, "Parsed container id: %s", cid);

                json_t *o = json_object();
                json_object_set_new(o, "{#FCONTAINERID}", json_string(cid));
                zbx_strlcpy(scontainerid, cid, 13);
                json_object_set_new(o, "{#SCONTAINERID}", json_string(scontainerid));
                json_object_set_new(o, "{#SYSTEM.HOSTNAME}", json_string(hostname));

                if (request->nparam > 1) {
                    // custom item for HCONTAINERID
                    struct inspect_result iresult;
                    iresult = zbx_module_docker_inspect_exec(cid, get_rparam(request, 0), get_rparam(request, 1), get_rparam(request, 2));
                    if (iresult.return_code == SYSINFO_RET_OK) {
                        zabbix_log(LOG_LEVEL_DEBUG, "zbx_module_docker_inspect_exec OK: %s", iresult.value);
                        free((void *)names);
                        names = iresult.value;
                        json_object_set_new(o, "{#HCONTAINERID}", json_string(names));
                    } else {
                        zabbix_log(LOG_LEVEL_DEBUG, "Default HCONTAINERID is used, because zbx_module_docker_inspect_exec FAIL: %s", iresult.value);
                        json_object_set_new(o, "{#HCONTAINERID}", json_string(names));
                    }
                } else {
                    json_object_set_new(o, "{#HCONTAINERID}", json_string(names));
                }
                json_array_append_new(a, o);
                free((void *)names);
            }

            // TODO expose labels in discovery
/*
            if (SUCCEED == zbx_json_brackets_by_name(&jp_row, "Labels", &jp_data2))
            {
                zabbix_log(LOG_LEVEL_DEBUG, "Parsing \"Labels\" array in the received JSON object");
                // {"label":"description", "label2":"description2"}
                if (SUCCEED == zbx_json_brackets_open(p, &jp_data2))
                {
                    zabbix_log(LOG_LEVEL_DEBUG, "IN BRACKET OPEN: %s", jp_data2);

                        char			buf[1024];
                        p = NULL
                    	while (NULL != (p = zbx_json_pair_next(jp_row, p, buf, sizeof(buf))) && SUCCEED == ret)
                    	{
                        }

                    p = NULL
                    while (NULL != (p = zbx_json_next_value_dyn(&jp_row, p, &buf, &buf_alloc, NULL)))
                	{
                		if (NULL == (fields[fields_count++] = DBget_field(table, buf)))
                		{
                			*error = zbx_dsprintf(*error, "invalid field name \"%s.%s\"", table->table, buf);
                			goto out;
                		}
                	}

                    while (strchr(&jp_row, ':') != NULL) {
                         // labels listing
                         zabbix_log(LOG_LEVEL_DEBUG, "Labels available");

                         jp_data2.start = jp_data2.start + s_size + 3;
                         if ((result = strchr(jp_data2.start, '"')) != NULL)
                         {
                            s_size = strlen(jp_data2.start) - strlen(result) + 1;
                            names = realloc(names, s_size);
                            zbx_strlcpy(names, jp_data2.start, s_size);
                         } else {
                            zabbix_log(LOG_LEVEL_DEBUG, "This should never happen - \" not found in non-first Names values");
                            break;
                         }

                    }

                }
            }
*/
        }

        json_t *j = json_object();
        json_object_set_new(j, "data", a);
        SET_STR_RESULT(result, json_dumps(j, 0));
        json_decref(j);
        free((void*) answer);
        json_decref(jp_data);

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
        if (socket_api == 0 && zbx_docker_api_detect() == 0)
        {
            return zbx_module_docker_discovery_basic(request, result);
        } else {
            return zbx_module_docker_discovery_extended(request, result);
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

        if (socket_api == 0 && zbx_docker_api_detect() == 0)
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

        json_t *jp_data = json_loads(answer, 0, NULL);
        const char* api_value = json_string_value(json_object_get(jp_data, info));
        if (NULL == api_value)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s] item in the received JSON object", info);
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s] item in the received JSON object", info));
            free((void*) answer);
            json_decref(jp_data);
            return SYSINFO_RET_FAIL;
        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object: %s", info, api_value);
            SET_STR_RESULT(result, zbx_strdup(NULL, api_value));
            free((void*) answer);
            json_decref(jp_data);
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

        if (socket_api == 0 && zbx_docker_api_detect() == 0)
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

        json_t *jp_data = json_loads(answer, 0, NULL);

        if (request->nparam > 1)
        {
            char *param1;
            param1 = get_rparam(request, 1);
            // 1st level - plain value search
            json_t *jp_data2 = json_object_get(jp_data, param1);
            if (!json_is_string(jp_data2))
            {
                 // 1st level - json object search
                if (NULL == jp_data2)
                {
                    zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s] item in the received JSON object", param1);
                    SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s] item in the received JSON object", param1));
                    free((void*) answer);
                    json_decref(jp_data);
                    return SYSINFO_RET_FAIL;
                } else {
                    // 2nd level
                    if (request->nparam > 2)
                    {
                        char *param2;
                        param2 = get_rparam(request, 2);
                        json_t *jp_data3 = json_object_get(jp_data2, param2);
                        if (!json_is_string(jp_data3))
                        {
                            // 2nd level - json object search
                            if (NULL == jp_data3)
                            {
                                zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s] item in the received JSON object", param1, param2);
                                SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s][%s] item in the received JSON object", param1, param2));
                                free((void*) answer);
                                json_decref(jp_data);
                                return SYSINFO_RET_FAIL;
                            } else {
                                // 3rd level
                                if (request->nparam > 3)
                                {
                                    char *param3;
                                    param3 = get_rparam(request, 3);
                                    const char *api_value3 = json_string_value(json_object_get(jp_data3, param3));
                                    if (NULL == api_value3)
                                    {
                                        zabbix_log(LOG_LEVEL_WARNING, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3);
                                        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find the [%s][%s][%s] item in the received JSON object", param1, param2, param3));
                                        free((void*) answer);
                                        json_decref(jp_data);
                                        return SYSINFO_RET_FAIL;
                                    } else {
                                        zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s][%s] found in the received JSON object: %s", param1, param2, param3, api_value3);
                                        SET_STR_RESULT(result, zbx_strdup(NULL, api_value3));
                                        free((void*) answer);
                                        json_decref(jp_data);
                                        return SYSINFO_RET_OK;
                                    }
                                } else {
                                    const char *api_value2 = json_dumps(jp_data3, 0);
                                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found the received JSON object: %s", param1, param2, api_value2);
                                    SET_STR_RESULT(result, zbx_strdup(NULL, api_value2));
                                    free((void*) answer);
                                    json_decref(jp_data);
                                    return SYSINFO_RET_OK;
                                }
                            }
                        } else {
                            const char *api_value2 = json_string_value(jp_data3);
                            zabbix_log(LOG_LEVEL_DEBUG, "Item [%s][%s] found in the received JSON object: %s", param1, param2, api_value2);
                            SET_STR_RESULT(result, zbx_strdup(NULL, api_value2));
                            free((void*) answer);
                            json_decref(jp_data);
                            return SYSINFO_RET_OK;
                        }
                    } else {
                        zabbix_log(LOG_LEVEL_WARNING, "Item [%s] found in the received JSON object, but it's not plain value object", param1);
                        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Item [%s] found in the received JSON object, but it's not plain value object", param1));
                        free((void*) answer);
                        json_decref(jp_data);
                        return SYSINFO_RET_FAIL;
                    }
                }
            } else {
                    const char* api_value = json_string_value(jp_data2);
                    zabbix_log(LOG_LEVEL_DEBUG, "Item [%s] found in the received JSON object: %s", param1, api_value);
                    SET_STR_RESULT(result, zbx_strdup(NULL, api_value));
                    free((void*) answer);
                    json_decref(jp_data);
                    return SYSINFO_RET_OK;
            }
        }
        free((void*) answer);
        json_decref(jp_data);
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

        if (socket_api == 0 && zbx_docker_api_detect() == 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not avalaible");
            SET_MSG_RESULT(result, strdup("Docker's socket API is not avalaible"));
            return SYSINFO_RET_FAIL;
        }

        // TODO use API filter for status &filters=%7B%status%22%3A%5B%22created%22%5D%7D created|restarting|running|paused|exited|dead
        if (1 > request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        char    *state;
        state = get_rparam(request, 0);

        if (strcmp(state, "Up") == 0)
        {
            // Up
            const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n", 0);
            int count = 0;

            if(strcmp(answer, "") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                return SYSINFO_RET_FAIL;
            }
            if(strcmp(answer, "[]\n") != 0)
            {
                json_t *jp_data = json_loads(answer, 0, NULL);
                count = json_array_size(jp_data);
                json_decref(jp_data);
            }
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
                int count = 0;

                if(strcmp(answer, "") == 0)
                {
                    zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                    SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                    return SYSINFO_RET_FAIL;
                }
                if(strcmp(answer, "[]\n") != 0)
                {
                    json_t *jp_data = json_loads(answer, 0, NULL);
                    count = json_array_size(jp_data);
                    json_decref(jp_data);
                }
                free((void*) answer);

                // # Up
                const char *answer2 = zbx_module_docker_socket_query("GET /containers/json?all=0 HTTP/1.0\r\n\n", 0);
                if(strcmp(answer2, "") == 0)
                {
                    zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                    SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                    return SYSINFO_RET_FAIL;
                }
                if(strcmp(answer2, "[]\n") != 0)
                {
                    json_t *jp_data = json_loads(answer2, 0, NULL);
                    count = count - json_array_size(jp_data);
                    json_decref(jp_data);
                }
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

                    // empty response
                    if (strcmp(answer, "[]\n") == 0) {
                       SET_UI64_RESULT(result, 0);
                       free((void*) answer);
                       return SYSINFO_RET_OK;
                    }

                    int count = 0;
                    json_t *jp_row;
                    size_t index;
            	    const char		*p = NULL;

                    // skipped zbx_json_brackets_open and zbx_json_brackets_by_name
                	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
                	/*         ^-------------------------------------------^  */
                    json_t *jp_data = json_loads(answer, 0, NULL);
                	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
                	/*          ^                                             */
                    json_array_foreach(jp_data, index, jp_row)
                	{
                		/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
                		/*          ^------------------^                          */
                        const char *status = json_string_value(json_object_get(jp_row, "Status"));
                        if (NULL == status)
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
                    json_decref(jp_data);
                    return SYSINFO_RET_OK;
                } else {
                    if (strcmp(state, "All") == 0)
                    {
                        // All
                        const char *answer = zbx_module_docker_socket_query("GET /containers/json?all=1 HTTP/1.0\r\n\n", 0);
                        int count = 0;
                        if(strcmp(answer, "") == 0)
                        {
                            zabbix_log(LOG_LEVEL_DEBUG, "docker.cstatus is not available at the moment - some problem with Docker's socket API");
                            SET_MSG_RESULT(result, strdup("docker.cstatus is not available at the moment - some problem with Docker's socket API"));
                            return SYSINFO_RET_FAIL;
                        }
                        if(strcmp(answer, "[]\n") != 0)
                        {
                            json_t *jp_data = json_loads(answer, 0, NULL);
                            count = json_array_size(jp_data);
                            json_decref(jp_data);
                        }
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

                            // empty reponse
                            if (strcmp(answer, "[]\n") == 0) {
                               SET_UI64_RESULT(result, 0);
                               free((void*) answer);
                               return SYSINFO_RET_OK;
                            }

                            int count = 0;
                            json_t *jp_row;
                            size_t index;
                    	    const char		*p = NULL;

                            // skipped zbx_json_brackets_open and zbx_json_brackets_by_name
                        	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
                        	/*         ^-------------------------------------------^  */
                            json_t *jp_data = json_loads(answer, 0, NULL);
                        	/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
                        	/*          ^                                             */
                            json_array_foreach(jp_data, index, jp_row)
                        	{
                        		/* {"data":[{"{#IFNAME}":"eth0"},{"{#IFNAME}":"lo"},...]} */
                        		/*          ^------------------^                          */
                                const char *status = json_string_value(json_object_get(jp_row, "Status"));

                                if (NULL == status)
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
                            json_decref(jp_data);
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

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_istatus                                        *
 *                                                                            *
 * Purpose: count of images in defined status                             *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_istatus(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_istatus()");

        if (socket_api == 0 && zbx_docker_api_detect() == 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not avalaible");
            SET_MSG_RESULT(result, strdup("Docker's socket API is not avalaible"));
            return SYSINFO_RET_FAIL;
        }

        if (1 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        char    *state;
        state = get_rparam(request, 0);
        int count = 0;

        if (strcmp(state, "All") == 0)
        {
            // All
            const char *answer = zbx_module_docker_socket_query("GET /images/json?all=1&dangling=true HTTP/1.0\r\n\n", 0);

            if(strcmp(answer, "") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.istatus is not available at the moment - some problem with Docker's socket API");
                SET_MSG_RESULT(result, strdup("docker.istatus is not available at the moment - some problem with Docker's socket API"));
                return SYSINFO_RET_FAIL;
            }
            if(strcmp(answer, "[]\n") != 0)
            {
                json_t *jp_data = json_loads(answer, 0, NULL);
                count = json_array_size(jp_data);
                json_decref(jp_data);
            }
            free((void*) answer);
            zabbix_log(LOG_LEVEL_DEBUG, "Count of images in %s status: %d", state, count);
            SET_UI64_RESULT(result, count);
            return SYSINFO_RET_OK;
        } else if (strcmp(state, "Dangling") == 0) {
            // Dangling
            const char *answer = zbx_module_docker_socket_query("GET /images/json?all=false&filters=%7B%22dangling%22%3A%5B%22true%22%5D%7D HTTP/1.0\r\n\n", 0);
            if(strcmp(answer, "") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.istatus is not available at the moment - some problem with Docker's socket API");
                SET_MSG_RESULT(result, strdup("docker.istatus is not available at the moment - some problem with Docker's socket API"));
                return SYSINFO_RET_FAIL;
            }
            if(strcmp(answer, "[]\n") != 0)
            {
                json_t *jp_data = json_loads(answer, 0, NULL);
                count = json_array_size(jp_data);
                json_decref(jp_data);
            }
            free((void*) answer);
            zabbix_log(LOG_LEVEL_DEBUG, "Count of images in %s status: %d", state, count);
            SET_UI64_RESULT(result, count);
            return SYSINFO_RET_OK;
        }

        zabbix_log(LOG_LEVEL_DEBUG, "Not supported image state: %s", state);
        SET_MSG_RESULT(result, strdup("Not supported image state"));
        return SYSINFO_RET_FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_vstatus                                        *
 *                                                                            *
 * Purpose: count of volumes in defined status                             *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_vstatus(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_vstatus()");

        if (socket_api == 0 && zbx_docker_api_detect() == 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "Docker's socket API is not avalaible");
            SET_MSG_RESULT(result, strdup("Docker's socket API is not avalaible"));
            return SYSINFO_RET_FAIL;
        }

        if (1 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters"));
                return SYSINFO_RET_FAIL;
        }

        char    *state;
        state = get_rparam(request, 0);
        // TODO count items in section "Volumes"
        if (strcmp(state, "All") == 0)
        {
            // All
            const char *answer = zbx_module_docker_socket_query("GET /volumes HTTP/1.0\r\n\n", 0);
            if(strcmp(answer, "") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.vstatus is not available at the moment - some problem with Docker's socket API");
                SET_MSG_RESULT(result, strdup("docker.vstatus is not available at the moment - some problem with Docker's socket API"));
                return SYSINFO_RET_FAIL;
            }

            if(strncmp(answer, "404 ", strlen("404 ")) == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.vstatus is not available for your Docker version");
                SET_MSG_RESULT(result, strdup("docker.vstatus is not available for your Docker version"));
                return SYSINFO_RET_FAIL;
            }

            json_t *jp_data = json_loads(answer, 0, NULL);
            json_t *jp_data2 = json_object_get(jp_data, "Volumes");
            int count;

            if (NULL != jp_data2) {
                count = json_array_size(jp_data2);
                free((void*) answer);
                json_decref(jp_data);
                zabbix_log(LOG_LEVEL_DEBUG, "Count of volumes in %s status: %d", state, count);
                SET_UI64_RESULT(result, count);
                return SYSINFO_RET_OK;
            } else {
                free((void*) answer);
                json_decref(jp_data);
                count = 0;
                zabbix_log(LOG_LEVEL_DEBUG, "Count of volumes in %s status: %d", state, count);
                SET_UI64_RESULT(result, count);
                return SYSINFO_RET_OK;
            }
        } else if (strcmp(state, "Dangling") == 0) {
            // Dangling
            const char *answer = zbx_module_docker_socket_query("GET /volumes?filters=%7B%22dangling%22%3A%5B%22true%22%5D%7D HTTP/1.0\r\n\n", 0);
            if(strcmp(answer, "") == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.vstatus is not available at the moment - some problem with Docker's socket API");
                SET_MSG_RESULT(result, strdup("docker.vstatus is not available at the moment - some problem with Docker's socket API"));
                return SYSINFO_RET_FAIL;
            }

            if(strncmp(answer, "404 ", strlen("404 ")) == 0)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.vstatus is not available for your Docker version");
                SET_MSG_RESULT(result, strdup("docker.vstatus is not available for your Docker version"));
                return SYSINFO_RET_FAIL;
            }

            json_t *jp_data = json_loads(answer, 0, NULL);
            json_t *jp_data2 = json_object_get(jp_data, "Volumes");
            int count;

            if (NULL != jp_data2) {
                count = json_array_size(jp_data2);
                free((void*) answer);
                json_decref(jp_data);
                zabbix_log(LOG_LEVEL_DEBUG, "Count of volumes in %s status: %d", state, count);
                SET_UI64_RESULT(result, count);
                return SYSINFO_RET_OK;
            } else {
                free((void*) answer);
                json_decref(jp_data);
                count = 0;
                zabbix_log(LOG_LEVEL_DEBUG, "Count of volumes in %s status: %d", state, count);
                SET_UI64_RESULT(result, count);
                return SYSINFO_RET_OK;
            }
        }

        zabbix_log(LOG_LEVEL_DEBUG, "Not supported volume state: %s", state);
        SET_MSG_RESULT(result, strdup("Not supported volume state"));
        return SYSINFO_RET_FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_docker_modver                                         *
 *                                                                            *
 * Purpose: return current docker module version                              *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - function failed, item will be marked      *
 *                                 as not supported by zabbix                 *
 *               SYSINFO_RET_OK - success                                     *
 *                                                                            *
 ******************************************************************************/
int     zbx_module_docker_modver(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_modver()");
        SET_STR_RESULT(result, zbx_strdup(NULL, m_version));
        return SYSINFO_RET_OK;
}
