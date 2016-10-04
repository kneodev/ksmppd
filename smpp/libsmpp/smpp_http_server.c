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
#include "smpp_server.h"
#include "smpp_http_server.h"

typedef struct {
    long port;
    Octstr *password;
    Dict *commands;
    long receive_thread;
    long start_time;
    int ssl;
    Octstr *interface;
} SMPPHTTPServer;

SMPPHTTPCommand *smpp_http_server_command_create() {
    SMPPHTTPCommand *smpp_http_command = gw_malloc(sizeof(SMPPHTTPCommand));
    smpp_http_command->callback = NULL;
    smpp_http_command->key = NULL;
    return smpp_http_command;
}

void smpp_http_server_command_destroy(SMPPHTTPCommand *smpp_http_command) {
    octstr_destroy(smpp_http_command->key);
    gw_free(smpp_http_command);
}

SMPPHTTPServer *smpp_http_server_create() {
    SMPPHTTPServer *smpp_http_server = gw_malloc(sizeof(SMPPHTTPServer));
    smpp_http_server->port = 0;
    smpp_http_server->password = NULL;
    smpp_http_server->commands = dict_create(128, (void(*)(void *))smpp_http_server_command_destroy);
    smpp_http_server->receive_thread = 0;
    smpp_http_server->interface = NULL;
    return smpp_http_server;
}

void smpp_http_server_destroy(SMPPHTTPServer *smpp_http_server) {
    octstr_destroy(smpp_http_server->password);
    dict_destroy(smpp_http_server->commands);
    gw_free(smpp_http_server);
}

SMPPHTTPCommandResult *smpp_http_command_result_create() {
    SMPPHTTPCommandResult *smpp_http_result = gw_malloc(sizeof(SMPPHTTPCommandResult));
    smpp_http_result->headers = http_create_empty_headers();
    smpp_http_result->result = NULL;
    smpp_http_result->status = HTTP_OK;
    return smpp_http_result;
}

void smpp_http_command_result_destroy(SMPPHTTPCommandResult *smpp_http_command_result) {
    if(!smpp_http_command_result) {
        return;
    }
    http_destroy_headers(smpp_http_command_result->headers);
    octstr_destroy(smpp_http_command_result->result);
    gw_free(smpp_http_command_result);
}

SMPPHTTPCommandResult *smpp_http_command_uptime(SMPPServer *smpp_server, List *cgivars, int content_type) {
    SMPPHTTPCommandResult *smpp_http_result = smpp_http_command_result_create();
    SMPPHTTPServer *smpp_http_server = smpp_server->http_server;
    long diff = time(NULL) - smpp_http_server->start_time;
    
    if(content_type == HTTP_CONTENT_TYPE_PLAIN) {
        smpp_http_result->result = octstr_format("Uptime %ldd %ldh %ldm %lds\n", diff/3600/24, diff/3600%24, diff/60%60, diff%60);
    } else if(content_type == HTTP_CONTENT_TYPE_XML) {
        smpp_http_result->result = octstr_format("<uptime>%ld</uptime>", diff);
    }
    
    return smpp_http_result;
    
}

SMPPHTTPCommandResult *smpp_http_command_log_level(SMPPServer *smpp_server, List *cgivars, int content_type) {
    SMPPHTTPCommandResult *smpp_http_result = smpp_http_command_result_create();
    
    Octstr *level = http_cgi_variable(cgivars, "level");

    int new_loglevel = -1;

    if(octstr_len(level)) {
        new_loglevel = atoi(octstr_get_cstr(level));
    }
    
    if(new_loglevel >= 0) {
        log_set_log_level(new_loglevel);
        if(content_type == HTTP_CONTENT_TYPE_PLAIN) {
            smpp_http_result->result = octstr_format("Log level set to %d\n",new_loglevel);
        } else if(content_type == HTTP_CONTENT_TYPE_XML) {
            smpp_http_result->result = octstr_format("<message>Log level set to %d</message>\n",new_loglevel);
        }
    } else {
        if(content_type == HTTP_CONTENT_TYPE_PLAIN) {
            smpp_http_result->result = octstr_format("Invalid level specified\n");
        } else if(content_type == HTTP_CONTENT_TYPE_XML) {
            smpp_http_result->result = octstr_format("<message>Invalid level specified</message>\n");
        }
    }
    
    return smpp_http_result;
}

