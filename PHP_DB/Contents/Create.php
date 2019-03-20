<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Connect.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Control.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Parsing/JSON_Parser.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/SystemLog_Lib.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";

    # Handle
    $master_info_db_handle;
    $slave_info_db_handle;
    $index_db_handle;
    $shard_db_handle;

    # Variable
    $check;
    $errno;
    $recent_AI;

    $parsing_data = array();

    $info_db_query_result;   
    $info_db_fetch_query;

    # Data Parsing
    Decode($parsing_data);

    # Info DB
    Connect_Slave_Info($slave_info_db_handle);

    $info_db_select_query = "SELECT `dbconnect`.`dbno`, `ip`, `port`, `id`, `pass`, `dbname`, `available` FROM `dbconnect` JOIN `available` ON `dbconnect`.dbno = `available`.`dbno` ORDER BY `available`.available DESC limit 1";
    $check = DB_ReadQuery($slave_info_db_handle, $info_db_select_query, $info_db_query_result);
    if($check === false)
    {
        SystemLog(-1, "Create", "{$info_db_select_query}");
        DB_Disconnection($slave_info_db_handle);
        exit;
    }

    $info_db_fetch_query = mysqli_fetch_assoc($info_db_query_result);

    # shard_DB 마다 할당된 available 값이 0보다 작거나 같으면 더 이상 유저를 받을 수 없는 상황을 의미
    if($info_db_fetch_query['available'] <= 0)
    {
        $result = array("result" => -2);
        echo Encode($result);
        
        DB_Disconnection($slave_info_db_handle);
        exit;
    }

    DB_Disconnection($slave_info_db_handle);

    Connect_Master_Info($master_info_db_handle);

    # 얻어온 shard_DB의 available 값 1 차감
    $info_db_update_query[] = "UPDATE `available` SET `available` = `available` - 1 WHERE `dbno` = \"{$info_db_fetch_query['dbno']}\"";
    $check = DB_WriteQuery($master_info_db_handle, $info_db_update_query, $errno);
    if($check === false)
    {
        $error = "Error Code: {$errno}\r\n";
        $error .= "Query: {$info_db_update_query[0]}";

        SystemLog(-1,"Create", $error);
        DB_Disconnection($master_info_db_handle);
        exit;
    }

    DB_Disconnection($master_info_db_handle);

    # Index DB
    Connect_Master_Index($index_db_handle);

    $index_db_insert_query[] = "INSERT INTO `allocate` VALUES(NULL, \"{$parsing_data['email']}\", \"{$info_db_fetch_query['dbno']}\")";
    $check = DB_WriteQuery($index_db_handle, $index_db_insert_query, $errno, $recent_AI);
    if($check === false)
    {
        $result = null;
        if($errno === 1054) # 존재하지 않는 컬럼
            $result = array("result" => -61);
        else if($errno === 1062) # 중복 데이터
            $result = array("result" => -1);
        else if($errno === 1064) # 쿼리 에러
            $result = array("result" => -60);
        else if($errno === 1146) # 해당 테이블 존재 x
            $result = array("result" => -62);
        
        $error = "Error Code: {$errno}\r\n";
        $error .= "Query: {$index_db_insert_query[0]}";

        SystemLog(-1, "Create", $error);
        DB_Disconnection($index_db_handle);
        
        echo Encode($result);
        exit;
    }

    DB_Disconnection($index_db_handle);

    # Shard DB
    Connect_Shard($shard_db_handle, $info_db_fetch_query);

    $shard_db_insert_query[] = "INSERT INTO `account` VALUES($recent_AI, \"{$parsing_data['email']}\", NULL, NULL, NULL)";
    $shard_db_insert_query[] = "INSERT INTO `contents` VALUES($recent_AI, 0, 0, 0, 0, 0)";

    $check = DB_WriteQuery($shard_db_handle, $shard_db_insert_query, $errno);
    if($check === false)
    {
        $error = "Error Code: {$errno}\r\n";
        $error .= "Query: {$shard_db_insert_query[0]} / {$shard_db_insert_query[1]}";

        SystemLog(-1,"Create", $error);
        DB_Disconnection($shard_db_handle);

        $result = array("result" => -3);
        echo Encode($result);
        exit;
    }

    DB_Disconnection($shard_db_handle);

    # Reply Data
    $result = array('result'     => 1, 
                    'accountno'  => $recent_AI,
                    'email'      => $parsing_data['email'],
                    'dbno'       => $info_db_fetch_query['dbno']);

    echo Encode($result);
?>