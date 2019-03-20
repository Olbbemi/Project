<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/ASSOC_LIB/FileLog_Lib.php";

    function Request_Http($url, $contents, $type, $need_recv = false)
    {
        $parsed_url = parse_url($url);

        if($parsed_url['scheme'] == 'http')
            $fp = fsockopen($parsed_url['host'], isset($parsed_url['port'])?$parsed_url['port']:80, $errno, $errstr, 2);
        else if($parsed_url['scheme'] == 'https')
            $fp = fsockopen("ssl://".$parsed_url['host'], isset($parsed_url['port'])?$parsed_url['port']:443, $errno, $errstr, 2);

        if($fp === false)
        {
            $error_log = "Sock Open Error Code = {$errno} [$errstr]";
            FileLog(__FILE__, __LINE__, $error_log);
            exit;
        }
            
        $contents_length = strlen($contents);

        $http_header = "$type ".$parsed_url['path']." HTTP/1.1\r\n";
        $http_header .= "Host: ".$parsed_url['host']."\r\n";
        $http_header .= "Content-Type:application/x-www-form-urlencoded\r\n";
        $http_header .= "Content-Length:".$contents_length."\r\n";
        $http_header .= "Connection:Close\r\n\r\n";

        if($type == 'POST')
            $http_header .= $contents;

        fwrite($fp, $http_header);

        if($need_recv == true)
        {
            $result = fread($fp, 1024);
            fclose($fp); 
            
            return $result;
        }

        fclose($fp);
    }
?>