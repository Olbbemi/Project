<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Contents/Config.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Connect.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Control.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Parsing/JSON_Parser.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/SystemLog_Lib.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Log/Socket_Http.php";

    # Handle
    $matchmaking_db_handle;

    # Variable
    $check;
    $errno;
    $recent_AI;
    $start_index;
    $end_index;

    $parsing_data = array();

    $matchmaking_db_query_result;
    $matchmaking_db_fetch_query;

    # Data Parsing
    Decode($parsing_data);

    $post_string;
    if($parsing_data['accountno'] !== null)
        $post_string = array('accountno' => $parsing_data['accountno']);  
    else
        $post_string = array('email' => $parsing_data['email']); 

    $response = Request_Http(URL, Encode($post_string), 'POST', true);

    # Server Response Code Check
    $start_index = strpos($response, ' ', 0);
    $end_index = strpos($response, ' ', $start_index + 1);
    
    $code = substr($response, $start_index + 1, $end_index - ($start_index + 1));
    if((int)($code) !== 200)
    {
        SystemLog(-1, "Get_MatchMaking", "Server Response Code: ".$code);
            
        $result = array('result' => -110);
        echo Encode($result);
        exit;
    }

    $start_index = strpos($response, '{', 0);
    $end_index = strpos($response, '}', $start_index + 1);
    
    $body = json_decode(substr($response, $start_index, $end_index - $start_index + 1), true);
    if($body['result'] !== 1)
    {
        SystemLog(-1, "Get_MatchMaking", "SessionKey Miss");

        $result;
        switch((int)($body['result']))
        {
            case -10:    $result = array('result' => -2);    break;
            case -11:    $result = array('result' => -1);    break;
            default:     $result = array('result' => -111);  break;
        }

        echo Encode($result);
        exit;
    }

    if(strcmp($body['sessionkey'], $parsing_data['sessionkey']) != 0)
    {
        SystemLog(-1, "Get_MatchMaking", "SessionKey Miss");
            
        $result = array('result' => -3);
        echo Encode($result);
        exit;
    }
    
    # MatchMaking DB
    Connect_Slave_MatchMaking($matchmaking_db_handle);

    $matchmaking_db_select_query = "SELECT `serverno`, `ip`, `port` FROM `server` WHERE TIME_TO_SEC(timediff(NOW(), `heartbeat`)) < {$g_matching_server_time_out} ORDER BY `connectuser` ASC limit 1";
    $check = DB_ReadQuery($matchmaking_db_handle, $matchmaking_db_select_query, $matchmaking_db_query_result);
    if($check === false)
    {
        SystemLog(-1, 'Get_MatchMaking', "{$matchmaking_db_select_query}");
        DB_Disconnection($matchmaking_db_handle);
        exit;
    }

    $matchmaking_db_fetch_query = mysqli_fetch_assoc($matchmaking_db_query_result);
    if($matchmaking_db_fetch_query === null)
    {
        SystemLog(-1, 'Get_MatchMaking', 'Not Exist MatchMaking Server');
        DB_Disconnection($matchmaking_db_handle);

        $result = array('result' => -4);
        echo Encode($result);
        exit;
    }

    DB_Disconnection($matchmaking_db_handle);

    # Reply Data
    $fetch_query_size = count($matchmaking_db_fetch_query);
    $fetch_query_key = array_keys($matchmaking_db_fetch_query);
    $result = array();

    $result['result'] = 1;
    for($i = 0; $i < $fetch_query_size; $i++)
    {
        if($fetch_query_key[$i] == 'ip')
            $result[$fetch_query_key[$i]] = $matchmaking_db_fetch_query[$fetch_query_key[$i]];
        else
            $result[$fetch_query_key[$i]] = (int)$matchmaking_db_fetch_query[$fetch_query_key[$i]];
    }
        
    echo Encode($result);
?>