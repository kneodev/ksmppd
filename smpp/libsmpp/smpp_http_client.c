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
#include "gw/load.h"
#include "gw/msg.h"
#include "gw/smsc/smpp_pdu.h"
#include "smpp_server.h"
#include "smpp_esme.h"
#include "smpp_http_client.h"
#include "smpp_esme.h"
#include "smpp_route.h"

typedef struct {
    Counter *outstanding_requests;
    Octstr *url;
    List *request_threads;
    List *queued_requests;
    long num_callers;
    long max_outstanding_requests;
} SMPPHTTPRouting;

typedef struct {
    Octstr *system_id;
    Octstr *smsc_id;
    int direction;
    Msg *msg;
    void(*callback)(void *context, SMPPRouteStatus *smpp_route_status);
    void *context;
    SMPPHTTPRouting *smpp_http_routing;
    SMPPRouteStatus *smpp_route_status;
} SMPPHTTPQueuedRoute;

SMPPHTTPQueuedRoute *smpp_http_queued_route_create() {
    SMPPHTTPQueuedRoute *smpp_http_queued_route = gw_malloc(sizeof(SMPPHTTPQueuedRoute));
    smpp_http_queued_route->callback = NULL;
    smpp_http_queued_route->context = NULL;
    smpp_http_queued_route->direction = 0;
    smpp_http_queued_route->msg = NULL;
    smpp_http_queued_route->smsc_id = NULL;
    smpp_http_queued_route->system_id = NULL;
    smpp_http_queued_route->smpp_http_routing = NULL;
    smpp_http_queued_route->smpp_route_status = NULL;
    return smpp_http_queued_route;
}

void smpp_http_queued_route_destroy(SMPPHTTPQueuedRoute *smpp_http_queued_route) {
    /* We don't destroy these pointers, they're just a mechanism for us to maintain state, as we'll pass them to result functions */
    gw_free(smpp_http_queued_route); 
}

