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

int smpp_queues_add_outbound(SMPPQueuedPDU *smpp_queued_pdu) {
    if(smpp_queued_pdu->callback) {
        /* This has a callback function so we're going to store this as an open ack */
        SMPPQueuedPDU *other = dict_get(smpp_queued_pdu->smpp_esme->open_acks, smpp_queued_pdu->id);
        if(other) {
            error(0, "SMPP[%s:%ld] Collision for pending ack %s (ESME is behaving badly...), ignoring", octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id), smpp_queued_pdu->smpp_esme->id, octstr_get_cstr(smpp_queued_pdu->id));
        } else {
            debug("smpp.queues.add.outbound", 0, "Added ack callback for %s", octstr_get_cstr(smpp_queued_pdu->id));
        }
        dict_put(smpp_queued_pdu->smpp_esme->open_acks, smpp_queued_pdu->id, smpp_queued_pdu);
    }
    
    counter_increase(smpp_queued_pdu->smpp_esme->outbound_queued);
    gw_prioqueue_produce(smpp_queued_pdu->smpp_esme->smpp_server->outbound_queue, smpp_queued_pdu);
    return 0;
}

int smpp_queues_add_inbound(SMPPQueuedPDU *smpp_queued_pdu) {
    counter_increase(smpp_queued_pdu->smpp_esme->inbound_queued);
    gw_prioqueue_produce(smpp_queued_pdu->smpp_esme->smpp_server->inbound_queue, smpp_queued_pdu);
    return 0;
}

int smpp_queues_send_pdu(Connection *conn, Octstr *id, SMPP_PDU *pdu) {
    Octstr *os;
    int ret;

    smpp_pdu_dump(id, pdu);
    os = smpp_pdu_pack(id, pdu);
    if (os) {
        ret = conn_write(conn, os); /* Caller checks for write errors later */
        octstr_destroy(os);
    } else {
        ret = -1;
    }
    return ret;
}

void smpp_queues_process_ack(SMPPEsme *smpp_esme, long sequence_number, long command_status) {
    smpp_esme_outbound_load_increase(smpp_esme);
    
    Octstr *key = octstr_format("%ld", sequence_number);
    
    gw_rwlock_wrlock(smpp_esme->ack_process_lock);
    SMPPQueuedPDU *smpp_queued_pdu = dict_remove(smpp_esme->open_acks, key);
    gw_rwlock_unlock(smpp_esme->ack_process_lock); /* Shouldn't be readable by the cleanup thread now */
    
    if(!smpp_queued_pdu) {
        error(0, "SMPP[%s] Unknown ack %s received, ignoring", octstr_get_cstr(smpp_esme->system_id), octstr_get_cstr(key));
        octstr_destroy(key);
        return;
    }
    
    debug("smpp.queues.process.ack", 0, "SMPP[%s:%ld] Processing ack %s",octstr_get_cstr(smpp_esme->system_id), smpp_esme->id, octstr_get_cstr(key));
    
    if(smpp_queued_pdu) {
        if(smpp_queued_pdu->callback) {
            smpp_queued_pdu->callback(smpp_queued_pdu, command_status);
        }
    }
    
    octstr_destroy(key);
}

void smpp_queues_callback_deliver_sm_resp(void *context, long status) {
    SMPPQueuedPDU *smpp_queued_pdu = context;
    int destroy = 1;
    debug("smpp.queues.callback.deliver.sm.resp", 0, "SMPP[%s] Got deliver_sm callback status %ld %p", octstr_get_cstr(smpp_queued_pdu->system_id), status, smpp_queued_pdu);
    if(status == SMPP_ESME_ROK) {
        if(smpp_queued_pdu->pdu && (smpp_queued_pdu->pdu->type == deliver_sm)) {
            if(smpp_queued_pdu->pdu->u.deliver_sm.esm_class & (0x04|0x08|0x20)) {
                /* DLR */
                counter_increase(smpp_queued_pdu->smpp_esme->dlr_counter);
                counter_increase(smpp_queued_pdu->smpp_esme->smpp_esme_global->dlr_counter);
            } else {
                /* MO */
                counter_increase(smpp_queued_pdu->smpp_esme->mo_counter);
                counter_increase(smpp_queued_pdu->smpp_esme->smpp_esme_global->mo_counter);
            }
        }
        if((smpp_queued_pdu->bearerbox) && (smpp_queued_pdu->bearerbox_id)) {
            smpp_bearerbox_acknowledge(smpp_queued_pdu->bearerbox, smpp_queued_pdu->bearerbox_id, ack_success);
        } else if(smpp_queued_pdu->global_id > 0) {
            /* Database remove */
            smpp_database_remove(smpp_queued_pdu->smpp_server, smpp_queued_pdu->global_id, 0);
        }
    } else if(status == SMPP_ESME_COMMAND_STATUS_QUEUED) {
        if((smpp_queued_pdu->bearerbox) && (smpp_queued_pdu->bearerbox_id)) {
            smpp_bearerbox_acknowledge(smpp_queued_pdu->bearerbox, smpp_queued_pdu->bearerbox_id, ack_success);
        } else if(smpp_queued_pdu->global_id > 0) {
            /* Database remove */
            smpp_database_remove(smpp_queued_pdu->smpp_server, smpp_queued_pdu->global_id, 0);
        }
    } else if(status == SMPP_ESME_COMMAND_STATUS_WAIT_ACK_TIMEOUT) {
        if(smpp_queued_pdu->smpp_server->wait_ack_action == SMPP_WAITACK_DISCONNECT) {
            /* Disconnect this client for misbehaving*/
            if ((smpp_queued_pdu->bearerbox) && (smpp_queued_pdu->bearerbox_id)) {
                smpp_bearerbox_acknowledge(smpp_queued_pdu->bearerbox, smpp_queued_pdu->bearerbox_id, ack_failed_tmp);
            }
            error(0, "SMPP[%s] ESME has not ack'd message sequence %lu, disconnecting",
                  octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id),
                  smpp_queued_pdu->pdu->u.deliver_sm.sequence_number);
            smpp_esme_disconnect(smpp_queued_pdu->smpp_esme);
        } else if(smpp_queued_pdu->smpp_server->wait_ack_action == SMPP_WAITACK_DROP) {
            error(0, "SMPP[%s] ESME has not ack'd message sequence %lu, dropping",
                  octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id),
                  smpp_queued_pdu->pdu->u.deliver_sm.sequence_number);
            if((smpp_queued_pdu->bearerbox) && (smpp_queued_pdu->bearerbox_id)) {
                smpp_bearerbox_acknowledge(smpp_queued_pdu->bearerbox, smpp_queued_pdu->bearerbox_id, ack_success);
            } else if(smpp_queued_pdu->global_id > 0) {
                /* Database remove */
                smpp_database_remove(smpp_queued_pdu->smpp_server, smpp_queued_pdu->global_id, 0);
            }
        }
    } else if(status == SMPP_QUEUED_PDU_DESTROYED) {
        /* Upstream is just notifying us we must destroy our context, which we do anyway */
        smpp_bearerbox_acknowledge(smpp_queued_pdu->bearerbox, smpp_queued_pdu->bearerbox_id, ack_failed_tmp);
    }
    
    if(destroy) {
        smpp_queued_pdu_destroy(smpp_queued_pdu);
    }
}

