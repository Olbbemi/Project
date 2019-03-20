<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/SystemLog_Lib.php";

    function Encode($data)
    {
        return json_encode($data);
    }

    function Decode(&$data_array)
    {
        $store_key_array = null;
        $key_array_size = null;

        $json_data = json_decode(file_get_contents('php://input'), true);    
        if($json_data === FALSE || $json_data === NULL || $json_data === '')
        {
            $message = json_last_error_msg();
            SystemLog(-1, "json_decode failed", "result: -100 "."{$message}");
            $result = array("result" => -100);
            echo Encode($result);
            exit;
        }

        $store_key_array = array_keys($json_data);
        $key_array_size = count($store_key_array);

        for($i = 0; $i < $key_array_size; $i++)
            $data_array[$store_key_array[$i]] =  $json_data[$store_key_array[$i]];
    }
?>