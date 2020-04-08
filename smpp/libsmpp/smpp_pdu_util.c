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
 #include <math.h>

#include "gwlib/gwlib.h"
#include "gw/smsc/smpp_pdu.h"
#include "gw/meta_data.h"
#include "gw/msg.h"
#include "gw/dlr.h"
#include "gw/load.h"
#include "gw/sms.h"
#include "smpp_server.h"
#include "smpp_bearerbox.h"
#include "smpp_esme.h"
#include "smpp_queued_pdu.h"
#include "smpp_queues.h"
#include "smpp_database.h"
#include "smpp_uuid.h"

#define BEARERBOX_DEFAULT_CHARSET "UTF-8"

 static int timestamp_to_minutes(Octstr *timestamp)
{
    struct tm tm, local;
    time_t valutc, utc;
    int rc, diff, dummy, localdiff;
    char relation;

    if (octstr_len(timestamp) == 0)
        return 0;

    if (octstr_len(timestamp) != 16)
        return -1;

    /*
    * Timestamp format:
    * YYMMDDhhmmsstnn[+-R]
    * t - tenths of second (not used by us)
    * nn - Time difference in quarter hours between local and UTC time
    */
    rc = sscanf(octstr_get_cstr(timestamp),
            "%02d%02d%02d%02d%02d%02d%1d%02d%1c",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
            &dummy, &diff, &relation);
    if (rc != 9)
       return -1;

    utc = time(NULL);
    if (utc == ((time_t)-1))
        return 0;

    if (relation == '+' || relation == '-') {
        tm.tm_year += 100; /* number of years since 1900 */
        tm.tm_mon--; /* month 0-11 */
        tm.tm_isdst = -1;
        /* convert to sec. since 1970 */
        valutc = gw_mktime(&tm);
        if (valutc == ((time_t)-1))
            return -1;

        /* work out local time, because gw_mktime assume local time */
        local = gw_localtime(utc);
        tm = gw_gmtime(utc);
        local.tm_isdst = tm.tm_isdst = -1;
        localdiff = difftime(gw_mktime(&local), gw_mktime(&tm));
        valutc += localdiff;

        debug("sms.smpp",0, "diff between utc and localtime (%d)", localdiff);
        diff = diff*15*60;
        switch(relation) {
            case '+':
                valutc -= diff;
                break;
            case '-':
                valutc += diff;
                break;
        }
    } else if (relation == 'R') { /* relative to SMSC localtime */
        local = gw_localtime(utc);
        local.tm_year += tm.tm_year;
        local.tm_mon += tm.tm_mon;
        local.tm_mday += tm.tm_mday;
        local.tm_hour += tm.tm_hour;
        local.tm_min += tm.tm_min;
        local.tm_sec += tm.tm_sec;
        valutc = gw_mktime(&local);
        if (valutc == ((time_t)-1))
           return -1;
    } else {
        return -1;
    }
    tm = gw_gmtime(valutc);
    debug("sms.smpp",0,"Requested UTC timestamp: %02d-%02d-%02d %02d:%02d:%02d",
            tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    debug("sms.smpp", 0, "requested timestamp in min. (%ld)", (valutc - utc)/60);

    return ceil ( difftime (valutc, utc) / 60 );
}

static long smpp_pdu_util_convert_addr(Octstr *id, Octstr *addr, long ton, long npi, Octstr *alt_addr_charset)
{
    long reason = SMPP_ESME_ROK;

    if (addr == NULL)
        return reason;

    switch(ton) {
        case GSM_ADDR_TON_INTERNATIONAL:
            /*
             * Checks to perform:
             *   1) assume international number has at least 7 chars
             *   2) the whole source addr consist of digits, exception '+' in front
             */
            if (octstr_len(addr) < 7) {
                /* We consider this as a "non-hard" condition, since there "may"
                 * be international numbers routable that are < 7 digits. Think
                 * of 2 digit country code + 3 digit emergency code. */
                warning(0, "SMPP[%s]: Malformed addr `%s', generally expected at least 7 digits. ",
                        octstr_get_cstr(id),
                        octstr_get_cstr(addr));
            } else if (octstr_get_char(addr, 0) == '+' &&
                       !octstr_check_range(addr, 1, 256, gw_isdigit)) {
                error(0, "SMPP[%s]: Malformed addr `%s', expected all digits. ",
                      octstr_get_cstr(id),
                      octstr_get_cstr(addr));
                reason = SMPP_ESME_RINVSRCADR;
                goto error;
            } else if (octstr_get_char(addr, 0) != '+' &&
                       !octstr_check_range(addr, 0, 256, gw_isdigit)) {
                error(0, "SMPP[%s]: Malformed addr `%s', expected all digits. ",
                      octstr_get_cstr(id),
                      octstr_get_cstr(addr));
                reason = SMPP_ESME_RINVSRCADR;
                goto error;
            }
            /* check if we received leading '00', then remove it*/
            if (octstr_search(addr, octstr_imm("00"), 0) == 0)
                octstr_delete(addr, 0, 2);
            
            /* international, insert '+' if not already here */
            if (octstr_get_char(addr, 0) != '+')
                octstr_insert_char(addr, 0, '+');
            
            break;
       default: /* otherwise don't touch addr, user should handle it */
            break;
    }

error:
    return reason;
}

static void smpp_pdu_util_compute_inbound_dcs(Msg *msg, Octstr *alt_charset, int data_coding, int esm_class)
{
    switch (data_coding) {
        case 0x00: /* default SMSC alphabet */
            /*
             * try to convert from something interesting if specified so
             * unless it was specified binary, i.e. UDH indicator was detected
             */
            if (alt_charset && msg->sms.coding != DC_8BIT) {
                if (charset_convert(msg->sms.msgdata, octstr_get_cstr(alt_charset), BEARERBOX_DEFAULT_CHARSET) != 0)
                    error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                          octstr_get_cstr(alt_charset), BEARERBOX_DEFAULT_CHARSET);
                msg->sms.coding = DC_7BIT;
            } else { /* assume GSM 03.38 7-bit alphabet */
                charset_gsm_to_utf8(msg->sms.msgdata);
                msg->sms.coding = DC_7BIT;
            }
            break;
        case 0x01:
            /* ASCII/IA5 - we don't need to perform any conversion
             * due that UTF-8's first range is exactly the ASCII table */
            msg->sms.coding = DC_7BIT; break;
        case 0x03: /* ISO-8859-1 - I'll convert to internal encoding */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-1", BEARERBOX_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from ISO-8859-1 to " BEARERBOX_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x02: /* 8 bit binary - do nothing */
        case 0x04: /* 8 bit binary - do nothing */
            msg->sms.coding = DC_8BIT; break;
        case 0x05: /* Japanese, JIS(X 0208-1990) */
            if (charset_convert(msg->sms.msgdata, "JIS_X0208-1990", BEARERBOX_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from Japanese (JIS_X0208-1990) to " BEARERBOX_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x06: /* Cyrllic - iso-8859-5, I'll convert to internal encoding */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-5", BEARERBOX_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from Cyrllic (ISO-8859-5) to " BEARERBOX_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x07: /* Hebrew iso-8859-8, I'll convert to internal encoding */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-8", BEARERBOX_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from Hebrew (ISO-8859-8) to " BEARERBOX_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x08: /* unicode UCS-2, yey */
            msg->sms.coding = DC_UCS2; break;
        case 0x0D: /* Japanese, Extended Kanji JIS(X 0212-1990) */
            if (charset_convert(msg->sms.msgdata, "JIS_X0212-1990", BEARERBOX_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from Japanese (JIS-X0212-1990) to " BEARERBOX_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x0E: /* Korean, KS C 5601 - now called KS X 1001, convert to Unicode */
            if (charset_convert(msg->sms.msgdata, "KSC_5601", BEARERBOX_DEFAULT_CHARSET) != 0 &&
                    charset_convert(msg->sms.msgdata, "KSC5636", BEARERBOX_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from Korean (KSC_5601/KSC5636) to " BEARERBOX_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;

            /*
             * don't much care about the others,
             * you implement them if you feel like it
             */

        default:
            /*
             * some of smsc send with dcs from GSM 03.38 , but these are reserved in smpp spec.
             * So we just look decoded values from dcs_to_fields and if none there make our assumptions.
             * if we have an UDH indicator, we assume DC_8BIT.
             */
            if (msg->sms.coding == DC_UNDEF && (esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR))
                msg->sms.coding = DC_8BIT;
            else if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) { /* assume GSM 7Bit , re-encode */
                msg->sms.coding = DC_7BIT;
                charset_gsm_to_utf8(msg->sms.msgdata);
            }
            break;
    }
}

Octstr *smpp_pdu_get_system_id_from_dlr_url(Octstr *received_dlr_url) {
    Octstr *dlr_service;
    long pos;
    pos = octstr_search_char(received_dlr_url, '|', 0);

    if (pos != -1) {
        dlr_service = octstr_copy(received_dlr_url, 0, (pos));

        return dlr_service;
    }

    return NULL;
}

List *smpp_pdu_msg_to_pdu(SMPPEsme *smpp_esme, Msg *msg) {
    SMPP_PDU *pdu, *pdu2;
    List *pdulist = gwlist_create(), *parts = NULL, *dlr_info;
    int dlrtype, catenate;
    int dlr_state = 7; /* UNKNOWN */
    long dlr_time = -1;
    
    Msg *dlr = NULL;
    char *text, *tmps, err[4] = {'0', '0', '0', '\0'};
    char submit_date_c_str[13] = {'\0'}, done_date_c_str[13] = {'\0'};
    struct tm tm_tmp;
    Octstr *msgid = NULL, *msgid2 = NULL, *dlr_status = NULL, *dlvrd = NULL;
    /* split variables */
    unsigned long msg_sequence, msg_count;
    int max_msgs;
    Octstr *header, *footer, *suffix, *split_chars, *tmp_str = NULL;
    Octstr *dlr_service = NULL, *dlr_submit = NULL, *dlr_url = NULL;
    Msg *msg2;

    Dict *metadata;

    pdu = smpp_pdu_create(deliver_sm, 0);

    pdu->u.deliver_sm.source_addr = octstr_duplicate(msg->sms.sender);
    pdu->u.deliver_sm.destination_addr = octstr_duplicate(msg->sms.receiver);

    /* Set the service type of the outgoing message. We'll use the config 
     * directive as default and 'binfo' as specific parameter. */
    pdu->u.deliver_sm.service_type = octstr_duplicate(msg->sms.binfo);

    /* setup default values */
    pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
    pdu->u.deliver_sm.source_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */

    if(octstr_len(pdu->u.deliver_sm.source_addr)) {
        /* lets see if its international or alphanumeric sender */
        if (octstr_get_char(pdu->u.deliver_sm.source_addr, 0) == '+') {
            if (!octstr_check_range(pdu->u.deliver_sm.source_addr, 1, 256, gw_isdigit)) {
                pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC; /* alphanum */
                pdu->u.deliver_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN; /* short code */
            } else {
                /* numeric sender address with + in front -> international (remove the +) */
                octstr_delete(pdu->u.deliver_sm.source_addr, 0, 1);
                pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
            }
        } else {
            if (!octstr_check_range(pdu->u.deliver_sm.source_addr, 0, 256, gw_isdigit)) {
                pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC;
                pdu->u.deliver_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;
            }
        }
    }

    pdu->u.deliver_sm.dest_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
    pdu->u.deliver_sm.dest_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */

    /*
     * if its a international number starting with +, lets remove the
     * '+' and set number type to international instead
     */
    if (octstr_len(pdu->u.deliver_sm.destination_addr) && octstr_get_char(pdu->u.deliver_sm.destination_addr, 0) == '+') {
        octstr_delete(pdu->u.deliver_sm.destination_addr, 0, 1);
        pdu->u.deliver_sm.dest_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
    }

    /* check length of src/dst address */
    if (octstr_len(pdu->u.deliver_sm.destination_addr) <= 0 ||
        octstr_len(pdu->u.deliver_sm.destination_addr) > 20 ||
            octstr_len(pdu->u.deliver_sm.source_addr) > 20) {
        smpp_pdu_destroy(pdu);
        gwlist_destroy(pdulist, NULL);
        return NULL;
    }

    /*
     * set the data coding scheme (DCS) field
     * check if we have a forced value for this from the smsc-group.
     * Note: if message class is set, then we _must_ force alt_dcs otherwise
     * dcs has reserved values (e.g. mclass=2, dcs=0x11). We check MWI flag
     * first here, because MWI and MCLASS can not be set at the same time and
     * function fields_to_dcs check MWI first, so we have no need to force alt_dcs
     * if MWI is set.
     */
    if (msg->sms.mwi == MWI_UNDEF && msg->sms.mclass != MC_UNDEF)
        pdu->u.deliver_sm.data_coding = fields_to_dcs(msg, 1); /* force alt_dcs */
    else
        pdu->u.deliver_sm.data_coding = fields_to_dcs(msg,
            msg->sms.alt_dcs);
    //(msg->sms.alt_dcs != SMS_PARAM_UNDEFINED ?
    //             /msg->sms.alt_dcs : box->alt_dcs));

    /* set protocol id */
    if (msg->sms.pid != SMS_PARAM_UNDEFINED)
        pdu->u.deliver_sm.protocol_id = msg->sms.pid;

    /*
     * set the esm_class field
     * default is store and forward, plus udh and rpi if requested
     */
    pdu->u.deliver_sm.esm_class = 0;
    if (octstr_len(msg->sms.udhdata))
        pdu->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class |
            ESM_CLASS_SUBMIT_UDH_INDICATOR;
    if (msg->sms.rpi > 0)
        pdu->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class |
            ESM_CLASS_SUBMIT_RPI;

    /* Is this a delivery report? */
    if (msg->sms.sms_type == report_mo) {
        metadata = meta_data_get_values(msg->sms.meta_data, "smpp");

        pdu->u.deliver_sm.esm_class |= ESM_CLASS_DELIVER_SMSC_DELIVER_ACK;
        dlrtype = msg->sms.dlr_mask;

        /* We expect the DLR-URL in the format UUID|SERVICE|SUBMIT-DATE */

        if (octstr_len(msg->sms.dlr_url) <= UUID_STR_LEN) {
            error(0, "Invalid/unknown DLR-URL returned, cannot process");
            smpp_pdu_destroy(pdu);
            gwlist_destroy(pdulist, NULL);
            octstr_destroy(msgid);
            gwlist_destroy(parts, octstr_destroy_item);
            dict_destroy(metadata);
            return NULL;
        } else {

            dlr_info = octstr_split(msg->sms.dlr_url, octstr_imm("|"));

            if(gwlist_len(dlr_info) == 3) {
                dlr_service = octstr_duplicate(gwlist_get(dlr_info, 0));
                dlr_submit = octstr_duplicate(gwlist_get(dlr_info, 1));
                dlr_url = octstr_duplicate(gwlist_get(dlr_info, 2));
                dlr_time = atol(octstr_get_cstr(dlr_submit));

                debug("smpp.pdu.msg.to.pdu", 0, "Processed DLR service = %s, time = %ld, id(s) = %s", octstr_get_cstr(dlr_service), dlr_time, octstr_get_cstr(dlr_url));
                gwlist_destroy(dlr_info, (void(*)(void *))octstr_destroy);
            } else {
                error(0, "Invalid/unknown DLR-URL returned, cannot process original = %s (%ld), dlr_service = %s dlr_submit = %s dlr_url = %s", octstr_get_cstr(msg->sms.dlr_url), gwlist_len(dlr_info), octstr_get_cstr(dlr_service), octstr_get_cstr(dlr_submit), octstr_get_cstr(dlr_url));
                octstr_destroy(dlr_service);
                octstr_destroy(dlr_submit);
                octstr_destroy(dlr_url);
                smpp_pdu_destroy(pdu);
                gwlist_destroy(pdulist, NULL);
                octstr_destroy(msgid);
                gwlist_destroy(parts, octstr_destroy_item);
                dict_destroy(metadata);
                gwlist_destroy(dlr_info, (void(*)(void *))octstr_destroy);
                return NULL;
            }
        }

        dlvrd = octstr_imm("000");
        switch (dlrtype) {
            case DLR_UNDEFINED:
            case DLR_NOTHING:
                dlr_state = 8;
                dlr_status = octstr_imm("REJECTD");
                break;
            case DLR_SUCCESS:
                dlr_state = 2;
                dlr_status = octstr_imm("DELIVRD");
                dlvrd = octstr_imm("001");
                break;
            case DLR_BUFFERED:
                dlr_state = 6;
                dlr_status = octstr_imm("ACCEPTD");
                break;
            case DLR_SMSC_SUCCESS:
                /* please note that this state does not quite conform to the SMMP v3.4 spec */
                dlr_state = 0;
                dlr_status = octstr_imm("BUFFRED");
                break;
            case DLR_FAIL:
            case DLR_SMSC_FAIL:
                dlr_state = 5;
                dlr_status = octstr_imm("UNDELIV");
                break;
        }

        if(metadata != NULL) {
            tmp_str = dict_get(metadata, octstr_imm("dlr_stat"));
            if(octstr_len(tmp_str) && (octstr_compare(tmp_str, octstr_imm("EXPIRED")) == 0)) {
                dlr_state = 3;
                dlr_status = octstr_imm("EXPIRED");
            }
        }


        text = octstr_get_cstr(msg->sms.msgdata);

        tmps = strstr(text, "err:");
        if (tmps != NULL) {
            /* we can't use 0-padding with %s, if this is really required,
             * then convert the numeric string to a real integer. - st */
            snprintf(err, sizeof (err), "%3.3s", tmps + (4 * sizeof (char)));
            tmps = strstr(tmps, " ");
            text = tmps ? tmps + (1 * sizeof (char)) : "";
        }

        tmps = strstr(text, "text:");
        if (tmps != NULL) {
            text = tmps + (5 * sizeof (char));
        }
        
        tmp_str = octstr_create(text);
        octstr_truncate(tmp_str, 12l);

        tm_tmp = gw_localtime(dlr_time);
        gw_strftime(submit_date_c_str, sizeof (submit_date_c_str), "%y%m%d%H%M%S", &tm_tmp);

        tm_tmp = gw_localtime(msg->sms.time);
        gw_strftime(done_date_c_str, sizeof (done_date_c_str), "%y%m%d%H%M%S", &tm_tmp);

        /* the msgids are in dlr->dlr_url as reported by Victor Luchitz */
        gwlist_destroy(parts, octstr_destroy_item);
        parts = octstr_split(dlr_url, octstr_imm(";"));
        
        while ((msgid2 = gwlist_extract_first(parts)) != NULL) {
            pdu2 = smpp_pdu_create(deliver_sm, 0);
            debug("smpp.pdu.msg.to.pdu", 0, "SMPP[%s:%ld] Creating deliver_sm for message: %s sequence number %ld.", octstr_get_cstr(smpp_esme->system_id), smpp_esme->id, octstr_get_cstr(msgid2), pdu2->u.deliver_sm.sequence_number);
            pdu2->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class;
            pdu2->u.deliver_sm.source_addr_ton = pdu->u.deliver_sm.source_addr_ton;
            pdu2->u.deliver_sm.source_addr_npi = pdu->u.deliver_sm.source_addr_npi;
            pdu2->u.deliver_sm.dest_addr_ton = pdu->u.deliver_sm.dest_addr_ton;
            pdu2->u.deliver_sm.dest_addr_npi = pdu->u.deliver_sm.dest_addr_npi;
            pdu2->u.deliver_sm.data_coding = pdu->u.deliver_sm.data_coding;
            pdu2->u.deliver_sm.protocol_id = pdu->u.deliver_sm.protocol_id;
            pdu2->u.deliver_sm.source_addr = octstr_duplicate(pdu->u.deliver_sm.source_addr);
            pdu2->u.deliver_sm.destination_addr = octstr_duplicate(pdu->u.deliver_sm.destination_addr);
            pdu2->u.deliver_sm.service_type = octstr_duplicate(pdu->u.deliver_sm.service_type);
            if (smpp_esme->version > 0x33) {
                pdu2->u.deliver_sm.receipted_message_id = octstr_duplicate(msgid2);
                pdu2->u.deliver_sm.message_state = dlr_state;
                dict_destroy(pdu2->u.deliver_sm.tlv);
                pdu2->u.deliver_sm.tlv = meta_data_get_values(msg->sms.meta_data, "smpp");
                if(metadata != NULL) {
                    pdu2->u.deliver_sm.network_error_code = octstr_duplicate(
                            dict_get(metadata, octstr_imm("network_error_code")));
                }
            }
            pdu2->u.deliver_sm.short_message = octstr_format("id:%S sub:001 dlvrd:%S submit date:%s done date:%s stat:%S err:%s text:%S", msgid2, dlvrd, submit_date_c_str, done_date_c_str, dlr_status, err, tmp_str);
            pdu2->u.deliver_sm.sm_length = octstr_len(pdu2->u.deliver_sm.short_message);
            octstr_destroy(msgid2);
            gwlist_append(pdulist, pdu2);
        }

        smpp_pdu_destroy(pdu);

        octstr_destroy(msgid);
        msg_destroy(dlr);
        gwlist_destroy(parts, octstr_destroy_item);
        octstr_destroy(tmp_str);
        octstr_destroy(dlr_service);
        octstr_destroy(dlr_submit);
        octstr_destroy(dlr_url);
        dict_destroy(metadata);
        return pdulist;
    } else {
        pdu->u.deliver_sm.short_message = octstr_duplicate(msg->sms.msgdata);
    }


    /* prepend udh if present */
    if (octstr_len(msg->sms.udhdata)) {
        octstr_insert(pdu->u.deliver_sm.short_message, msg->sms.udhdata, 0);
    }

    /* set priority */
    if (msg->sms.priority >= 0 && msg->sms.priority <= 3)
        pdu->u.deliver_sm.priority_flag = msg->sms.priority;


    header = NULL;
    footer = NULL;
    suffix = NULL;
    split_chars = NULL;
    catenate = 1;
    max_msgs = 255;
    
    if (catenate) {
        msg_sequence = counter_increase(smpp_esme->catenated_sms_counter) & 0xFF;
    } else {
        msg_sequence = 0;
    }

    if(msg->sms.msgdata != NULL) {
        /* split sms */
        parts = sms_split(msg, header, footer, suffix, split_chars, catenate,
                          msg_sequence, max_msgs, MAX_SMS_OCTETS);
    } else {
        parts = gwlist_create();
        gwlist_produce(parts, msg_duplicate(msg));
    }

    msg_count = gwlist_len(parts);

    debug("SMPP", 0, "message length %ld, sending %ld message%s",
            octstr_len(msg->sms.msgdata), msg_count, msg_count == 1 ? "" : "s");

    while ((msg2 = gwlist_extract_first(parts)) != NULL) {
        pdu2 = smpp_pdu_create(deliver_sm, counter_increase(smpp_esme->sequence_number));
        pdu2->u.deliver_sm.source_addr_ton = pdu->u.deliver_sm.source_addr_ton;
        pdu2->u.deliver_sm.source_addr_npi = pdu->u.deliver_sm.source_addr_npi;
        pdu2->u.deliver_sm.dest_addr_ton = pdu->u.deliver_sm.dest_addr_ton;
        pdu2->u.deliver_sm.dest_addr_npi = pdu->u.deliver_sm.dest_addr_npi;
        pdu2->u.deliver_sm.data_coding = pdu->u.deliver_sm.data_coding;
        pdu2->u.deliver_sm.protocol_id = pdu->u.deliver_sm.protocol_id;
        pdu2->u.deliver_sm.source_addr = octstr_duplicate(pdu->u.deliver_sm.source_addr);
        pdu2->u.deliver_sm.destination_addr = octstr_duplicate(pdu->u.deliver_sm.destination_addr);
        pdu2->u.deliver_sm.service_type = octstr_duplicate(pdu->u.deliver_sm.service_type);

        /* the following condition is currently always true */
        /* uncomment in case we're doing a SAR-split instead */
        if (octstr_len(msg2->sms.udhdata) > 0) {
            pdu2->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class | ESM_CLASS_DELIVER_UDH_INDICATOR;
            pdu2->u.deliver_sm.short_message = octstr_cat(msg2->sms.udhdata, msg2->sms.msgdata);
        } else {
            pdu2->u.deliver_sm.short_message = octstr_duplicate(msg2->sms.msgdata);
            /*
             * only re-encoding if using default smsc charset that is defined via
             * alt-charset in smsc group and if MT is not binary
             */
            if (msg->sms.coding == DC_7BIT || (msg->sms.coding == DC_UNDEF && octstr_len(msg->sms.udhdata))) {
                /*
                 * consider 3 cases:
                 *  a) data_coding 0xFX: encoding should always be GSM 03.38 charset
                 *  b) data_coding 0x00: encoding may be converted according to alt-charset
                 *  c) data_coding 0x00: assume GSM 03.38 charset if alt-charset is not defined
                 */
                if ((pdu2->u.deliver_sm.data_coding & 0xF0) ||
                    (!smpp_esme->alt_charset && pdu2->u.deliver_sm.data_coding == 0)) {
                    charset_utf8_to_gsm(pdu2->u.deliver_sm.short_message);
                } else if (pdu2->u.deliver_sm.data_coding == 0 && smpp_esme->alt_charset) {
                    /*
                     * convert to the given alternative charset
                     */
                    if (charset_convert(pdu2->u.deliver_sm.short_message, BEARERBOX_DEFAULT_CHARSET,
                                        octstr_get_cstr(smpp_esme->alt_charset)) != 0)
                        error(0, "Failed to convert msgdata from charset <%s> to <%s>, will send as is.",
                              BEARERBOX_DEFAULT_CHARSET, octstr_get_cstr(smpp_esme->alt_charset));
                }
            }
        }

        pdu2->u.deliver_sm.sm_length = octstr_len(pdu2->u.deliver_sm.short_message);

        if (smpp_esme->version > 0x33) {
            dict_destroy(pdu2->u.deliver_sm.tlv);
            pdu2->u.deliver_sm.tlv = meta_data_get_values(msg->sms.meta_data, "smpp");
        }

        gwlist_append(pdulist, pdu2);
        msg_destroy(msg2);
    }
    
    gwlist_destroy(parts, NULL);

    smpp_pdu_destroy(pdu);

    return pdulist;
}



Msg *smpp_submit_sm_to_msg(SMPPEsme *smpp_esme, SMPP_PDU *pdu, long *reason)
{
    Msg *msg;
    int ton, npi;

    gw_assert(pdu->type == submit_sm);

    msg = msg_create(sms);
    msg->sms.sms_type = mt_push;
    msg->sms.service = octstr_duplicate(smpp_esme->system_id);
    
    *reason = SMPP_ESME_ROK;

    /*
     * Reset source addr to have a prefixed '+' in case we have an
     * intl. TON to allow backend boxes (ie. smsbox) to distinguish
     * between national and international numbers.
     */
    ton = pdu->u.submit_sm.source_addr_ton;
    npi = pdu->u.submit_sm.source_addr_npi;
    /* check source addr */
    if ((*reason = smpp_pdu_util_convert_addr(smpp_esme->system_id, pdu->u.submit_sm.source_addr, ton, npi, smpp_esme->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.sender = octstr_duplicate(pdu->u.submit_sm.source_addr);
//    pdu->u.submit_sm.source_addr = NULL;

    /*
     * Follows SMPP spec. v3.4. issue 1.2
     * it's not allowed to have destination_addr NULL
     */
    if (pdu->u.submit_sm.destination_addr == NULL) {
        error(0, "SMPP[%s]: Malformed destination_addr `%s', may not be empty. "
              "Discarding MO message.", octstr_get_cstr(smpp_esme->system_id),
              octstr_get_cstr(pdu->u.submit_sm.destination_addr));
        *reason = SMPP_ESME_RINVDSTADR;
        goto error;
    }

    /* Same reset of destination number as for source */
    ton = pdu->u.submit_sm.dest_addr_ton;
    npi = pdu->u.submit_sm.dest_addr_npi;
    /* check destination addr */
    if ((*reason = smpp_pdu_util_convert_addr(smpp_esme->system_id, pdu->u.submit_sm.destination_addr, ton, npi, smpp_esme->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.receiver = octstr_duplicate(pdu->u.submit_sm.destination_addr);
//    pdu->u.submit_sm.destination_addr = NULL;

    /* SMSCs use service_type for billing information
     * According to SMPP v5.0 there is no 'billing_identification'
     * TLV in the submit_sm PDU optional TLVs. */
    msg->sms.binfo = octstr_duplicate(pdu->u.submit_sm.service_type);
//    pdu->u.submit_sm.service_type = NULL;

    if (pdu->u.submit_sm.esm_class & ESM_CLASS_SUBMIT_RPI)
        msg->sms.rpi = 1;

    /*
     * Check for message_payload if version > 0x33 and sm_length == 0
     * Note: SMPP spec. v3.4. doesn't allow to send both: message_payload & short_message!
     */
    if (smpp_esme->version > 0x33 && pdu->u.submit_sm.sm_length == 0 && pdu->u.submit_sm.message_payload) {
        msg->sms.msgdata = octstr_duplicate(pdu->u.submit_sm.message_payload);
//        pdu->u.submit_sm.message_payload = NULL;
    }
    else {
        msg->sms.msgdata = octstr_duplicate(pdu->u.submit_sm.short_message);
//        pdu->u.submit_sm.short_message = NULL;
    }

    /* check sar_msg_ref_num, sar_segment_seqnum, sar_total_segments */
    if (smpp_esme->version > 0x33 &&
    	pdu->u.submit_sm.sar_msg_ref_num >= 0 && pdu->u.submit_sm.sar_segment_seqnum > 0 && pdu->u.submit_sm.sar_total_segments > 0) {
    	/*
    		For GSM networks, the concatenation related TLVs (sar_msg_ref_num, sar_total_segments, sar_segment_seqnum)
    		or port addressing related TLVs
    		(source_port, dest_port) cannot be used in conjunction with encoded User Data Header in the short_message
    		(user data) field. This means that the above listed TLVs cannot be used if the User Data Header Indicator flag is set.
    	*/
    	if (pdu->u.submit_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
    		error(0, "SMPP[%s]: sar_msg_ref_num, sar_segment_seqnum, sar_total_segments in conjuction with UDHI used, rejected.",
    			  octstr_get_cstr(smpp_esme->system_id));
    		*reason = SMPP_ESME_RINVTLVVAL;
    		goto error;
    	}
    	/* create multipart UDH */
    	prepend_catenation_udh(msg,
    						   pdu->u.submit_sm.sar_segment_seqnum,
    						   pdu->u.submit_sm.sar_total_segments,
    						   pdu->u.submit_sm.sar_msg_ref_num);
    }

    /*
     * Encode udh if udhi set
     * for reference see GSM03.40, section 9.2.3.24
     */
    if (pdu->u.submit_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
        int udhl;
        udhl = octstr_get_char(msg->sms.msgdata, 0) + 1;
        debug("bb.sms.smpp",0,"SMPP[%s]: UDH length read as %d",
              octstr_get_cstr(smpp_esme->system_id), udhl);
        if (udhl > octstr_len(msg->sms.msgdata)) {
            error(0, "SMPP[%s]: Malformed UDH length indicator 0x%03x while message length "
                  "0x%03lx. Discarding MO message.", octstr_get_cstr(smpp_esme->system_id),
                  udhl, octstr_len(msg->sms.msgdata));
            *reason = SMPP_ESME_RINVESMCLASS;
            goto error;
        }
        msg->sms.udhdata = octstr_copy(msg->sms.msgdata, 0, udhl);
        octstr_delete(msg->sms.msgdata, 0, udhl);
    }

    dcs_to_fields(&msg, pdu->u.submit_sm.data_coding);

    /* handle default data coding */
    smpp_pdu_util_compute_inbound_dcs(msg, smpp_esme->alt_charset, pdu->u.submit_sm.data_coding, pdu->u.submit_sm.esm_class);

    msg->sms.pid = pdu->u.submit_sm.protocol_id;
    
    msg->sms.time = time(NULL);

    /* set validity period if needed */
    if (pdu->u.submit_sm.validity_period) {
        msg->sms.validity = time(NULL) + timestamp_to_minutes(pdu->u.submit_sm.validity_period) * 60;
    }

    /* set priority flag */
    msg->sms.priority = pdu->u.submit_sm.priority_flag;

    if (msg->sms.meta_data == NULL)
        msg->sms.meta_data = octstr_create("");
    meta_data_set_values(msg->sms.meta_data, pdu->u.submit_sm.tlv, "smpp", 1);
    
    
    switch (pdu->u.submit_sm.registered_delivery & 0x03) {
        case 1:
            msg->sms.dlr_mask = (DLR_SUCCESS | DLR_FAIL | DLR_SMSC_FAIL);
            break;
        case 2:
            msg->sms.dlr_mask = (DLR_FAIL | DLR_SMSC_FAIL);
            break;
        default:
            msg->sms.dlr_mask = 0;
            break;
    }

    return msg;

error:
    msg_destroy(msg);
    return NULL;
}


Msg *smpp_data_sm_to_msg(SMPPEsme *smpp_esme, SMPP_PDU *pdu, long *reason)
{
    Msg *msg;
    int ton, npi;

    gw_assert(pdu->type == data_sm);

    msg = msg_create(sms);
    msg->sms.sms_type = mt_push;
    msg->sms.service = octstr_duplicate(smpp_esme->system_id);
    
    *reason = SMPP_ESME_ROK;

    /*
     * Reset source addr to have a prefixed '+' in case we have an
     * intl. TON to allow backend boxes (ie. smsbox) to distinguish
     * between national and international numbers.
     */
    ton = pdu->u.data_sm.source_addr_ton;
    npi = pdu->u.data_sm.source_addr_npi;
    /* check source addr */
    if ((*reason = smpp_pdu_util_convert_addr(smpp_esme->system_id, pdu->u.data_sm.source_addr, ton, npi, smpp_esme->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.sender = pdu->u.data_sm.source_addr;
    pdu->u.data_sm.source_addr = NULL;

    /*
     * Follows SMPP spec. v3.4. issue 1.2
     * it's not allowed to have destination_addr NULL
     */
    if (pdu->u.data_sm.destination_addr == NULL) {
        error(0, "SMPP[%s]: Malformed destination_addr `%s', may not be empty. "
              "Discarding MO message.", octstr_get_cstr(smpp_esme->system_id),
              octstr_get_cstr(pdu->u.data_sm.destination_addr));
        *reason = SMPP_ESME_RINVDSTADR;
        goto error;
    }

    /* Same reset of destination number as for source */
    ton = pdu->u.data_sm.dest_addr_ton;
    npi = pdu->u.data_sm.dest_addr_npi;
    /* check destination addr */
    if ((*reason = smpp_pdu_util_convert_addr(smpp_esme->system_id, pdu->u.data_sm.destination_addr, ton, npi, smpp_esme->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.receiver = pdu->u.data_sm.destination_addr;
    pdu->u.data_sm.destination_addr = NULL;

    /* SMSCs use service_type for billing information
     * According to SMPP v5.0 there is no 'billing_identification'
     * TLV in the data_sm PDU optional TLVs. */
    msg->sms.binfo = pdu->u.data_sm.service_type;
    pdu->u.data_sm.service_type = NULL;

    if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_RPI)
        msg->sms.rpi = 1;

    /*
     * Check for message_payload if version > 0x33 and sm_length == 0
     * Note: SMPP spec. v3.4. doesn't allow to send both: message_payload & short_message!
     */
    msg->sms.msgdata = pdu->u.data_sm.message_payload;
    pdu->u.data_sm.message_payload = NULL;

    /* check sar_msg_ref_num, sar_segment_seqnum, sar_total_segments */
    if (smpp_esme->version > 0x33 &&
    	pdu->u.data_sm.sar_msg_ref_num >= 0 && pdu->u.data_sm.sar_segment_seqnum > 0 && pdu->u.data_sm.sar_total_segments > 0) {
    	/*
    		For GSM networks, the concatenation related TLVs (sar_msg_ref_num, sar_total_segments, sar_segment_seqnum)
    		or port addressing related TLVs
    		(source_port, dest_port) cannot be used in conjunction with encoded User Data Header in the short_message
    		(user data) field. This means that the above listed TLVs cannot be used if the User Data Header Indicator flag is set.
    	*/
    	if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
    		error(0, "SMPP[%s]: sar_msg_ref_num, sar_segment_seqnum, sar_total_segments in conjuction with UDHI used, rejected.",
    			  octstr_get_cstr(smpp_esme->system_id));
    		*reason = SMPP_ESME_RINVTLVVAL;
    		goto error;
    	}
    	/* create multipart UDH */
    	prepend_catenation_udh(msg,
    						   pdu->u.data_sm.sar_segment_seqnum,
    						   pdu->u.data_sm.sar_total_segments,
    						   pdu->u.data_sm.sar_msg_ref_num);
    }

    /*
     * Encode udh if udhi set
     * for reference see GSM03.40, section 9.2.3.24
     */
    if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
        int udhl;
        udhl = octstr_get_char(msg->sms.msgdata, 0) + 1;
        debug("bb.sms.smpp",0,"SMPP[%s]: UDH length read as %d",
              octstr_get_cstr(smpp_esme->system_id), udhl);
        if (udhl > octstr_len(msg->sms.msgdata)) {
            error(0, "SMPP[%s]: Malformed UDH length indicator 0x%03x while message length "
                  "0x%03lx. Discarding MO message.", octstr_get_cstr(smpp_esme->system_id),
                  udhl, octstr_len(msg->sms.msgdata));
            *reason = SMPP_ESME_RINVESMCLASS;
            goto error;
        }
        msg->sms.udhdata = octstr_copy(msg->sms.msgdata, 0, udhl);
        octstr_delete(msg->sms.msgdata, 0, udhl);
    }

    dcs_to_fields(&msg, pdu->u.data_sm.data_coding);

    /* handle default data coding */
    smpp_pdu_util_compute_inbound_dcs(msg, smpp_esme->alt_charset, pdu->u.data_sm.data_coding, pdu->u.data_sm.esm_class);
    
    msg->sms.time = time(NULL);

    /* set priority flag */

    if (msg->sms.meta_data == NULL)
        msg->sms.meta_data = octstr_create("");
    meta_data_set_values(msg->sms.meta_data, pdu->u.data_sm.tlv, "smpp", 1);
    
    
    switch (pdu->u.data_sm.registered_delivery & 0x03) {
        case 1:
            msg->sms.dlr_mask = (DLR_SUCCESS | DLR_FAIL | DLR_SMSC_FAIL);
            break;
        case 2:
            msg->sms.dlr_mask = (DLR_FAIL | DLR_SMSC_FAIL);
            break;
        default:
            msg->sms.dlr_mask = 0;
            break;
    }

    return msg;

error:
    msg_destroy(msg);
    return NULL;
}