void smpp_queues_callback_submit_sm(void *context, int status) {
    SMPPQueuedPDU *smpp_queued_response_pdu = context;

    if (status != SMPP_ESME_ROK) {
        counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
        counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
        octstr_destroy(smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id);
        smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = NULL;
        smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = status;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
    } else {
        counter_increase(smpp_queued_response_pdu->smpp_esme->mt_counter);
        counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->mt_counter);
        smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = status;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
    }
}

void smpp_queues_callback_data_sm(void *context, int status) {
    SMPPQueuedPDU *smpp_queued_response_pdu = context;

    if (status != SMPP_ESME_ROK) {
        counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
        counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
        octstr_destroy(smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id);
        smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id = NULL;
        smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = status;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
    } else {
        counter_increase(smpp_queued_response_pdu->smpp_esme->mt_counter);
        counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->mt_counter);
        smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = status;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
    }
}

void smpp_queues_msg_set_dlr_url(SMPPEsme *smpp_esme, Msg *msg) {
    octstr_destroy(msg->sms.dlr_url);
    msg->sms.dlr_url = octstr_duplicate(smpp_esme->system_id);
    Octstr *id = smpp_uuid_get(msg->sms.id);
    octstr_format_append(msg->sms.dlr_url, "|%ld|%S", time(NULL), id);
    octstr_destroy(id);
}

void smpp_queues_submit_routing_done(void *context, SMPPRouteStatus *smpp_route_status) {
    SMPPQueuedPDU *smpp_queued_response_pdu = context;
    counter_decrease(smpp_queued_response_pdu->smpp_esme->pending_routing);
    double cost = smpp_route_status->parts * smpp_route_status->cost;
    if(smpp_route_status->status == SMPP_ESME_ROK) {
        if(!smpp_queued_response_pdu->smpp_esme->smpp_esme_global->enable_prepaid_billing || smpp_database_deduct_credit(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->smpp_esme->system_id, cost)) {
            info(0, "SMPP[%s] Successfully routed message for %s to %s for cost %f", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), cost);
            
            octstr_destroy(smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id);
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = smpp_uuid_get(smpp_queued_response_pdu->msg->sms.id);
            
            smpp_queues_msg_set_dlr_url(smpp_queued_response_pdu->smpp_esme, smpp_queued_response_pdu->msg);
            
            smpp_bearerbox_add_message(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->msg, smpp_queues_callback_submit_sm, smpp_queued_response_pdu);
        } else {
            counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
            counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
            warning(0, "SMPP[%s] Successfully routed message for %s to %s for cost %f, but not enough credit", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), cost);
            octstr_destroy(smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id);
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = NULL;
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = SMPP_ESME_RSUBMITFAIL;
            msg_destroy(smpp_queued_response_pdu->msg);
            smpp_queued_response_pdu->msg = NULL;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
        }
    } else {
        if(octstr_len(smpp_queued_response_pdu->msg->sms.smsc_id)) {
            cost = smpp_route_status->parts * smpp_queued_response_pdu->smpp_esme->default_cost;
            if(!smpp_queued_response_pdu->smpp_esme->smpp_esme_global->enable_prepaid_billing || smpp_database_deduct_credit(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->smpp_esme->system_id, cost)) {
                info(0, "SMPP[%s] Using default routing for %s to %s for cost %f", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), smpp_queued_response_pdu->smpp_esme->default_cost);
                
                octstr_destroy(smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id);
                smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = smpp_uuid_get(smpp_queued_response_pdu->msg->sms.id);

                smpp_queues_msg_set_dlr_url(smpp_queued_response_pdu->smpp_esme, smpp_queued_response_pdu->msg);
                
                smpp_bearerbox_add_message(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->msg, smpp_queues_callback_submit_sm, smpp_queued_response_pdu);
            } else {
                counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
                counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
                warning(0, "SMPP[%s] Successfully routed message for %s to %s for cost %f, but not enough credit", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), cost);
                octstr_destroy(smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id);
                smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = NULL;
                smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = SMPP_ESME_RSUBMITFAIL;
                msg_destroy(smpp_queued_response_pdu->msg);
                smpp_queued_response_pdu->msg = NULL;
                smpp_queues_add_outbound(smpp_queued_response_pdu);
            }
        } else {
            counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
            counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
            warning(0, "SMPP[%s] could not route message to %s, rejecting", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver));
            octstr_destroy(smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id);
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = NULL;
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = smpp_route_status->status;
            msg_destroy(smpp_queued_response_pdu->msg);
            smpp_queued_response_pdu->msg = NULL;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
        }
    }
    smpp_route_status_destroy(smpp_route_status);
}

