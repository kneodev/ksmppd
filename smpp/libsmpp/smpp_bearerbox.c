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
#include "gw/sms.h"
#include "gw/load.h"
#include "gw/shared.h"
#include "gw/smsc/smpp_pdu.h"
#include "smpp_server.h"
#include "smpp_esme.h"
#include "smpp_pdu_util.h"
#include "smpp_bearerbox.h"
#include "smpp_queued_pdu.h"
#include "smpp_queues.h"
#include "smpp_uuid.h"
#include "smpp_database.h"
#include "smpp_route.h"

sig_atomic_t smpp_bearerbox_get_state(SMPPBearerbox *smpp_bearerbox) {
    sig_atomic_t result;
    gw_rwlock_rdlock(smpp_bearerbox->lock);
    result = smpp_bearerbox->alive;
    gw_rwlock_unlock(smpp_bearerbox->lock);
    return result;
}

void smpp_bearerbox_set_state(SMPPBearerbox *smpp_bearerbox, sig_atomic_t state) {
    gw_rwlock_wrlock(smpp_bearerbox->lock);
    smpp_bearerbox->alive = state;
    gw_rwlock_unlock(smpp_bearerbox->lock);
}

int smpp_bearerbox_deliver(SMPPBearerbox *smpp_bearerbox, Msg *msg) {
    int result = deliver_to_bearerbox_real(smpp_bearerbox->connection, msg);

    if (result == -1) {
        smpp_bearerbox_set_state(smpp_bearerbox, 0);
        msg_destroy(msg);
    } else {
        smpp_bearerbox->last_msg = time(NULL);
    }
    return result;
}

int smpp_bearerbox_acknowledge(SMPPBearerbox *smpp_bearerbox, Octstr *id, ack_status_t status) {
    if(!octstr_len(id)) {
        error(0, "Cannot ack null message to bearerbox (shutting down?)");
        return -1;
    }
    Msg *msg = msg_create(ack);
    uuid_parse(octstr_get_cstr(id), msg->ack.id);
    msg->ack.nack = status;
    msg->ack.time = time(NULL);
    int result;
    
    if(smpp_bearerbox_get_state(smpp_bearerbox)) {
        debug("smpp.bearerbox.acknowledge", 0, "Acknowledging %s to bearerbox", octstr_get_cstr(id));

        result = deliver_to_bearerbox_real(smpp_bearerbox->connection, msg);

        if (result == -1) {
            smpp_bearerbox_set_state(smpp_bearerbox, 0);
            msg_destroy(msg);
        } else {
            smpp_bearerbox->last_msg = time(NULL);
        }
    } else {
        error(0, "Bearerbox %s is offline, cannot send.", octstr_get_cstr(smpp_bearerbox->id));
        result = -1;
        msg_destroy(msg);
    }
    
    return result;
}

SMPPBearerboxMsg *smpp_bearerbox_msg_create(Msg *msg, void(*callback)(void*, int), void *context) {
    SMPPBearerboxMsg *smpp_bearerbox_msg = gw_malloc(sizeof (SMPPBearerboxMsg));
    smpp_bearerbox_msg->msg = msg;
    smpp_bearerbox_msg->callback = callback;
    smpp_bearerbox_msg->context = context;
    smpp_bearerbox_msg->id = smpp_uuid_get(msg->sms.id);
    return smpp_bearerbox_msg;
}

void smpp_bearerbox_msg_destroy(SMPPBearerboxMsg *smpp_bearerbox_msg) {
    octstr_destroy(smpp_bearerbox_msg->id);
    gw_free(smpp_bearerbox_msg);
}

int smpp_bearerbox_online(void *item, void *pattern) {
    SMPPBearerbox *smpp_bearerbox = (SMPPBearerbox*) item;
    int result = 0;
    gw_rwlock_rdlock(smpp_bearerbox->lock);
    if (smpp_bearerbox->alive) {
        result = 1;
    }
    gw_rwlock_unlock(smpp_bearerbox->lock);
    return result;
}

