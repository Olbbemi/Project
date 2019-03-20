<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Connect.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Control.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Parsing/JSON_Parser.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/SystemLog_Lib.php";

    # Handle
    $info_db_handle;
    $index_db_handle;
    $shard_db_handle;

    # Variable
    $check;
    $errno;
    $recent_AI;

    $parsing_data = array();
    $info_db_select_query;

    $info_db_query_result;    
    $info_db_fetch_query;

    $index_db_query_result;  
    $index_db_fetch_query;
    
    $shard_db_query_result;   
    $shard_db_fetch_query;

    # Data Parsing
    Decode($parsing_data);
 
    # Index DB
    Connect_Slave_Index($index_db_handle);

    $index_db_select_query;
    if(isset($parsing_data['accountno']) === true)
        $index_db_select_query = "SELECT `accountno`, `dbno` FROM `allocate` WHERE `accountno` = \"{$parsing_data['accountno']}\"";
    else
        $index_db_select_query = "SELECT `accountno`, `dbno` FROM `allocate` WHERE `email` = \"{$parsing_data['email']}\"";
    
    $check = DB_ReadQuery($index_db_handle, $index_db_select_query, $index_db_query_result);
    if($check === false)
    {
        SystemLog(-1, "Select_Contents", "{$index_db_select_query}");
        DB_Disconnection($index_db_handle);
        exit;
    }
    
    $index_db_fetch_query = mysqli_fetch_assoc($index_db_query_result);
    if($index_db_fetch_query === null)
    {
        if($parsing_data['accountno'] !== null)
            SystemLog(-1, "Select_Contents", "Not Member: {$parsing_data['accountno']}");
        else
            SystemLog(-1, "Select_Contents", "Not Member: {$parsing_data['email']}");

        DB_Disconnection($index_db_handle);

        $result = array('result' => -10);
        echo Encode($result);
        exit;
    }

    DB_Disconnection($index_db_handle);

    # Info DB
    Connect_Slave_Info($info_db_handle);

    $info_db_select_query = "SELECT `*` FROM `dbconnect` WHERE `dbno` = \"{$index_db_fetch_query['dbno']}\"";
    $check = DB_ReadQuery($info_db_handle, $info_db_select_query, $info_db_query_result);
    if($check === false)
    {
        SystemLog(-1, "Select_Contents", "{$info_db_select_query}");
        DB_Disconnection($info_db_handle);
        exit;
    }

    $info_db_fetch_query = mysqli_fetch_assoc($info_db_query_result);
    DB_Disconnection($info_db_handle);

    # Shard DB
    Connect_Shard($shard_db_handle, $info_db_fetch_query);

    $shard_db_select_query = "SELECT `*` FROM `contents` WHERE `accountno` = \"{$index_db_fetch_query['accountno']}\"";
    $check = DB_ReadQuery($shard_db_handle, $shard_db_select_query, $shard_db_query_result);
    if($check === false)
    {
        SystemLog(-1, "Select_Contents", "{$shard_db_select_query}");
        DB_Disconnection($shard_db_handle);
        exit;
    } 

    $shard_db_fetch_query = mysqli_fetch_assoc($shard_db_query_result);
    if($shard_db_fetch_query === null)
    {
        Connect_Master_Index($index_db_handle);

        $index_db_delete_query[] = "DELETE FROM `allocate` WHERE `accountno` = \"{$index_db_fetch_query['accountno']}\"";         
        $check = DB_WriteQuery($index_db_handle, $index_db_delete_query, $errno);
        if($check === false)
        {
            $error = "Error Code: {$errno}\r\n";
            $error .= "Query: {$index_db_insert_query[0]}";

            SystemLog(-1,"Select_Contents", $error);
            DB_Disconnection($index_db_handle);
            exit;
        }

        if($parsing_data['accountno'] !== null)
            SystemLog(-1, "Select_Contents", "Not Alloc Account Table: {$parsing_data['accountno']}");
        else
            SystemLog(-1, "Select_Contents", "Not Alloc Account Table: {$parsing_data['email']}");

        DB_Disconnection($shard_db_handle);
        DB_Disconnection($index_db_handle);

        $result = array('result' => -11);
        echo Encode($result);
        exit;
    }

    DB_Disconnection($shard_db_handle);

    $fetch_query_size = count($shard_db_fetch_query);
    $fetch_query_key = array_keys($shard_db_fetch_query);
    $result = array();

    $result['result'] = 1;
    for($i = 0; $i < $fetch_query_size; $i++)
        $result[$fetch_query_key[$i]] = (int)$shard_db_fetch_query[$fetch_query_key[$i]];
    
    echo Encode($result);
?>