void smpp_queues_data_sm_routing_done(void *context, SMPPRouteStatus *smpp_route_status) {
    SMPPQueuedPDU *smpp_queued_response_pdu = context;
    counter_decrease(smpp_queued_response_pdu->smpp_esme->pending_routing);
    double cost = smpp_route_status->parts * smpp_route_status->cost;
    if(smpp_route_status->status == SMPP_ESME_ROK) {
        if(!smpp_queued_response_pdu->smpp_esme->smpp_esme_global->enable_prepaid_billing || smpp_database_deduct_credit(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->smpp_esme->system_id, cost)) {
            info(0, "SMPP[%s] Successfully routed message for %s to %s for cost %f", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), cost);
            smpp_bearerbox_add_message(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->msg, smpp_queues_callback_data_sm, smpp_queued_response_pdu);
        } else {
            counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
            counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
            warning(0, "SMPP[%s] Successfully routed message for %s to %s for cost %f, but not enough credit", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), cost);
            octstr_destroy(smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id);
            smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id = NULL;
            smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = SMPP_ESME_RSUBMITFAIL;
            msg_destroy(smpp_queued_response_pdu->msg);
            smpp_queued_response_pdu->msg = NULL;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
        }
    } else {
        if(octstr_len(smpp_queued_response_pdu->msg->sms.smsc_id)) {
            cost = smpp_route_status->parts * smpp_queued_response_pdu->smpp_esme->default_cost;
            if(!smpp_queued_response_pdu->smpp_esme->smpp_esme_global->enable_prepaid_billing || smpp_database_deduct_credit(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->smpp_esme->system_id, cost)) {
                info(0, "SMPP[%s] Using default routing for %s to %s for cost %f", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), smpp_queued_response_pdu->smpp_esme->default_cost);
                smpp_bearerbox_add_message(smpp_queued_response_pdu->smpp_esme->smpp_server, smpp_queued_response_pdu->msg, smpp_queues_callback_data_sm, smpp_queued_response_pdu);
            } else {
                counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
                counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
                warning(0, "SMPP[%s] Successfully routed message for %s to %s for cost %f, but not enough credit", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.smsc_id), cost);
                octstr_destroy(smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id);
                smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id = NULL;
                smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = SMPP_ESME_RSUBMITFAIL;
                msg_destroy(smpp_queued_response_pdu->msg);
                smpp_queued_response_pdu->msg = NULL;
                smpp_queues_add_outbound(smpp_queued_response_pdu);
            }
        } else {
            counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
            counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
            warning(0, "SMPP[%s] could not route message to %s, rejecting", octstr_get_cstr(smpp_queued_response_pdu->smpp_esme->system_id), octstr_get_cstr(smpp_queued_response_pdu->msg->sms.receiver));
            octstr_destroy(smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id);
            smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id = NULL;
            smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = smpp_route_status->status;
            msg_destroy(smpp_queued_response_pdu->msg);
            smpp_queued_response_pdu->msg = NULL;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
        }
    }
    smpp_route_status_destroy(smpp_route_status);
}


void smpp_queues_handle_submit_sm(SMPPQueuedPDU *smpp_queued_pdu) {
    SMPPEsme *smpp_esme = smpp_queued_pdu->smpp_esme;
    SMPPQueuedPDU *smpp_queued_response_pdu = NULL, *smpp_queued_deliver_pdu = NULL;
    SMPP_PDU *pdu;
    
    if(!(smpp_esme->bind_type & SMPP_ESME_TRANSMIT)) {
        smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, submit_sm_resp, smpp_queued_pdu->pdu->u.submit_sm.sequence_number);
        smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = SMPP_ESME_RINVBNDSTS;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
        smpp_queued_pdu_destroy(smpp_queued_pdu);
        return;
    }
    
    double current_load = load_get(smpp_esme->smpp_esme_global->inbound_load, 1);
    
    if(smpp_esme->smpp_esme_global->throughput > 0 && (current_load > smpp_esme->smpp_esme_global->throughput)) {
        error(0, "SMPP[%s] Exceeded throughput %f, %f, throttling.", octstr_get_cstr(smpp_esme->system_id), current_load, smpp_esme->smpp_esme_global->throughput);
        smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, submit_sm_resp, smpp_queued_pdu->pdu->u.submit_sm.sequence_number);
        smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = SMPP_ESME_RTHROTTLED;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
        smpp_queued_pdu_destroy(smpp_queued_pdu);
        return;
    } else {
        debug("smpp.queues.handle.submit_sm", 0, "Current load is ok %f", current_load);
    }
    
    
    smpp_esme_inbound_load_increase(smpp_queued_pdu->smpp_esme);

    long error_reason;
    Msg *msg;
    List *queued_outbound_pdus;
    Octstr *tmp;
    
    int response_sent = 0;

    if (smpp_esme->simulate) {
        debug("smpp.queues.handle.submit.sm", 0, "Handling simulated submission");
        /* Simulation, no delivery */
        smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, submit_sm_resp, smpp_queued_pdu->pdu->u.submit_sm.sequence_number);
        unsigned long simulation_count = counter_value(smpp_esme->inbound_processed);

        if (smpp_esme->simulate_temporary_failure_every && ((simulation_count % smpp_esme->simulate_temporary_failure_every) == 0)) {
            counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
            counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
            debug("smpp.queues.handle.submit.sm", 0, "SMPP[%s:%ld] Simulating temporary failure", octstr_get_cstr(smpp_esme->system_id), smpp_esme->id);
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = SMPP_ESME_RMSGQFUL;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
        } else if (smpp_esme->simulate_permanent_failure_every && ((simulation_count % smpp_esme->simulate_permanent_failure_every) == 0)) {
            counter_increase(smpp_queued_response_pdu->smpp_esme->error_counter);
            counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->error_counter);
            debug("smpp.queues.handle.submit.sm", 0, "SMPP[%s:%ld] Simulating permanent failure", octstr_get_cstr(smpp_esme->system_id), smpp_esme->id);
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = SMPP_ESME_RSUBMITFAIL;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
        } else {
            msg = smpp_submit_sm_to_msg(smpp_queued_pdu->smpp_esme, smpp_queued_pdu->pdu, &error_reason);
            
            if(msg == NULL) {
                smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = error_reason;
                error(0, "Error converting to PDU %ld", error_reason);
                smpp_queues_add_outbound(smpp_queued_response_pdu);
            } else {
                counter_increase(smpp_queued_response_pdu->smpp_esme->mt_counter);
                counter_increase(smpp_queued_response_pdu->smpp_esme->smpp_esme_global->mt_counter);
                smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = SMPP_ESME_ROK;
                smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = smpp_uuid_get(msg->sms.id);
                
                if(smpp_esme->simulate_deliver_every && ((simulation_count % smpp_esme->simulate_deliver_every) == 0)) {
                    msg->sms.sms_type = report_mo;
                    
                    msg->sms.dlr_mask = DLR_SUCCESS;

                    smpp_queues_msg_set_dlr_url(smpp_esme, msg);
                    
                    debug("smpp.queues.handle.submit_sm", 0, "Built DLR-URL %s", octstr_get_cstr(msg->sms.dlr_url));
                    
                    smpp_queues_add_outbound(smpp_queued_response_pdu);
                    response_sent = 1;
                    
                    queued_outbound_pdus = smpp_pdu_msg_to_pdu(smpp_esme, msg);
                    
                    
                    while((pdu = gwlist_consume(queued_outbound_pdus)) != NULL) {                        
                        smpp_queued_deliver_pdu = smpp_queued_pdu_create();
                        smpp_queued_deliver_pdu->pdu = pdu;
                        smpp_queued_deliver_pdu->system_id = octstr_duplicate(smpp_esme->system_id);
                        gw_prioqueue_produce(smpp_esme->smpp_server->simulation_queue, smpp_queued_deliver_pdu);
                    }
                    gwlist_destroy(queued_outbound_pdus, NULL);
                    
                } 
                if(smpp_esme->simulate_mo_every && ((simulation_count % smpp_esme->simulate_mo_every) == 0)) {
                    debug("smpp.queues.handle.submit_sm", 0, "SMPP[%s] Simulating MO message to:%s", octstr_get_cstr(smpp_esme->system_id), octstr_get_cstr(msg->sms.sender));
                    msg->sms.sms_type = mo;
                    tmp = msg->sms.receiver;
                    msg->sms.receiver = msg->sms.sender;
                    msg->sms.sender = tmp;
                    
                    queued_outbound_pdus = smpp_pdu_msg_to_pdu(smpp_esme, msg);

                    while((pdu = gwlist_consume(queued_outbound_pdus)) != NULL) {                        
                        smpp_queued_deliver_pdu = smpp_queued_pdu_create();
                        smpp_queued_deliver_pdu->pdu = pdu;
                        smpp_queued_deliver_pdu->system_id = octstr_duplicate(smpp_esme->system_id);
                        gw_prioqueue_produce(smpp_esme->smpp_server->simulation_queue, smpp_queued_deliver_pdu);
                    }
                    gwlist_destroy(queued_outbound_pdus, NULL);
                }
                
                
                if(!response_sent) {
                    smpp_queues_add_outbound(smpp_queued_response_pdu);
                }
                   
            }
            msg_destroy(msg);
            
        }
       

        
        smpp_queued_pdu_destroy(smpp_queued_pdu);
    } else {
        /* Deliver to bearerbox */
        msg = smpp_submit_sm_to_msg(smpp_queued_pdu->smpp_esme, smpp_queued_pdu->pdu, &error_reason);


        if (msg == NULL) {
            smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, submit_sm_resp, smpp_queued_pdu->pdu->u.submit_sm.sequence_number);
            error(0, "SMPP[%s] Couldn't generate message from PDU, rejecting %ld", octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id), error_reason);
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.command_status = error_reason;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
        } else {
            if (octstr_len(smpp_esme->default_smsc)) {
                octstr_destroy(msg->sms.smsc_id);
                msg->sms.smsc_id = octstr_duplicate(smpp_esme->default_smsc);
            }

            smpp_queues_msg_set_dlr_url(smpp_esme, msg);
            
            smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, submit_sm_resp, smpp_queued_pdu->pdu->u.submit_sm.sequence_number);
            smpp_queued_response_pdu->pdu->u.submit_sm_resp.message_id = smpp_uuid_get(msg->sms.id);
            smpp_queued_response_pdu->msg = msg;
            
            counter_increase(smpp_esme->pending_routing);
            smpp_route_message(smpp_esme->smpp_server, SMPP_ROUTE_DIRECTION_OUTBOUND, NULL, smpp_esme->system_id, msg, smpp_queues_submit_routing_done, smpp_queued_response_pdu);

            /* We used to add here, but now we first route above and wait for a callback */
