#### No longer maintained by Kurt. [Donald Jackson](https://github.com/donald-jackson) has taken over.

# KSMPPD aka Kurt's SMPP Daemon

This is my attempt at an alternative to OpenSMPPBox and the Commercial SMPPBox available

I would recommend reading this page in its entiriety and then moving on to the [wiki](https://github.com/kneodev/ksmppd/wiki) for more details.

What I have tried to do here is implement an SMPP server which connects to the bearerbox as currently SMSBox with the following features:

* :white_check_mark: Not bound by threads per client (built with libevent) 
* :white_check_mark: Fast start up and regardless of workload/queues 
* :white_check_mark: Limited memory usage (disk based excess queue storage)
* :white_check_mark: Database authentication support 
* :white_check_mark: Database routing support 
* :white_check_mark: HTTP based authentication support
* :white_check_mark: HTTP based routing support
* :white_check_mark: Prepaid billing support
* :white_check_mark: Multiple bearerbox connections 
* :white_check_mark: Throttling support 
* :white_check_mark: Full support for simulation (delivery reports, MO, failures)
* :white_check_mark: Fully asynchronous 
* :white_check_mark: submit_sm_resp PDU's only provided once bearerbox or database has accepted storage 
* :white_check_mark: HTTP routers use a callback mechanism

### TO-DO
* Embeddable code (almost, just some thread joins to deal with)
* others proposed via issue tracker

This software will be free, forever. If you get charged for this software, please notify me.

### Acknowledgements

This product includes software developed by the Kannel Group (http://www.kannel.org/).

I'd also like to specifically thank the developer of OpenSMPPBox Rene Kluwen as I used some of his PDU conversion mechanisms in OpenSMPPBox.

It makes extensive use of gwlib and other features developed for Kannel, it would not be possible without them.

Special thanks to [donald-jackson](https://github.com/donald-jackson) for multiple contributions.

If you do wish to donate to this project, please do so via the bitcoin address below.

### BTC address: 1NhLkTDiZtFTJMefvjQY4pUWM3jD641jWN

## Building

Thanks to [rajesh6115](https://github.com/rajesh6115) this build can now be completed using autotools.

### Dependencies


#### Kannel

You need to have kannel installed with MySQL support in order to compile successfully. If you don't have kannel installed, you can do so by executing the following commands.

    svn co https://svn.kannel.org/gateway/trunk kannel-trunk
    cd kannel-trunk
    ./bootstrap.sh
    ./configure --with-mysql --enable-ssl --enable-start-stop-daemon --enable-static
    make
    sudo make install

Assuming the above is done successfully (you may need to install MySQL, libxml2, etc dependencies to successfully build).

#### Other dependencies

libevent

    yum install -y libevent-devel
    
### Building KSMPPD

Now that you have the dependencies ready you can do the following.

    git clone https://github.com/kneodev/ksmppd.git
    cd ksmppd
    ./bootstrap.sh

If the above goes well (you will only need to bootstrap once)

    make

You can now run using ./smpp/ksmppd.

## Using KSMPPD

I have created a number of example configurations in this repository located under examples/configurations

* database-only is a system that requires no HTTP server and uses a database for authentication and routing
* http-auth-database-routing uses an HTTP request to authenticate ESME's and a database for routing
* http-only uses HTTP for both authentication and routing.

If using a database in any of the above examples, you will need to create the table schemas at a minimum located under database-schemas in this repository.

There are commands available via the built in HTTP server which allow you to perform certain tasks. Appending ".xml" to commands will produce output in XML format.

http://ksmppdhost:port/esme-status

    curl "http://localhost:14010/esme-status?password=ksmppdpass"
    Summary: 

    Unique known ESME's: 3
    Total inbound processed:0 load: 0.00/0.00/0.00/sec
    Total outbound processed:0 load: 0.00/0.00/0.00/sec

    smppusera - binds:4/0, total inbound load:(0.00/0.00/0.00)/11.00/sec, outbound load:(0.00/0.00/0.00)/sec
    -- id:4 uptime:0d 0h 0m 11s, type:2, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1
    -- id:3 uptime:0d 0h 0m 11s, type:2, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1
    -- id:9 uptime:0d 0h 0m 10s, type:1, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1
    -- id:8 uptime:0d 0h 0m 10s, type:1, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1

    smppuserb - binds:2/2, total inbound load:(0.00/0.00/0.00)/0.00/sec, outbound load:(0.00/0.00/0.00)/sec
    -- id:0 uptime:0d 0h 0m 13s, type:1, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1
    -- id:1 uptime:0d 0h 0m 12s, type:2, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1

    smppuserc - binds:2/0, total inbound load:(0.00/0.00/0.00)/0.00/sec, outbound load:(0.00/0.00/0.00)/sec
    -- id:2 uptime:0d 0h 0m 11s, type:2, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1
    -- id:5 uptime:0d 0h 0m 11s, type:1, open-acks:0, inbound (load/queued/processed/routing):0.00/0/1/0, outbound (load/queued/processed):0.00/0/1

http://ksmppdhost:port/esme-unbind

    curl "http://localhost:14010/esme-unbind?password=ksmppdpass&system-id=smppuserb&bind-id=1"
    1 binds disconnected
    curl "http://localhost:14010/esme-unbind?password=ksmppdpass&system-id=smppuserb"
    2 binds disconnected   

http://ksmppd:port/rebuild-routes (you MUST run this if you change routes in the database)

    curl "http://localhost:14010/rebuild-routes?password=ksmppdpass
    Routes updated

## Performance

Benchmarks on a Core i5 @ 3.2 GHz this software processes 1534 messages per second (more thorough benchmarks later). 

KSMPPD has been thoroughly tested for memory leaks in all scenarios and all have been dealt with that have been found.

## Contributing

Please create issues or pull requests here to contribute.

## Behaviour

At this stage I have determined optimal configuration to be with a database queue enabled. This enables better handling of failure in cases where ESME's are offline or bearerbox is unavailable.

All scenarios will allow ESME's to authenticate as normal, unless the database is down.

### Some scenarios

#### There are no bearerbox connections alive for deliver of messages.

* submit_sm (message submissions) will be queued to the database for later delivery and successful responses returned.
* Any queued MO/DLR's from a previously alive bearerbox will be forwarded to connected ESME's.

#### At least one bearerbox is alive but no ESME's (or some receivers) are not available
* MO/DLR's received from bearerbox will be attempt to be routed to a target receiver ESME - if not available they will be queued to try later.
* Once a previously unavailable ESME becomes available, pending MO/DLR's will be forwarded.
* It's important to note that there is an 'open ack' limit to ESME's, and if an ESME hits this limit, messages will begin being queued to the database.

#### System is behaving as normal (database up, bearerbox(s) up, ESME's available)
* If a bearerbox rejects a message (invalid routing, etc) - the ESME will get this result immediately via submit_sm_resp
* If permanent routing failure for MO (eg: no routes) messages will be discarded and bearerbox notified accordingly (ack_failed)
* If an ESME exceeds their max allowed open acks, messages will be queued to the database and requeued as space becomes available. This is to solve excessive memory use.

#### System restart
* The system first starts, connects to bearerbox and allows connections from ESMEs. Once started it begins reprocessing bearerbox queues if any.
* Once ESME's reconnect - their queued messages (in database) will begin being reprocessed.







