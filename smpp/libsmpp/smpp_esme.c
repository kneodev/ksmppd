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

#include <ctype.h>
#include <event2/listener.h>
#include "gwlib/gwlib.h"
#include "gw/load.h"
#include "gw/msg.h"
#include "gw/smsc/smpp_pdu.h"
#include "smpp_server.h"
#include "smpp_listener.h"
#include "smpp_bearerbox.h"
#include "smpp_esme.h"
#include "smpp_queued_pdu.h"
#include "smpp_queues.h"
#include "smpp_database.h"
#include "smpp_http_server.h"
#include "smpp_http_client.h"
#include "smpp_route.h"
#include "smpp_plugin.h"


SMPPESMEAuthResult *smpp_esme_auth_result_create() {
    SMPPESMEAuthResult *smpp_esme_auth_result = gw_malloc(sizeof(SMPPESMEAuthResult));
    smpp_esme_auth_result->default_smsc = NULL;
    smpp_esme_auth_result->callback_url = NULL;
    smpp_esme_auth_result->throughput = 0;
    smpp_esme_auth_result->simulate = 0;
    smpp_esme_auth_result->simulate_deliver_every = 0;
    smpp_esme_auth_result->simulate_mo_every = 0;
    smpp_esme_auth_result->simulate_permanent_failure_every = 0;
    smpp_esme_auth_result->simulate_temporary_failure_every = 0;
    smpp_esme_auth_result->default_cost = 0;
    smpp_esme_auth_result->max_binds = 0;
    smpp_esme_auth_result->enable_prepaid_billing = 0;
    smpp_esme_auth_result->allowed_ips = NULL;
    smpp_esme_auth_result->alt_charset = NULL;
    
    
    return smpp_esme_auth_result;
}

void smpp_esme_auth_result_destroy(SMPPESMEAuthResult *smpp_esme_auth_result) {
    if(smpp_esme_auth_result == NULL) {
        return;
    }
    octstr_destroy(smpp_esme_auth_result->default_smsc);
    octstr_destroy(smpp_esme_auth_result->callback_url);
    octstr_destroy(smpp_esme_auth_result->allowed_ips);
    octstr_destroy(smpp_esme_auth_result->alt_charset);
    gw_free(smpp_esme_auth_result);
}

int smpp_esme_compare(void *a, void *b) {
    if (a == b) {
        return 1;
    }
    return 0;
}

void smpp_esme_inbound_load_increase(SMPPEsme *smpp_esme) {
    SMPPServer *smpp_server = smpp_esme->smpp_server;
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    counter_increase(smpp_esme_data->inbound_processed);
    load_increase(smpp_esme_data->inbound_load);
    load_increase(smpp_esme->inbound_load);
    load_increase(smpp_esme->smpp_esme_global->inbound_load);
}

void smpp_esme_outbound_load_increase(SMPPEsme *smpp_esme) {
    SMPPServer *smpp_server = smpp_esme->smpp_server;
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    counter_increase(smpp_esme_data->outbound_processed);
    load_increase(smpp_esme_data->outbound_load);
    load_increase(smpp_esme->outbound_load);
    load_increase(smpp_esme->smpp_esme_global->outbound_load);
}

void smpp_esme_cleanup(SMPPEsme *smpp_esme) {
    SMPPEsmeData *smpp_esme_data = smpp_esme->smpp_server->esme_data;

    smpp_esme_stop_listening(smpp_esme);

    debug("smpp.esme.cleanup", 0, "SMPP[%s] Adding %ld to cleanup queue", octstr_get_cstr(smpp_esme->system_id), smpp_esme->id);
    smpp_esme->connected = 0;
    gwlist_append_unique(smpp_esme_data->cleanup_queue, smpp_esme, smpp_esme_compare);
}

SMPPEsmeGlobal *smpp_esme_global_create() {
    SMPPEsmeGlobal *smpp_esme_global = gw_malloc(sizeof (SMPPEsmeGlobal));
    smpp_esme_global->binds = gwlist_create();
    smpp_esme_global->inbound_load = load_create_real(0);
    load_add_interval(smpp_esme_global->inbound_load, -1);
    load_add_interval(smpp_esme_global->inbound_load, 1);
    load_add_interval(smpp_esme_global->inbound_load, 60);
    load_add_interval(smpp_esme_global->inbound_load, 300);
    smpp_esme_global->outbound_load = load_create_real(0);
    load_add_interval(smpp_esme_global->outbound_load, -1);
    load_add_interval(smpp_esme_global->outbound_load, 1);
    load_add_interval(smpp_esme_global->outbound_load, 60);
    load_add_interval(smpp_esme_global->outbound_load, 300);
    smpp_esme_global->throughput = 0;
    smpp_esme_global->system_id = NULL;
    smpp_esme_global->inbound_processed = counter_create();
    smpp_esme_global->outbound_processed = counter_create();
    smpp_esme_global->max_binds = 0;
    smpp_esme_global->enable_prepaid_billing = 0;
    
    smpp_esme_global->mo_counter = counter_create();
    smpp_esme_global->mt_counter = counter_create();
    smpp_esme_global->dlr_counter = counter_create();
    smpp_esme_global->error_counter = counter_create();

    return smpp_esme_global;
}

void smpp_esme_global_destroy(SMPPEsmeGlobal *smpp_esme_global) {
    gwlist_destroy(smpp_esme_global->binds, (void(*)(void *))smpp_esme_destroy);
    load_destroy(smpp_esme_global->inbound_load);
    load_destroy(smpp_esme_global->outbound_load);
    counter_destroy(smpp_esme_global->inbound_processed);
    counter_destroy(smpp_esme_global->outbound_processed);
    octstr_destroy(smpp_esme_global->system_id);
    counter_destroy(smpp_esme_global->mo_counter);
    counter_destroy(smpp_esme_global->mt_counter);
    counter_destroy(smpp_esme_global->dlr_counter);
    counter_destroy(smpp_esme_global->error_counter);
    gw_free(smpp_esme_global);
}

static int smpp_esme_matches(void *a, void *b) {
    if(octstr_case_compare(a, b) == 0) {
        return 1;
    }
    return 0;
}

/* The caller of this function MUST lock smpp_esme_data->lock, 
 * otherwise there is a risk of results being destroyed before you can process them */