//            smpp_bearerbox_add_message(smpp_queued_response_pdu->smpp_esme->smpp_server, msg, smpp_queues_callback_submit_sm, smpp_queued_response_pdu);
        }
        
        smpp_queued_pdu_destroy(smpp_queued_pdu);
    }
}
void smpp_queues_handle_data_sm(SMPPQueuedPDU *smpp_queued_pdu) {
    SMPPEsme *smpp_esme = smpp_queued_pdu->smpp_esme;
    SMPPQueuedPDU *smpp_queued_response_pdu = NULL;

    if (!(smpp_esme->bind_type & SMPP_ESME_TRANSMIT)) {
        smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, data_sm_resp, smpp_queued_pdu->pdu->u.data_sm.sequence_number);
        smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = SMPP_ESME_RINVBNDSTS;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
        smpp_queued_pdu_destroy(smpp_queued_pdu);
        return;
    }

    double current_load = load_get(smpp_esme->smpp_esme_global->inbound_load, 1);

    if (smpp_esme->smpp_esme_global->throughput > 0 && (current_load > smpp_esme->smpp_esme_global->throughput)) {
        error(0, "SMPP[%s] Exceeded throughput %f, %f, throttling.", octstr_get_cstr(smpp_esme->system_id), current_load, smpp_esme->smpp_esme_global->throughput);
        smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, data_sm_resp, smpp_queued_pdu->pdu->u.data_sm.sequence_number);
        smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = SMPP_ESME_RTHROTTLED;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
        smpp_queued_pdu_destroy(smpp_queued_pdu);
        return;
    } else {
        debug("smpp.queues.handle.data_sm", 0, "Current load is ok %f", current_load);
    }


    smpp_esme_inbound_load_increase(smpp_queued_pdu->smpp_esme);

    long error_reason;
    Msg *msg;
    
    /* Deliver to bearerbox */
    msg = smpp_data_sm_to_msg(smpp_queued_pdu->smpp_esme, smpp_queued_pdu->pdu, &error_reason);


    if (msg == NULL) {
        smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, data_sm_resp, smpp_queued_pdu->pdu->u.data_sm.sequence_number);
        error(0, "SMPP[%s] Couldn't generate message from PDU, rejecting %ld", octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id), error_reason);
        smpp_queued_response_pdu->pdu->u.data_sm_resp.command_status = error_reason;
        smpp_queues_add_outbound(smpp_queued_response_pdu);
    } else {
        if (octstr_len(smpp_esme->default_smsc)) {
            octstr_destroy(msg->sms.smsc_id);
            msg->sms.smsc_id = octstr_duplicate(smpp_esme->default_smsc);
        }

        smpp_queues_msg_set_dlr_url(smpp_esme, msg);

        smpp_queued_response_pdu = smpp_queued_pdu_create_quick(smpp_esme, data_sm_resp, smpp_queued_pdu->pdu->u.data_sm.sequence_number);
        smpp_queued_response_pdu->pdu->u.data_sm_resp.message_id = smpp_uuid_get(msg->sms.id);
        smpp_queued_response_pdu->msg = msg;

        counter_increase(smpp_esme->pending_routing);
        smpp_route_message(smpp_esme->smpp_server, SMPP_ROUTE_DIRECTION_OUTBOUND, NULL, smpp_esme->system_id, msg, smpp_queues_data_sm_routing_done, smpp_queued_response_pdu);
    }

    smpp_queued_pdu_destroy(smpp_queued_pdu);
}

