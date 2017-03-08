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
#include "smpp_listener.h"
#include "smpp_esme.h"
#include "smpp_bearerbox.h"
#include "smpp_queued_pdu.h"
#include "smpp_queues.h"
#include <errno.h>
#include <event2/listener.h>

 typedef struct {
    Octstr *ip;
    long time_blocked;
    long attempts;
 } SMPPBlockedIp;

 SMPPBlockedIp *smpp_blocked_ip_create() {
    SMPPBlockedIp *smpp_blocked_ip = gw_malloc(sizeof(SMPPBlockedIp));
    smpp_blocked_ip->ip = NULL;
    smpp_blocked_ip->time_blocked = 0;
    smpp_blocked_ip->attempts = 0;

    return smpp_blocked_ip;
 }

 void smpp_blocked_ip_destroy(SMPPBlockedIp *smpp_blocked_ip) {
    octstr_destroy(smpp_blocked_ip->ip);
    gw_free(smpp_blocked_ip);
 }

 void smpp_listener_auth_failed(SMPPServer *smpp_server, Octstr *ip) {
    if (octstr_len(smpp_server->ip_blocklist_exempt_ips) && (octstr_search(smpp_server->ip_blocklist_exempt_ips, ip, 0) > -1)) {
       debug("smpp.listener.auth.failed", 0, "IP address %s is exempt from the IP block list", octstr_get_cstr(ip));
       return;
    }
    gw_rwlock_wrlock(smpp_server->ip_blocklist_lock);
    SMPPBlockedIp *smpp_blocked_ip = dict_get(smpp_server->ip_blocklist, ip);
    if(smpp_blocked_ip == NULL) {
        smpp_blocked_ip = smpp_blocked_ip_create();
        dict_put(smpp_server->ip_blocklist, ip, smpp_blocked_ip);
    }
    smpp_blocked_ip->attempts++;
    smpp_blocked_ip->time_blocked = time(NULL);
    debug("smpp.listener.auth.failed", 0, "IP address %s, attempts %ld have failed", octstr_get_cstr(ip), smpp_blocked_ip->attempts);
    gw_rwlock_unlock(smpp_server->ip_blocklist_lock);
 }

 int smpp_listener_ip_is_blocked(SMPPServer *smpp_server, Octstr *ip) {
    int result = 0;
    long diff, remaining;
    gw_rwlock_wrlock(smpp_server->ip_blocklist_lock);
    SMPPBlockedIp *smpp_blocked_ip = dict_get(smpp_server->ip_blocklist, ip);
    if(smpp_blocked_ip != NULL) {
        diff = time(NULL) - smpp_blocked_ip->time_blocked;
        if(diff > smpp_server->ip_blocklist_time) {
            debug("smpp.listener.ip.is.blocked", 0, "IP address %s is now unblocked", octstr_get_cstr(ip));
            /* Time has elapsed, can reset (unblock) */
            smpp_blocked_ip->time_blocked = 0;
            smpp_blocked_ip->attempts = 0;
        } else {
            /* Time has not elapsed, how many attempts have they had? */
            if(smpp_blocked_ip->attempts >= smpp_server->ip_blocklist_attempts) {
                result = 1;
                /* Still blocked */
                remaining = smpp_server->ip_blocklist_time - diff;
                debug("smpp.listener.ip.is.blocked", 0, "IP address %s is blocked for another %ld seconds", octstr_get_cstr(ip), remaining);
            }
        }
    } else {
        /* Not blocked */
    }
    gw_rwlock_unlock(smpp_server->ip_blocklist_lock);
    return result;
 }