List *smpp_esme_global_get_readers(SMPPServer *smpp_server, int best_only) {
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    List *receiver_smpp_esmes = gwlist_create();
    List *esmes_with_queued = NULL;
    List *keys = NULL, *actual_keys = NULL;

    Octstr *system_id;

    long num, i;

    long limit, j, num_binds;
    SMPPEsmeGlobal *smpp_esme_global;
    SMPPEsme *smpp_esme;

    Octstr *key;

    int found = 0;

    if(smpp_server->database_enable_queue) {
        esmes_with_queued = smpp_database_get_esmes_with_queued(smpp_server);
        num = gwlist_len(esmes_with_queued);
        keys = dict_keys(smpp_esme_data->esmes);
        actual_keys = gwlist_create();
        for(i=0;i<num;i++) {
            found = 0;
            system_id = gwlist_search(keys, gwlist_get(esmes_with_queued, i), smpp_esme_matches);
            if(system_id) {
                smpp_esme_global =  dict_get(smpp_esme_data->esmes, system_id);
                if(smpp_esme_global) {
                    num_binds = gwlist_len(smpp_esme_global->binds);
                    for (j = 0; j < num_binds; j++) {
                        smpp_esme = gwlist_get(smpp_esme_global->binds, j);
                        if (smpp_esme->connected && (smpp_esme->bind_type & SMPP_ESME_RECEIVE)) {
                            limit = smpp_esme->max_open_acks - dict_key_count(smpp_esme->open_acks);
                            if (limit <= 0) {
                                /* ESME has no space */
                            } else {
                                /* Found a capable receiver */
                                debug("smpp.esme.global.get.readers", 0,
                                      "SMPP[%s] has queued messages and receivers available", octstr_get_cstr(system_id));
                                gwlist_produce(actual_keys, octstr_duplicate(system_id));
                                found = 1;
                                break;
                            }
                        }
                    }
                }
            }
            if(!found) {
                debug("smpp.esme.global.get.readers", 0, "SMPP[%s] has queued messages but no receivers available", octstr_get_cstr(gwlist_get(esmes_with_queued, i)));
            }

        }
        gwlist_destroy(keys, (void(*)(void *))octstr_destroy);
        gwlist_destroy(esmes_with_queued, (void(*)(void *))octstr_destroy);

        keys = actual_keys;
    } else {
        keys = dict_keys(smpp_esme_data->esmes);
    }

    num = gwlist_len(keys);

    
    for (i = 0; i < num; i++) {
        limit = SMPP_ESME_DEFAULT_MAX_OPEN_ACKS;
        key = gwlist_get(keys, i);

        smpp_esme_global = dict_get(smpp_esme_data->esmes, key);
        num_binds = gwlist_len(smpp_esme_global->binds);
        for (j = 0; j < num_binds; j++) {
            smpp_esme = gwlist_get(smpp_esme_global->binds, j);
            if (smpp_esme->connected && (smpp_esme->bind_type & SMPP_ESME_RECEIVE)) {
                limit = smpp_esme->max_open_acks - dict_key_count(smpp_esme->open_acks);
                if (limit <= 0) {
                    /* ESME has no space */
                } else {
                    /* Found a capable receiver */
                    gwlist_produce(receiver_smpp_esmes, smpp_esme);
                    if(best_only) {
                        break;
                    }
                }
            }
        }
    }
    
    gwlist_destroy(keys, (void(*)(void *))octstr_destroy);
    
    return receiver_smpp_esmes;
}