void smpp_queues_send_enquire_link(SMPPEsme *smpp_esme) {
    SMPPQueuedPDU *smpp_queued_response_pdu = NULL;
    smpp_queued_response_pdu = smpp_queued_pdu_create();
    smpp_queued_response_pdu->smpp_esme = smpp_esme;
    smpp_queued_response_pdu->pdu = smpp_pdu_create(enquire_link, counter_increase(smpp_esme->sequence_number));
    smpp_queues_add_outbound(smpp_queued_response_pdu);
}

void smpp_queues_handle_enquire_link(SMPPQueuedPDU *smpp_queued_pdu) {
    SMPPQueuedPDU *smpp_queued_response_pdu = NULL;
    switch (smpp_queued_pdu->pdu->type) {
        case enquire_link:
            smpp_queued_response_pdu = smpp_queued_pdu_create();
            smpp_queued_response_pdu->smpp_esme = smpp_queued_pdu->smpp_esme;
            smpp_queued_response_pdu->pdu = smpp_pdu_create(enquire_link_resp, smpp_queued_pdu->pdu->u.enquire_link.sequence_number);
            smpp_queues_add_outbound(smpp_queued_response_pdu);
            break;
    }
    smpp_queued_pdu_destroy(smpp_queued_pdu);
}

void smpp_queues_handle_unbind(SMPPQueuedPDU *smpp_queued_pdu) {
    SMPPQueuedPDU *smpp_queued_response_pdu = NULL;
    switch (smpp_queued_pdu->pdu->type) {
        case unbind:
            smpp_queued_response_pdu = smpp_queued_pdu_create();
            smpp_queued_response_pdu->smpp_esme = smpp_queued_pdu->smpp_esme;
            smpp_queued_response_pdu->pdu = smpp_pdu_create(unbind_resp, smpp_queued_pdu->pdu->u.unbind.sequence_number);
            smpp_queued_response_pdu->disconnect = 1;
            smpp_queues_add_outbound(smpp_queued_response_pdu);
            break;
    }
    smpp_queued_pdu_destroy(smpp_queued_pdu);
}

