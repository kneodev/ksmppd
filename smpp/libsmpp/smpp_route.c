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

SMPPRouteStatus *smpp_route_status_create(Msg *msg) {
    SMPPRouteStatus *smpp_route_status = gw_malloc(sizeof(SMPPRouteStatus));
    
    if(msg && (msg_type(msg) == sms) && (msg->sms.msgdata != NULL)) {
        List *parts = sms_split(msg, NULL, NULL, NULL, NULL, 1, 1, 255, MAX_SMS_OCTETS);
        smpp_route_status->parts = gwlist_len(parts);
        gwlist_destroy(parts, (void(*)(void *))msg_destroy);
    } else {
        smpp_route_status->parts = 1;
    }
    smpp_route_status->status = SMPP_ESME_RINVDSTADR;
    smpp_route_status->cost = 0;
    
    return smpp_route_status;
}

void smpp_route_status_destroy(SMPPRouteStatus *smpp_route_status) {
    gw_free(smpp_route_status);
}

SMPPRoute *smpp_route_create() {
    SMPPRoute *smpp_route = gw_malloc(sizeof(SMPPRoute));
    smpp_route->cost = 0;
    smpp_route->direction = SMPP_ROUTE_DIRECTION_UNKNOWN;
    smpp_route->regex = NULL;
    smpp_route->system_id = NULL;
    smpp_route->smsc_id = NULL;
    smpp_route->source_regex = NULL;
    return smpp_route;
}

void smpp_route_destroy(SMPPRoute *smpp_route) {
    octstr_destroy(smpp_route->system_id);
    octstr_destroy(smpp_route->smsc_id);
    gw_regex_destroy(smpp_route->regex);
    gw_regex_destroy(smpp_route->source_regex);
    gw_free(smpp_route);
}

SMPPOutboundRoutes *smpp_outbound_routes_create() {
    SMPPOutboundRoutes *smpp_outbound_routes = gw_malloc(sizeof(SMPPOutboundRoutes));
    smpp_outbound_routes->system_id = NULL;
    smpp_outbound_routes->routes = NULL;
    smpp_outbound_routes->lock = gw_rwlock_create();
    return smpp_outbound_routes;
}

void smpp_outbound_routes_destroy(List *smpp_outbound_routes) {
    gwlist_destroy(smpp_outbound_routes, (void(*)(void *))smpp_route_destroy);
}

void smpp_route_shutdown_database(SMPPServer *smpp_server) {
    
}

void smpp_route_rebuild_database(SMPPServer *smpp_server) {
    info(0, "Rebuilding database routes");
    SMPPRouting *smpp_routing = smpp_server->routing;
    List *inbound_routes = smpp_database_get_routes(smpp_server, SMPP_ROUTE_DIRECTION_INBOUND, NULL); /* Only inbound are built, outbound are built when ESME's connect */
    
    List *old_inbound;
    Dict *old_outbound;
    
    gw_rwlock_wrlock(smpp_routing->lock);
    old_inbound = smpp_routing->inbound_routes;
    old_outbound = smpp_routing->outbound_routes;
    smpp_routing->inbound_routes = inbound_routes;
    smpp_routing->outbound_routes = dict_create(1024, (void(*)(void *))smpp_outbound_routes_destroy); /* Just reset, they will repopulate on their own */
    
    gw_rwlock_unlock(smpp_routing->lock);
    
    gwlist_destroy(old_inbound, (void(*)(void *))smpp_route_destroy);
    dict_destroy(old_outbound);
}

