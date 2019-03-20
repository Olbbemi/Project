<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Parsing/JSON_Parser.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Log/Log_Config.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Log/Socket_Http.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";

    function SystemLog($who, $action, $message, $level = 1)
    {
        global $g_config_log_level;
        global $g_config_system_log_URL;

        if($g_config_log_level < $level)
            return;

        if($who < 0)
        {
            if(array_key_exists('HTTP_X_FORWARDED_FOR', $_SERVER))
                $who = $_SERVER['HTTP_X_FORWARDED_FOR'];
            else if(array_key_exists('REMOTE_ADDR',$_SERVER))
                $who = $_SERVER["REMOTE_ADDR"];
            else
                $who = 'local';
        }

        $post_string = array("who" => $who, "action" => $action, "message" => $message);
        Request_Http($g_config_system_log_URL, Encode($post_string), "POST");
    }

 ?>