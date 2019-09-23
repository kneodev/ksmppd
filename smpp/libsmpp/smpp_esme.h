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

#ifndef SMPP_ESME_H
#define SMPP_ESME_H

#define SMPP_ESME_CLEANUP_INTERVAL 30
#define SMPP_ESME_CLEANUP_QUEUE_DELAY 15
#define SMPP_ESME_MAX_CONSECUTIVE_ERRORS 10
#define SMPP_ESME_WAIT_ACK_TIME 120
#define SMPP_ESME_WAIT_ACK_DISCONNECT 1
#define SMPP_ESME_COMMAND_STATUS_WAIT_ACK_TIMEOUT 0x0400
#define SMPP_ESME_COMMAND_STATUS_QUEUED 0x0401
#define SMPP_ESME_COMMAND_STATUS_QUEUE_ERROR 0x0402

#define SMPP_ESME_DEFAULT_ENQUIRE_LINK_INTERVAL 180
#define SMPP_ESME_DEFAULT_MAX_OPEN_ACKS 500

#define SMPP_ESME_UNDEFINED 0
#define SMPP_ESME_TRANSMIT 1
#define SMPP_ESME_RECEIVE 2



#ifdef __cplusplus
extern "C" {
#endif
    typedef struct {
        Octstr *system_id;
        double throughput;
        Load *inbound_load;
        Load *outbound_load;
        List *binds;
        List *outbound_routes;
        long max_binds;
        Counter *inbound_processed;
        Counter *outbound_processed;
        int enable_prepaid_billing;
        Counter *mt_counter;
        Counter *mo_counter;
        Counter *dlr_counter;
        Counter *error_counter;
    } SMPPEsmeGlobal;
    
    
    typedef struct {
        double throughput;
        Octstr *default_smsc;
        double default_cost;
        
        int max_binds;
        
        Octstr *callback_url;
        
        int simulate;
        unsigned long simulate_deliver_every;
        unsigned long simulate_mo_every;
        unsigned long simulate_permanent_failure_every;
        unsigned long simulate_temporary_failure_every;
        
        int enable_prepaid_billing;
        
        Octstr *allowed_ips;
        
        Octstr *alt_charset;
    } SMPPESMEAuthResult;
    
    typedef struct {
        Octstr *system_id;
        Octstr *system_type;
        Connection *conn;
        volatile int authenticated;
        volatile int connected;
        volatile int pending_disconnect;
        
        struct event *event_container;
        SMPPServer *smpp_server;
        long time_connected;
        long time_disconnected;
        long time_last_pdu;
        long time_last_queue_process;
        
        long enquire_link_interval;
        
        int bind_type;
        int version;
        
        Load *inbound_load;
        Load *outbound_load;
        
        Counter *inbound_queued;
        Counter *outbound_queued;
        Counter *pending_routing;
        Counter *sequence_number;
        
        Counter *errors;
        RWLock *event_lock;
        
        long id;
        
        
        Counter *inbound_processed;
        Counter *outbound_processed;
        
        int simulate;
        unsigned long simulate_deliver_every;
        unsigned long simulate_mo_every;
        unsigned long simulate_permanent_failure_every;
        unsigned long simulate_temporary_failure_every;
        
        Octstr *alt_charset;
        Octstr *alt_addr_charset;
        
        Octstr *default_smsc;
        double default_cost;
        
        Dict *open_acks;
        RWLock *ack_process_lock;
        
        Counter *catenated_sms_counter;
        
        int wait_ack_action;
        int wait_ack_time;
        
        long max_open_acks;
        
        SMPPEsmeGlobal *smpp_esme_global;
        
        Octstr *ip;
        
        long pending_len;
        
        Counter *mt_counter;
        Counter *mo_counter;
        Counter *dlr_counter;
        Counter *error_counter;
    } SMPPEsme;

    typedef struct {
        Dict *esmes;
        RWLock *lock;
        long cleanup_thread_id;
        int g_thread_id;
        RWLock *cleanup_lock;
        List *cleanup_queue;
        Load *inbound_load;
        Load *outbound_load;
        Counter *inbound_processed;
        Counter *outbound_processed;
    } SMPPEsmeData;
    
    SMPPESMEAuthResult *smpp_esme_auth_result_create();
    
    void smpp_esme_auth_result_destroy(SMPPESMEAuthResult *smpp_esme_auth_result);
    
    SMPPEsme *smpp_esme_create();
    void smpp_esme_destroy(SMPPEsme *smpp_esme);
    
    void smpp_esme_init(SMPPServer *smpp_server);
    void smpp_esme_shutdown(SMPPServer *smpp_server);
    
    void smpp_esme_global_add(SMPPServer *smpp_server, SMPPEsme *smpp_esme);
    
    void smpp_esme_cleanup(SMPPEsme *smpp_esme);
    
    /* This will just disconnect the connection, no unbind */
    void smpp_esme_disconnect(SMPPEsme *smpp_esme);
    
    /* Cancel event listeners */
    void smpp_esme_stop_listening(SMPPEsme *smpp_esme);
    
    void smpp_esme_inbound_load_increase(SMPPEsme *smpp_esme);
    void smpp_esme_outbound_load_increase(SMPPEsme *smpp_esme);
    SMPPEsme *smpp_esme_find_best_receiver(SMPPServer *smpp_server, Octstr *system_id);
    List *smpp_esme_global_get_readers(SMPPServer *smpp_server, int best_only);
    List *smpp_esme_global_get_queued(SMPPServer *smpp_server);
    
    SMPPESMEAuthResult *smpp_esme_auth(SMPPServer *smpp_server, Octstr *system_id, Octstr *password, SMPPEsme *smpp_esme);

#ifdef __cplusplus
}
#endif

#endif /* SMPP_ESME_H */