void smpp_route_message_database(SMPPServer *smpp_server, int direction, Octstr *smsc_id, Octstr *system_id, Msg *msg, void(*callback)(void *context, SMPPRouteStatus *smpp_route_status), void *context) {
    SMPPRouting *smpp_routing = smpp_server->routing;
    List *routes;
    
    long i, num_routes;
    
    int found = 0;
    SMPPRoute *route;
    
    SMPPRouteStatus *smpp_route_status = smpp_route_status_create(msg);
    
    gw_rwlock_rdlock(smpp_routing->lock);
    if(msg_type(msg) == sms) { /* we can only route sms's */
        if((direction == SMPP_ROUTE_DIRECTION_OUTBOUND) && octstr_len(system_id)) {
            /* Look for our ESME routes */
            gw_rwlock_wrlock(smpp_routing->outbound_lock);
            routes = dict_get(smpp_routing->outbound_routes, system_id);
            if(!routes) {
                routes = smpp_database_get_routes(smpp_server, direction, system_id);
                dict_put(smpp_routing->outbound_routes, system_id, routes);
            }
            gw_rwlock_unlock(smpp_routing->outbound_lock);

            num_routes = gwlist_len(routes);
            for(i=0;i<num_routes;i++) {
                route = gwlist_get(routes, i);
                found = gw_regex_match_pre(route->regex, msg->sms.receiver);

                if(found) {
                    if(route->source_regex) {
                        found = 0;
                        found = gw_regex_match_pre(route->source_regex, msg->sms.sender);
                        if(found) {
                            break;
                        } else {
                            debug("smpp.route.message.database", 0, "Found matching outbound route for %s but declined sender %s", octstr_get_cstr(msg->sms.receiver), octstr_get_cstr(msg->sms.sender));
                            smpp_route_status->status = SMPP_ESME_RINVSRCADR;
                        }
                    } else {
                        break;
                    }                    
                }
            }
            
            if(found) {
                smpp_route_status->status = SMPP_ESME_ROK;
                smpp_route_status->cost = route->cost;
                octstr_destroy(msg->sms.smsc_id);
                msg->sms.smsc_id = octstr_duplicate(route->smsc_id);
                debug("smpp.route.message.database", 0, "SMPP[%s] Found outbound route from %s for %s towards %s", octstr_get_cstr(system_id), octstr_get_cstr(msg->sms.sender), octstr_get_cstr(msg->sms.receiver), octstr_get_cstr(msg->sms.smsc_id));
                callback(context, smpp_route_status);
            } else {
                callback(context, smpp_route_status);
            }
        } else if((direction == SMPP_ROUTE_DIRECTION_INBOUND) && octstr_len(smsc_id)) {
            routes = smpp_routing->inbound_routes;
            num_routes = gwlist_len(routes);
            for(i=0;i<num_routes;i++) {
                route = gwlist_get(routes, i);

                found = 0;

                if(octstr_len(route->smsc_id)) {
                    if(octstr_case_compare(route->smsc_id, smsc_id) != 0) {
                        debug("smpp.route.message.database", 0, "Cannot route messages from SMSC %s to route with SMSC %s", octstr_get_cstr(smsc_id), octstr_get_cstr(route->smsc_id));
                        continue;
                    }
                }

                found = gw_regex_match_pre(route->regex, msg->sms.receiver);

                if(found) {
                    if(route->source_regex) {
                        found = 0;
                        found = gw_regex_match_pre(route->source_regex, msg->sms.sender);
                        if(found) {
                            break;
                        } else {
                            debug("smpp.route.message.database", 0, "Found matching inbound route for %s but declined sender %s", octstr_get_cstr(msg->sms.receiver), octstr_get_cstr(msg->sms.sender));
                            smpp_route_status->status = SMPP_ESME_RINVSRCADR;
                        }
                    } else {
                        break;
                    }
                }
            }
            
            if(found) {
                smpp_route_status->status = SMPP_ESME_ROK;
                smpp_route_status->cost = route->cost;
                octstr_destroy(msg->sms.service);
                msg->sms.service = octstr_duplicate(route->system_id);
                debug("smpp.route.message.database", 0, "SMPP[%s] Found inbound route from %s for %s from %s", octstr_get_cstr(route->system_id), octstr_get_cstr(msg->sms.sender), octstr_get_cstr(msg->sms.receiver), octstr_get_cstr(smsc_id));
                callback(context, smpp_route_status);
            } else {
                callback(context, smpp_route_status);
            }
        } else {
            callback(context, smpp_route_status);
        }
    } else {
        callback(context, smpp_route_status);
    }
    
    gw_rwlock_unlock(smpp_routing->lock);
}

void smpp_route_message_plugin(SMPPServer *smpp_server, int direction, Octstr *smsc_id, Octstr *system_id, Msg *msg, void(*callback)(void *context, SMPPRouteStatus *smpp_route_status), void *context) {
    SMPPPlugin *smpp_plugin = smpp_server->plugin_route;
    if(smpp_plugin) {
        if(smpp_plugin->route_message) {
            smpp_plugin->route_message(smpp_plugin, direction, smsc_id, system_id, msg, callback, context);
            return;
        }
    }
    SMPPRouteStatus *smpp_route_status = smpp_route_status_create(msg);
    callback(context,  smpp_route_status);
}

void smpp_route_message(SMPPServer *smpp_server, int direction, Octstr *smsc_id, Octstr *system_id, Msg *msg, void(*callback)(void *context, SMPPRouteStatus *smpp_route_status), void *context) {
    SMPPRouting *smpp_routing = smpp_server->routing;
    if(smpp_routing->route_message) {
        smpp_routing->route_message(smpp_server, direction, smsc_id, system_id, msg, callback, context);
    }
}

void smpp_route_rebuild(SMPPServer *smpp_server) {
    SMPPRouting *smpp_routing = smpp_server->routing;
    if(smpp_routing->reload) {
        smpp_routing->reload(smpp_server);
    }
}

void smpp_route_init_method(SMPPServer *smpp_server) {
    SMPPRouting *smpp_routing = smpp_server->routing;
    if(smpp_routing->init) {
        smpp_routing->init(smpp_server);
    }
}