void smpp_queues_handle_bind_pdu(SMPPQueuedPDU *smpp_queued_pdu) {
    debug("smpp.queues.handle.bind.pdu", 0, "Handling bind PDU");
    SMPPESMEAuthResult *auth_result = NULL;
    SMPPQueuedPDU *smpp_queued_response_pdu = NULL;
    switch (smpp_queued_pdu->pdu->type) {
        case bind_transmitter:
            auth_result = smpp_esme_auth(smpp_queued_pdu->smpp_esme->smpp_server, smpp_queued_pdu->pdu->u.bind_transmitter.system_id, smpp_queued_pdu->pdu->u.bind_transmitter.password, smpp_queued_pdu->smpp_esme);
            smpp_queued_response_pdu = smpp_queued_pdu_create();
            smpp_queued_response_pdu->smpp_esme = smpp_queued_pdu->smpp_esme;
            smpp_queued_response_pdu->pdu = smpp_pdu_create(bind_transmitter_resp, smpp_queued_pdu->pdu->u.bind_transmitter.sequence_number);
            if (!auth_result) {
                smpp_queued_response_pdu->pdu->u.bind_transmitter_resp.command_status = SMPP_ESME_RBINDFAIL;
                smpp_queued_response_pdu->disconnect = 1;
                smpp_queued_pdu->smpp_esme->pending_disconnect = 1;
            } else {
                smpp_queued_pdu->smpp_esme->bind_type = SMPP_ESME_TRANSMIT;
                smpp_queued_pdu->smpp_esme->authenticated = 1;
                smpp_queued_pdu->smpp_esme->system_id = octstr_duplicate(smpp_queued_pdu->pdu->u.bind_transmitter.system_id);
                smpp_queued_pdu->smpp_esme->version = smpp_queued_pdu->pdu->u.bind_transmitter.interface_version;
                smpp_queued_response_pdu->pdu->u.bind_transmitter_resp.command_status = SMPP_ESME_ROK;
                smpp_queued_response_pdu->pdu->u.bind_transmitter_resp.system_id = octstr_duplicate(smpp_queued_pdu->smpp_esme->smpp_server->server_id);
                smpp_esme_global_add(smpp_queued_pdu->smpp_esme->smpp_server, smpp_queued_pdu->smpp_esme);
            }

            smpp_queues_add_outbound(smpp_queued_response_pdu);
            break;
        case bind_transceiver:
            auth_result = smpp_esme_auth(smpp_queued_pdu->smpp_esme->smpp_server, smpp_queued_pdu->pdu->u.bind_transceiver.system_id, smpp_queued_pdu->pdu->u.bind_transceiver.password, smpp_queued_pdu->smpp_esme);
            smpp_queued_response_pdu = smpp_queued_pdu_create();
            smpp_queued_response_pdu->smpp_esme = smpp_queued_pdu->smpp_esme;
            smpp_queued_response_pdu->pdu = smpp_pdu_create(bind_transceiver_resp, smpp_queued_pdu->pdu->u.bind_transceiver.sequence_number);
            if (!auth_result) {
                smpp_queued_response_pdu->pdu->u.bind_transceiver_resp.command_status = SMPP_ESME_RBINDFAIL;
                smpp_queued_response_pdu->disconnect = 1;
                smpp_queued_pdu->smpp_esme->pending_disconnect = 1;
            } else {
                smpp_queued_pdu->smpp_esme->authenticated = 1;
                smpp_queued_pdu->smpp_esme->bind_type = SMPP_ESME_TRANSMIT | SMPP_ESME_RECEIVE;
                smpp_queued_pdu->smpp_esme->system_id = octstr_duplicate(smpp_queued_pdu->pdu->u.bind_transceiver.system_id);
                smpp_queued_pdu->smpp_esme->version = smpp_queued_pdu->pdu->u.bind_transceiver.interface_version;
                smpp_queued_response_pdu->pdu->u.bind_transceiver_resp.command_status = SMPP_ESME_ROK;
                smpp_queued_response_pdu->pdu->u.bind_transceiver_resp.system_id = octstr_duplicate(smpp_queued_pdu->smpp_esme->smpp_server->server_id);
                smpp_esme_global_add(smpp_queued_pdu->smpp_esme->smpp_server, smpp_queued_pdu->smpp_esme);
            }

            smpp_queues_add_outbound(smpp_queued_response_pdu);
            break;
        case bind_receiver:
            auth_result = smpp_esme_auth(smpp_queued_pdu->smpp_esme->smpp_server, smpp_queued_pdu->pdu->u.bind_receiver.system_id, smpp_queued_pdu->pdu->u.bind_receiver.password, smpp_queued_pdu->smpp_esme);
            smpp_queued_response_pdu = smpp_queued_pdu_create();
            smpp_queued_response_pdu->smpp_esme = smpp_queued_pdu->smpp_esme;
            smpp_queued_response_pdu->pdu = smpp_pdu_create(bind_receiver_resp, smpp_queued_pdu->pdu->u.bind_receiver.sequence_number);
            if (!auth_result) {
                smpp_queued_response_pdu->pdu->u.bind_receiver_resp.command_status = SMPP_ESME_RBINDFAIL;
                smpp_queued_response_pdu->disconnect = 1;
                smpp_queued_pdu->smpp_esme->pending_disconnect = 1;
            } else {
                smpp_queued_pdu->smpp_esme->bind_type = SMPP_ESME_RECEIVE;
                smpp_queued_pdu->smpp_esme->authenticated = 1;
                smpp_queued_pdu->smpp_esme->version = smpp_queued_pdu->pdu->u.bind_receiver.interface_version;
                smpp_queued_pdu->smpp_esme->system_id = octstr_duplicate(smpp_queued_pdu->pdu->u.bind_receiver.system_id);
                smpp_queued_response_pdu->pdu->u.bind_receiver_resp.command_status = SMPP_ESME_ROK;
                smpp_queued_response_pdu->pdu->u.bind_receiver_resp.system_id = octstr_duplicate(smpp_queued_pdu->smpp_esme->smpp_server->server_id);
                smpp_esme_global_add(smpp_queued_pdu->smpp_esme->smpp_server, smpp_queued_pdu->smpp_esme);
            }

            smpp_queues_add_outbound(smpp_queued_response_pdu);
            break;
    }

    if (auth_result) {
        info(0, "SMPP[%s] Successfully authenticated", octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id));

        if (octstr_len(auth_result->default_smsc)) {
            smpp_queued_pdu->smpp_esme->default_smsc = octstr_duplicate(auth_result->default_smsc);
        }

        if(octstr_len(auth_result->alt_charset)) {
            debug("smpp.queues", 0, "SMPP[%s] Has requested default charset %s",
                    octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id), 
                    octstr_get_cstr(auth_result->alt_charset)
                    );
            smpp_queued_pdu->smpp_esme->alt_charset = octstr_duplicate(auth_result->alt_charset);
        }

        smpp_queued_pdu->smpp_esme->default_cost = auth_result->default_cost;

        if (auth_result->simulate) {
            warning(0, "SMPP[%s] Bind has simulation enabled, messages WILL NOT be delivered.", octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id));
            warning(0, "SMPP[%s] Permanent failures every %ld messages, Temporary failures every %ld messages",
                    octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id),
                    auth_result->simulate_permanent_failure_every,
                    auth_result->simulate_temporary_failure_every
                    );
            
            warning(0, "SMPP[%s] Delivery simulated every %ld messages, MO every %ld messages Max open acks %ld",
                    octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id),
                    auth_result->simulate_deliver_every,
                    auth_result->simulate_mo_every,
                    smpp_queued_pdu->smpp_esme->max_open_acks
                    );

            smpp_queued_pdu->smpp_esme->simulate = auth_result->simulate;
            smpp_queued_pdu->smpp_esme->simulate_deliver_every = auth_result->simulate_deliver_every;
            smpp_queued_pdu->smpp_esme->simulate_permanent_failure_every = auth_result->simulate_permanent_failure_every;
            smpp_queued_pdu->smpp_esme->simulate_temporary_failure_every = auth_result->simulate_temporary_failure_every;
            smpp_queued_pdu->smpp_esme->simulate_mo_every = auth_result->simulate_mo_every;
        }
        smpp_queued_pdu->smpp_esme->smpp_esme_global->throughput = auth_result->throughput;
        smpp_queued_pdu->smpp_esme->smpp_esme_global->max_binds = auth_result->max_binds;
        smpp_queued_pdu->smpp_esme->smpp_esme_global->enable_prepaid_billing = auth_result->enable_prepaid_billing;
        if(smpp_queued_pdu->smpp_esme->smpp_esme_global->enable_prepaid_billing) {
            info(0, "SMPP[%s] has prepaid billing enabled.", octstr_get_cstr(smpp_queued_pdu->smpp_esme->smpp_esme_global->system_id));
        }
    }

    smpp_queued_pdu_destroy(smpp_queued_pdu);

    smpp_esme_auth_result_destroy(auth_result);
}

