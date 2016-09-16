/* ==================================================================== 
 * KSMPPD Software License, Version 1.0 
 * 
 * Copyright (c) 2016 Kurt Neo 
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by 
 *        Kurt Neo <kneodev@gmail.com> & the Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "KSMPPD" and "KSMPPD" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact kneodev@gmail.com 
 * 
 * 5. Products derived from this software may not be called "KSMPPD", 
 *    nor may "KSMPPD" appear in their name, without prior written 
 *    permission of the Kurt Neo. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL KURT NEO OR CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by Kurt Neo.
 * 
 * KSMPPD or "Kurt's SMPP Daemon" were written by Kurt Neo.
 * 
 * If you would like to donate to this project you may do so via Bitcoin to
 * the address: 1NhLkTDiZtFTJMefvjQY4pUWM3jD641jWN
 * 
 * If you require commercial support for this software you can contact
 * 
 * Kurt Neo <kneodev@gmail.com>
 * 
 * This product includes software developed by the Kannel Group (http://www.kannel.org/).
 * 
 */

/*
 * Implementation of dynamic library plugins
 * 
 * Donald Jackson <donald@ddj.co.za>
 *
 */
#include <dlfcn.h>
#include "gwlib/gwlib.h"
#include "gw/smsc/smpp_pdu.h"
#include "gw/load.h"
#include "gw/msg.h"
#include "gw/sms.h"
#include "gw/dlr.h"
#include "smpp_server.h"
#include "smpp_bearerbox.h"
#include "smpp_esme.h"
#include "smpp_queued_pdu.h"
#include "smpp_queues.h"
#include "smpp_database.h"
#include "smpp_uuid.h"
#include "smpp_bearerbox.h"
#include "smpp_pdu_util.h"
#include "smpp_route.h"
#include "smpp_http_server.h"
#include "smpp_http_client.h"
#include "smpp_plugin.h"

SMPPPlugin *smpp_plugin_create() {
    SMPPPlugin *smpp_plugin = gw_malloc(sizeof (SMPPPlugin));
    smpp_plugin->authenticate = NULL;
    smpp_plugin->context = NULL;
    smpp_plugin->init = NULL;
    smpp_plugin->reload = NULL;
    smpp_plugin->route_message = NULL;
    smpp_plugin->shutdown = NULL;
    smpp_plugin->args = NULL;
    smpp_plugin->id = NULL;
    return smpp_plugin;
}

void smpp_plugin_destroy_real(SMPPPlugin *smpp_plugin) {
    octstr_destroy(smpp_plugin->args);
    octstr_destroy(smpp_plugin->id);
    gw_free(smpp_plugin);
}

void smpp_plugin_destroy(SMPPPlugin *smpp_plugin) {
    if (smpp_plugin->shutdown) {
        smpp_plugin->shutdown(smpp_plugin);
    } else {
        smpp_plugin_destroy_real(smpp_plugin);
    }
}

SMPPPlugin *smpp_plugin_init(SMPPServer *smpp_server, Octstr *id) {
    void *lib;
    Octstr *path;
    Octstr *tmp = NULL;
    char *error_str;
    
    int result = 0;


    SMPPPlugin *smpp_plugin = dict_get(smpp_server->plugins, id);
    if (smpp_plugin == NULL) {
        /* Not loaded yet, load now */
        smpp_plugin = smpp_plugin_create();
        smpp_plugin->id = octstr_duplicate(id);
        CfgGroup *grp = NULL;
        List *grplist;
        Octstr *p = NULL;

        grplist = cfg_get_multi_group(smpp_server->running_configuration, octstr_imm("ksmppd-plugin"));
        while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
            p = cfg_get(grp, octstr_imm("id"));
            if (p != NULL && octstr_compare(p, id) == 0) {
                goto found;
            }
            if (p != NULL) octstr_destroy(p);
        }
        panic(0, "Plugin settings for id '%s' are not specified!",
                octstr_get_cstr(id));

found:
        octstr_destroy(p);
        gwlist_destroy(grplist, NULL);

        path = cfg_get(grp, octstr_imm("path"));
       
        if (octstr_len(path)) {
            lib = dlopen(octstr_get_cstr(path), RTLD_NOW | RTLD_GLOBAL);
            if (!lib) {
                error_str = dlerror();
                error(0, "Error opening '%s' for plugin '%s' (%s)", octstr_get_cstr(path), octstr_get_cstr(id), error_str);
                goto error;

            }

            error_str = dlerror();
            if (error_str != NULL) {
                error(0, "DL returned error %s", error_str);
                goto error;
            }

            tmp = cfg_get(grp, octstr_imm("init-function"));
            if (octstr_len(tmp)) {
                smpp_plugin->init = dlsym(lib, octstr_get_cstr(tmp));
                if (!smpp_plugin->init) {
                    panic(0, "init-function %s unable to load from %s", octstr_get_cstr(tmp), octstr_get_cstr(path));
                }
                smpp_plugin->args = cfg_get(grp, octstr_imm("args"));
                result = smpp_plugin->init(smpp_plugin);
            } else {
                result = 1;
            }
        }
error:
        octstr_destroy(path);
        octstr_destroy(tmp);

        if (result) {
            debug("ksmppd.vsmsc.init", 0, "Adding plugin with id %s and args %s", octstr_get_cstr(smpp_plugin->id), octstr_get_cstr(smpp_plugin->args));
            dict_put(smpp_server->plugins, id, smpp_plugin);
        } else {
            smpp_plugin_destroy_real(smpp_plugin);
            smpp_plugin = NULL;
        }
    } else {
        info(0, "Plugin with id '%s' already initialized", octstr_get_cstr(id));
    }
    return smpp_plugin;
}