SMPPHTTPCommandResult *smpp_route_rebuild_command(SMPPServer *smpp_server, List *cgivars, int content_type) {
    smpp_route_rebuild(smpp_server);
    
    SMPPHTTPCommandResult *smpp_http_command_result = smpp_http_command_result_create();
    
    if(content_type == HTTP_CONTENT_TYPE_PLAIN) {
        smpp_http_command_result->result = octstr_create("Routes updated");
    } else if(content_type == HTTP_CONTENT_TYPE_XML) {
        smpp_http_command_result->result = octstr_create("<status>Routes updated</status>");
    }
    
    return smpp_http_command_result;
}


void smpp_route_init(SMPPServer *smpp_server) {
    SMPPRouting *smpp_routing = gw_malloc(sizeof(SMPPRouting));
    smpp_server->routing = smpp_routing;
    smpp_routing->lock = gw_rwlock_create();
    smpp_routing->inbound_routes = NULL;
    smpp_routing->outbound_routes = NULL;
    smpp_routing->reload = NULL;
    smpp_routing->route_message = NULL;
    smpp_routing->shutdown = NULL;
    smpp_routing->init = NULL;
    smpp_routing->outbound_lock = gw_rwlock_create();
    smpp_routing->initialized = 0;
    
   
    CfgGroup *grp = cfg_get_single_group(smpp_server->running_configuration, octstr_imm("smpp-routing"));
    long tmp;
    Octstr *tmp_str;
    
    if(!grp) {
        warning(0, "No 'smpp-routing' group specified, using defaults (database)");
        tmp = SMPP_ROUTING_DEFAULT_METHOD;
    } else {
        if(cfg_get_integer(&tmp, grp, octstr_imm("routing-method")) == -1) {
            /* Unable to read an integer */
            tmp_str = cfg_get(grp, octstr_imm("routing-method"));
            if(!octstr_len(tmp_str)) {
                tmp = SMPP_ROUTING_DEFAULT_METHOD;
            } else {
                /* Read a non-integer string */
                if(octstr_case_compare(tmp_str, octstr_imm("database")) == 0) {
                    tmp = SMPP_ROUTING_METHOD_DATABASE;
                } else if(octstr_case_compare(tmp_str, octstr_imm("http")) == 0) {
                    tmp = SMPP_ROUTING_METHOD_HTTP;
                } else if(octstr_case_compare(tmp_str, octstr_imm("plugin")) == 0) {
                    tmp = SMPP_ROUTING_METHOD_PLUGIN;
                    octstr_destroy(tmp_str);
                    tmp_str = cfg_get(grp, octstr_imm("plugin-id"));
                    if(!octstr_len(tmp_str)) {
                        panic(0, "Requested plugin routing but no id specified, cannot continue");
                    } else {
                        smpp_server->plugin_route = smpp_plugin_init(smpp_server, tmp_str);
                        if(!smpp_server->plugin_route || !smpp_server->plugin_route->route_message) {
                            panic(0, "Plugin based routing initialization failed.");
                        } else {
                            info(0, "Plugin based routing initialization OK.");
                            smpp_routing->route_message = smpp_route_message_plugin;
                        }
                    }
                } else {
                    panic(0, "Unknown routing method '%s'", octstr_get_cstr(tmp_str));
                }
            }
            octstr_destroy(tmp_str);
        }
    }
   
    if(tmp == SMPP_ROUTING_METHOD_DATABASE) {
        info(0, "Initializing database based routing");
        smpp_routing->reload = smpp_route_rebuild_database;
        smpp_routing->route_message = smpp_route_message_database;
        smpp_routing->shutdown = smpp_route_shutdown_database;
    } else if(tmp == SMPP_ROUTING_METHOD_HTTP) {
        smpp_routing->init = smpp_http_client_route_init;
    } else if(tmp == SMPP_ROUTING_METHOD_PLUGIN) {
        info(0, "Initializing plugin based routing");
    }
    
    
    
    smpp_route_init_method(smpp_server);
    
    smpp_route_rebuild(smpp_server);
    
    smpp_http_server_add_command(smpp_server, octstr_imm("rebuild-routes"), smpp_route_rebuild_command);
}
void smpp_route_shutdown(SMPPServer *smpp_server) {
    SMPPRouting *smpp_routing = smpp_server->routing;
    if(smpp_routing->shutdown) {
        smpp_routing->shutdown(smpp_server);
    }
    
    dict_destroy(smpp_routing->outbound_routes);
    gwlist_destroy(smpp_routing->inbound_routes, (void(*)(void *))smpp_route_destroy);
    gw_rwlock_destroy(smpp_routing->lock);
    gw_rwlock_destroy(smpp_routing->outbound_lock);
    
    gw_free(smpp_routing);
}

