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

#ifndef SMPP_BEARERBOX_H
#define SMPP_BEARERBOX_H

#define HEARTBEAT_INTERVAL 10

#ifdef __cplusplus
extern "C" {
#endif
    typedef struct {
        Msg *msg;
        void (*callback)(void *, int);
        void *context;
        Octstr *id;
    } SMPPBearerboxMsg;
    
    typedef struct {
        List *bearerboxes;
        gw_prioqueue_t *outbound_queue;
        gw_prioqueue_t *inbound_queue;
        RWLock *lock;
        SMPPServer *smpp_server;
        Dict *pending_requeues;
        long requeue_thread_id;
    } SMPPBearerboxState;

    typedef struct {
        Octstr *id;
        Octstr *host;
        long port;
        int ssl;
        Connection *connection;
        long last_msg;
        volatile sig_atomic_t alive;
        volatile sig_atomic_t writer_alive;
        SMPPBearerboxState *smpp_bearerbox_state;
        RWLock *lock;
        Dict *open_acks;
        RWLock *ack_lock;
    } SMPPBearerbox;
    
    void smpp_bearerbox_init(SMPPServer *smpp_server);
    void smpp_bearerbox_shutdown(SMPPServer *smpp_server);
    
    void smpp_bearerbox_add_message(SMPPServer *smpp_server, Msg *msg, void(*callback)(void*,int), void *context);
    SMPPBearerboxMsg *smpp_bearerbox_msg_create(Msg *msg, void(*callback)(void*,int), void *context);
    void smpp_bearerbox_msg_destroy(SMPPBearerboxMsg *smpp_bearerbox_msg);
    int smpp_bearerbox_acknowledge(SMPPBearerbox *smpp_bearerbox, Octstr *id, ack_status_t status);



#ifdef __cplusplus
}
#endif

#endif /* SMPP_BEARERBOX_H */

