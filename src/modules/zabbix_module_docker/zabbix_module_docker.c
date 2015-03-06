/*
** Zabbix module for docker container monitoring - v 0.0.3
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

/* the variable keeps timeout setting for item processing */
static int      item_timeout = 0;
char      *stat_dir, *driver;
int     zbx_module_docker_discovery(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_up(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_mem(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_cpu(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_net(AGENT_REQUEST *request, AGENT_RESULT *result);
int     zbx_module_docker_dev(AGENT_REQUEST *request, AGENT_RESULT *result);

static ZBX_METRIC keys[] =
/*      KEY                     FLAG            FUNCTION                TEST PARAMETERS */
{
        {"docker.discovery", 0, zbx_module_docker_discovery,    NULL},
        {"docker.up",   CF_HAVEPARAMS,  zbx_module_docker_up,   "full container id"},
        {"docker.mem",  CF_HAVEPARAMS,  zbx_module_docker_mem,  "full container id, memory metric name"},
        {"docker.cpu",  CF_HAVEPARAMS,  zbx_module_docker_cpu,  "full container id, cpu metric name"},
        {"docker.net",  CF_HAVEPARAMS,  zbx_module_docker_net,  "full container id, network metric name"},
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
        
        if (stat_dir == NULL || driver == NULL) {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.up check is not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.up check is not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }        

        container = get_rparam(request, 0);
        char    *stat_file = "/cpuacct.stat";
        char    *cgroup = "cpuacct/";
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        zbx_strlcat(filename, container, filename_size);
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
 ******************************************************************************/
int     zbx_module_docker_dev(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        // documentation https://www.kernel.org/doc/Documentation/cgroups/blkio-controller.txt
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_dev()");
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (2 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
        
        if (stat_dir == NULL || driver == NULL) {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.dev metrics are not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.dev metrics are not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }        

        container = get_rparam(request, 0);
        metric = get_rparam(request, 1);
        // TODO stat file depends on metric - create map metric->stat_file
        char    *stat_file = "/blkio.throttle.read_iops_device";
        char    *cgroup = "blkio/";
        size_t  filename_size = strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);
        zbx_strlcat(filename, container, filename_size);
        zbx_strlcat(filename, stat_file, filename_size);
        zabbix_log(LOG_LEVEL_DEBUG, "Metric source file: %s", filename);
        FILE    *file;
        if (NULL == (file = fopen(filename, "r")))
        {
                zabbix_log(LOG_LEVEL_ERR, "Can't open docker container metric file: '%s'", filename);
                SET_MSG_RESULT(result, strdup("Can't open docker container blkio file"));
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
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value)) {
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
 ******************************************************************************/
int     zbx_module_docker_mem(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        // documentation https://www.kernel.org/doc/Documentation/cgroups/memory.txt
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_mem()");
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (2 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
        
        if (stat_dir == NULL || driver == NULL) {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.mem metrics are not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.mem metrics are not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }        

        container = get_rparam(request, 0);
        metric = get_rparam(request, 1);
        char    *stat_file = "/memory.stat";
        char    *cgroup = "memory/";
        size_t  filename_size =  strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);        
        zbx_strlcat(filename, container, filename_size);
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
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value)) {
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
 ******************************************************************************/
int     zbx_module_docker_cpu(AGENT_REQUEST *request, AGENT_RESULT *result)
{
        // documentation https://www.kernel.org/doc/Documentation/cgroups/cpuacct.txt
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_cpu()");
        // TODO
        // use shared memory for "global" cpu variables - because method can be executed in different agent workers
        // use code libs/zbxsysinfo/linux/cpu.c for parsing /proc/stat
        
        char    *container, *metric;
        int     ret = SYSINFO_RET_FAIL;

        if (2 != request->nparam)
        {
                zabbix_log(LOG_LEVEL_ERR, "Invalid number of parameters: %d",  request->nparam);
                SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
                return SYSINFO_RET_FAIL;
        }
        
        if (stat_dir == NULL || driver == NULL) {
                zabbix_log(LOG_LEVEL_DEBUG, "docker.cpu metrics are not available at the moment - no stat directory.");
                SET_MSG_RESULT(result, zbx_strdup(NULL, "docker.cpu metrics are not available at the moment - no stat directory."));
                return SYSINFO_RET_OK;
        }        

        container = get_rparam(request, 0);
        metric = get_rparam(request, 1);
        char    *stat_file = "/cpuacct.stat";
        char    *cgroup = "cpuacct/";
        size_t  filename_size =  strlen(cgroup) + strlen(container) + strlen(stat_dir) + strlen(driver) + strlen(stat_file) + 2;
        char    *filename = malloc(filename_size);
        zbx_strlcpy(filename, stat_dir, filename_size);
        zbx_strlcat(filename, cgroup, filename_size);
        zbx_strlcat(filename, driver, filename_size);        
        zbx_strlcat(filename, container, filename_size);
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
                if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value)) {
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
        #include "zbxjson.h"
        zabbix_log(LOG_LEVEL_DEBUG, "In zbx_module_docker_discovery()");
        
        struct zbx_json j;
        if (stat_dir == NULL && zbx_docker_stat_detect() == SYSINFO_RET_FAIL) {
            zabbix_log(LOG_LEVEL_DEBUG, "docker.discovery is not available at the moment - no stat directory - empty discovery");
            zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
            zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
            zbx_json_close(&j);
            SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
            zbx_json_free(&j);
            return SYSINFO_RET_OK;
        }        
        
        // TODO if zabbix has root permission, we can read human names of containers
        // https://docs.docker.com/reference/api/docker_remote_api/
                
        char            line[MAX_STRING_LEN], *p, *mpoint, *mtype, container;
        FILE            *f;
        DIR             *dir;
        zbx_stat_t      sb;
        char            *file = NULL;
        struct dirent   *d;
        char    *cgroup = "cpuacct/";
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

        char    scontainerid[13];

        while (NULL != (d = readdir(dir)))
        {
                if(0 == strcmp(d->d_name, ".") || 0 == strcmp(d->d_name, ".."))
                        continue;

                file = zbx_dsprintf(file, "%s/%s", ddir, d->d_name);

                if (0 != zbx_stat(file, &sb) || 0 == S_ISDIR(sb.st_mode))
                        continue;
        
                zbx_json_addobject(&j, NULL);
                zbx_json_addstring(&j, "{#FCONTAINERID}", d->d_name, ZBX_JSON_TYPE_STRING);
                zbx_strlcpy(scontainerid, d->d_name, 13);
                zbx_json_addstring(&j, "{#SCONTAINERID}", scontainerid, ZBX_JSON_TYPE_STRING);
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
        zbx_docker_stat_detect();
        return ZBX_MODULE_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_docker_stat_detect                                           *
 *                                                                            *
 * Purpose: it should find metric folder - it depends on docker version       *
 *            or execution environment                                        *
 *                                                                            *
 * Return value: SYSINFO_RET_FAIL - stat folder was not found                 *
 *               SYSINFO_RET_OK - stat folder was found                       *
 *                                                                            *
 ******************************************************************************/
int zbx_docker_stat_detect() 
{
    zabbix_log(LOG_LEVEL_DEBUG, "In zbx_docker_stat_detect()");

    char *drivers[] = {
        "docker/",        // exec driver native: docker -d -e native
        "system.slice/",  // ???    
        "lxc/",           // exec driver lxc: docker -d -e lxc
        ""
    }, **tdriver;
    char path[512];
    char *temp;
    FILE *fp;
    DIR  *dir;

    if((fp = fopen("/proc/mounts", "r")) == NULL) {
        zabbix_log(LOG_LEVEL_WARNING, "Cannot open /proc/mounts: %s", zbx_strerror(errno));
        return SYSINFO_RET_FAIL;
    }
    
    while(fgets(path, 512, fp) != NULL) {
        if((strstr(path, "/cpuacct cgroup")) != NULL) {
            // found line e.g. cgroup /cgroup/cpuacct cgroup rw,relatime,cpuacct 0 0
            temp = string_replace(path, "cgroup ", "");
            temp = string_replace(temp, strstr(temp, " "), "");
            stat_dir = string_replace(temp, "cpuacct", "");
            zabbix_log(LOG_LEVEL_DEBUG, "Detected docker stat directory: %s", stat_dir);
            
            char *cgroup = "cpuacct/";
            tdriver = drivers;
            while (*tdriver != "") {                
                size_t  ddir_size = strlen(cgroup) + strlen(stat_dir) + strlen(*tdriver) + 1;
                char    *ddir = malloc(ddir_size);
                zbx_strlcpy(ddir, stat_dir, ddir_size);
                zbx_strlcat(ddir, cgroup, ddir_size);
                zbx_strlcat(ddir, *tdriver, ddir_size);                
                if (NULL != (dir = opendir(ddir)))
                {
                    driver = *tdriver;
                    zabbix_log(LOG_LEVEL_DEBUG, "Detected used docker driver: %s", driver);                    
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