SMPPESMEAuthResult *smpp_http_client_auth(SMPPServer *smpp_server, Octstr *system_id, Octstr *password) {
    HTTPCaller *caller = http_caller_create();
    
    SMPPESMEAuthResult *res = NULL;
    
    List *request_headers = http_create_empty_headers();
    http_header_add(request_headers, SMPP_HTTP_HEADER_PREFIX "system-id", octstr_get_cstr(system_id));
    http_header_add(request_headers, SMPP_HTTP_HEADER_PREFIX "password", octstr_get_cstr(password));
    
    http_start_request(caller, HTTP_METHOD_GET, smpp_server->auth_url, request_headers, NULL, 0, NULL, NULL);
    
    int status = 0;
    Octstr *final_url = NULL, *body = NULL;
    
    List *response_headers = NULL;
    Octstr *tmp, *auth;
    int auth_result;
    
    http_receive_result(caller, &status, &final_url, &response_headers, &body);
    
    if(status == 200) {
        if(response_headers) {
            auth = http_header_value(response_headers, octstr_imm("x-ksmppd-auth"));
            if(octstr_len(auth)) {
                auth_result = atoi(octstr_get_cstr(auth));
                if(auth_result) {
                    info(0, "SMPP[%s] Authenticated OK via HTTP", octstr_get_cstr(system_id));
                    res = smpp_esme_auth_result_create();
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-throughput"));
                    if(octstr_len(tmp)) {
                        res->throughput = atof(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-max-binds"));
                    if(octstr_len(tmp)) {
                        res->max_binds = atoi(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-simulate"));
                    if(octstr_len(tmp)) {
                        res->simulate = atoi(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-simulate-deliver-every"));
                    if(octstr_len(tmp)) {
                        res->simulate_deliver_every = atol(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-simulate-mo-every"));
                    if(octstr_len(tmp)) {
                        res->simulate_mo_every = atol(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-permanent-failure-every"));
                    if(octstr_len(tmp)) {
                        res->simulate_permanent_failure_every = atol(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-temporary-failure-every"));
                    if(octstr_len(tmp)) {
                        res->simulate_temporary_failure_every = atol(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-default-smsc"));
                    if(octstr_len(tmp)) {
                        res->default_smsc = octstr_duplicate(tmp);
                    }
                    octstr_destroy(tmp);
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-default-smsc-cost"));
                    if(octstr_len(tmp)) {
                        res->default_cost = atof(octstr_get_cstr(tmp));
                    }
                    octstr_destroy(tmp);
                    
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-callback-url"));
                    if(octstr_len(tmp)) {
                        res->callback_url = octstr_duplicate(tmp);
                    }
                    octstr_destroy(tmp);
                    
                    tmp = http_header_value(response_headers, octstr_imm("x-ksmppd-connect-allow-ip"));
                    if(octstr_len(tmp)) {
                        res->allowed_ips = octstr_duplicate(tmp);
                    }
                    octstr_destroy(tmp);
                }
            }
            octstr_destroy(auth);
        }
    }

    octstr_destroy(final_url);
    octstr_destroy(body);
    http_destroy_headers(response_headers);
    http_destroy_headers(request_headers);

    http_caller_destroy(caller);
    
    return res;
}

void smpp_http_client_receive_thread(void *arg) {
    SMPPHTTPQueuedRoute *smpp_http_queued_route;
    HTTPCaller *caller = arg;


    int status = 0;
    Octstr *final_url = NULL, *body = NULL;

    List *response_headers = NULL;

    Msg *msg, *old_message;
    
    int route_status;
    double route_cost;
    
    Octstr *header_key, *header_val;

    while ((smpp_http_queued_route = http_receive_result(caller, &status, &final_url, &response_headers, &body)) != NULL) {
        debug("smpp.http.client.receive.thread", 0, "Received request result %s", octstr_get_cstr(body));
        counter_decrease(smpp_http_queued_route->smpp_http_routing->outstanding_requests);
        
        header_val = http_header_value(response_headers, octstr_imm(SMPP_HTTP_HEADER_PREFIX "Route-Status"));
        
        if(header_val != NULL) {
            route_status = atoi(octstr_get_cstr(header_val));
            octstr_destroy(header_val);
            if(route_status) {
                header_val = http_header_value(response_headers, octstr_imm(SMPP_HTTP_HEADER_PREFIX "Route-Cost"));
                if(header_val != NULL) {
                    if(octstr_parse_double(&route_cost, header_val, 0) != -1) {
                        octstr_destroy(header_val);
                        old_message = msg_duplicate(smpp_http_queued_route->msg);
                        msg = smpp_http_queued_route->msg;
                        if (msg_type(msg) == sms) {
                            /* Let's build a comparitive msg */
    #define INTEGER(name) header_key = octstr_format("%s",  SMPP_HTTP_HEADER_PREFIX #name); octstr_replace(header_key, octstr_imm("_"), octstr_imm("-")); header_val = http_header_value(response_headers, header_key); \
                          if(octstr_len(header_val)) { \
                                p->name = atol(octstr_get_cstr(header_val)); \
                                debug("smpp.http.client.receive.thread", 0, "HTTP route result setting msg->sms.%s to %s", #name, octstr_get_cstr(header_val)); \
                          } \
                          octstr_destroy(header_val); \
                          octstr_destroy(header_key); 
    #define OCTSTR(name) header_key = octstr_format("%s",  SMPP_HTTP_HEADER_PREFIX #name); octstr_replace(header_key, octstr_imm("_"), octstr_imm("-")); header_val = http_header_value(response_headers, header_key); \
                          if(octstr_len(header_val)) { \
                                octstr_destroy(p->name); \
                                p->name = octstr_duplicate(header_val); \
                                debug("smpp.http.client.receive.thread", 0, "HTTP route result setting msg->sms.%s to %s", #name, octstr_get_cstr(header_val)); \
                          } \
                          octstr_destroy(header_val); \
                          octstr_destroy(header_key); 
    #define UUID(name) header_key = octstr_format("%s",  SMPP_HTTP_HEADER_PREFIX #name); octstr_replace(header_key, octstr_imm("_"), octstr_imm("-")); header_val = http_header_value(response_headers, header_key); \
                          if(octstr_len(header_val)) { \
                                uuid_parse(octstr_get_cstr(header_val), p->name); \
                                debug("smpp.http.client.receive.thread", 0, "HTTP route result setting msg->sms.%s to %s", #name, octstr_get_cstr(header_val)); \
                          } \
                          octstr_destroy(header_val); \
                          octstr_destroy(header_key); 

    #define VOID(name) ;
    #define MSG(type, stmt) \
            case type: {struct type *p = &msg->type; stmt} break;
                            switch (msg->type) {
    #include "gw/msg-decl.h"
                                default:
                                    break;
                            }
                        }
                        
                        /* Based on the direction of the message certain properties can't be overridden */
                        if(smpp_http_queued_route->direction == SMPP_ROUTE_DIRECTION_OUTBOUND) {
                            octstr_destroy(msg->sms.service);
                            msg->sms.service = octstr_duplicate(old_message->sms.service);
                        } else if(smpp_http_queued_route->direction == SMPP_ROUTE_DIRECTION_INBOUND) {
                            octstr_destroy(msg->sms.smsc_id);
                            msg->sms.smsc_id = octstr_duplicate(old_message->sms.smsc_id);
                        }
                        
                        msg_destroy(old_message);
                        
                        smpp_http_queued_route->smpp_route_status->status = SMPP_ESME_ROK;
                        smpp_http_queued_route->smpp_route_status->cost = route_cost;

                        smpp_http_queued_route->callback(smpp_http_queued_route->context, smpp_http_queued_route->smpp_route_status);

                    } else {
                        warning(0, SMPP_HTTP_HEADER_PREFIX "Route-Cost header returned, was not valid number, failing");
                        smpp_http_queued_route->smpp_route_status->status = SMPP_ESME_RSUBMITFAIL;
                        smpp_http_queued_route->callback(smpp_http_queued_route->context, smpp_http_queued_route->smpp_route_status);
                        octstr_destroy(header_val);
                    }
                    
                } else {
                    warning(0, "No " SMPP_HTTP_HEADER_PREFIX "Route-Cost header returned, failing message");
                    smpp_http_queued_route->smpp_route_status->status = SMPP_ESME_RSUBMITFAIL;
                    smpp_http_queued_route->callback(smpp_http_queued_route->context, smpp_http_queued_route->smpp_route_status);
                }
            } else {
                warning(0, SMPP_HTTP_HEADER_PREFIX "Route-Status indicated routing failure (code %d), rejecting", route_status);
                header_val = http_header_value(response_headers, octstr_imm(SMPP_HTTP_HEADER_PREFIX "Route-Error-Code"));
                if(octstr_len(header_val)) {
                    smpp_http_queued_route->smpp_route_status->status = atoi(octstr_get_cstr(header_val));
                    if(smpp_http_queued_route->smpp_route_status->status == SMPP_ESME_ROK) {
                        warning(0, "HTTP responded with Route-Error-Code indicating success, overwriting");
                        smpp_http_queued_route->smpp_route_status->status = SMPP_ESME_RSUBMITFAIL;
                    }
                    smpp_http_queued_route->callback(smpp_http_queued_route->context, smpp_http_queued_route->smpp_route_status);    
                } else {
                    smpp_http_queued_route->smpp_route_status->status = SMPP_ESME_RSUBMITFAIL;
                    smpp_http_queued_route->callback(smpp_http_queued_route->context, smpp_http_queued_route->smpp_route_status);    
                }
                octstr_destroy(header_val);
            }
        } else {
            warning(0, "No " SMPP_HTTP_HEADER_PREFIX "Route-Status header returned, failing message");
            smpp_http_queued_route->smpp_route_status->status = SMPP_ESME_RSUBMITFAIL;
            smpp_http_queued_route->callback(smpp_http_queued_route->context, smpp_http_queued_route->smpp_route_status);
        }
        
        smpp_http_queued_route_destroy(smpp_http_queued_route);

        octstr_destroy(final_url);
        octstr_destroy(body);
        http_destroy_headers(response_headers);

    }
}

List *smpp_http_client_msg_to_headers(Msg *msg) {
    List *result = gwlist_create();
    Octstr *key, *val;
    
    char id[UUID_STR_LEN + 1];
    
    if(msg_type(msg) == sms) {
#define INTEGER(name) if(p->name != MSG_PARAM_UNDEFINED) {  \
                    key = octstr_format("%s", SMPP_HTTP_HEADER_PREFIX #name); \
                       octstr_replace(key, octstr_imm("_"), octstr_imm("-")); \
                       val = octstr_format("%ld", p->name); \
                    http_header_add(result, octstr_get_cstr(key), octstr_get_cstr(val)); \
                    octstr_destroy(key); \
                    octstr_destroy(val); \
                    }
#define OCTSTR(name) if(octstr_len(p->name)) { \
                    key = octstr_format("%s", SMPP_HTTP_HEADER_PREFIX #name); \
                       octstr_replace(key, octstr_imm("_"), octstr_imm("-")); \
                       val = octstr_format("%E", p->name); \
                    http_header_add(result, octstr_get_cstr(key), octstr_get_cstr(val)); \
                    octstr_destroy(key); \
                    octstr_destroy(val);  \
                    }
#define UUID(name) uuid_unparse(p->name, id); \
                   key = octstr_format("%s", SMPP_HTTP_HEADER_PREFIX #name); \
                       octstr_replace(key, octstr_imm("_"), octstr_imm("-")); \
                       val = octstr_format("%s", id); \
                    http_header_add(result, octstr_get_cstr(key), octstr_get_cstr(val)); \
                    octstr_destroy(key); \
                    octstr_destroy(val);  
        
#define VOID(name) ;
#define MSG(type, stmt) \
        case type: {struct type *p = &msg->type; stmt} break;
    switch (msg->type) {
#include "gw/msg-decl.h"
        default:
            return result;
    }
        
    }
    return result;
}

void smpp_http_client_request_thread(void *arg) {
    SMPPServer *smpp_server = arg;
    SMPPRouting *smpp_routing = smpp_server->routing;
    SMPPHTTPRouting *smpp_http_routing = smpp_routing->context;
    
    SMPPHTTPQueuedRoute *smpp_http_queued_route;
    
    long max, count;
    
    HTTPCaller *caller = http_caller_create();
    
    long receive_thread = gwthread_create(smpp_http_client_receive_thread, caller);
    
    
    List *request_headers;
    
    Octstr *tmp_direction;
    
    int busy;
    
    while(!(smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
        if(counter_value(smpp_http_routing->outstanding_requests) >= smpp_http_routing->max_outstanding_requests) {
            warning(0, "Max outstanding requests reached %ld/%ld - waiting", counter_value(smpp_http_routing->outstanding_requests), smpp_http_routing->max_outstanding_requests);
            gwthread_sleep(0.1);
            continue;
        }
        
        busy = 0;
        
        count = 0;
        max = smpp_http_routing->max_outstanding_requests - counter_value(smpp_http_routing->outstanding_requests);
        
        while((count < max) && ((smpp_http_queued_route = gwlist_consume(smpp_http_routing->queued_requests)) != NULL)) {
            count++;
            request_headers = smpp_http_client_msg_to_headers(smpp_http_queued_route->msg);
            tmp_direction = octstr_format("%d", smpp_http_queued_route->direction);
            http_header_add(request_headers, SMPP_HTTP_HEADER_PREFIX "Routing-Direction", octstr_get_cstr(tmp_direction));
            octstr_destroy(tmp_direction);
            counter_increase(smpp_http_routing->outstanding_requests);
            http_start_request(caller, HTTP_METHOD_GET, smpp_http_routing->url, request_headers, NULL, 0, smpp_http_queued_route, NULL);
            http_destroy_headers(request_headers);
            
            busy = 1;
        }
        
        if(!busy) {
            gwthread_sleep(1.0);
        }
    }
    
    http_caller_signal_shutdown(caller);
    
    gwthread_join(receive_thread);
    
    http_caller_destroy(caller);
}

void smpp_http_client_route_message(SMPPServer *smpp_server, int direction, Octstr *smsc_id, Octstr *system_id, Msg *msg, void(*callback)(void *context, SMPPRouteStatus *smpp_route_status), void *context) {
    SMPPRouting *smpp_routing = smpp_server->routing;
    SMPPHTTPRouting *smpp_http_routing = smpp_routing->context;
    SMPPHTTPQueuedRoute *smpp_http_queued_route = smpp_http_queued_route_create();
    smpp_http_queued_route->direction = direction;
    smpp_http_queued_route->smsc_id = smsc_id;
    smpp_http_queued_route->system_id = system_id;
    smpp_http_queued_route->msg = msg;
    smpp_http_queued_route->callback = callback;
    smpp_http_queued_route->context = context;
    smpp_http_queued_route->smpp_http_routing = smpp_http_routing;
    smpp_http_queued_route->smpp_route_status = smpp_route_status_create(msg);
    
    gwlist_produce(smpp_http_routing->queued_requests, smpp_http_queued_route);
    
    long num, i;
    long *p;
    num = gwlist_len(smpp_http_routing->request_threads);
    for(i=0;i<num;i++) {
        p = gwlist_get(smpp_http_routing->request_threads, i);
        gwthread_wakeup(*p);
    }
}
void smpp_http_client_route_shutdown(SMPPServer *smpp_server) {
    info(0, "Shutting down HTTP router");
    SMPPRouting *smpp_routing = smpp_server->routing;
    SMPPHTTPRouting *smpp_http_routing = smpp_routing->context;
    
    long *p;
    
    while((p = gwlist_consume(smpp_http_routing->request_threads)) != NULL) {
        gwthread_wakeup(*p);
        gwthread_join(*p);
        gw_free(p);
    }
    
    gwlist_destroy(smpp_http_routing->request_threads, NULL);
    gwlist_destroy(smpp_http_routing->queued_requests, (void(*)(void *))smpp_http_queued_route_destroy);
    counter_destroy(smpp_http_routing->outstanding_requests);
    
    octstr_destroy(smpp_http_routing->url);
    
    gw_free(smpp_http_routing);
}

void smpp_http_client_route_init(SMPPServer *smpp_server) {
    SMPPRouting *smpp_routing = smpp_server->routing;
    SMPPHTTPRouting *smpp_http_routing = gw_malloc(sizeof(SMPPHTTPRouting));
    smpp_routing->context = smpp_http_routing;
    
    info(0, "Initializing HTTP based routing");
    
    CfgGroup *grp = cfg_get_single_group(smpp_server->running_configuration, octstr_imm("smpp-routing"));

    smpp_http_routing->url = cfg_get(grp, octstr_imm("http-routing-url"));
    if (!octstr_len(smpp_http_routing->url)) {
        panic(0, "No 'http-routing-url' specified in 'smpp-routing' group, cannot proceed");
    }
    
    info(0, "HTTP Router URL %s", octstr_get_cstr(smpp_http_routing->url));
    
    if(cfg_get_integer(&smpp_http_routing->num_callers, grp, octstr_imm("http-num-callers")) == -1) {
        debug("smpp.http.client.route.init", 0, "Using default 'http-num-callers' of 1");
        smpp_http_routing->num_callers = 1;
    }
    
    if(cfg_get_integer(&smpp_http_routing->max_outstanding_requests, grp, octstr_imm("http-max-outstanding-requests")) == -1) {
        debug("smpp.http.client.route.init", 0, "Using default 'http-max-outstanding-requests' of %d", HTTP_DEFAULT_MAX_OUTSTANDING);
        smpp_http_routing->max_outstanding_requests = HTTP_DEFAULT_MAX_OUTSTANDING;
    }
    
    smpp_http_routing->queued_requests = gwlist_create();
    smpp_http_routing->request_threads = gwlist_create();
    smpp_http_routing->outstanding_requests = counter_create();
    
    long i;
    long *p;
    
    for(i=0;i<smpp_http_routing->num_callers;i++) {
        p = gw_malloc(sizeof(long));
        *p = gwthread_create(smpp_http_client_request_thread, smpp_server);
        gwlist_produce(smpp_http_routing->request_threads, p);
    }
    
    smpp_routing->route_message = smpp_http_client_route_message;
    smpp_routing->shutdown = smpp_http_client_route_shutdown;

}