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

#ifndef SMPP_DATABASE_H
#define SMPP_DATABASE_H

#define SMPP_DATABASE_BATCH_LIMIT 1000

#ifdef __cplusplus
extern "C" {
#endif
    typedef struct {
        Msg *msg;
        unsigned long global_id;
        SMPPServer *smpp_server;
        long wakeup_thread_id;
    } SMPPDatabaseMsg;
    
    
    
    typedef struct {
        SMPPESMEAuthResult *(*authenticate) (void *context, Octstr *system_id, Octstr *password);
        int (*add_message)(SMPPServer *context, Msg *msg);      
        int (*add_pdu)(SMPPServer *context, SMPPQueuedPDU *smpp_queued_pdu);      
        List *(*get_stored)(SMPPServer *context, long sms_type, Octstr *service, long limit);
        List *(*get_stored_pdu)(SMPPServer *context, Octstr *service, long limit);
        List *(*get_routes)(SMPPServer *context, int direction, Octstr *service);
        int (*deduct_credit)(SMPPServer *context, Octstr *service, double value);
        int (*delete)(SMPPServer *context, unsigned long global_id, int temporary);
        List *(*get_esmes_with_queued)(SMPPServer *smpp_server);
        void (*shutdown)(SMPPServer *context);
        void *context;
        Dict *pending_pdu;
        Dict *pending_msg;
        
    } SMPPDatabase;
    
    
    SMPPDatabaseMsg *smpp_database_msg_create();
    void smpp_database_msg_destroy();

    SMPPDatabase *smpp_database_create();
    void smpp_database_destroy(SMPPDatabase *smpp_database);
            

    void *smpp_database_init(SMPPServer *smpp_server);
    void smpp_database_shutdown(SMPPServer *smpp_server);
    
    void *smpp_database_mysql_init(SMPPServer *smpp_server);
    
    SMPPESMEAuthResult *smpp_database_auth(SMPPServer *smpp_server, Octstr *username, Octstr *password);
    
    int smpp_database_add_message(SMPPServer *smpp_server, Msg *msg);
    int smpp_database_add_pdu(SMPPServer *smpp_server, SMPPQueuedPDU *smpp_queued_pdu);
    List *smpp_database_get_stored(SMPPServer *smpp_server, long sms_type, Octstr *service, long limit);
    List *smpp_database_get_stored_pdu(SMPPServer *smpp_server, Octstr *service, long limit);
    List *smpp_database_get_routes(SMPPServer *smpp_server, int direction, Octstr *service);
    int smpp_database_deduct_credit(SMPPServer *smpp_server, Octstr *service, double value);
    List *smpp_database_get_esmes_with_queued(SMPPServer *smpp_server);
    
    int smpp_database_remove(SMPPServer *smpp_server, unsigned long global_id, int temporary);


#ifdef __cplusplus
}
#endif

#endif /* SMPP_DATABASE_H */

