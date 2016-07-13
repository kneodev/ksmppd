

-- This sample creates 3 users

-- smppuserA - is a prepaid billing user with a balance of 10
-- smppuserB - is a post-paid billing user (no balance)
-- smppuserC - is a simulated user (only simulations)

INSERT INTO `smpp_user` 
    (`system_id`, `password`, `throughput`, `default_smsc`, `default_cost`, `enable_prepaid_billing`, `credit`, 
    `callback_url`, `simulate`, `simulate_deliver_every`, `simulate_permanent_failure_every`, `simulate_temporary_failure_every`, 
    `simulate_mo_every`, `max_binds`) 
VALUES 
    ('smppuserA',PASSWORD('userA'),11.00000,'ksmppd2',0,1,0,NULL,0,0,0,0,0,0),
    ('smppuserB',PASSWORD('userB'),0.00000,NULL,0,0,0,NULL,1,1,0,0,0,2),
    ('smppuserC',PASSWORD('userC'),0.00000,NULL,0,0,0,NULL,1,1,0,0,2,0);
