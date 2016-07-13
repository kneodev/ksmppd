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
#include "gw/msg.h"
#include "gw/load.h"
#include "smpp_server.h"
#include "smpp_esme.h"
#include "smpp_bearerbox.h"
#include "smpp_queued_pdu.h"




SMPPQueuedPDU *smpp_queued_pdu_create() {
    SMPPQueuedPDU *smpp_queued_pdu = gw_malloc(sizeof(SMPPQueuedPDU));
    smpp_queued_pdu->pdu = NULL;
    smpp_queued_pdu->smpp_esme = NULL;
    smpp_queued_pdu->priority = 0;
    smpp_queued_pdu->disconnect = 0;
    smpp_queued_pdu->id = NULL;
    smpp_queued_pdu->system_id = NULL;
    smpp_queued_pdu->callback = NULL;
    smpp_queued_pdu->context = NULL;
    smpp_queued_pdu->bearerbox = NULL;
    smpp_queued_pdu->bearerbox_id = NULL;
    smpp_queued_pdu->smpp_server = NULL;
    smpp_queued_pdu->time_sent = 0;
    smpp_queued_pdu->sequence = 0;
    smpp_queued_pdu->global_id = 0;
    return smpp_queued_pdu;
}

SMPPQueuedPDU *smpp_queued_pdu_create_quick(SMPPEsme *smpp_esme, unsigned long type, unsigned long seq_no) {
    SMPPQueuedPDU *smpp_queued_pdu = smpp_queued_pdu_create();
    smpp_queued_pdu->smpp_esme = smpp_esme;
    smpp_queued_pdu->pdu = smpp_pdu_create(type, seq_no);
    smpp_queued_pdu->id = NULL;
    smpp_queued_pdu->system_id = octstr_duplicate(smpp_esme->system_id);
    smpp_queued_pdu->smpp_server = smpp_esme->smpp_server;
    return smpp_queued_pdu;
}

void smpp_queued_pdu_destroy(SMPPQueuedPDU *smpp_queued_pdu) {
    smpp_pdu_destroy(smpp_queued_pdu->pdu);
    octstr_destroy(smpp_queued_pdu->id);
    octstr_destroy(smpp_queued_pdu->system_id);
    octstr_destroy(smpp_queued_pdu->bearerbox_id);
    gw_free(smpp_queued_pdu);
}