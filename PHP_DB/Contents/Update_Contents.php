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
    $recent_AI;

    $parsing_data = array();

    $info_db_query_result;    
    $info_db_fetch_query;

    $index_db_query_result;   
    $index_db_fetch_query;

    $shard_db_query_result;

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
        SystemLog(-1, "Update_Contents", "{$index_db_select_query}");
        DB_Disconnection($index_db_handle);
        exit;
    }

    $index_db_fetch_query = mysqli_fetch_assoc($index_db_query_result);
    DB_Disconnection($index_db_handle);

    # Info DB
    Connect_Slave_Info($info_db_handle);

    $info_db_select_query = "SELECT `*` FROM `dbconnect` WHERE `dbno` = \"{$index_db_fetch_query['dbno']}\"";
    $check = DB_ReadQuery($info_db_handle, $info_db_select_query, $info_db_query_result);
    if($check === false)
    {
        SystemLog(-1, "Update_Contents", "{$info_db_select_query}");
        DB_Disconnection($info_db_handle);
        exit;
    }

    $info_db_fetch_query = mysqli_fetch_assoc($info_db_query_result);
    DB_Disconnection($info_db_handle);

    # Shard DB  
    Connect_Shard($shard_db_handle, $info_db_fetch_query);

    # 해당 유저가 Shard DB 에 있는지 확인
    $shard_db_select_query = "SELECT `accountno` FROM `account` WHERE `accountno` = \"{$index_db_fetch_query['accountno']}\"";
    $check = DB_ReadQuery($shard_db_handle, $shard_db_select_query, $shard_db_query_result);
    if($check === false)
    {
        SystemLog(-1, "Update_Contents", "{$shard_db_select_query}");
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
            $error .= "Query: {$index_db_delete_query[0]}";

            SystemLog(-1, "Update_Contents", $error);
            DB_Disconnection($index_db_handle);
            exit;
        }

        SystemLog(-1, "Update_Contents", "Not Alloc Account Table: {$index_db_fetch_query['accountno']}");
        DB_Disconnection($shard_db_handle);
        DB_Disconnection($index_db_handle);

        $result = array('result' => -11);
        echo Encode($result);
        exit;
    }

    $data_array_size = count($parsing_data);
    $store_key_array = array_keys($parsing_data);

    # Update 쿼리문 제작
    $loop_count = 0;
    $query_sentence = "UPDATE `contents` SET ";
   
    for($i = 0; $i < $data_array_size; $i++)
    {
        $loop_count++;

        if($store_key_array[$i] === "accountno" || $store_key_array[$i] === "email")
            continue;

        $query_sentence .= "`{$store_key_array[$i]}` = \"{$parsing_data[$store_key_array[$i]]}\"";
        if($loop_count != $data_array_size)
            $query_sentence .= ", ";
    }   
    $query_sentence .= " WHERE `accountno` = \"{$index_db_fetch_query['accountno']}\"";

    # Shard DB 에 해당 유저 정보 갱신
    $shard_db_update_query[] = $query_sentence;
    $check = DB_WriteQuery($shard_db_handle, $shard_db_update_query, $errno);
    if($check === false)
    {
        $result = null;

        $error = "Error Code: {$errno}\r\n";
        $error .= "Query: {$shard_db_update_query[0]}";

        SystemLog(-1,"Update_Contents", $error);
        DB_Disconnection($shard_db_handle);

        if($errno === 1054) # 존재하지 않는 컬럼
            $result = array("result" => -61);
        else if($errno === 1062) # 중복 데이터
            $result = array("result" => -1);
        else if($errno === 1064) # 쿼리 에러
            $result = array("result" => -60);
        else if($errno === 1146) # 해당 테이블 존재 x
            $result = array("result" => -62);
        
        echo Encode($result);
        exit;
    }

    DB_Disconnection($shard_db_handle);

    # Reply Data
    $result = array('result' => 1);
    echo Encode($result);
?>