/* Copied from Kannel smsc_smpp.c */
static int smpp_listener_read_pdu(SMPPEsme *smpp_esme, long *len, SMPP_PDU **pdu)
{
    Connection *conn = smpp_esme->conn;
    if(conn == NULL) {
        return -1;
    }
    Octstr *os;

    if (*len == 0) {
        *len = smpp_pdu_read_len(conn);
        if (*len == -1) {
            error(0, "SMPP[%s:%ld]: Client sent garbage, ignored.",
                  octstr_get_cstr(smpp_esme->system_id), smpp_esme->id);
            *len = 0;
            return -2;
        } else if (*len == 0) {
            if (conn_eof(conn) || conn_error(conn))
                return -1;
            return 0;
        }
    }

    os = smpp_pdu_read_data(conn, *len);
    if (os == NULL) {
        if (conn_eof(conn) || conn_error(conn))
            return -1;
        return 0;
    }
    *len = 0;

    *pdu = smpp_pdu_unpack(smpp_esme->system_id, os);
    if (*pdu == NULL) {
        error(0, "SMPP[%s]: PDU unpacking failed.",
              octstr_get_cstr(smpp_esme->system_id));
        debug("smpp.listener.read.pdu", 0, "SMPP[%s]: Failed PDU follows.",
              octstr_get_cstr(smpp_esme->system_id));
        octstr_dump(os, 0);
        octstr_destroy(os);
        return -2;
    }

    octstr_destroy(os);
    return 1;
}

void smpp_listener_event(evutil_socket_t fd, short what, void *arg)
{
    SMPPEsme *smpp_esme = arg;
    SMPP_PDU *pdu = NULL;
    int result;
    SMPPQueuedPDU *smpp_queued_pdu;
    
    if(what & EV_READ) {
        debug("smpp.listener.event", 0, "Got a read event for SMPP esme connection %ld %d", smpp_esme->id, smpp_esme->bind_type);
        while((result = smpp_listener_read_pdu(smpp_esme, &smpp_esme->pending_len, &pdu)) > 0) {
            counter_set(smpp_esme->errors, 0L);
            smpp_queued_pdu = smpp_queued_pdu_create();
            smpp_queued_pdu->pdu = pdu;
            smpp_queued_pdu->smpp_esme = smpp_esme;
            smpp_queues_add_inbound(smpp_queued_pdu);
        }
        
        if(result == 0) {
            /* Just no data, who cares*/
        } else {
            if(result == -1) {
                error(0, "Could not read PDU from %s status was %d", octstr_get_cstr(smpp_esme->system_id), result);
                /* This is a connection error we can close this ESME */
                /* Stop listening on this connection, its dead */
                smpp_esme_stop_listening(smpp_esme);
                
                if(!smpp_esme->authenticated) { /* If there is a pending disconnect operation it means an unbind/rejected bind requested to disconnect, let outbound queue handle */
                    /* This bind is not authenticated so will never be cleaned up, lets do it here  */
                    debug("smpp.listener.event", 0, "Cleaning up disconnected ESME %ld", smpp_esme->id);
                    smpp_listener_auth_failed(smpp_esme->smpp_server, smpp_esme->ip);
                    smpp_esme_cleanup(smpp_esme);
                } else {
                    debug("smpp.listener.event", 0, "Allowing background thread to clean up %ld", smpp_esme->id);
                }
            } else if(result == -2) {
                error(0, "Could not read PDU from %s status was %d", octstr_get_cstr(smpp_esme->system_id), result);
                counter_increase(smpp_esme->errors);
                
                if(counter_value(smpp_esme->errors) >= SMPP_ESME_MAX_CONSECUTIVE_ERRORS) {
                    error(0, "SMPP[%s] max consecutive PDU errors, disconnecting", octstr_get_cstr(smpp_esme->system_id));
                    smpp_esme_stop_listening(smpp_esme);
                    
                    if(!smpp_esme->authenticated) {
                        /* This bind is not authenticated so will never be cleaned up, lets do it here  */
                        smpp_listener_auth_failed(smpp_esme->smpp_server, smpp_esme->ip);
                        smpp_esme_cleanup(smpp_esme);
                    } else {
                        debug("smpp.listener.event", 0, "Allowing background thread to clean up mangled %ld", smpp_esme->id);
                    }
                    
                }
            }
        }
    } else {
        debug("smpp.listener.event", 0, "Got a other event for SMPP esme connection %ld", smpp_esme->id);
    }
}

