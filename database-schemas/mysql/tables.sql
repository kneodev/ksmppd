
CREATE TABLE `smpp_queued_pdu` (
  `global_id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `system_id` varchar(64) NOT NULL,
  `time` bigint(20) DEFAULT NULL,
  `pdu` blob,
  PRIMARY KEY (`global_id`),
  KEY `system_id` (`system_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;


CREATE TABLE `smpp_route` (
  `route_id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `direction` int(11) NOT NULL,
  `regex` text,
  `cost` double DEFAULT NULL,
  `system_id` varchar(64) DEFAULT NULL,
  `smsc_id` varchar(64) DEFAULT NULL,
  PRIMARY KEY (`route_id`),
  KEY `direction` (`direction`),
  KEY `system_id` (`system_id`),
  KEY `smsc_id` (`smsc_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

CREATE TABLE `smpp_store` (
  `global_id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `sender` text,
  `receiver` text,
  `udhdata` text,
  `msgdata` text,
  `time` bigint(20) NOT NULL,
  `smsc_id` text,
  `smsc_number` text,
  `foreign_id` text,
  `service` text,
  `account` text,
  `id` varchar(128) DEFAULT NULL,
  `sms_type` bigint(20) NOT NULL,
  `mclass` bigint(20) NOT NULL,
  `mwi` bigint(20) NOT NULL,
  `coding` bigint(20) NOT NULL,
  `compress` bigint(20) NOT NULL,
  `validity` bigint(20) NOT NULL,
  `deferred` bigint(20) NOT NULL,
  `dlr_mask` bigint(20) NOT NULL,
  `dlr_url` text,
  `pid` bigint(20) NOT NULL,
  `alt_dcs` bigint(20) NOT NULL,
  `rpi` bigint(20) NOT NULL,
  `charset` text,
  `boxc_id` text,
  `binfo` text,
  `msg_left` bigint(20) NOT NULL,
  `priority` bigint(20) NOT NULL,
  `resend_try` bigint(20) NOT NULL,
  `resend_time` bigint(20) NOT NULL,
  `meta_data` text,
  PRIMARY KEY (`global_id`),
  KEY `service` (`service`(16)),
  KEY `sms_type` (`sms_type`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

CREATE TABLE `smpp_user` (
  `system_id` varchar(15) NOT NULL,
  `password` varchar(64) NOT NULL,
  `throughput` double(10,5) NOT NULL DEFAULT '0.00000',
  `default_smsc` varchar(64) DEFAULT NULL,
  `default_cost` double NOT NULL,
  `enable_prepaid_billing` int(10) unsigned NOT NULL DEFAULT '0',
  `credit` double NOT NULL DEFAULT '0',
  `callback_url` varchar(255) DEFAULT NULL,
  `simulate` tinyint(1) NOT NULL DEFAULT '0',
  `simulate_deliver_every` int(10) unsigned NOT NULL,
  `simulate_permanent_failure_every` int(10) unsigned NOT NULL,
  `simulate_temporary_failure_every` int(10) unsigned NOT NULL,
  `simulate_mo_every` int(10) unsigned NOT NULL,
  `max_binds` int(10) unsigned NOT NULL DEFAULT '0',
  `connect_allow_ip` text,
  PRIMARY KEY (`system_id`)
) ENGINE=InnoDB;
