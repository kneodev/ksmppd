
-- This is a sample SQL set to create some example routes

-- Direction = 1 = outbound messages
-- Direction = 2 = inbound messages

-- In this example, outbound messages for smppuserA are routed via ksmppd2 if they begin with 1800
-- In this example, outbound messages for smppuserA are routed via ksmppd3 if they begin with 7800
-- In this example, inbound messages with the destination 6800 are routed to smppuserA

INSERT INTO 
    `smpp_route` 
    (`route_id`, `direction`, `regex`, `cost`, `system_id`, `smsc_id`) 
VALUES 
    (1,1,'^(1800)',1,'smppusera','ksmppd2'),
    (2,1,'^(7800)',1,'smppusera','ksmppd3'),
    (3,2,'^(6800)',NULL,'smppusera','ksmppd3');
