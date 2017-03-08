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
#include "gwlib/gwlib.h"
#include "gw/smsc/smpp_pdu.h"
#include "gw/load.h"
#include "gw/msg.h"
#include "smpp_server.h"
#include "smpp_server_cfg.h"
#include "smpp_esme.h"
#include "smpp_bearerbox.h"
#include "smpp_queued_pdu.h"
#include "smpp_queues.h"
#include "smpp_database.h"
#include "gw/msg.h"
#include "smpp_route.h"
#include "smpp_http_server.h"
#include "smpp_plugin.h"

SMPPServer *smpp_server_create() {
    SMPPServer *smpp_server = gw_malloc(sizeof (SMPPServer));
    smpp_server->bearerbox_inbound_queue = gwlist_create();
    smpp_server->bearerbox_outbound_queue = gwlist_create();
    smpp_server->config_lock = gw_rwlock_create();
    smpp_server->configured = 0;
    smpp_server->server_id = NULL;
    smpp_server->config_filename = NULL;
    smpp_server->database_type = NULL;
    smpp_server->database_config = NULL;
    smpp_server->database_pdu_table = NULL;
    smpp_server->database_route_table = NULL;
    smpp_server->database_store_table = NULL;
    smpp_server->database_user_table = NULL;
    smpp_server->database_version_table = NULL;
    
    smpp_server->running_configuration = NULL;
    smpp_server->inbound_queue = NULL;
    smpp_server->outbound_queue = NULL;
    smpp_server->simulation_queue = NULL;
    smpp_server->esme_counter = counter_create();
    smpp_server->bearerbox = NULL;
    smpp_server->database_pdu_table = NULL;
    smpp_server->auth_url = NULL;
    smpp_server->plugin_auth = NULL;
    smpp_server->plugin_route = NULL;
    smpp_server->plugins = dict_create(8, (void(*)(void *))smpp_plugin_destroy);
    smpp_server->ip_blocklist = NULL;
    smpp_server->ip_blocklist_lock = gw_rwlock_create();
    smpp_server->ip_blocklist_time = 0;
    smpp_server->ip_blocklist_exempt_ips = NULL;

    smpp_server->default_max_open_acks = SMPP_ESME_DEFAULT_MAX_OPEN_ACKS;
    smpp_server->wait_ack_action = SMPP_WAITACK_DISCONNECT;

    return smpp_server;
}

void smpp_server_destroy(SMPPServer *smpp_server) {
    smpp_http_server_shutdown(smpp_server);
    smpp_database_shutdown(smpp_server);
    smpp_route_shutdown(smpp_server);
    dict_destroy(smpp_server->plugins);
    octstr_destroy(smpp_server->server_id);
    gwlist_destroy(smpp_server->bearerbox_inbound_queue, (void(*)(void *))msg_destroy);
    gwlist_destroy(smpp_server->bearerbox_outbound_queue, (void(*)(void *))msg_destroy);
    gw_rwlock_destroy(smpp_server->config_lock);
    octstr_destroy(smpp_server->database_type);
    octstr_destroy(smpp_server->database_config);
    octstr_destroy(smpp_server->database_pdu_table);
    octstr_destroy(smpp_server->database_route_table);
    octstr_destroy(smpp_server->database_store_table);
    octstr_destroy(smpp_server->database_user_table);
    octstr_destroy(smpp_server->database_version_table);
    octstr_destroy(smpp_server->config_filename);
    counter_destroy(smpp_server->esme_counter);
    counter_destroy(smpp_server->running_threads);
    octstr_destroy(smpp_server->auth_url);
    gw_rwlock_destroy(smpp_server->ip_blocklist_lock);
    octstr_destroy(smpp_server->ip_blocklist_exempt_ips);
    
    cfg_destroy(smpp_server->running_configuration);
    
    
    gw_free(smpp_server);
}