void smpp_queues_inbound_thread(void *arg) {
    SMPPServer *smpp_server = arg;
    SMPPQueuedPDU *smpp_queued_pdu;
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;

    debug("smpp.queues.inbound.thread", 0, "Starting inbound PDU processor thread");

    counter_increase(smpp_server->running_threads);

    while ((smpp_queued_pdu = gw_prioqueue_consume(smpp_server->inbound_queue)) != NULL) {
        debug("smpp.queues.inbound.thread", 0, "SMPP[%s] Got queued PDU (%d):", octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id), smpp_queued_pdu->smpp_esme->connected);
        smpp_pdu_dump(smpp_queued_pdu->smpp_esme->system_id, smpp_queued_pdu->pdu);
        smpp_queued_pdu->smpp_esme->time_last_pdu = time(NULL);

        smpp_queued_pdu->smpp_esme->time_last_queue_process = time(NULL);
        counter_decrease(smpp_queued_pdu->smpp_esme->inbound_queued);
        counter_increase(smpp_queued_pdu->smpp_esme->inbound_processed);

        gw_rwlock_rdlock(smpp_esme_data->lock);

        switch (smpp_queued_pdu->pdu->type) {
            case bind_transmitter:
            case bind_transceiver:
            case bind_receiver:
                smpp_queues_handle_bind_pdu(smpp_queued_pdu);
                break;
            case enquire_link:
                smpp_queues_handle_enquire_link(smpp_queued_pdu);
                break;
            case unbind:
                smpp_queues_handle_unbind(smpp_queued_pdu);
                break;
            case submit_sm:
                smpp_queues_handle_submit_sm(smpp_queued_pdu);
                break;
            case data_sm:
                smpp_queues_handle_data_sm(smpp_queued_pdu);
                break;
            case deliver_sm_resp:
                smpp_queues_process_ack(smpp_queued_pdu->smpp_esme, smpp_queued_pdu->pdu->u.deliver_sm_resp.sequence_number, smpp_queued_pdu->pdu->u.deliver_sm_resp.command_status);
                smpp_queued_pdu_destroy(smpp_queued_pdu);
                break;
            default:
                debug("smpp.queues.inbound.thread", 0, "Got unsupported PDU type %s", smpp_queued_pdu->pdu->type_name);
                smpp_queued_pdu_destroy(smpp_queued_pdu);
                break;
        }

        gw_rwlock_unlock(smpp_esme_data->lock);
        
        if(gw_prioqueue_len(smpp_server->inbound_queue) > 100) {
            warning(0, "Inbound queues are at %ld", gw_prioqueue_len(smpp_server->inbound_queue));
        }
    }

    counter_decrease(smpp_server->running_threads);

    debug("smpp.queues.inbound.thread", 0, "Shutting down inbound PDU processor thread");
}