void smpp_bearerbox_add_to_queue(SMPPBearerboxState *smpp_bearerbox_state, SMPPBearerboxMsg *smpp_bearerbox_msg) {
    gw_rwlock_rdlock(smpp_bearerbox_state->lock);
    SMPPBearerbox *bearerbox = gwlist_search(smpp_bearerbox_state->bearerboxes, NULL, smpp_bearerbox_online);
    int ok = 0;
    if (bearerbox) {
        gw_prioqueue_produce(smpp_bearerbox_state->outbound_queue, smpp_bearerbox_msg);
    } else {
        if (smpp_bearerbox_state->smpp_server->database_enable_queue) {
            debug("smpp.bearerbox.add.to.queue", 0, "No bearerboxes connected, queuing to database");
            if (smpp_database_add_message(smpp_bearerbox_state->smpp_server, smpp_bearerbox_msg->msg)) {
                debug("smpp.bearerbox.add.to.queue", 0, "Message inserted into database.");
                ok = 1;
            } else {
                error(0, "Unable to insert into database, rejecting");
            }
        } else {
            error(0, "No bearerboxes connected, rejecting.");
        }

        if (!ok) {
            smpp_bearerbox_msg->callback(smpp_bearerbox_msg->context, SMPP_ESME_RSYSERR);
        } else {
            smpp_bearerbox_msg->callback(smpp_bearerbox_msg->context, 0);
        }
        /* Done, either in database or rejected */
        
        debug("smpp.bearerbox.add.to.queue", 0, "BB[(null)] Destroying %s", octstr_get_cstr(smpp_bearerbox_msg->id));
        
        msg_destroy(smpp_bearerbox_msg->msg); /* Destroy this now, it will be reloaded from the database */
        smpp_bearerbox_msg_destroy(smpp_bearerbox_msg);
    }
    gw_rwlock_unlock(smpp_bearerbox_state->lock);
}

void smpp_bearerbox_add_message(SMPPServer *smpp_server, Msg *msg, void(*callback)(void*, int), void *context) {
    SMPPBearerboxMsg *smpp_bearerbox_msg = smpp_bearerbox_msg_create(msg, callback, context);
    SMPPBearerboxState *smpp_bearerbox_state = smpp_server->bearerbox;


    smpp_bearerbox_add_to_queue(smpp_bearerbox_state, smpp_bearerbox_msg);
}

int smpp_bearerbox_msg_compare(const void *a, const void *b) {
    SMPPBearerboxMsg *msga = (SMPPBearerboxMsg*) a;
    SMPPBearerboxMsg *msgb = (SMPPBearerboxMsg*) b;

    return sms_priority_compare(msga->msg, msgb->msg);
}

SMPPBearerbox *smpp_bearerbox_create() {
    SMPPBearerbox *smpp_bearerbox = gw_malloc(sizeof (SMPPBearerbox));
    smpp_bearerbox->connection = NULL;
    smpp_bearerbox->host = NULL;
    smpp_bearerbox->id = NULL;
    smpp_bearerbox->last_msg = 0l;
    smpp_bearerbox->port = 0l;
    smpp_bearerbox->smpp_bearerbox_state = NULL;
    smpp_bearerbox->lock = gw_rwlock_create();
    smpp_bearerbox->ack_lock = gw_rwlock_create();
    smpp_bearerbox->ssl = 0;
    smpp_bearerbox->writer_alive = 0;
    smpp_bearerbox->open_acks = NULL;
    smpp_bearerbox->alive = 0;
    return smpp_bearerbox;
}

void smpp_bearerbox_destroy(SMPPBearerbox *smpp_bearerbox) {
    octstr_destroy(smpp_bearerbox->host);
    octstr_destroy(smpp_bearerbox->id);
    conn_destroy(smpp_bearerbox->connection);
    gw_rwlock_destroy(smpp_bearerbox->lock);
    gw_rwlock_destroy(smpp_bearerbox->ack_lock);
    dict_destroy(smpp_bearerbox->open_acks);

    gw_free(smpp_bearerbox);
}

void smpp_bearerbox_requeue_routing_done(void *context, SMPPRouteStatus *smpp_route_status) {
    SMPPDatabaseMsg *smpp_database_msg = context;
    Octstr *ack_id = smpp_uuid_get(smpp_database_msg->msg->sms.id);
    SMPPEsme *smpp_esme;
    List *queued_outbound_pdus;
    SMPPQueuedPDU *smpp_queued_deliver_pdu;
    SMPP_PDU *pdu;
    
    if(smpp_route_status->status == SMPP_ESME_ROK) {
        smpp_esme = smpp_esme_find_best_receiver(smpp_database_msg->smpp_server, smpp_database_msg->msg->sms.service);
        if (smpp_esme) {
            info(0, "SMPP[%s] Successfully routed message for %s", octstr_get_cstr(smpp_esme->system_id), octstr_get_cstr(smpp_database_msg->msg->sms.receiver));
            queued_outbound_pdus = smpp_pdu_msg_to_pdu(smpp_esme, smpp_database_msg->msg);

            while ((pdu = gwlist_consume(queued_outbound_pdus)) != NULL) {
                smpp_queued_deliver_pdu = smpp_queued_pdu_create();
                smpp_queued_deliver_pdu->pdu = pdu;
                smpp_queued_deliver_pdu->system_id = octstr_duplicate(smpp_esme->system_id);
                smpp_queued_deliver_pdu->pdu->u.deliver_sm.sequence_number = counter_increase(smpp_esme->sequence_number);
                smpp_queued_deliver_pdu->callback = smpp_queues_callback_deliver_sm_resp;
                smpp_queued_deliver_pdu->context = smpp_queued_deliver_pdu;
                smpp_queued_deliver_pdu->smpp_esme = smpp_esme;
                smpp_queued_deliver_pdu->smpp_server = smpp_database_msg->smpp_server;
                smpp_queued_deliver_pdu->id = octstr_format("%ld", smpp_queued_deliver_pdu->pdu->u.deliver_sm.sequence_number);
                smpp_queued_deliver_pdu->global_id = smpp_database_msg->global_id;
                smpp_queues_add_outbound(smpp_queued_deliver_pdu);
            }
            gwlist_destroy(queued_outbound_pdus, NULL);
            if(smpp_database_msg->wakeup_thread_id != 0) {
                gwthread_wakeup(smpp_database_msg->wakeup_thread_id);
            }
        }  else {
            debug("smpp.bearerbox.requeue.routing.done", 0, "No receivers connected for %s", octstr_get_cstr(smpp_database_msg->msg->sms.service));
            smpp_database_remove(smpp_database_msg->smpp_server, smpp_database_msg->global_id, 1);
        }
    } else {        
        smpp_database_remove(smpp_database_msg->smpp_server, smpp_database_msg->global_id, 0);
    }
    
    octstr_destroy(ack_id);
    smpp_database_msg_destroy(smpp_database_msg);
    smpp_route_status_destroy(smpp_route_status);
}

