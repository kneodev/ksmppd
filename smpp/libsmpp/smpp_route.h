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
#ifndef SMPP_ROUTE_H
#define	SMPP_ROUTE_H

#include "gwlib/gw-regex.h"

#ifdef	__cplusplus
extern "C" {
#endif
    
    
#define SMPP_ROUTE_DIRECTION_UNKNOWN 0
#define SMPP_ROUTE_DIRECTION_OUTBOUND 1
#define SMPP_ROUTE_DIRECTION_INBOUND 2

#define SMPP_ROUTING_METHOD_DATABASE 1    
#define SMPP_ROUTING_METHOD_HTTP 2
#define SMPP_ROUTING_METHOD_PLUGIN 4
#define SMPP_ROUTING_DEFAULT_METHOD SMPP_ROUTING_METHOD_DATABASE
    
    typedef struct {
        long parts;
        double cost;
        int status;
    } SMPPRouteStatus;
    
    typedef struct {
        regex_t *regex;
        Octstr *system_id;
        Octstr *smsc_id;
        double cost;
        int direction;
        void *context;
        regex_t *source_regex;
    } SMPPRoute;
    
    typedef struct {
        Octstr *system_id;
        List *routes;
        RWLock *lock;
    } SMPPOutboundRoutes;

    typedef struct {
        Dict *outbound_routes;
        List *inbound_routes;
        void (*route_message)(SMPPServer *smpp_server, int direction, Octstr *smsc_id, Octstr *system_id, Msg *msg, void(*callback)(void *context, SMPPRouteStatus *smpp_route_status), void *context);
        void (*reload)(SMPPServer *smpp_server);
        void (*shutdown)(SMPPServer *smpp_server);
        void (*init)(SMPPServer *smpp_server);
        RWLock *lock;
        RWLock *outbound_lock;
        int initialized;
        Octstr *http_routing_url;
        void *context;
    } SMPPRouting;

    
    void smpp_route_message_database(SMPPServer *smpp_server, int direction, Octstr *smsc_id, Octstr *system_id, Msg *msg, void(*callback)(void *context, SMPPRouteStatus *smpp_route_status), void *context);
    
    void smpp_route_init(SMPPServer *smpp_server);
    void smpp_route_shutdown(SMPPServer *smpp_server);
    void smpp_route_rebuild(SMPPServer *smpp_server);
    void smpp_route_message(SMPPServer *smpp_server, int direction, Octstr *smsc_id, Octstr *system_id, Msg *msg, void(*callback)(void *context, SMPPRouteStatus *smpp_route_status), void *context);
    
    SMPPRoute *smpp_route_create();
    void smpp_route_destroy(SMPPRoute *smpp_route);
    
    SMPPRouteStatus *smpp_route_status_create(Msg *msg);
    void smpp_route_status_destroy(SMPPRouteStatus *smpp_route_status);



#ifdef	__cplusplus
}
#endif

#endif	/* SMPP_ROUTE_H */

