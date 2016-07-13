
<?php

define('ROUTE_DIRECTION_OUTBOUND', 1);
define('ROUTE_DIRECTION_INBOUND', 2);

$headers = getallheaders();

/* These are all the possible headers that can be sent, only non-null and defined values (!= -1) will be sent. Strings be url encoded */
$allPossible = array (
  'X-Ksmppd-Meta-Data' => '%3Fsmpp%3F',
  'X-Ksmppd-Resend-Time' => '-1',
  'X-Ksmppd-Resend-Try' => '-1',
  'X-Ksmppd-Priority' => '0',
  'X-Ksmppd-Msg-Left' => '-1',
  'X-Ksmppd-Binfo' => '(null)',
  'X-Ksmppd-Boxc-Id' => '(null)',
  'X-Ksmppd-Charset' => '(null)',
  'X-Ksmppd-Rpi' => '-1',
  'X-Ksmppd-Alt-Dcs' => '0',
  'X-Ksmppd-Pid' => '0',
  'X-Ksmppd-Dlr-Url' => 'smppusera%7C1468405837%7C7a9a1271-7ed4-4c6f-af6f-8652252010ab',
  'X-Ksmppd-Dlr-Mask' => '19',
  'X-Ksmppd-Deferred' => '-1',
  'X-Ksmppd-Validity' => '-1',
  'X-Ksmppd-Compress' => '0',
  'X-Ksmppd-Coding' => '0',
  'X-Ksmppd-Mwi' => '-1',
  'X-Ksmppd-Mclass' => '-1',
  'X-Ksmppd-Sms-Type' => '2',
  'X-Ksmppd-Id' => '7a9a1271-7ed4-4c6f-af6f-8652252010ab',
  'X-Ksmppd-Account' => '(null)',
  'X-Ksmppd-Service' => 'smppusera',
  'X-Ksmppd-Foreign-Id' => '(null)',
  'X-Ksmppd-Smsc-Number' => '(null)',
  'X-Ksmppd-Smsc-Id' => '(null)',
  'X-Ksmppd-Time' => '-1',
  'X-Ksmppd-Msgdata' => 'Hello',
  'X-Ksmppd-Udhdata' => '(null)',
  'X-Ksmppd-Receiver' => '1800123',
  'X-Ksmppd-Sender' => 'userD',
  'X-Ksmppd-Routing-Direction' => '1'
);

$allHeaderKeys = array(
	"X-Ksmppd-Meta-Data",
	"X-Ksmppd-Resend-Time",
	"X-Ksmppd-Resend-Try",
	"X-Ksmppd-Priority",
	"X-Ksmppd-Msg-Left",
	"X-Ksmppd-Binfo",
	"X-Ksmppd-Boxc-Id",
	"X-Ksmppd-Charset",
	"X-Ksmppd-Rpi",
	"X-Ksmppd-Alt-Dcs",
	"X-Ksmppd-Pid",
	"X-Ksmppd-Dlr-Url",
	"X-Ksmppd-Dlr-Mask",
	"X-Ksmppd-Deferred",
	"X-Ksmppd-Validity",
	"X-Ksmppd-Compress",
	"X-Ksmppd-Coding",
	"X-Ksmppd-Mwi",
	"X-Ksmppd-Mclass",
	"X-Ksmppd-Sms-Type",
	"X-Ksmppd-Id",
	"X-Ksmppd-Account",
	"X-Ksmppd-Service",
	"X-Ksmppd-Foreign-Id",
	"X-Ksmppd-Smsc-Number",
	"X-Ksmppd-Smsc-Id",
	"X-Ksmppd-Time",
	"X-Ksmppd-Msgdata",
	"X-Ksmppd-Udhdata",
	"X-Ksmppd-Receiver",
	"X-Ksmppd-Sender",
        "X-Ksmppd-Routing-Direction"
);



Header("X-KSMPPD-Route-Status: 1"); /* Must be non-zero to indicate OK */
Header("X-KSMPPD-Route-Cost: 1.0"); /* Mandatory (even if not used), must be a number */

if($headers['X-Ksmppd-Routing-Direction'] == ROUTE_DIRECTION_OUTBOUND) {
    Header("X-KSMPPD-SMSC-ID: ksmppd3"); /* The SMSC ID you require to route to */
} else if($headers['X-Ksmppd-Routing-Direction'] == ROUTE_DIRECTION_INBOUND) {
    Header("X-KSMPPD-Service: smppusera");
}