void smpp_bearerbox_routing_done(void *context, SMPPRouteStatus *smpp_route_status) {
    SMPPBearerboxMsg *smpp_bearerbox_msg = context;
    SMPPBearerbox *smpp_bearerbox = smpp_bearerbox_msg->context;
    Octstr *ack_id = smpp_uuid_get(smpp_bearerbox_msg->msg->sms.id);
    SMPPEsme *smpp_esme;
    List *queued_outbound_pdus;
    SMPPQueuedPDU *smpp_queued_deliver_pdu;
    SMPP_PDU *pdu;
    
    
    
    if(smpp_route_status->status == SMPP_ESME_ROK) {
        smpp_esme = smpp_esme_find_best_receiver(smpp_bearerbox->smpp_bearerbox_state->smpp_server, smpp_bearerbox_msg->msg->sms.service);
        if (smpp_esme) {
            info(0, "SMPP[%s] Successfully routed message for %s", octstr_get_cstr(smpp_esme->system_id), octstr_get_cstr(smpp_bearerbox_msg->msg->sms.receiver));
            queued_outbound_pdus = smpp_pdu_msg_to_pdu(smpp_esme, smpp_bearerbox_msg->msg);

            while ((queued_outbound_pdus != NULL) && (pdu = gwlist_consume(queued_outbound_pdus)) != NULL) {
                smpp_queued_deliver_pdu = smpp_queued_pdu_create();
                smpp_queued_deliver_pdu->pdu = pdu;
                smpp_queued_deliver_pdu->system_id = octstr_duplicate(smpp_esme->system_id);
                smpp_queued_deliver_pdu->bearerbox = smpp_bearerbox;
                smpp_queued_deliver_pdu->pdu->u.deliver_sm.sequence_number = counter_increase(smpp_esme->sequence_number);
                smpp_queued_deliver_pdu->callback = smpp_queues_callback_deliver_sm_resp;
                smpp_queued_deliver_pdu->context = smpp_queued_deliver_pdu;
                smpp_queued_deliver_pdu->smpp_esme = smpp_esme;
                smpp_queued_deliver_pdu->id = octstr_format("%ld", smpp_queued_deliver_pdu->pdu->u.deliver_sm.sequence_number);
                smpp_queued_deliver_pdu->bearerbox_id = octstr_duplicate(ack_id);
                smpp_queues_add_outbound(smpp_queued_deliver_pdu);
            }
            gwlist_destroy(queued_outbound_pdus, NULL);
        } else {
            if (smpp_bearerbox->smpp_bearerbox_state->smpp_server->database_enable_queue) {
                warning(0, "SMPP[%s] Has no receivers connected, queuing in database", octstr_get_cstr(smpp_bearerbox_msg->msg->sms.service));
                if (smpp_database_add_message(smpp_bearerbox->smpp_bearerbox_state->smpp_server, smpp_bearerbox_msg->msg)) {
                    debug("smpp.bearerbox.inbound.thread", 0, "Queued response message for %s to database", octstr_get_cstr(smpp_bearerbox_msg->msg->sms.service));
                    smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_success);
                } else {
                    smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_failed);
                }
            } else {
                warning(0, "ESME[%s] Has no receivers connected, reporting temporary failure", octstr_get_cstr(smpp_bearerbox_msg->msg->sms.service));
                smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_failed_tmp);
            }
        }
        
    } else {
        warning(0, "SMPP[%s] Could not route message for %s", octstr_get_cstr(smpp_bearerbox_msg->msg->sms.service), octstr_get_cstr(smpp_bearerbox_msg->msg->sms.receiver));
        smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_failed);
    }
    
    octstr_destroy(ack_id);
    msg_destroy(smpp_bearerbox_msg->msg);
    smpp_bearerbox_msg_destroy(smpp_bearerbox_msg);
    smpp_route_status_destroy(smpp_route_status);
}

