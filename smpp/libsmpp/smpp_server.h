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

#ifndef SMPP_SERVER_H
#include <signal.h>

#define SMPP_SERVER_H
#define SMPP_SERVER_NAME "KSMPPD"
#define SMPP_SERVER_VERSION "0.6"

#define SMPP_SERVER_AUTH_METHOD_DATABASE 1
#define SMPP_SERVER_AUTH_METHOD_HTTP 2
#define SMPP_SERVER_AUTH_METHOD_PLUGIN 4

#define SMPP_SERVER_STATUS_STARTUP 1
#define SMPP_SERVER_STATUS_RUNNING 2
#define SMPP_SERVER_STATUS_LOG_REOPEN 4
#define SMPP_SERVER_STATUS_SHUTDOWN 8

#define SMPP_WAITACK_DISCONNECT 0
#define SMPP_WAITACK_DROP 1

#ifdef __cplusplus
extern "C" {
#endif
    
    typedef struct {
        Cfg *running_configuration;
        Octstr *config_filename;
        RWLock *config_lock;
        Octstr *server_id;
        
        List *bearerbox_outbound_queue;
        List *bearerbox_inbound_queue;
        
        int configured;
        
        volatile sig_atomic_t server_status;
        
        long smpp_port;
        
        int enable_ssl;
        
        void *esme_data;
        
        gw_prioqueue_t *inbound_queue;
        gw_prioqueue_t *outbound_queue;
        gw_prioqueue_t *simulation_queue;
        
        long num_inbound_queue_threads;
        long num_outbound_queue_threads;
        
        Counter *running_threads;
        
        Octstr *database_type;
        Octstr *database_config;
        Octstr *database_user_table;
        Octstr *database_store_table;
        Octstr *database_pdu_table;
        Octstr *database_route_table;
        Octstr *database_version_table;
        
        int database_enable_queue;
        
        
        void *database;
        void *bearerbox;
        void *routing;
        void *http_server;
        void *http_client;
        
        
        
        struct event_base *event_base;
        struct evconnlistener *event_listener;
        
        Counter *esme_counter;
        long authentication_method;
        Octstr *auth_url;
        
        struct SMPPPlugin *plugin_auth;
        struct SMPPPlugin *plugin_route;
        
        
        Dict *plugins;

        Dict *ip_blocklist;
        RWLock *ip_blocklist_lock;
        long ip_blocklist_time;
        long ip_blocklist_attempts;
        Octstr *ip_blocklist_exempt_ips;

        long default_max_open_acks;

        long wait_ack_action;
    } SMPPServer;
    
    SMPPServer *smpp_server_create();
    void smpp_server_destroy(SMPPServer *smpp_server);
    int smpp_server_reconfigure(SMPPServer *smpp_server);

#ifdef __cplusplus
}
#endif

#endif /* SMPP_SERVER_H */

