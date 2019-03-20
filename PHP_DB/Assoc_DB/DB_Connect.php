<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Config.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Control.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Parsing/JSON_Parser.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/FileLog_Lib.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";

    # Master Index DB
    function Connect_Master_Index(&$db_handle)
    {
        global $g_master_index_DB_ip;    
        global $g_master_index_DB_id;
        global $g_master_index_DB_pass;  
        global $g_master_index_DB_name;
        global $g_master_index_DB_port;

        $check = DB_Connection($db_handle, 
                               $g_master_index_DB_ip,
                               $g_master_index_DB_id,
                               $g_master_index_DB_pass,
                               $g_master_index_DB_name,
                               $g_master_index_DB_port
                              );
        if($check == false)
        {
            $log = null;
            $log .= mysqli_connect_errno()."\r\n";
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$g_master_index_DB_ip}\r\n";        
            $log .= "DB_ID: {$g_master_index_DB_id}\r\n";
            $log .= "DB_Pass: {$g_master_index_DB_pass}\r\n";    
            $log .= "DB_Name: {$g_master_index_DB_name}\r\n";
            $log .= "DB_Port: {$g_master_index_DB_port}\r\n";
            
            FileLog(__FILE__, __LINE__, $log);
    
            $result = array("result" => -50);
            echo Encode($result);
            exit;
        }
    }

    # Slave Index DB
    function Connect_Slave_Index(&$db_handle)
    {
        global $g_index_slave_DB_info;    
        $check;

        $random = rand(0, 1);
        $check = DB_Connection($db_handle, 
                               $g_index_slave_DB_info[$random]['DB_ip'],
                               $g_index_slave_DB_info[$random]['DB_id'], 
                               $g_index_slave_DB_info[$random]['DB_pass'],
                               $g_index_slave_DB_info[$random]['DB_name'],
                               $g_index_slave_DB_info[$random]['DB_port']
                              );
        if($check == false)
        {
            $log = null;
            $log .= mysqli_connect_errno()."\r\n";
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$g_index_slave_DB_info[$random]['DB_ip']}\r\n";        
            $log .= "DB_ID: {$g_index_slave_DB_info[$random]['DB_id']}\r\n";
            $log .= "DB_Pass: {$g_index_slave_DB_info[$random]['DB_pass']}\r\n";    
            $log .= "DB_Name: {$g_index_slave_DB_info[$random]['DB_name']}\r\n";
            $log .= "DB_Port: {$g_index_slave_DB_info[$random]['DB_port']}\r\n";
            
            FileLog(__FILE__, __LINE__, $log);

            $result = array("result" => -51);
            echo Encode($result);
            exit;
        }
    }

    # Info DB
    function Connect_Master_Info(&$db_handle)
    {
        global $g_master_info_DB_ip;    
        global $g_master_info_DB_id;
        global $g_master_info_DB_pass;  
        global $g_master_info_DB_name;
        global $g_master_info_DB_port;

        $check = DB_Connection($db_handle, 
                               $g_master_info_DB_ip,
                               $g_master_info_DB_id,
                               $g_master_info_DB_pass, 
                               $g_master_info_DB_name, 
                               $g_master_info_DB_port
                              );
        if($check == false)
        {
            $log = null;
            $log .= mysqli_connect_errno()."\r\n";
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$g_master_info_DB_ip}\r\n";        
            $log .= "DB_ID: {$g_master_info_DB_id}\r\n";
            $log .= "DB_Pass: {$g_master_info_DB_pass}\r\n";    
            $log .= "DB_Name: {$g_master_info_DB_name}\r\n";
            $log .= "DB_Port: {$g_master_info_DB_port}\r\n";
            
            FileLog(__FILE__, __LINE__, $log);
            exit;
        }
    }

    function Connect_Slave_Info(&$db_handle)
    {
        global $g_info_slave_DB_info;    
        $check;

        $random = rand(0, 1);
        $check = DB_Connection($db_handle, 
                               $g_info_slave_DB_info[$random]['DB_ip'],
                               $g_info_slave_DB_info[$random]['DB_id'], 
                               $g_info_slave_DB_info[$random]['DB_pass'],
                               $g_info_slave_DB_info[$random]['DB_name'],
                               $g_info_slave_DB_info[$random]['DB_port']
                              );
        if($check == false)
        {
            $log = null;
            $log .= mysqli_connect_errno()."\r\n";
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$g_info_slave_DB_info[$random]['DB_ip']}\r\n";        
            $log .= "DB_ID: {$g_info_slave_DB_info[$random]['DB_id']}\r\n";
            $log .= "DB_Pass: {$g_info_slave_DB_info[$random]['DB_pass']}\r\n";    
            $log .= "DB_Name: {$g_info_slave_DB_info[$random]['DB_name']}\r\n";
            $log .= "DB_Port: {$g_info_slave_DB_info[$random]['DB_port']}\r\n";
            
            FileLog(__FILE__, __LINE__, $log);

            $result = array("result" => -51);
            echo Encode($result);
            exit;
        }
    }

    # Shard DB
    function Connect_Shard(&$db_handle, $fetch_query)
    {
        $check = DB_Connection($db_handle, $fetch_query["ip"], $fetch_query["id"], $fetch_query["pass"], $fetch_query["dbname"], $fetch_query["port"]);
        if($check == false)
        {
            $log = null;
            $log .= mysqli_connect_errno()."\r\n";
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$fetch_query["ip"]}\r\n";        
            $log .= "DB_ID: {$fetch_query["id"]}\r\n";
            $log .= "DB_Pass: {$fetch_query["pass"]}\r\n";   
            $log .= "DB_Name: {$fetch_query["dbname"]}\r\n";
            $log .= "DB_Port: {$fetch_query["port"]}\r\n";
            
            FileLog(__FILE__, __LINE__, $log);

            $result = array("result" => -52);
            echo Encode($result);
            exit;
        }
    }

    # Master MatchMaking DB
    function Connect_Master_MatchMaking(&$db_handle)
    {
        global $g_master_matchmaking_DB_ip;    
        global $g_master_matchmaking_DB_id;
        global $g_master_matchmaking_DB_pass;  
        global $g_master_matchmaking_DB_name;
        global $g_master_matchmaking_DB_port;

        $check = DB_Connection($db_handle, 
                               $g_master_matchmaking_DB_ip, 
                               $g_master_matchmaking_DB_id, 
                               $g_master_matchmaking_DB_pass, 
                               $g_master_matchmaking_DB_name, 
                               $g_master_matchmaking_DB_port
                              );
        if($check == false)
        {
            $log = null;
            $log .= mysqli_connect_errno()."\r\n";
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$g_master_matchmaking_DB_ip}\r\n";        
            $log .= "DB_ID: {$g_master_matchmaking_DB_id}\r\n";
            $log .= "DB_Pass: {$g_master_matchmaking_DB_pass}\r\n";    
            $log .= "DB_Name: {$g_master_matchmaking_DB_name}\r\n";
            $log .= "DB_Port: {$g_master_matchmaking_DB_port}\r\n";
            
            FileLog(__FILE__, __LINE__, $log);
            exit;
        }
    }

    # Slave MatchMaking DB
    function Connect_Slave_MatchMaking(&$db_handle)
    {
        global $g_matchmaking_slave_DB_info;    
        $check;

        $random = rand(0, 1);
        $check = DB_Connection($db_handle, 
                               $g_matchmaking_slave_DB_info[$random]['DB_ip'],
                               $g_matchmaking_slave_DB_info[$random]['DB_id'], 
                               $g_matchmaking_slave_DB_info[$random]['DB_pass'],
                               $g_matchmaking_slave_DB_info[$random]['DB_name'],
                               $g_matchmaking_slave_DB_info[$random]['DB_port']
                              );
        if($check == false)
        {
            $log = null;
            $log .= mysqli_connect_errno()."\r\n";
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$g_matchmaking_slave_DB_info[$random]['DB_ip']}\r\n";        
            $log .= "DB_ID: {$g_matchmaking_slave_DB_info[$random]['DB_id']}\r\n";
            $log .= "DB_Pass: {$g_matchmaking_slave_DB_info[$random]['DB_pass']}\r\n";    
            $log .= "DB_Name: {$g_matchmaking_slave_DB_info[$random]['DB_name']}\r\n";
            $log .= "DB_Port: {$g_matchmaking_slave_DB_info[$random]['DB_port']}\r\n";
            
            FileLog(__FILE__, __LINE__, $log);

            $result = array("result" => -51);
            echo Encode($result);
            exit;
        }
    }
?>