int smpp_bearerbox_identify(SMPPBearerbox *smpp_bearerbox) {
    Msg *msg;
    msg = msg_create(admin);
    msg->admin.command = cmd_identify;
    msg->admin.boxc_id = octstr_duplicate(smpp_bearerbox->id);

    if (deliver_to_bearerbox_real(smpp_bearerbox->connection, msg) == -1) {
        msg_destroy(msg);
        return -1;
    }

    smpp_bearerbox->last_msg = time(NULL);

    return 0;
}

void smpp_bearerbox_outbound_thread(void *arg) {
    SMPPBearerbox *smpp_bearerbox = arg;
    SMPPBearerboxMsg *smpp_bearerbox_msg;

    int result;

    int requeue = 0;

    smpp_bearerbox->writer_alive = 1;

    while (!(smpp_bearerbox->smpp_bearerbox_state->smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
        /* If we are here it means we don't have a connection yet or the previous one died */
        if (!smpp_bearerbox_online(smpp_bearerbox, NULL)) {
            debug("smpp.bearerbox.outbound.thread", 0, "Bearerbox connection is still dead, waiting for reader thread to reconnect it %s:%ld %s", octstr_get_cstr(smpp_bearerbox->host), smpp_bearerbox->port, octstr_get_cstr(smpp_bearerbox->id));
            gwthread_sleep(1.0);
            continue;
        }


        while ((smpp_bearerbox_msg = gw_prioqueue_consume(smpp_bearerbox->smpp_bearerbox_state->outbound_queue)) != NULL) {
            gw_rwlock_rdlock(smpp_bearerbox->lock);
            requeue = 0;
            if ((smpp_bearerbox->alive) && (smpp_bearerbox->connection != NULL)) {
                if(msg_type(smpp_bearerbox_msg->msg) == sms) {
                    /* Only SMS messages get acks */
                    debug("smpp.bearerbox.outbound.thread", 0, "BB[%s] Adding %s to open acks", octstr_get_cstr(smpp_bearerbox->id), octstr_get_cstr(smpp_bearerbox_msg->id));
                    dict_put(smpp_bearerbox->open_acks, smpp_bearerbox_msg->id, smpp_bearerbox_msg);
                    if(smpp_bearerbox_msg->msg->sms.boxc_id) {
                        octstr_destroy(smpp_bearerbox_msg->msg->sms.boxc_id);
                    }
                    smpp_bearerbox_msg->msg->sms.boxc_id = octstr_duplicate(smpp_bearerbox->id);
                }
                result = deliver_to_bearerbox_real(smpp_bearerbox->connection, smpp_bearerbox_msg->msg);
                if (result == -1) {
                    dict_remove(smpp_bearerbox->open_acks, smpp_bearerbox_msg->id); /* We don't want this being cleaned up as it will be requeued */
                    error(0, "Error writing message to bearerbox, disconnecting");
                    smpp_bearerbox->alive = 0;
                    requeue = 1;
                } else {
                    /* Nothing else to do, it will get ack'd/nack'd and the reader thread will deal with clean up */
                }
            } else {
                requeue = 1;
            }
            gw_rwlock_unlock(smpp_bearerbox->lock);

            if (requeue) {
                smpp_bearerbox_add_to_queue(smpp_bearerbox->smpp_bearerbox_state, smpp_bearerbox_msg); /* Requeue */
            }

            if (!smpp_bearerbox_online(smpp_bearerbox, NULL)) {
                break;
            }
        }
    }

    /* No cleanup here, reader cleans up */
    smpp_bearerbox->writer_alive = 0;
}

void smpp_bearerbox_inbound_thread(void *arg) {
    SMPPBearerbox *smpp_bearerbox = arg;
    SMPPBearerboxMsg *smpp_bearerbox_msg;

    Msg *msg;
    int result;

    long diff;

    Octstr *ack_id;
    Octstr *system_id;
    SMPPEsme *smpp_esme;
    List *queued_outbound_pdus;
    SMPP_PDU *pdu;
    SMPPQueuedPDU *smpp_queued_deliver_pdu;
    List *keys;


    while (!(smpp_bearerbox->smpp_bearerbox_state->smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
        /* If we are here it means we don't have a connection yet or the previous one died */
        if (!smpp_bearerbox_online(smpp_bearerbox, NULL)) {
            debug("smpp.bearerbox.outbound.thread", 0, "Bearerbox connection is dead, (re)connecting it %s:%ld %s", octstr_get_cstr(smpp_bearerbox->host), smpp_bearerbox->port, octstr_get_cstr(smpp_bearerbox->id));
            gw_rwlock_wrlock(smpp_bearerbox->lock);
            conn_destroy(smpp_bearerbox->connection);
            
            if(smpp_bearerbox->open_acks) {
               /* This box has some pending acks, lets notify the binds they failed (temporary) */
               gw_rwlock_wrlock(smpp_bearerbox->ack_lock);
               keys = dict_keys(smpp_bearerbox->open_acks); 
               while((ack_id = gwlist_consume(keys)) != NULL) {
                   debug("smpp.bearerbox.inbound.thread", 0, "BB[%s] Ack cleanup removing %s", octstr_get_cstr(smpp_bearerbox->id), octstr_get_cstr(ack_id));
                   smpp_bearerbox_msg = dict_remove(smpp_bearerbox->open_acks, ack_id);
                   if(smpp_bearerbox_msg) {
                       smpp_bearerbox_msg->callback(smpp_bearerbox_msg->context, SMPP_ESME_RSYSERR);
                   }
                   smpp_bearerbox_msg_destroy(smpp_bearerbox_msg);
                   octstr_destroy(ack_id);
               }
               gwlist_destroy(keys, NULL);
               gw_rwlock_unlock(smpp_bearerbox->ack_lock);
            }

            dict_destroy(smpp_bearerbox->open_acks);
            smpp_bearerbox->open_acks = dict_create(512, (void(*)(void *))smpp_bearerbox_msg_destroy);

            smpp_bearerbox->connection = connect_to_bearerbox_real(smpp_bearerbox->host, smpp_bearerbox->port, smpp_bearerbox->ssl, NULL);

            if (smpp_bearerbox->connection != NULL) {
                if (smpp_bearerbox_identify(smpp_bearerbox) != -1) {
                    info(0, "Successfully connected and identified.");
                    /* Already have a write lock */
                    smpp_bearerbox->alive = 1;
                } else {
                    error(0, "Could not identify ourselves to bearerbox connection");
                }
            }


            gw_rwlock_unlock(smpp_bearerbox->lock);
        }

        if (!smpp_bearerbox_online(smpp_bearerbox, NULL)) {
            debug("smpp.bearerbox.outbound.thread", 0, "Connection still not alive, lets try again in a few seconds");
            gwthread_sleep(1.0);
            continue;
        }

        while ((result = read_from_bearerbox_real(smpp_bearerbox->connection, &msg, 1)) != -1) {

            if (result == 0) {
                debug("smpp.bearerbox.inbound.thread", 0, "Got msg from bearerbox");
                if (msg_type(msg) == ack) {
                    /* We deal with acks right here */

                    ack_id = smpp_uuid_get(msg->ack.id);

                    debug("smpp.bearerbox.inbound.thread", 0, "BB[%s] Message type ack %s", octstr_get_cstr(smpp_bearerbox->id), octstr_get_cstr(ack_id));

                    gw_rwlock_wrlock(smpp_bearerbox->ack_lock);
                    smpp_bearerbox_msg = dict_remove(smpp_bearerbox->open_acks, ack_id);
                    gw_rwlock_unlock(smpp_bearerbox->ack_lock);

                    if (smpp_bearerbox_msg != NULL) {
                        switch (msg->ack.nack) {
                            case ack_buffered:
                            case ack_success:
                                smpp_bearerbox_msg->callback(smpp_bearerbox_msg->context, 0);
                                break;
                            case ack_failed:
                                smpp_bearerbox_msg->callback(smpp_bearerbox_msg->context, SMPP_ESME_RSUBMITFAIL);
                                break;
                            case ack_failed_tmp:
                                smpp_bearerbox_msg->callback(smpp_bearerbox_msg->context, SMPP_ESME_RSYSERR);
                                break;
                            default:
                                debug("smpp.bearerbox.inbound.thread", 0, "Unknown ack.nack type: %ld.", msg->ack.nack);
                                smpp_bearerbox_msg->callback(smpp_bearerbox_msg->context, SMPP_ESME_RSYSERR);
                                break;
                        }
                        /* We're done with this message now */
                        smpp_bearerbox_msg_destroy(smpp_bearerbox_msg);
                    } else {
                        error(0, "Unknown ack %s received", octstr_get_cstr(ack_id));
                    }
                    octstr_destroy(ack_id);
                    msg_destroy(msg);
                } else if (msg_type(msg) == admin) {
                    switch (msg->admin.command) {
                        case cmd_shutdown:
                            warning(0, "Bearerbox asked us to shutdown, we'll just disconnect (naughty...)");
                            smpp_bearerbox_set_state(smpp_bearerbox, 0);
                            break;
                        case cmd_suspend:
                            /* Do nothing */
                            break;
                        case cmd_resume:
                            break;
                        case cmd_identify:
                            break;
                        case cmd_restart:
                            warning(0, "Bearerbox asked us to reconnect, OK");
                            smpp_bearerbox_set_state(smpp_bearerbox, 0);
                            break;
                    }
                    msg_destroy(msg);
                } else if(msg_type(msg) == sms) {
                    ack_id = smpp_uuid_get(msg->sms.id);
                    if((msg->sms.sms_type == report_mo) && octstr_len(msg->sms.dlr_url)) {
                        system_id = smpp_pdu_get_system_id_from_dlr_url(msg->sms.dlr_url);
                        debug("smpp.bearerbox.inbound.thread", 0, "Got message from bearerbox %s for %s", octstr_get_cstr(smpp_bearerbox->id), octstr_get_cstr(system_id));
                        smpp_esme = smpp_esme_find_best_receiver(smpp_bearerbox->smpp_bearerbox_state->smpp_server, system_id);
                        
                        if(smpp_esme) {
                            queued_outbound_pdus = smpp_pdu_msg_to_pdu(smpp_esme, msg);

                            if(queued_outbound_pdus != NULL) {
                                while ((pdu = gwlist_consume(queued_outbound_pdus)) != NULL) {
                                    smpp_queued_deliver_pdu = smpp_queued_pdu_create();
                                    smpp_queued_deliver_pdu->pdu = pdu;
                                    smpp_queued_deliver_pdu->system_id = octstr_duplicate(smpp_esme->system_id);
                                    smpp_queued_deliver_pdu->bearerbox = smpp_bearerbox;
                                    smpp_queued_deliver_pdu->pdu->u.deliver_sm.sequence_number = counter_increase(
                                            smpp_esme->sequence_number);
                                    smpp_queued_deliver_pdu->callback = smpp_queues_callback_deliver_sm_resp;
                                    smpp_queued_deliver_pdu->context = smpp_queued_deliver_pdu;
                                    smpp_queued_deliver_pdu->smpp_esme = smpp_esme;
                                    smpp_queued_deliver_pdu->id = octstr_format("%ld",
                                                                                smpp_queued_deliver_pdu->pdu->u.deliver_sm.sequence_number);
                                    smpp_queued_deliver_pdu->bearerbox_id = octstr_duplicate(ack_id);
                                    smpp_queues_add_outbound(smpp_queued_deliver_pdu);
                                }
                                gwlist_destroy(queued_outbound_pdus, NULL);
                            }
                        } else {
                            
                            if(smpp_bearerbox->smpp_bearerbox_state->smpp_server->database_enable_queue) {
                                warning(0, "ESME[%s] Has no receivers connected, queuing in database", octstr_get_cstr(system_id));
                                if(octstr_len(system_id) && smpp_database_add_message(smpp_bearerbox->smpp_bearerbox_state->smpp_server, msg)) {
                                    debug("smpp.bearerbox.inbound.thread", 0, "Queued response message for %s to database", octstr_get_cstr(system_id));
                                    smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_success);
                                } else {
                                    warning(0, "Could not queue message for %s, reporting failure to bearerbox", octstr_get_cstr(system_id));
                                    smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_failed);
                                }
                            } else {
                                warning(0, "ESME[%s] Has no receivers connected, reporting temporary failure", octstr_get_cstr(system_id));
                                smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_failed_tmp);
                            }
                        }

                        
                        
                        octstr_destroy(system_id);
                    } else if(msg->sms.sms_type == mo) {
                        smpp_bearerbox_msg = smpp_bearerbox_msg_create(msg_duplicate(msg), NULL, smpp_bearerbox);
//                        smpp_bearerbox_msg->context = smpp_bearerbox;
//                        smpp_bearerbox_msg->msg = msg_duplicate(msg);
                        smpp_route_message(smpp_bearerbox->smpp_bearerbox_state->smpp_server, SMPP_ROUTE_DIRECTION_INBOUND, smpp_bearerbox_msg->msg->sms.smsc_id, NULL, smpp_bearerbox_msg->msg, smpp_bearerbox_routing_done, smpp_bearerbox_msg);
                    } else {
                        debug("smpp.bearerbox.inbound.thread", 0, "No DLR URL or unknown type, now what? Ack'ing so BB doesn't spin");
                        smpp_bearerbox_acknowledge(smpp_bearerbox, ack_id, ack_failed);
                    }
                    octstr_destroy(ack_id);
                    msg_destroy(msg);
                } else {
                    warning(0, "Unknown message type received destroying message");
                    msg_destroy(msg);
//                    gw_prioqueue_produce(smpp_bearerbox->smpp_bearerbox_state->inbound_queue, msg);
                }
            } else {
                /* Timeout */
                diff = time(NULL) - smpp_bearerbox->last_msg;

                if (diff > HEARTBEAT_INTERVAL) {
                    msg = msg_create(heartbeat);
                    smpp_bearerbox_deliver(smpp_bearerbox, msg); /* Implicit destroy */
                }
            }

            if (!smpp_bearerbox_online(smpp_bearerbox, NULL)) {
                break;
            }

            if (smpp_bearerbox->smpp_bearerbox_state->smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN) {
                break;
            }
        }

        smpp_bearerbox_set_state(smpp_bearerbox, 0);

        error(0, "Connection to bearerbox broke!");
    }

    info(0, "Shutting down bearerbox reader thread");
    smpp_bearerbox_set_state(smpp_bearerbox, 0);

    debug("smpp.bearerbox.inbound.thread", 0, "Waiting for the writing thread to exit");
    while (smpp_bearerbox->writer_alive) {
        gwthread_sleep(0.1);
    }

    smpp_bearerbox_destroy(smpp_bearerbox);

}

