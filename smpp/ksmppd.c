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
 *        Kurt Neo & the Kannel Group (http://www.kannel.org/)." 
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
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gw/heartbeat.h"
#include "libsmpp/smpp_server.h"
#include "libsmpp/smpp_listener.h"
#include <config.h>
/* Global context for the SMPP Server */
static SMPPServer *smpp_server_global;


static void signal_handler(int signum) {
    /* On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one.
     */
    if (!gwthread_shouldhandlesignal(signum))
        return;

    switch (signum) {
        case SIGINT:
        case SIGTERM:
       	    if (!(smpp_server_global->server_status & SMPP_SERVER_STATUS_SHUTDOWN)) {
                error(0, "SIGINT received, aborting program...");
                smpp_server_global->server_status |= SMPP_SERVER_STATUS_SHUTDOWN;
                smpp_listener_shutdown(smpp_server_global);
                gwthread_wakeup_all();
            }
            break;

        case SIGUSR2:
            warning(0, "SIGUSR2 received, catching and re-opening logs");
            log_reopen();
            alog_reopen();
            break;
        case SIGHUP:
            warning(0, "SIGHUP received, catching and re-opening logs");
            log_reopen();
            alog_reopen();
            break;

        /* 
         * It would be more proper to use SIGUSR1 for this, but on some
         * platforms that's reserved by the pthread support. 
         */
        case SIGQUIT:
	       warning(0, "SIGQUIT ±received, reporting memory usage.");
	       gw_check_leaks();
	       break;
               
        case SIGSEGV:
            panic(0, "SIGSEGV received, exiting immediately");
            break;
    }
}


static void setup_signal_handlers(void) {
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
    sigaction(SIGUSR2, &act, NULL);
//    sigaction(SIGSEGV, &act, NULL);
}

static int check_args(int i, int argc, char **argv) {
    return 0;
} 


/*
 * 
 */int main(int argc, char **argv)
{
    int cf_index;

    gwlib_init();
    cf_index = get_and_set_debugs(argc, argv, check_args);
    setup_signal_handlers();


    SMPPServer *smpp_server = smpp_server_create();
    
    smpp_server_global = smpp_server;
    
    smpp_server->server_status = SMPP_SERVER_STATUS_STARTUP;
    
    
    if (argv[cf_index] == NULL)
	smpp_server->config_filename = octstr_create("ksmppd.conf");
    else
	smpp_server->config_filename = octstr_create(argv[cf_index]);
    
    debug("smpp", 0, "Initializing configuration file %s", octstr_get_cstr(smpp_server->config_filename));
    
    smpp_server_reconfigure(smpp_server);

    report_versions("ksmppd");

    info(0, "----------------------------------------------");
    info(0, SMPP_SERVER_NAME " kmppd version %s starting", GITVERSION);
    info(0, SMPP_SERVER_NAME " system platform %s ", PLATFORMINFO);
    info(0, "----------------------------------------------");

    smpp_server->server_status = SMPP_SERVER_STATUS_RUNNING;
    
    smpp_listener_start(smpp_server); // This request will block until stopped
    
    smpp_server_destroy(smpp_server);
   

    log_close_all();
    gwlib_shutdown();

    return 0;
}
