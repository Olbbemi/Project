<?php

    # Log_DB 정보
    $g_log_DB_ip = "172.16.2.2";
    $g_log_DB_id   = "log_user";
    $g_log_DB_pass = "1q2w3e4r";
    $g_log_DB_name = "log_db";
    $g_log_DB_port = 10022;

    # Index_DB Master 정보
    $g_master_index_DB_ip = "172.16.2.1";
    $g_master_index_DB_id   = "root";
    $g_master_index_DB_pass = "auddhkdwls1!@";
    $g_master_index_DB_name = "shdb_index";
    $g_master_index_DB_port = 10021;

    # Index_DB Slave 정보
    $g_index_slave_DB_info = array( array("DB_ip" => "172.16.1.1",
                                          "DB_id" => "replicate_user",
                                          "DB_pass" => "1q2w3e4r",
                                          "DB_name" => "shdb_index",
                                          "DB_port" => 10011
                                         ),

                                    array("DB_ip" => "172.16.1.2",
                                          "DB_id" => "replicate_user",
                                          "DB_pass" => "1q2w3e4r",
                                          "DB_name" => "shdb_index",
                                          "DB_port" => 10012
                                         )
                                  );

    # Info_DB Master 정보
    $g_master_info_DB_ip = "172.16.2.1";
    $g_master_info_DB_id   = "root";
    $g_master_info_DB_pass	= "auddhkdwls1!@";
    $g_master_info_DB_name = "shdb_info";
    $g_master_info_DB_port = 10021;

    # Info_DB Slave 정보
    $g_info_slave_DB_info = array( array("DB_ip" => "172.16.1.1",
                                         "DB_id" => "replicate_user",
                                         "DB_pass" => "1q2w3e4r",
                                         "DB_name" => "shdb_info",
                                         "DB_port" => 10011
                                        ),

                                   array("DB_ip" => "172.16.1.2",
                                         "DB_id" => "replicate_user",
                                         "DB_pass" => "1q2w3e4r",
                                         "DB_name" => "shdb_info",
                                         "DB_port" => 10012
                                        )
                                  );

    # MatchMaking_DB Master 정보
    $g_master_matchmaking_DB_ip = "172.16.2.1";
    $g_master_matchmaking_DB_id   = "root";
    $g_master_matchmaking_DB_pass	= "auddhkdwls1!@";
    $g_master_matchmaking_DB_name = "matchmaking_status";
    $g_master_matchmaking_DB_port = 10021;

    $g_matchmaking_slave_DB_info = array( array("DB_ip" => "172.16.1.1",
                                                "DB_id" => "replicate_user",
                                                "DB_pass" => "1q2w3e4r",
                                                "DB_name" => "matchmaking_status",
                                                "DB_port" => 10011
                                               ),

                                          array("DB_ip" => "172.16.1.2",
                                                "DB_id" => "replicate_user",
                                                "DB_pass" => "1q2w3e4r",
                                                "DB_name" => "matchmaking_status",
                                                "DB_port" => 10012
                                               )
                                        );
?>