int smpp_server_reconfigure(SMPPServer *smpp_server) {
    int status = 0;
    long tmp;
    gw_rwlock_wrlock(smpp_server->config_lock);
    Cfg *cfg = cfg_create(smpp_server->config_filename);
    CfgGroup *grp;
    Octstr *tmp_str;
    Octstr *plugin_id;
    
    if(!smpp_server->configured) {
        debug("smpp", 0, "Adding configuration hooks");
        cfg_add_hooks(smpp_server_cfg_is_allowed_in_group, smpp_server_cfg_is_single_group);
    }
        

    if (cfg_read(cfg) == -1) {
        status = -1;
        if (!smpp_server->configured) {
            panic(0, "Unable to read configuration file %s", octstr_get_cstr(smpp_server->config_filename));
        } else {
            error(0, "Unable to refresh configuration file %s", octstr_get_cstr(smpp_server->config_filename));
        }
    } else {
        grp = cfg_get_single_group(cfg, octstr_imm("ksmppd"));

        if (grp) {
            if(smpp_server->configured) {
                cfg_destroy(smpp_server->running_configuration);
            }
            smpp_server->running_configuration = cfg;
            if(!smpp_server->configured) {
                debug("smpp", 0, "Running one time (initial) configuration");
                smpp_pdu_init(cfg);
                
                Octstr *logfile = cfg_get(grp, octstr_imm("log-file"));
                cfg_get_integer(&tmp, grp, octstr_imm("log-level"));

                if (logfile != NULL) {
                    info(0, "Starting to log to file %s level %ld", octstr_get_cstr(logfile), tmp);
                    log_open(octstr_get_cstr(logfile), tmp, GW_NON_EXCL);
                    octstr_destroy(logfile);
                }

                if(cfg_get_integer(&smpp_server->smpp_port, grp, octstr_imm("smpp-port")) == -1) {
                    smpp_server->smpp_port = 2345;
                }
                
                
                
                smpp_server->server_id = cfg_get(grp, octstr_imm("id"));

                smpp_server->running_threads = counter_create();

                smpp_server->enable_ssl = 0;
                
                if(cfg_get_integer(&smpp_server->num_inbound_queue_threads, grp, octstr_imm("inbound-queue-threads")) == -1) {
                    smpp_server->num_inbound_queue_threads = 1;
                }
                
                if(cfg_get_integer(&smpp_server->num_outbound_queue_threads, grp, octstr_imm("outbound-queue-threads")) == -1) {
                    smpp_server->num_outbound_queue_threads = 1;
                }

                if(cfg_get_integer(&smpp_server->ip_blocklist_time, grp, octstr_imm("ip-blocklist-time")) == -1) {
                    smpp_server->ip_blocklist_time = 300;
                }

                if(cfg_get_integer(&smpp_server->ip_blocklist_attempts, grp, octstr_imm("ip-blocklist-attempts")) == -1) {
                    smpp_server->ip_blocklist_attempts = 5;
                }

                smpp_server->ip_blocklist_exempt_ips = cfg_get(grp, octstr_imm("ip-blocklist-exempt-ips"));

                cfg_get_integer(&smpp_server->default_max_open_acks, grp, octstr_imm("default-max-open-acks"));

                info(0, "SMPP ESME Default Max Open Acks set to %ld", smpp_server->default_max_open_acks);

                cfg_get_integer(&smpp_server->wait_ack_action, grp, octstr_imm("wait-ack-action"));

                info(0, "SMPP Wait ack behaviour %ld", smpp_server->wait_ack_action);

                debug("smpp", 0, "Blocking users for %ld seconds on %ld authentication failures", smpp_server->ip_blocklist_time, smpp_server->ip_blocklist_attempts);

                smpp_server->database_type = cfg_get(grp, octstr_imm("database-type"));
                smpp_server->database_config = cfg_get(grp, octstr_imm("database-config"));
                smpp_server->database_store_table = cfg_get(grp, octstr_imm("database-store-table"));
                smpp_server->database_user_table = cfg_get(grp, octstr_imm("database-user-table"));
                smpp_server->database_pdu_table = cfg_get(grp, octstr_imm("database-pdu-table"));
                smpp_server->database_route_table = cfg_get(grp, octstr_imm("database-route-table"));
                smpp_server->database_version_table = cfg_get(grp, octstr_imm("database-version-table"));
                
                if(!octstr_len(smpp_server->database_type)) {
                    panic(0, "The SMPP server cannot function without a 'database-type' parameter");
                }
                
                cfg_get_bool(&smpp_server->database_enable_queue, grp, octstr_imm("database-enable-queue"));
                
                smpp_server->database = smpp_database_init(smpp_server);
                
                if(smpp_server->database == NULL) {
                    panic(0, "Error configuring database %s configuration %s", octstr_get_cstr(smpp_server->database_type), octstr_get_cstr(smpp_server->database_config));
                }
                
                if(cfg_get_integer(&smpp_server->authentication_method, grp, octstr_imm("auth-method")) == -1) {
                    smpp_server->authentication_method = SMPP_SERVER_AUTH_METHOD_DATABASE;
                    tmp_str = cfg_get(grp, octstr_imm("auth-method"));
                    if(!octstr_len(tmp_str)) {
                        debug("smpp", 0, "No 'auth-method' specified, using database as default");
                        smpp_server->authentication_method = SMPP_SERVER_AUTH_METHOD_DATABASE;
                    } else {
                        /* Read a non-integer string */
                        if(octstr_case_compare(tmp_str, octstr_imm("database")) == 0) {
                            debug("smpp", 0, "Authentication using database");
                            smpp_server->authentication_method = SMPP_SERVER_AUTH_METHOD_DATABASE;
                        } else if(octstr_case_compare(tmp_str, octstr_imm("http")) == 0) {
                            debug("smpp", 0, "Authentication using http");
                            smpp_server->authentication_method = SMPP_SERVER_AUTH_METHOD_HTTP;
                        } else if(octstr_case_compare(tmp_str, octstr_imm("plugin")) == 0) {
                            plugin_id = cfg_get(grp, octstr_imm("auth-plugin-id"));
                            
                            if(!octstr_len(plugin_id)) {
                                panic(0, "Requested plugin based authentication but no 'auth-plugin-id' set");
                            }
                            
                            debug("smpp", 0, "Authentication using plugin");
                            smpp_server->authentication_method = SMPP_SERVER_AUTH_METHOD_PLUGIN;
                            smpp_server->plugin_auth = smpp_plugin_init(smpp_server, plugin_id);
                            if(smpp_server->plugin_auth == NULL) {
                                panic(0, "Plugin '%s' initialization failed, cannot continue", octstr_get_cstr(plugin_id));
                            } else {
                                if(!smpp_server->plugin_auth->authenticate) {
                                    panic(0, "Plugin '%s' has no registered auth function, cannot continue", octstr_get_cstr(plugin_id));
                                }
                            }
                            
                            octstr_destroy(plugin_id);
                        } else {
                            panic(0, "Unknown auth method '%s'", octstr_get_cstr(tmp_str));
                        }
                    }
                    octstr_destroy(tmp_str);
                }
                
                smpp_server->auth_url = cfg_get(grp, octstr_imm("auth-url"));
                
                smpp_http_server_init(smpp_server);
                
                smpp_route_init(smpp_server);

                smpp_server->configured = 1;
            }
        } else {
            if (!smpp_server->configured) {
                panic(0, "No group 'ksmppd' configured in %s, cannot start", octstr_get_cstr(smpp_server->config_filename));
            } else {
                error(0, "Unable to refresh configuration file %s, no 'ksmppd' group configured", octstr_get_cstr(smpp_server->config_filename));
            }
        }

    }
    

    gw_rwlock_unlock(smpp_server->config_lock);

    return status;
}