static void smpp_listener_connection_callback(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int socklen, void *ctx) {
    struct event_base *base = evconnlistener_get_base(listener);
    
    SMPPServer *smpp_server = ctx;
    Octstr *ip = NULL; //host_ip(address);
    
    if (address->sa_family == AF_INET)
    {
        struct sockaddr_in *sin = (struct sockaddr_in *) address;
        ip = host_ip(*sin);
    }

    debug("smpp.listener.connection.callback", 0, "Got connection from %s", octstr_get_cstr(ip));

    if(octstr_len(ip)) {
        if(smpp_listener_ip_is_blocked(smpp_server, ip)) {
            debug("smpp.listener.connection.callback", 0, "%s is temporarily banned from connecting. Rejecting.", octstr_get_cstr(ip));
            evutil_closesocket(fd);
            octstr_destroy(ip);
            return;
        }
    }
    
    struct event *event_container;
    
    SMPPEsme *smpp_esme = smpp_esme_create();
    smpp_esme->conn = conn_wrap_fd(fd, smpp_server->enable_ssl);
    smpp_esme->connected = 1;
    smpp_esme->smpp_server = smpp_server;
    smpp_esme->time_connected = time(NULL);
    smpp_esme->id = counter_value(smpp_server->esme_counter);
    smpp_esme->ip = ip;
    smpp_esme->max_open_acks = smpp_server->default_max_open_acks;
    
    counter_increase(smpp_server->esme_counter);
    
    event_container = event_new(base, fd, EV_TIMEOUT|EV_READ|EV_PERSIST, smpp_listener_event,
           smpp_esme);
    
    event_add(event_container, NULL);
    
    smpp_esme->event_container = event_container;
   
    
}

static void smpp_listener_accept_error_callback(struct evconnlistener *listener, void *ctx) {
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    fprintf(stderr, "Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

void smpp_listener_shutdown(SMPPServer *smpp_server) {
    event_base_loopbreak(smpp_server->event_base);
}


int smpp_listener_start(SMPPServer *smpp_server) {
    struct sockaddr_in sin;
    /* Create new event base */
    smpp_server->event_base = event_base_new();
    if (!smpp_server->event_base) {
        error(0, "Couldn't open event base");
        return 1;
    }

    /* Clear the sockaddr before using it, in case there are extra
     * platform-specific fields that can mess us up. */
    memset(&sin, 0, sizeof (sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(smpp_server->smpp_port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    /* Create a new listener */
    info(0, "Starting SMPP server on port %ld", smpp_server->smpp_port);
    smpp_server->event_listener = evconnlistener_new_bind(smpp_server->event_base, smpp_listener_connection_callback, smpp_server,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1,
            (struct sockaddr *) &sin, sizeof (sin));
    if (!smpp_server->event_listener) {
        panic(0,"Couldn't create listener");
        return 1;
    }


    smpp_server->ip_blocklist = dict_create(512, (void(*)(void *))smpp_blocked_ip_destroy);

    smpp_bearerbox_init(smpp_server);
    smpp_esme_init(smpp_server);
    smpp_queues_init(smpp_server);
    
    
    
    evconnlistener_set_error_cb(smpp_server->event_listener, smpp_listener_accept_error_callback);
    
    event_base_dispatch(smpp_server->event_base);
    
    
    smpp_queues_shutdown(smpp_server);
    smpp_esme_shutdown(smpp_server);
    smpp_bearerbox_shutdown(smpp_server);

    dict_destroy(smpp_server->ip_blocklist);
    
    evconnlistener_free(smpp_server->event_listener);
    
    event_base_free(smpp_server->event_base);
    
    return 0;
}

