<?php
/**
 This is an example for handling authentication from KSMPPD 

 To successfully authenticate a user, you must provide at least
 An 'X-KSMPPD-Auth: 1' response header, otherwise the bind will 
 be denied.

*/



$headers = getallheaders();

/* Headers get normalized by PHP */
$systemIdHeader = 'X-Ksmppd-System-Id'; /* Actually X-KSMPPD-system-id */
$passwordHeader = 'X-Ksmppd-Password'; /* Actually X-KSMPPD-password */

$users = array(
    'smppuserA:userA' => array(
        'X-KSMPPD-Auth: 1', 'X-KSMPPD-Throughput: 10', 'X-KSMPPD-Max-Binds: 2', 'X-KSMPPD-Connect-Allow-IP: *.*.*.*'
    ),
    'smppuserB:userB' => array(
        'X-KSMPPD-Auth: 1', 'X-KSMPPD-Throughput: 10', 'X-KSMPPD-Max-Binds: 2'
    ),
    'smppuserC:userC' => array(
        'X-KSMPPD-Auth: 1', 'X-KSMPPD-Simulate: 1', 'X-KSMPPD-Simulate-Deliver-Every: 1', 'X-KSMPPD-Simulate-MO-Every: 10', 'X-KSMPPD-Permanent-Failure-Every: 50', 'X-KSMPPD-Temporary-Failure-Every: 50'
    )
);

if(isset($headers[$systemIdHeader]) && isset($headers[$passwordHeader])) {
    $key = $headers[$systemIdHeader].':'.$headers[$passwordHeader];

    if(isset($users[$key])) {
        foreach($users[$key] as $header) {
            Header($header);
        }
    }
}

