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
#include "gw/msg.h"
#include "gw/load.h"
#include "gw/smsc/smpp_pdu.h"
#include "smpp_server.h"
#include "smpp_bearerbox.h"
#include "smpp_esme.h"
#include "smpp_queued_pdu.h"
#include "smpp_database.h"

SMPPESMEAuthResult *smpp_database_mysql_auth(SMPPServer *smpp_server, Octstr *username, Octstr *mysql);
List *smpp_database_mysql_get_stored(SMPPServer *smpp_server, long sms_type, Octstr *service);

SMPPDatabaseMsg *smpp_database_msg_create() {
    SMPPDatabaseMsg *smpp_database_msg = gw_malloc(sizeof(SMPPDatabaseMsg));
    smpp_database_msg->global_id = 0;
    smpp_database_msg->msg = NULL;
    smpp_database_msg->wakeup_thread_id = 0;
    return smpp_database_msg;
}

void smpp_database_msg_destroy(SMPPDatabaseMsg *smpp_database_msg) {
    msg_destroy(smpp_database_msg->msg);
    gw_free(smpp_database_msg);
}


SMPPESMEAuthResult *smpp_database_auth(SMPPServer *smpp_server, Octstr *username, Octstr *password) {
    SMPPDatabase *smpp_database = smpp_server->database;
    return smpp_database->authenticate(smpp_server, username, password);
}


SMPPDatabase *smpp_database_create() {
    SMPPDatabase *smpp_database = gw_malloc(sizeof(SMPPDatabase));
    smpp_database->authenticate = NULL;
    smpp_database->add_message = NULL;
    smpp_database->context = NULL;
    smpp_database->add_pdu = NULL;
    smpp_database->delete = NULL;
    smpp_database->get_stored = NULL;
    smpp_database->get_stored_pdu = NULL;
    smpp_database->pending_msg = NULL;
    smpp_database->pending_pdu = NULL;
    smpp_database->get_routes = NULL;
    smpp_database->deduct_credit = NULL;
    smpp_database->get_esmes_with_queued = NULL;
    
    
    return smpp_database;
}

int smpp_database_add_message(SMPPServer *smpp_server, Msg *msg) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->add_message) {
        return smpp_database->add_message(smpp_server, msg);
    }
    return 0;
}

int smpp_database_add_pdu(SMPPServer *smpp_server, SMPPQueuedPDU *smpp_queued_pdu) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->add_pdu) {
        return smpp_database->add_pdu(smpp_server, smpp_queued_pdu);
    }
    return 0;
}

List *smpp_database_get_routes(SMPPServer *smpp_server, int direction, Octstr *service) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->get_routes) {
        return smpp_database->get_routes(smpp_server, direction, service);
    }
    return gwlist_create(); /* Caller will destroy */
}

List *smpp_database_get_stored(SMPPServer *smpp_server, long sms_type, Octstr *service, long limit) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->get_stored) {
        return smpp_database->get_stored(smpp_server, sms_type, service, limit);
    }
    return gwlist_create(); /* Caller will destroy */
}

List *smpp_database_get_stored_pdu(SMPPServer *smpp_server, Octstr *service, long limit) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->get_stored_pdu) {
        return smpp_database->get_stored_pdu(smpp_server, service, limit);
    }
    return gwlist_create(); /* Caller will destroy */
}

int smpp_database_remove(SMPPServer *smpp_server, unsigned long global_id, int temporary) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->delete) {
        return smpp_database->delete(smpp_server, global_id, temporary);
    }
    return 0;
}

int smpp_database_deduct_credit(SMPPServer *smpp_server, Octstr *service, double cost) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->deduct_credit) {
        return smpp_database->deduct_credit(smpp_server, service, cost);
    }
    return 0;
}

List *smpp_database_get_esmes_with_queued(SMPPServer *smpp_server) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->get_esmes_with_queued) {
        return smpp_database->get_esmes_with_queued(smpp_server);
    }
    return gwlist_create();
}

void smpp_database_destroy(SMPPDatabase *smpp_database) {
    
    gw_free(smpp_database);
}

void *smpp_database_init(SMPPServer *smpp_server) {
    if(octstr_case_compare(smpp_server->database_type, octstr_imm("mysql")) == 0) {
        debug("smpp.database.init", 0, "Initialize database type to MySQL");
        return smpp_database_mysql_init(smpp_server);
    }
    
    return NULL;
}

void smpp_database_shutdown(SMPPServer *smpp_server) {
    SMPPDatabase *smpp_database = smpp_server->database;
    if(smpp_database->shutdown) {
        smpp_database->shutdown(smpp_server);
    }
    
    smpp_database_destroy(smpp_database);
}