void smpp_esme_db_queue_callback(void *context, long status) {
    SMPPQueuedPDU *smpp_queued_pdu = context;
    SMPPServer *smpp_server = smpp_queued_pdu->smpp_server;
    info(0, "Got db msg queue callback for %ld status %ld", smpp_queued_pdu->sequence, status);
    if((status == SMPP_ESME_ROK) || (status == SMPP_ESME_COMMAND_STATUS_QUEUED)) {
        if(status == SMPP_ESME_ROK) {
            if(smpp_queued_pdu->smpp_esme && smpp_queued_pdu->pdu && (smpp_queued_pdu->pdu->type == deliver_sm)) {
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
        }
        /* These SHOULD be the only two states if database queueing is active */
        smpp_database_remove(smpp_server, smpp_queued_pdu->sequence, 0);
    } else {
        /* Shift this PDU to the PDU tables so we don't have to build it again */
        smpp_database_add_pdu(smpp_server, smpp_queued_pdu);
        smpp_database_remove(smpp_server, smpp_queued_pdu->sequence, 0);
    }
    smpp_queued_pdu_destroy(smpp_queued_pdu);
}


List *smpp_esme_global_get_queued(SMPPServer *smpp_server) {
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    List *queues = gwlist_create();
    List *esme_queue;
    List *msg_queue;
    List *pdus;
    long i, num;
    SMPPEsme *smpp_esme;
    SMPPQueuedPDU *smpp_queued_pdu;
    SMPP_PDU *pdu;
    SMPPDatabaseMsg *smpp_database_msg;


    gw_rwlock_rdlock(smpp_esme_data->lock);
    
    List *esmes = smpp_esme_global_get_readers(smpp_server, 1);
    
    num = gwlist_len(esmes);
    long limit;
    for (i = 0; i < num; i++) {
        limit = SMPP_ESME_DEFAULT_MAX_OPEN_ACKS;
        smpp_esme = gwlist_get(esmes, i);
        
        limit = smpp_esme->max_open_acks - dict_key_count(smpp_esme->open_acks);

        if (limit > 0) {
            esme_queue = smpp_database_get_stored_pdu(smpp_server, smpp_esme->system_id,limit);
            
            limit = limit - gwlist_len(esme_queue);
            
            while ((smpp_queued_pdu = gwlist_consume(esme_queue)) != NULL) {
                gwlist_produce(queues, smpp_queued_pdu);
            }
            gwlist_destroy(esme_queue, NULL);
            
            if(limit > 0) {
                /* Still space for these */
                msg_queue = smpp_database_get_stored(smpp_server, report_mo, smpp_esme->system_id, limit);
                while((smpp_database_msg = gwlist_consume(msg_queue)) != NULL) {
                    pdus = smpp_pdu_msg_to_pdu(smpp_esme, smpp_database_msg->msg);
                    if(pdus == NULL) {
                        continue;
                    }
                    while((pdu = gwlist_consume(pdus)) != NULL) {
                        smpp_queued_pdu = smpp_queued_pdu_create();
                        smpp_queued_pdu->pdu = pdu;
                        smpp_queued_pdu->smpp_server = smpp_esme->smpp_server;
                        smpp_queued_pdu->sequence = smpp_database_msg->global_id;
                        smpp_queued_pdu->system_id = octstr_duplicate(smpp_esme->system_id);
                        smpp_queued_pdu->bearerbox_id = octstr_format("%ld", smpp_database_msg->global_id);
                        smpp_queued_pdu->smpp_esme = smpp_esme;
                        smpp_queued_pdu->context = smpp_queued_pdu;
                        smpp_queued_pdu->callback = smpp_esme_db_queue_callback;
                        gwlist_produce(queues, smpp_queued_pdu);
                    }
                    gwlist_destroy(pdus, NULL);
                    smpp_database_msg_destroy(smpp_database_msg);
                }
                gwlist_destroy(msg_queue, NULL);
            }
            
            
        }
    }

    gw_rwlock_unlock(smpp_esme_data->lock);

    gwlist_destroy(esmes, NULL); /* Don't destroy the ESMEs, these are 'semi-permanent' */

    return queues;
}

void smpp_esme_global_add(SMPPServer *smpp_server, SMPPEsme *smpp_esme) {
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;

    Octstr *key = octstr_duplicate(smpp_esme->system_id);
    octstr_convert_range(key, 0, octstr_len(key), tolower);
    octstr_convert_range(smpp_esme->system_id, 0, octstr_len(smpp_esme->system_id), tolower); 

    SMPPEsmeGlobal *smpp_global = dict_get(smpp_esme_data->esmes, key);
    if (smpp_global == NULL) {
        smpp_global = smpp_esme_global_create();
        smpp_global->system_id = octstr_duplicate(key);
        dict_put(smpp_esme_data->esmes, key, smpp_global);
    }
    
    smpp_esme->smpp_esme_global = smpp_global;

    gwlist_produce(smpp_global->binds, smpp_esme);

    octstr_destroy(key);
}

SMPPESMEAuthResult *smpp_esme_auth(SMPPServer *smpp_server, Octstr *system_id, Octstr *password, SMPPEsme *smpp_esme) {
    SMPPESMEAuthResult *smpp_auth_result = NULL;
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    Octstr *tmp_system_id;
    SMPPEsmeGlobal *smpp_esme_global;
    
    if(smpp_server->authentication_method == SMPP_SERVER_AUTH_METHOD_DATABASE) {
        smpp_auth_result = smpp_database_auth(smpp_server, system_id, password);
    } else if(smpp_server->authentication_method == SMPP_SERVER_AUTH_METHOD_HTTP) {
        smpp_auth_result = smpp_http_client_auth(smpp_server, system_id, password);
    } else if(smpp_server->authentication_method == SMPP_SERVER_AUTH_METHOD_PLUGIN) {
        if(smpp_server->plugin_auth) {
            info(0, "Authenticating via plugin %s", octstr_get_cstr(smpp_server->plugin_auth->id));
            smpp_auth_result = smpp_server->plugin_auth->authenticate(smpp_server->plugin_auth, system_id, password);
        } else {
            warning(0, "Plugin authentication specified but no plugin configured?");
        }
    } else {
        warning(0, "Unknown 'auth-method' provided, defaulting to database");
        smpp_auth_result = smpp_database_auth(smpp_server, system_id, password);
    }
    
    if(smpp_auth_result) {
        if(octstr_len(smpp_auth_result->allowed_ips)) {
            if(connect_denied(smpp_auth_result->allowed_ips, smpp_esme->ip)) {
                error(0, "SMPP[%s] denying bind due to non-allowed IP (%s vs %s)", octstr_get_cstr(system_id), octstr_get_cstr(smpp_auth_result->allowed_ips), octstr_get_cstr(smpp_esme->ip));
                smpp_esme_auth_result_destroy(smpp_auth_result);
                smpp_auth_result = NULL;
            }
        }
    }

    if(smpp_auth_result) {
        /* Successful authentication, lets check max binds */
        if(smpp_auth_result->max_binds > 0) {
            tmp_system_id = octstr_duplicate(system_id);
            octstr_convert_range(tmp_system_id, 0, octstr_len(tmp_system_id), tolower);
            
            smpp_esme_global = dict_get(smpp_esme_data->esmes, tmp_system_id);
            
            if(smpp_esme_global) {
                if(gwlist_len(smpp_esme_global->binds) >= smpp_auth_result->max_binds) {
                    warning(0, "SMPP[%s] has exceeded its bind limit (%ld/%d), rejecting", octstr_get_cstr(tmp_system_id), gwlist_len(smpp_esme_global->binds), smpp_auth_result->max_binds);
                    smpp_esme_auth_result_destroy(smpp_auth_result);
                    smpp_auth_result = NULL;
                } else {
                    info(0, "SMPP[%s] bind limit still ok (%ld/%d), allowing", octstr_get_cstr(tmp_system_id), gwlist_len(smpp_esme_global->binds), smpp_auth_result->max_binds);
                }
            }
            octstr_destroy(tmp_system_id);
        }
    }

    if(!smpp_auth_result) {
        smpp_listener_auth_failed(smpp_server, smpp_esme->ip);
    }

    return smpp_auth_result;
}

SMPPEsme *smpp_esme_find_best_receiver(SMPPServer *smpp_server, Octstr *system_id) {
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    if (!octstr_len(system_id)) {
        return NULL;
    }

    Octstr *key = octstr_duplicate(system_id);
    octstr_convert_range(key, 0, octstr_len(key), tolower);

    gw_rwlock_rdlock(smpp_esme_data->lock);
    SMPPEsmeGlobal *smpp_global = dict_get(smpp_esme_data->esmes, key);
    SMPPEsme *best_esme = NULL, *smpp_esme;
    List *options;
    long i, num;
    long limit, current;

    if (smpp_global) {
        options = gwlist_create();
        num = gwlist_len(smpp_global->binds);
        for (i = 0; i < num; i++) {
            smpp_esme = gwlist_get(smpp_global->binds, i);
            if (smpp_esme->connected && (smpp_esme->bind_type & SMPP_ESME_RECEIVE)) {
                current = counter_value(smpp_esme->outbound_queued);
                limit = smpp_esme->max_open_acks - current;
                debug("smpp.esme.find.best.receiver", 0, "SMPP[%s] has %ld/%ld in queues (%ld)", octstr_get_cstr(smpp_esme->system_id), current, smpp_esme->max_open_acks, limit);
                if (limit > 0) {
                    /* Can only use binds that have space in their queues */
                    current = dict_key_count(smpp_esme->open_acks);
                    limit = smpp_esme->max_open_acks - current;

                    debug("smpp.esme.find.best.receiver", 0, "SMPP[%s] has %ld/%ld open acks pending (%ld)", octstr_get_cstr(smpp_esme->system_id), current, smpp_esme->max_open_acks, limit);
                    if(limit > 0) {
                        gwlist_produce(options, smpp_esme);
                    } else {
                        debug("smpp.esme.find.best.receiver", 0, "SMPP[%s] Outbound acks are full %ld/%ld", octstr_get_cstr(smpp_esme->system_id), current, smpp_esme->max_open_acks);
                    }
                } else {
                    debug("smpp.esme.find.best.receiver", 0, "SMPP[%s] Outbound queue is full %ld/%ld", octstr_get_cstr(smpp_esme->system_id), current, smpp_esme->max_open_acks);
                }
            }
        }

        num = gwlist_len(options);
        while ((smpp_esme = gwlist_consume(options)) != NULL) {
            if (!best_esme) {
                best_esme = smpp_esme;
            } else {
                gw_rwlock_rdlock(smpp_esme->ack_process_lock);
                gw_rwlock_rdlock(best_esme->ack_process_lock);
                if(dict_key_count(smpp_esme->open_acks) < dict_key_count(best_esme->open_acks)) {
                    gw_rwlock_unlock(best_esme->ack_process_lock);
                    best_esme = smpp_esme;
                } else if (counter_value(smpp_esme->outbound_queued) < counter_value(best_esme->outbound_queued)) {
                    gw_rwlock_unlock(best_esme->ack_process_lock);
                    best_esme = smpp_esme;
                } else {
                    gw_rwlock_unlock(best_esme->ack_process_lock);
                }
                gw_rwlock_unlock(smpp_esme->ack_process_lock);
            }
        }
        gwlist_destroy(options, NULL);
    }

    gw_rwlock_unlock(smpp_esme_data->lock);

    octstr_destroy(key);

    return best_esme;
}

void smpp_esme_cleanup_thread(void *arg) {
    SMPPServer *smpp_server = arg;
    SMPPEsmeData *smpp_esme_data;

    Octstr *key, *ack_key;
    long i, num;
    long j, bind_num;
    long k, ack_num;
    long diff;
    List *keys, *ack_keys;
    List *replace;
    SMPPEsmeGlobal *smpp_esme_global;
    SMPPEsme *smpp_esme;
    SMPPQueuedPDU *queued_ack;

    unsigned long queue_depth;
    long timediff;

    int alive;
    
    double current_inbound_load = 0, current_outbound_load = 0;
    double max_inbound_load = 0, max_outbound_load = 0;

    debug("smpp.esme.cleanup.thread", 0, "ESME cleanup thread starting");
    while (!(smpp_server->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
        smpp_esme_data = smpp_server->esme_data;
        
        current_inbound_load = load_get(smpp_esme_data->inbound_load, 1);
        current_outbound_load = load_get(smpp_esme_data->outbound_load, 1);
        if(current_inbound_load > max_inbound_load) {
            max_inbound_load = current_inbound_load;
            info(0, "New maximum inbound load: %f/sec", max_inbound_load);
        }
        if(current_outbound_load > max_outbound_load) {
            max_outbound_load = current_outbound_load;
            info(0, "New maximum outbound load: %f/sec", max_outbound_load);
        }
        info(0, "Current SMPP load is %f/sec inbound %f/sec outbound", current_inbound_load, current_outbound_load);
        gw_rwlock_wrlock(smpp_esme_data->lock);
        keys = dict_keys(smpp_esme_data->esmes);

        num = gwlist_len(keys);

        for (i = 0; i < num; i++) {
            key = gwlist_get(keys, i);
            smpp_esme_global = dict_get(smpp_esme_data->esmes, key);

            bind_num = gwlist_len(smpp_esme_global->binds);

            if (bind_num > 0) {
                info(0, "SMPP[%s] currently has %ld binds connected", octstr_get_cstr(key), bind_num);
            } else {
                debug("smpp.esme.cleanup.thread", 0, "SMPP[%s] currently has %ld binds connected", octstr_get_cstr(key), bind_num);
            }



            replace = gwlist_create();

            for (j = 0; j < bind_num; j++) {
                alive = 1;
                smpp_esme = gwlist_get(smpp_esme_global->binds, j);

                info(0, " -- %s:%ld (%d)-- openacks:%lu inbound:%f,outbound:%f,inbound-queue:%lu,outbound-queue:%lu,inbound-processed:%lu,outbound-processed:%lu",
                        octstr_get_cstr(smpp_esme->system_id),
                        smpp_esme->id,
                        smpp_esme->bind_type,
                        dict_key_count(smpp_esme->open_acks),
                        load_get(smpp_esme->inbound_load, 0),
                        load_get(smpp_esme->outbound_load, 0),
                        counter_value(smpp_esme->inbound_queued),
                        counter_value(smpp_esme->outbound_queued),
                        counter_value(smpp_esme->inbound_processed),
                        counter_value(smpp_esme->outbound_processed)
                        );

                if (!smpp_esme->connected) {
                    smpp_esme_cleanup(smpp_esme);
                    alive = 0;
                } else {
                    diff = time(NULL) - smpp_esme->time_last_pdu;

                    ack_keys = dict_keys(smpp_esme->open_acks);
                    ack_num = gwlist_len(ack_keys);
                    for (k = 0; k < ack_num; k++) {
                        ack_key = gwlist_get(ack_keys, k);
                        gw_rwlock_rdlock(smpp_esme->ack_process_lock);
                        queued_ack = dict_get(smpp_esme->open_acks, ack_key);
                        if (queued_ack) { /* Could have been removed by another thread */
                            timediff = difftime(time(NULL), queued_ack->time_sent);
                            if ((queued_ack->time_sent > 0) && (timediff > smpp_esme->wait_ack_time)) {
                                dict_remove(smpp_esme->open_acks, ack_key);
				                queued_ack->smpp_server = smpp_esme->smpp_server;
                                warning(0, "SMPP[%s] Queued ack %s has expired (diff %ld)", octstr_get_cstr(smpp_esme->system_id), octstr_get_cstr(queued_ack->id), timediff);
                                queued_ack->callback(queued_ack, SMPP_ESME_COMMAND_STATUS_WAIT_ACK_TIMEOUT);
                            }
                        }
                        gw_rwlock_unlock(smpp_esme->ack_process_lock);
                    }
                    gwlist_destroy(ack_keys, (void(*)(void *))octstr_destroy);

                    if (diff > smpp_esme->enquire_link_interval) {
                        if ((diff / smpp_esme->enquire_link_interval) > 2) {
                            debug("smpp.esme.cleanup.thread", 0, "SMPP[%s] Has not transmitted anything to us in %ld seconds, disconnecting", octstr_get_cstr(smpp_esme->system_id), diff);
                            alive = 0;
                            smpp_esme_cleanup(smpp_esme);
                        } else {
                            debug("smpp.esme.cleanup.thread", 0, "SMPP[%s] No activity in %ld seconds, sending enquire link", octstr_get_cstr(smpp_esme->system_id), diff);
                            smpp_queues_send_enquire_link(smpp_esme);
                        }
                    }
                }
                if (alive) {
                    gwlist_produce(replace, smpp_esme);
                }
            }

            gwlist_destroy(smpp_esme_global->binds, NULL); /* No destroy, already destroyed above */

            smpp_esme_global->binds = replace;
        }

        gwlist_destroy(keys, (void(*)(void *))octstr_destroy);

        num = gwlist_len(smpp_esme_data->cleanup_queue);

        if (num > 0) {
            debug("smpp.esme.cleanup.thread", 0, "%ld ESME's queued for cleanup", num);
            replace = gwlist_create();
            for (i = 0; i < num; i++) {
                smpp_esme = gwlist_get(smpp_esme_data->cleanup_queue, i);
                queue_depth = counter_value(smpp_esme->inbound_queued) + counter_value(smpp_esme->outbound_queued) + counter_value(smpp_esme->pending_routing);
                if (queue_depth <= 0) {
                    diff = time(NULL) - smpp_esme->time_last_queue_process;
                    if (diff > SMPP_ESME_CLEANUP_QUEUE_DELAY) {
                        /* No queues and all processed, lets kill it */
                        debug("smpp.esme.cleanup.thread", 0, "SMPP[%s] destroying expired bind id %ld", octstr_get_cstr(smpp_esme->system_id), smpp_esme->id);
                        smpp_esme_destroy(smpp_esme);
                    } else {
                        /* Still need to give it a moment */
                        gwlist_produce(replace, smpp_esme);
                        debug("smpp.esme.cleanup.thread", 0, "SMPP[%s] can't destroy because time has not elapsed (%ld) id %ld", octstr_get_cstr(smpp_esme->system_id), diff, smpp_esme->id);
                    }
                } else {
                    /* Not finished its queues yet */
                    debug("smpp.esme.cleanup.thread", 0, "SMPP[%s] can't destroy because queues still exist (%ld) id %ld", octstr_get_cstr(smpp_esme->system_id), queue_depth, smpp_esme->id);
                    gwlist_produce(replace, smpp_esme);
                }
            }
            gwlist_destroy(smpp_esme_data->cleanup_queue, NULL); /* Items have already been destroyed, rest are in replace queue */
            smpp_esme_data->cleanup_queue = replace;
        }

        gw_rwlock_unlock(smpp_esme_data->lock);

        gwthread_sleep(SMPP_ESME_CLEANUP_INTERVAL);
    }

    debug("smpp.esme.cleanup.thread", 0, "ESME cleanup thread shutting down");
}

void smpp_esme_send_unbind(SMPPEsme *smpp_esme) {
    
}

SMPPHTTPCommandResult *smpp_esme_unbind_command(SMPPServer *smpp_server, List *cgivars, int content_type) {
    SMPPHTTPCommandResult *smpp_http_command_result = smpp_http_command_result_create();
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    gw_rwlock_wrlock(smpp_esme_data->lock);
    Octstr *system_id = http_cgi_variable(cgivars, "system-id");
    Octstr *bind_id = http_cgi_variable(cgivars, "bind-id");
    
    Octstr *message;
    
    long i, num, bind_id_num;
    long unbinds_sent = 0;
    SMPPEsme *smpp_esme;
    SMPPEsmeGlobal *smpp_esme_global;
    SMPPQueuedPDU *smpp_queued_pdu;
    
    if(!octstr_len(system_id)) {
        message = octstr_create("You must specify a 'system-id' parameter");
        smpp_http_command_result->status = HTTP_NOT_ACCEPTABLE;
    } else {
        octstr_convert_range(system_id, 0, octstr_len(system_id), tolower);
        smpp_esme_global = dict_get(smpp_esme_data->esmes, system_id);
        if(smpp_esme_global) {
            if(octstr_len(bind_id)) {
                bind_id_num = atol(octstr_get_cstr(bind_id));
                /* User requested a specific bind ID disconnect */
                num = gwlist_len(smpp_esme_global->binds);
                for(i=0;i<num;i++) {
                    smpp_esme = gwlist_get(smpp_esme_global->binds, i);
                    if(smpp_esme->id == bind_id_num) {
                        info(0, "SMPP[%s] Disconnecting bind id %ld", octstr_get_cstr(smpp_esme->system_id), bind_id_num);
                        smpp_queued_pdu = smpp_queued_pdu_create_quick(smpp_esme, unbind, counter_increase(smpp_esme->sequence_number));
                        smpp_queued_pdu->disconnect = 1;
                        smpp_queues_add_outbound(smpp_queued_pdu);
                        ++unbinds_sent;
                        break;
                    }
                }
            } else {
                /* Disconnect all users matching this system id */
                num = gwlist_len(smpp_esme_global->binds);
                for(i=0;i<num;i++) {
                    smpp_esme = gwlist_get(smpp_esme_global->binds, i);
                    info(0, "SMPP[%s] Disconnecting bind id %ld", octstr_get_cstr(smpp_esme->system_id), smpp_esme->id);
                    smpp_queued_pdu = smpp_queued_pdu_create_quick(smpp_esme, unbind, counter_increase(smpp_esme->sequence_number));
                    smpp_queued_pdu->disconnect = 1;
                    smpp_queues_add_outbound(smpp_queued_pdu);
                    ++unbinds_sent;
                }
            }
            message = octstr_format("%ld binds disconnected", unbinds_sent);
        } else {
            smpp_http_command_result->status = HTTP_NOT_FOUND;
            message = octstr_format("No such system-id '%S' found", system_id);
        }
    }
    
    if(content_type == HTTP_CONTENT_TYPE_PLAIN) {
        smpp_http_command_result->result = octstr_format("%S\n", message);
    } else {
        smpp_http_command_result->result = octstr_format("<status>%S</status>", message);
    }
    
    octstr_destroy(message);
    
    gw_rwlock_unlock(smpp_esme_data->lock);
    
    return smpp_http_command_result;
}

SMPPHTTPCommandResult *smpp_esme_status_command(SMPPServer *smpp_server, List *cgivars, int content_type) {
    SMPPHTTPCommandResult *smpp_http_command_result = smpp_http_command_result_create();
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    
    smpp_http_command_result->result = octstr_create("");
    gw_rwlock_rdlock(smpp_esme_data->lock);
    
    Octstr *key;
    List *keys;
    SMPPEsmeGlobal *smpp_esme_global;
    SMPPEsme *smpp_esme;
    long i, num;
    long timediff;
    
    if(content_type == HTTP_CONTENT_TYPE_PLAIN) {
        octstr_format_append(smpp_http_command_result->result, "Summary: \n\nUnique known ESME's: %ld\n"
                "Total inbound processed:%ld load: %0.2f/%0.2f/%0.2f/sec\n"
                "Total outbound processed:%ld load: %0.2f/%0.2f/%0.2f/sec\n", dict_key_count(smpp_esme_data->esmes), 
                counter_value(smpp_esme_data->inbound_processed),
                load_get(smpp_esme_data->inbound_load, 0),load_get(smpp_esme_data->inbound_load, 1), load_get(smpp_esme_data->inbound_load, 2),  
                counter_value(smpp_esme_data->outbound_processed),
                load_get(smpp_esme_data->outbound_load, 0),load_get(smpp_esme_data->outbound_load, 1),load_get(smpp_esme_data->outbound_load, 2)
                );
        
        keys = dict_keys(smpp_esme_data->esmes);
        while((key = gwlist_consume(keys)) != NULL) {
            smpp_esme_global = dict_get(smpp_esme_data->esmes, key);
            num = gwlist_len(smpp_esme_global->binds);
            
            octstr_format_append(smpp_http_command_result->result, "\n%S - binds:%ld/%ld, total inbound load:(%0.2f/%0.2f/%0.2f)/%0.2f/sec, outbound load:(%0.2f/%0.2f/%0.2f)/sec, mt/mo/dlr/errors:(%ld/%ld/%ld/%ld)\n", 
                    key, num, 
                    smpp_esme_global->max_binds, 
                    load_get(smpp_esme_global->inbound_load, 0), load_get(smpp_esme_global->inbound_load, 1), load_get(smpp_esme_global->inbound_load, 2), 
                    smpp_esme_global->throughput, 
                    load_get(smpp_esme_global->outbound_load, 0),load_get(smpp_esme_global->outbound_load, 1),load_get(smpp_esme_global->outbound_load, 2),
                    counter_value(smpp_esme_global->mt_counter), counter_value(smpp_esme_global->mo_counter), counter_value(smpp_esme_global->dlr_counter), counter_value(smpp_esme_global->error_counter)
                    );
            
            for(i=0;i<num;i++) {
                //smpp_http_result->result = octstr_format("Uptime %ldd %ldh %ldm %lds\n", diff/3600/24, diff/3600%24, diff/60%60, diff%60);
                
                smpp_esme = gwlist_get(smpp_esme_global->binds, i);
                timediff = time(NULL) - smpp_esme->time_connected;
                octstr_format_append(smpp_http_command_result->result, "-- id:%ld ip:%S uptime:%ldd %ldh %ldm %lds, "
                        "type:%d, "
                        "open-acks:%ld, "
                        "simulate: %s, "
                        "inbound (load/queued/processed/routing):%0.2f/%ld/%ld/%ld, "
                        "outbound (load/queued/processed):%0.2f/%ld/%ld, "
                        "mt/mo/dlr/errors:%ld/%ld/%ld/%ld\n",
                        smpp_esme->id,
                        smpp_esme->ip,
                        timediff/3600/24, timediff/3600%24, timediff/60%60, timediff%60,
                        smpp_esme->bind_type,
                        dict_key_count(smpp_esme->open_acks),
                        (smpp_esme->simulate ? "yes" : "no"),
                        load_get(smpp_esme->inbound_load, 0),counter_value(smpp_esme->inbound_queued),counter_value(smpp_esme->inbound_processed),counter_value(smpp_esme->pending_routing),
                        load_get(smpp_esme->outbound_load, 0),counter_value(smpp_esme->outbound_queued),counter_value(smpp_esme->outbound_processed),
                        counter_value(smpp_esme->mt_counter), counter_value(smpp_esme->mo_counter), counter_value(smpp_esme->dlr_counter), counter_value(smpp_esme->error_counter)
                        );
            }
            
            octstr_destroy(key);
        }
        gwlist_destroy(keys, NULL);
        
    } else {
        octstr_format_append(smpp_http_command_result->result, "<esmes>");
        
        
        octstr_format_append(smpp_http_command_result->result, "<summary>\n\t<unique>%ld</unique>\n"
                "\t<inbound-processed>%ld</inbound-processed>\n\t<inbound-load>%0.2f/%0.2f/%0.2f</inbound-load>\n"
                "\t<outbound-processed>%ld</outbound-processed>\n\t<outbound-load>%0.2f/%0.2f/%0.2f</outbound-load>\n</summary>", dict_key_count(smpp_esme_data->esmes), 
                counter_value(smpp_esme_data->inbound_processed),
                load_get(smpp_esme_data->inbound_load, 0),load_get(smpp_esme_data->inbound_load, 1), load_get(smpp_esme_data->inbound_load, 2),  
                counter_value(smpp_esme_data->outbound_processed),
                load_get(smpp_esme_data->outbound_load, 0),load_get(smpp_esme_data->outbound_load, 1),load_get(smpp_esme_data->outbound_load, 2)
                );
        
        keys = dict_keys(smpp_esme_data->esmes);
        while((key = gwlist_consume(keys)) != NULL) {
            smpp_esme_global = dict_get(smpp_esme_data->esmes, key);
            num = gwlist_len(smpp_esme_global->binds);
            
            octstr_format_append(smpp_http_command_result->result, "\n<esme>\n"
                    "\t<system-id>%S</system-id>\n"
                    "\t<bind-count>%ld</bind-count>\n"
                    "\t<max-binds>%ld</max-binds>\n"
                    "\t<inbound-load>%0.2f/%0.2f/%0.2f</inbound-load>\n"
                    "\t<max-inbound-load>%0.2f</max-inbound-load>\n"
                    "\t<outbound-load>%0.2f/%0.2f/%0.2f</outbound-load>\n"
                    "\t<mt>%ld</mt>\n"
                    "\t<mo>%ld</mo>\n"
                    "\t<dlr>%ld</dlr>\n"
                    "\t<errors>%ld</errors>\n", 
                    key, num, 
                    smpp_esme_global->max_binds, 
                    load_get(smpp_esme_global->inbound_load, 0), load_get(smpp_esme_global->inbound_load, 1), load_get(smpp_esme_global->inbound_load, 2), 
                    smpp_esme_global->throughput, 
                    load_get(smpp_esme_global->outbound_load, 0),load_get(smpp_esme_global->outbound_load, 1),load_get(smpp_esme_global->outbound_load, 2),
                    counter_value(smpp_esme_global->mt_counter), counter_value(smpp_esme_global->mo_counter), counter_value(smpp_esme_global->dlr_counter), counter_value(smpp_esme_global->error_counter)
                    );
            
            for(i=0;i<num;i++) {
                smpp_esme = gwlist_get(smpp_esme_global->binds, i);
                timediff = time(NULL) - smpp_esme->time_connected;
                octstr_format_append(smpp_http_command_result->result, "\t<bind>\n\t\t<bind-id>%ld</bind-id>\n"
                        "\t\t<ip>%S</ip>\n"
                        "\t\t<uptime>%ld</uptime>\n"
                        "\t\t<bind-type>%d</bind-type>\n"
                        "\t\t<open-acks>%ld</open-acks>\n"
                        "\t\t<simulate>%s</simulate>\n" 
                        "\t\t<inbound-load>%0.2f</inbound-load>\n"
                        "\t\t<inbound-queued>%ld</inbound-queued>\n"
                        "\t\t<inbound-processed>%ld</inbound-processed>\n"
                        "\t\t<inbound-routing>%ld</inbound-routing>\n"
                        "\t\t<outbound-load>%0.2f</outbound-load>\n"
                        "\t\t<outbound-queued>%ld</outbound-queued>\n"
                        "\t\t<outbound-processed>%ld</outbound-processed>\n"
                        "\t\t<mt>%ld</mt>\n"
                        "\t\t<mo>%ld</mo>\n"
                        "\t\t<dlr>%ld</dlr>\n"
                        "\t\t<errors>%ld</errors>\n"
                        "\t</bind>\n",
                        smpp_esme->id,
                        smpp_esme->ip,
                        timediff,
                        smpp_esme->bind_type,
                        dict_key_count(smpp_esme->open_acks),
                        (smpp_esme->simulate ? "yes" : "no"),
                        load_get(smpp_esme->inbound_load, 0),counter_value(smpp_esme->inbound_queued),counter_value(smpp_esme->inbound_processed),counter_value(smpp_esme->pending_routing),
                        load_get(smpp_esme->outbound_load, 0),counter_value(smpp_esme->outbound_queued),counter_value(smpp_esme->outbound_processed),
                        counter_value(smpp_esme->mt_counter), counter_value(smpp_esme->mo_counter), counter_value(smpp_esme->dlr_counter), counter_value(smpp_esme->error_counter)
                        );
            }
            
            octstr_format_append(smpp_http_command_result->result, "</esme>");
            
            octstr_destroy(key);
        }
        gwlist_destroy(keys, NULL);
        
        
        
        octstr_format_append(smpp_http_command_result->result, "</esmes>");
    }
    
    gw_rwlock_unlock(smpp_esme_data->lock);

    return smpp_http_command_result;
}

void smpp_esme_init(SMPPServer *smpp_server) {
    SMPPEsmeData *smpp_esme_data = gw_malloc(sizeof (SMPPEsmeData));
    smpp_esme_data->esmes = dict_create(512, NULL);
    smpp_esme_data->lock = gw_rwlock_create();
    smpp_esme_data->cleanup_queue = gwlist_create();
    smpp_esme_data->cleanup_lock = gw_rwlock_create();
    smpp_esme_data->inbound_load = load_create_real(0);
    smpp_esme_data->outbound_load = load_create_real(0);
    load_add_interval(smpp_esme_data->inbound_load, -1);
    load_add_interval(smpp_esme_data->inbound_load, 1);
    load_add_interval(smpp_esme_data->inbound_load, 60);
    load_add_interval(smpp_esme_data->inbound_load, 300);
    load_add_interval(smpp_esme_data->outbound_load, -1);
    load_add_interval(smpp_esme_data->outbound_load, 1);
    load_add_interval(smpp_esme_data->outbound_load, 60);
    load_add_interval(smpp_esme_data->outbound_load, 300);
    smpp_esme_data->inbound_processed = counter_create();
    smpp_esme_data->outbound_processed = counter_create();
    smpp_server->esme_data = smpp_esme_data;
    smpp_esme_data->cleanup_thread_id = gwthread_create(smpp_esme_cleanup_thread, smpp_server);
    
    smpp_http_server_add_command(smpp_server, octstr_imm("esme-status"), smpp_esme_status_command);
    smpp_http_server_add_command(smpp_server, octstr_imm("esme-unbind"), smpp_esme_unbind_command);
}

void smpp_esme_shutdown(SMPPServer *smpp_server) {
    SMPPEsmeData *smpp_esme_data = smpp_server->esme_data;
    gwthread_wakeup(smpp_esme_data->cleanup_thread_id);
    gwthread_join(smpp_esme_data->cleanup_thread_id);
    
    gw_rwlock_wrlock(smpp_esme_data->lock);
    
    List *keys = dict_keys(smpp_esme_data->esmes);
    Octstr *key;
    SMPPEsmeGlobal *smpp_esme_global;
    
    while((key = gwlist_consume(keys)) != NULL) {
        smpp_esme_global = dict_remove(smpp_esme_data->esmes, key);
        smpp_esme_global_destroy(smpp_esme_global);
        octstr_destroy(key);
    }
    gwlist_destroy(keys, NULL);
    
    gw_rwlock_unlock(smpp_esme_data->lock);
    
    dict_destroy(smpp_esme_data->esmes);
    gwlist_destroy(smpp_esme_data->cleanup_queue, (void(*)(void *))smpp_esme_destroy);
    load_destroy(smpp_esme_data->inbound_load);
    load_destroy(smpp_esme_data->outbound_load);
    counter_destroy(smpp_esme_data->inbound_processed);
    counter_destroy(smpp_esme_data->outbound_processed);
    gw_rwlock_destroy(smpp_esme_data->lock);
    gw_rwlock_destroy(smpp_esme_data->cleanup_lock);
    gw_free(smpp_esme_data);
}

void smpp_esme_stop_listening(SMPPEsme *smpp_esme) {
    gw_rwlock_wrlock(smpp_esme->event_lock);
    smpp_esme->connected = 0;
    if (smpp_esme->event_container != NULL) {
        event_del(smpp_esme->event_container);
        event_free(smpp_esme->event_container);
        smpp_esme->event_container = NULL;
    }
    gw_rwlock_unlock(smpp_esme->event_lock);
}

void smpp_esme_disconnect(SMPPEsme *smpp_esme) {
    smpp_esme_stop_listening(smpp_esme);

    if (smpp_esme->conn != NULL) {
        conn_destroy(smpp_esme->conn);
        smpp_esme->conn = NULL;
    }
}

void smpp_esme_destroy_open_acks(SMPPEsme *smpp_esme) {
    gw_rwlock_wrlock(smpp_esme->ack_process_lock);
    List *keys = dict_keys(smpp_esme->open_acks);
    Octstr *key;
    SMPPQueuedPDU *smpp_queued_pdu;
    while((key = gwlist_consume(keys)) != NULL) {
        smpp_queued_pdu = dict_remove(smpp_esme->open_acks, key);
        if((smpp_queued_pdu) && (smpp_queued_pdu->callback)) {
            debug("smpp.esme.destroy.open.acks", 0, "Destroying open ack "
                    " key: %s, "
                    " system_id: %s, "
                    " bearerbox_id: %s, "
                    " sequence: %ld, "
                    " disconnect: %d, "
                    " pdu_type: %s", octstr_get_cstr(key), octstr_get_cstr(smpp_queued_pdu->system_id), octstr_get_cstr(smpp_queued_pdu->bearerbox_id), smpp_queued_pdu->sequence, smpp_queued_pdu->disconnect, smpp_queued_pdu->pdu->type_name);
            smpp_queued_pdu->callback(smpp_queued_pdu, SMPP_QUEUED_PDU_DESTROYED);
        }
        octstr_destroy(key);
    }
    gwlist_destroy(keys, NULL);
    dict_destroy(smpp_esme->open_acks);
    
    gw_rwlock_unlock(smpp_esme->ack_process_lock);
}

SMPPEsme *smpp_esme_create() {
    SMPPEsme *smpp_esme = gw_malloc(sizeof (SMPPEsme));
    smpp_esme->authenticated = 0;
    smpp_esme->conn = NULL;
    smpp_esme->system_id = NULL;
    smpp_esme->connected = 0;
    smpp_esme->time_connected = 0L;
    smpp_esme->time_disconnected = 0L;
    smpp_esme->time_last_pdu = 0L;
    smpp_esme->bind_type = SMPP_ESME_UNDEFINED;
    smpp_esme->version = 0;
    smpp_esme->inbound_queued = counter_create();
    smpp_esme->outbound_queued = counter_create();
    smpp_esme->inbound_load = load_create_real(0);
    smpp_esme->outbound_load = load_create_real(0);
    load_add_interval(smpp_esme->inbound_load, 1);
    load_add_interval(smpp_esme->outbound_load, 1);
    smpp_esme->errors = counter_create();
    smpp_esme->pending_disconnect = 0;
    smpp_esme->id = 0l;
    smpp_esme->event_container = NULL;
    smpp_esme->time_last_queue_process = 0L;
    smpp_esme->enquire_link_interval = SMPP_ESME_DEFAULT_ENQUIRE_LINK_INTERVAL;
    smpp_esme->wait_ack_time = SMPP_ESME_WAIT_ACK_TIME;
    smpp_esme->wait_ack_action = SMPP_ESME_WAIT_ACK_DISCONNECT;
    smpp_esme->sequence_number = counter_create();
    counter_increase(smpp_esme->sequence_number);

    smpp_esme->event_lock = gw_rwlock_create();
    smpp_esme->simulate = 0;
    smpp_esme->inbound_processed = counter_create();
    smpp_esme->outbound_processed = counter_create();
    smpp_esme->simulate_deliver_every = 0;
    smpp_esme->simulate_mo_every = 0;
    smpp_esme->simulate_permanent_failure_every = 0;
    smpp_esme->simulate_temporary_failure_every = 0;
    smpp_esme->alt_addr_charset = NULL;
    smpp_esme->alt_charset = NULL;
    smpp_esme->default_smsc = NULL;
    smpp_esme->open_acks = dict_create(1024, (void(*)(void *))smpp_queued_pdu_destroy);
    smpp_esme->catenated_sms_counter = counter_create();
    smpp_esme->system_type = NULL;
    smpp_esme->max_open_acks = SMPP_ESME_DEFAULT_MAX_OPEN_ACKS;
    smpp_esme->ack_process_lock = gw_rwlock_create();
    smpp_esme->pending_routing = counter_create();
    smpp_esme->ip = NULL;
    
    smpp_esme->mo_counter = counter_create();
    smpp_esme->mt_counter = counter_create();
    smpp_esme->dlr_counter = counter_create();
    smpp_esme->error_counter = counter_create();
    
    smpp_esme->pending_len = 0;
    
    return smpp_esme;
}

void smpp_esme_destroy(SMPPEsme *smpp_esme) {
    debug("smpp.esme.destroy", 0, "Destroying ESME id:%ld - %s", smpp_esme->id, octstr_get_cstr(smpp_esme->system_id));
    smpp_esme_destroy_open_acks(smpp_esme);
    
    counter_destroy(smpp_esme->inbound_queued);
    counter_destroy(smpp_esme->outbound_queued);
    counter_destroy(smpp_esme->errors);
    counter_destroy(smpp_esme->inbound_processed);
    counter_destroy(smpp_esme->outbound_processed);
    counter_destroy(smpp_esme->pending_routing);
    octstr_destroy(smpp_esme->system_id);
    octstr_destroy(smpp_esme->alt_addr_charset);
    octstr_destroy(smpp_esme->alt_charset);
    octstr_destroy(smpp_esme->default_smsc);
    octstr_destroy(smpp_esme->system_type);
    load_destroy(smpp_esme->inbound_load);
    load_destroy(smpp_esme->outbound_load);
    
    octstr_destroy(smpp_esme->ip);

    smpp_esme_disconnect(smpp_esme);

    counter_destroy(smpp_esme->catenated_sms_counter);
    counter_destroy(smpp_esme->sequence_number);
    
    counter_destroy(smpp_esme->mo_counter);
    counter_destroy(smpp_esme->mt_counter);
    counter_destroy(smpp_esme->dlr_counter);
    counter_destroy(smpp_esme->error_counter);
    
    gw_rwlock_destroy(smpp_esme->event_lock);
    gw_rwlock_destroy(smpp_esme->ack_process_lock);
    
    gw_free(smpp_esme);
}