void smpp_bearerbox_requeue_result(void *context, int status) {
    SMPPDatabaseMsg *smpp_database_msg = context;
    
    if(status == 0) {
        /* Successfully processed */
        debug("smpp.bearerbox.requeue.result", 0, "Message %lu requeued successfully, removing", smpp_database_msg->global_id);
        smpp_database_msg->msg = NULL; /* it's been destroyed by the sender */
        smpp_database_remove(smpp_database_msg->smpp_server, smpp_database_msg->global_id, 0);
    } else if(status == SMPP_ESME_RSUBMITFAIL) {
        error(0, "Unable to requeue message %lu will not attempt again (permanent failure)", smpp_database_msg->global_id);
    } else {
        error(0, "Unable to requeue message %lu will attempt later", smpp_database_msg->global_id);
    }
    
    smpp_database_msg_destroy(smpp_database_msg);
}

void smpp_bearerbox_requeue_thread(void *arg) {
    SMPPBearerboxState *smpp_bearerbox_state = arg;
    SMPPServer *smpp_server = smpp_bearerbox_state->smpp_server;
    SMPPBearerbox *bearerbox;
    SMPPDatabaseMsg *smpp_database_msg;
    SMPPBearerboxMsg *smpp_bearerbox_msg;
    
    List *stored;
    Msg *msg;

    int busy = 0;

    info(0, "Starting bearerbox requeue thread");

    while (!(smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
        busy = 0;
        gw_rwlock_rdlock(smpp_bearerbox_state->lock);
        bearerbox = gwlist_search(smpp_bearerbox_state->bearerboxes, NULL, smpp_bearerbox_online);
        if (bearerbox) {
            stored = smpp_database_get_stored(smpp_server, mt_push, NULL, 0);

            while ((smpp_database_msg = gwlist_consume(stored)) != NULL) {                
                msg = smpp_database_msg->msg;
                
                if(msg->sms.sms_type == mt_push) {
                    debug("smpp.bearerbox.requeue.thread", 0, "Got MT message to requeue sender = %s receiver = %s", octstr_get_cstr(smpp_database_msg->msg->sms.sender), octstr_get_cstr(smpp_database_msg->msg->sms.receiver));
                    smpp_bearerbox_msg = smpp_bearerbox_msg_create(msg, smpp_bearerbox_requeue_result, smpp_database_msg);
                    smpp_bearerbox_msg->msg = msg_duplicate(msg);
                    gw_prioqueue_produce(smpp_bearerbox_state->outbound_queue, smpp_bearerbox_msg);
                    busy = 1;
                } else {
                    debug("smpp.bearerbox.requeue.thread", 0, "Unknown message type received %ld, deleting", msg->sms.sms_type);
                    smpp_database_remove(smpp_database_msg->smpp_server, smpp_database_msg->global_id, 0);
                    smpp_database_msg_destroy(smpp_database_msg);
                }
            }
            gwlist_destroy(stored, NULL);
            
            stored = smpp_database_get_stored(smpp_server, mo, NULL, 0);

            while ((smpp_database_msg = gwlist_consume(stored)) != NULL) {
                msg = smpp_database_msg->msg;
                
                if(msg->sms.sms_type == mo) {
                    smpp_database_msg->wakeup_thread_id = gwthread_self();
                    debug("smpp.bearerbox.requeue.thread", 0, "Got MO message to requeue sender = %s receiver = %s", octstr_get_cstr(smpp_database_msg->msg->sms.sender), octstr_get_cstr(smpp_database_msg->msg->sms.receiver));
                    smpp_route_message(smpp_server, SMPP_ROUTE_DIRECTION_INBOUND,
                                       smpp_database_msg->msg->sms.smsc_id, NULL, smpp_database_msg->msg,
                                       smpp_bearerbox_requeue_routing_done, smpp_database_msg);

                } else {
                    debug("smpp.bearerbox.requeue.thread", 0, "Unknown message type received %ld, deleting", msg->sms.sms_type);
                    smpp_database_remove(smpp_database_msg->smpp_server, smpp_database_msg->global_id, 0);
                    smpp_database_msg_destroy(smpp_database_msg);
                }
            }
            gwlist_destroy(stored, NULL);
        } else {
            warning(0, "No bearerboxes active, waiting to try again");
        }

        gw_rwlock_unlock(smpp_bearerbox_state->lock);
     
        
        if (!busy) {
            gwthread_sleep(10.0);
        }
    }

    info(0, "Shutting down requeue thread");
}

void smpp_bearerbox_init(SMPPServer *smpp_server) {
    SMPPBearerboxState *smpp_bearerbox_state = gw_malloc(sizeof (SMPPBearerboxState));
    smpp_bearerbox_state->outbound_queue = gw_prioqueue_create(smpp_bearerbox_msg_compare);
    smpp_bearerbox_state->inbound_queue = gw_prioqueue_create(smpp_bearerbox_msg_compare);
    smpp_bearerbox_state->bearerboxes = gwlist_create();
    smpp_bearerbox_state->smpp_server = smpp_server;
    smpp_bearerbox_state->lock = gw_rwlock_create();

    SMPPBearerbox *smpp_bearerbox;

    CfgGroup *grp;
    List *grplist = cfg_get_multi_group(smpp_server->running_configuration, octstr_imm("bearerbox-connection"));
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        smpp_bearerbox = smpp_bearerbox_create();
        smpp_bearerbox->host = cfg_get(grp, octstr_imm("host"));
        smpp_bearerbox->id = cfg_get(grp, octstr_imm("id"));
        smpp_bearerbox->open_acks = dict_create(512, (void(*)(void *))smpp_bearerbox_msg_destroy);

        cfg_get_integer(&smpp_bearerbox->port, grp, octstr_imm("port"));

        cfg_get_bool(&smpp_bearerbox->ssl, grp, octstr_imm("ssl"));

        if (octstr_len(smpp_bearerbox->host) && (smpp_bearerbox->port)) {
            info(0, "Successfully configured bearerbox %s:%ld %s", octstr_get_cstr(smpp_bearerbox->host), smpp_bearerbox->port, octstr_get_cstr(smpp_bearerbox->id));
            smpp_bearerbox->smpp_bearerbox_state = smpp_bearerbox_state;
            gwlist_produce(smpp_bearerbox_state->bearerboxes, smpp_bearerbox);
        } else {
            warning(0, "Mandatory parameters missing for bearerbox %s:%ld %s", octstr_get_cstr(smpp_bearerbox->host), smpp_bearerbox->port, octstr_get_cstr(smpp_bearerbox->id));
            smpp_bearerbox_destroy(smpp_bearerbox);
        }
    }

    gwlist_destroy(grplist, NULL);

    long num = gwlist_len(smpp_bearerbox_state->bearerboxes);
    long i;

    if (num > 0) {
        gw_prioqueue_add_producer(smpp_bearerbox_state->outbound_queue);
        gw_prioqueue_add_producer(smpp_bearerbox_state->inbound_queue);

        for (i = 0; i < num; i++) {
            smpp_bearerbox = gwlist_get(smpp_bearerbox_state->bearerboxes, i);

            gwthread_create(smpp_bearerbox_inbound_thread, smpp_bearerbox);
            gwthread_create(smpp_bearerbox_outbound_thread, smpp_bearerbox);
        }
    } else {
        panic(0, "No valid 'bearerbox-connection' configuration specified, cannot continue");
    }

    smpp_server->bearerbox = smpp_bearerbox_state;

    if (smpp_server->database_enable_queue) {
        smpp_bearerbox_state->pending_requeues = NULL;
        smpp_bearerbox_state->requeue_thread_id = gwthread_create(smpp_bearerbox_requeue_thread, smpp_bearerbox_state);
    }

}

void smpp_bearerbox_shutdown(SMPPServer *smpp_server) {
    SMPPBearerboxState *smpp_bearerbox_state = smpp_server->bearerbox;
    gw_prioqueue_remove_producer(smpp_bearerbox_state->outbound_queue);
    gw_prioqueue_remove_producer(smpp_bearerbox_state->inbound_queue);

    gwthread_join_every(smpp_bearerbox_outbound_thread);
    gwthread_join_every(smpp_bearerbox_inbound_thread);
    gwthread_join_every(smpp_bearerbox_requeue_thread);

    gw_rwlock_destroy(smpp_bearerbox_state->lock);
    
    gw_prioqueue_destroy(smpp_bearerbox_state->outbound_queue, (void(*)(void *))smpp_bearerbox_msg_destroy);
    gw_prioqueue_destroy(smpp_bearerbox_state->inbound_queue, (void(*)(void *))smpp_bearerbox_msg_destroy);
    gwlist_destroy(smpp_bearerbox_state->bearerboxes, NULL);


    gw_free(smpp_bearerbox_state);

}