void smpp_queues_outbound_thread(void *arg) {
    SMPPServer *smpp_server = arg;
    SMPPQueuedPDU *smpp_queued_pdu;
    SMPPEsme *smpp_esme;
    
    int disconnect;
    int callback;

    debug("smpp.queues.outbound.thread", 0, "Starting outbound PDU processor thread");

    counter_increase(smpp_server->running_threads);

    while ((smpp_queued_pdu = gw_prioqueue_consume(smpp_server->outbound_queue)) != NULL) {
        smpp_esme = smpp_queued_pdu->smpp_esme;
        disconnect = smpp_queued_pdu->disconnect;
        
        debug("smpp.queues.outbound.thread", 0, "SMPP[%s:%ld] Got outbound queued PDU (%d) seq %ld:", octstr_get_cstr(smpp_esme->system_id), smpp_esme->id, smpp_esme->connected, smpp_queued_pdu->sequence);

        smpp_esme->time_last_queue_process = time(NULL); /* Need these even if the client isn't connected so we can disconnect him */
        counter_decrease(smpp_esme->outbound_queued);
        counter_increase(smpp_esme->outbound_processed);

        if (smpp_esme->connected) {
            debug("smpp.queues.outbound.thread", 0, "SMPP[%s] Sending %s:", octstr_get_cstr(smpp_esme->system_id), smpp_queued_pdu->pdu->type_name);

            if (disconnect) { /* Before we send this PDU, let's stop listening incase the other end disconnects first */
                smpp_esme_stop_listening(smpp_esme);
            }
            
            if(smpp_queued_pdu->callback) {
                callback = 1;
                smpp_queued_pdu->time_sent = time(NULL);
            } else {
                callback = 0;
            }

            smpp_queues_send_pdu(smpp_queued_pdu->smpp_esme->conn, smpp_queued_pdu->smpp_esme->system_id, smpp_queued_pdu->pdu);

            if (disconnect) {
                if (!smpp_esme->authenticated) {
                    smpp_esme_cleanup(smpp_esme);
                }
            }


            if(!callback) {
                /* No callback, lets remove now */
                smpp_queued_pdu_destroy(smpp_queued_pdu);
            } else {
                /* Nothing else to do, inbound handler will process resp PDU */
            }
        } else {
            error(0, "SMPP[%s] Client no longer connected!", octstr_get_cstr(smpp_queued_pdu->smpp_esme->system_id));
            if(!smpp_queued_pdu->callback) {
                smpp_queued_pdu_destroy(smpp_queued_pdu);
            }
        }
        if(gw_prioqueue_len(smpp_server->outbound_queue) > 100) {
            if((gw_prioqueue_len(smpp_server->outbound_queue) % 100) == 0) {
	            warning(0, "Outbound queues are at %ld", gw_prioqueue_len(smpp_server->outbound_queue));
            }
        }
 
    }

    counter_decrease(smpp_server->running_threads);

    debug("smpp.queues.outbound.thread", 0, "Shutting down inbound PDU processor thread");
}
void smpp_queues_requeue_thread(void *arg) {
    SMPPServer *smpp_server = arg;
    SMPPQueuedPDU *smpp_queued_pdu;
    SMPPEsme *smpp_esme;

    debug("smpp.queues.requeue.thread", 0, "Starting deliver_sm (and other?) requeue thread");

    counter_increase(smpp_server->running_threads);
    
    long more = 0;
    List *stored;
    
    Load *requeue_load = load_create_real(0);
    load_add_interval(requeue_load, 1);

    double current_load;


    while(!(smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
        stored = smpp_esme_global_get_queued(smpp_server);
        
        more = gwlist_len(stored);
        
        if(more > 0) {
            load_increase_with(requeue_load, more);
        }
                
        
        while((smpp_queued_pdu = gwlist_consume(stored)) != NULL) {
            smpp_esme = smpp_esme_find_best_receiver(smpp_server, smpp_queued_pdu->system_id);
            if(smpp_esme) {
                smpp_queued_pdu->smpp_esme = smpp_esme;
                
                switch(smpp_queued_pdu->pdu->type) {
                    case deliver_sm:
                        smpp_queued_pdu->pdu->u.deliver_sm.sequence_number = counter_increase(smpp_esme->sequence_number);
                        smpp_queued_pdu->id = octstr_format("%ld", smpp_queued_pdu->pdu->u.deliver_sm.sequence_number);
                        break;
                }
                
                smpp_queues_add_outbound(smpp_queued_pdu);
            } else {
                smpp_queued_pdu->callback(smpp_queued_pdu, SMPP_ESME_RMSGQFUL);
            }
        }
        gwlist_destroy(stored, NULL);

        current_load = load_get(requeue_load, 0);

        if(current_load < 1) { /* We don't want to wait a whole second before our next attempt, lets keep aggressively going while load is > 1/sec */
            debug("smpp.queues.requeue.thread", 0, "Load was %f for requeue (not busy), waiting before next check", current_load);
            gwthread_sleep(1);
        } else {
            debug("smpp.queues.requeue.thread", 0, "Load was %f for requeue (busy), checking immediately", current_load);
        }
    }

    counter_decrease(smpp_server->running_threads);
    load_destroy(requeue_load);

    debug("smpp.queues.outbound.thread", 0, "Shutting down inbound PDU processor thread");
}

void smpp_queues_simulation_thread(void *arg) {
    SMPPServer *smpp_server = arg;
    SMPPQueuedPDU *smpp_queued_pdu;
    SMPPEsme *smpp_esme;

    debug("smpp.queues.simulation.thread", 0, "Starting simulation processor thread");

    counter_increase(smpp_server->running_threads);


    while(!(smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
        while ((smpp_queued_pdu = gw_prioqueue_consume(smpp_server->simulation_queue)) != NULL) {
            switch(smpp_queued_pdu->pdu->type) {
                case deliver_sm:
                    debug("smpp.queues.simulation.thread", 0, "SMPP[%s] Simulation PDU:", octstr_get_cstr(smpp_queued_pdu->system_id));
                    smpp_esme = smpp_esme_find_best_receiver(smpp_server, smpp_queued_pdu->system_id);
                    smpp_queued_pdu->callback = smpp_queues_callback_deliver_sm_resp;
                    
                    if(smpp_esme) {
                        smpp_queued_pdu->pdu->u.deliver_sm.sequence_number = counter_increase(smpp_esme->sequence_number);
                        smpp_queued_pdu->sequence = smpp_queued_pdu->pdu->u.deliver_sm.sequence_number;
                        smpp_queued_pdu->smpp_esme = smpp_esme;
                        
                        smpp_queued_pdu->id = octstr_format("%ld", smpp_queued_pdu->pdu->u.deliver_sm.sequence_number);

                        smpp_queues_add_outbound(smpp_queued_pdu);
                    } else {
                        if(smpp_server->database_enable_queue) {
                             debug("smpp.queues.simulation.thread", 0, "Unable to route PDU for system_id %s, adding to database", octstr_get_cstr(smpp_queued_pdu->system_id));
                            if(smpp_database_add_pdu(smpp_server, smpp_queued_pdu)) {
                                debug("smpp.queues.simulation.thread", 0, "PDU queued for later delivery");
                                smpp_queued_pdu->callback(smpp_queued_pdu, SMPP_ESME_COMMAND_STATUS_QUEUED);
                            } else {
                                smpp_queued_pdu->callback(smpp_queued_pdu, SMPP_ESME_COMMAND_STATUS_QUEUE_ERROR);
                            }
                        } else {
                            error(0, "Unable to route PDU for system_id %s, discarding", octstr_get_cstr(smpp_queued_pdu->system_id));
                            smpp_queued_pdu->callback(smpp_queued_pdu, SMPP_ESME_RDELIVERYFAILURE);
                        }
                        
                        /* Callback handler destroys the queued pdu */
                    }
                    
                    break;
            }
        }
        gwthread_sleep(1.0);
    }

    counter_decrease(smpp_server->running_threads);

    debug("smpp.queues.outbound.thread", 0, "Shutting down inbound PDU processor thread");
}

int smpp_queues_pdu_compare(const void *a, const void *b) {
    return 0;
}

int smpp_queues_deliver_sm_compare(const void *a, const void *b) {
    return 0;
}

void smpp_queues_init(SMPPServer *smpp_server) {
    debug("smpp.queues.init", 0, "Initializing SMPP Server queues");

    long i;
    
    if(!smpp_server->simulation_queue) {
        /* we use a background thread to simulate deliver_sm's as to not send them instantly (more 'real world') */
        info(0, "Starting simulation queue threads");
        smpp_server->simulation_queue = gw_prioqueue_create(smpp_queues_deliver_sm_compare);
        gwthread_create(smpp_queues_simulation_thread, smpp_server);
    }

    if (!smpp_server->inbound_queue) {
        smpp_server->inbound_queue = gw_prioqueue_create(smpp_queues_pdu_compare);
        gw_prioqueue_add_producer(smpp_server->inbound_queue);

        debug("smpp.queues.init", 0, "Staring %ld inbound queue threads", smpp_server->num_inbound_queue_threads);
        for (i = 0; i < smpp_server->num_inbound_queue_threads; i++) {
            gwthread_create(smpp_queues_inbound_thread, smpp_server);
        }
    }

    if (!smpp_server->outbound_queue) {
        smpp_server->outbound_queue = gw_prioqueue_create(smpp_queues_pdu_compare);
        gw_prioqueue_add_producer(smpp_server->outbound_queue);

        debug("smpp.queues.init", 0, "Staring %ld outbound queue threads", smpp_server->num_outbound_queue_threads);
        for (i = 0; i < smpp_server->num_outbound_queue_threads; i++) {
            gwthread_create(smpp_queues_outbound_thread, smpp_server);
        }
        gwthread_create(smpp_queues_requeue_thread, smpp_server);
    }
    
    

}

void smpp_queues_shutdown(SMPPServer *smpp_server) {
    debug("smpp.queues.init", 0, "Shutting down SMPP Server queues");
    if (smpp_server->inbound_queue) {
        gw_prioqueue_remove_producer(smpp_server->inbound_queue);
    }

    if (smpp_server->outbound_queue) {
        gw_prioqueue_remove_producer(smpp_server->outbound_queue);
    }

    while (counter_value(smpp_server->running_threads) > 0) {
        debug("smpp.queues.shutdown", 0, "Waiting for threads to shutdown %ld", counter_value(smpp_server->running_threads));
        gwthread_sleep(1.0);
    }
    
    gw_prioqueue_destroy(smpp_server->inbound_queue, (void(*)(void *))smpp_queued_pdu_destroy);
    gw_prioqueue_destroy(smpp_server->outbound_queue, NULL);
    gw_prioqueue_destroy(smpp_server->simulation_queue, (void(*)(void *))smpp_queued_pdu_destroy);
}