void smpp_http_server_request_handler(void *arg) {
    SMPPServer *smpp_server = arg;
    SMPPHTTPServer *smpp_http_server = smpp_server->http_server;
    
    HTTPClient *client;
    Octstr *ip, *url, *body, *answer;
    List *hdrs, *args, *reply_headers;
    int status;
    
    Octstr *password;
    long pos;
    
    Octstr *extension;
    int content_type;
    List *keys;
    Octstr *key;
    
    SMPPHTTPCommand *smpp_http_command;
    SMPPHTTPCommandResult *smpp_http_command_result;
    
    info(0, "Starting HTTP Server thread on port %ld", smpp_http_server->port);
    
    for (;;) {
    	/* reset request wars */
    	ip = url = body = answer = NULL;
    	hdrs = args = NULL;
        reply_headers = NULL;
        content_type = HTTP_CONTENT_TYPE_PLAIN;
        smpp_http_command_result = NULL;

        client = http_accept_request(smpp_http_server->port, &ip, &url, &hdrs, &body, &args);
        if(!client) {
            break;
        }
        
        debug("smpp.http.server.request.handler", 0, "Received request %s", octstr_get_cstr(url));
        
        password = http_cgi_variable(args, "password");
        
        if(!octstr_len(password) || (octstr_compare(password, smpp_http_server->password) != -0)) {
            status = 403;
            answer = octstr_imm("Denied");
        } else {
            pos = octstr_rsearch_char(url, '.', (octstr_len(url)-1));
            if(pos != -1) {
                extension = octstr_copy(url, pos, octstr_len(url) - pos);
                if(octstr_compare(extension, octstr_imm(".xml")) == 0) {
                    content_type = HTTP_CONTENT_TYPE_XML;
                }
                octstr_destroy(extension);
                octstr_truncate(url, pos);
            }
            
            
            smpp_http_command = dict_get(smpp_http_server->commands, url);
            
            if(smpp_http_command) {
                smpp_http_command_result = smpp_http_command->callback(smpp_server, args, content_type);
                status = smpp_http_command_result->status;
                answer = octstr_duplicate(smpp_http_command_result->result);
                reply_headers = smpp_http_command_result->headers;
                
            } else {
                status = 404;
                answer = octstr_format("Command '%S' not found, available commands are: \n\n", url);
                keys = dict_keys(smpp_http_server->commands);
                while((key = gwlist_consume(keys)) != NULL) {
                    octstr_format_append(answer, "%S\n", key);
                    octstr_destroy(key);
                }
                gwlist_destroy(keys, NULL);
            }
        }
        http_send_reply(client, status, reply_headers, answer);
        
        smpp_http_command_result_destroy(smpp_http_command_result);
        
        octstr_destroy(answer);
        octstr_destroy(ip);
        octstr_destroy(url);
        http_destroy_headers(hdrs);
        octstr_destroy(body);
        http_destroy_cgiargs(args);
    }
    
    info(0, "Shutting down HTTP Server thread on port %ld", smpp_http_server->port);
    
    
}



void smpp_http_server_add_command(SMPPServer *smpp_server, Octstr *key, SMPPHTTPCommandResult *(*callback)(SMPPServer *smpp_server, List *cgivars, int content_type)) {
    SMPPHTTPCommand *smpp_http_command = smpp_http_server_command_create();
    smpp_http_command->key = octstr_format("/%S", key);
    smpp_http_command->callback = callback;

    SMPPHTTPServer *smpp_http_server = smpp_server->http_server;
    dict_put(smpp_http_server->commands, smpp_http_command->key, smpp_http_command);
}

void smpp_http_server_init(SMPPServer *smpp_server) {
    
    CfgGroup *grp = cfg_get_single_group(smpp_server->running_configuration, octstr_imm("http-server"));
    if(!grp) {
        panic(0, "No 'http-server' group configured, cannot continue");
    }
    
    SMPPHTTPServer *smpp_http_server = smpp_http_server_create();
    smpp_server->http_server = smpp_http_server;
    
    if(cfg_get_integer(&smpp_http_server->port, grp, octstr_imm("port")) == -1) {
        panic(0, "No 'port' specified in 'http-server' group, cannot continue");
    }
    
    smpp_http_server->password = cfg_get(grp, octstr_imm("password"));
    if(!octstr_len(smpp_http_server->password)) {
        panic(0, "No 'password' specified in 'http-server' group, cannot continue");
    }
    
    cfg_get_bool(&smpp_http_server->ssl, grp, octstr_imm("ssl"));
        
    if(http_open_port_if(smpp_http_server->port, smpp_http_server->ssl, smpp_http_server->interface) == -1) {
        panic(0, "Could not start HTTP server on port %ld", smpp_http_server->port);
    }
    
    smpp_http_server_add_command(smpp_server, octstr_imm("uptime"), smpp_http_command_uptime);
    smpp_http_server_add_command(smpp_server, octstr_imm("log-level"), smpp_http_command_log_level);
    
    smpp_http_server->start_time = time(NULL);
        
    smpp_http_server->receive_thread = gwthread_create(smpp_http_server_request_handler, smpp_server);
    
    
    
}

void smpp_http_server_shutdown(SMPPServer *smpp_server) {
    SMPPHTTPServer *smpp_http_server = smpp_server->http_server;
    http_close_port(smpp_http_server->port);
    
    gwthread_join(smpp_http_server->receive_thread);
    
    smpp_http_server_destroy(smpp_http_server);
}