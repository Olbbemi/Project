<?php

    function FileLog($file, $line, $log_str)
    {
        $dir_path = "Log";
        $file_name = date("Ymd")."_PHPLog.txt";
        $prefix = "-----[Line:{$line}] ".date("Y/m/d_H:i:s")."-----\r\n";
        $suffix = "--------------------------------------";
        if(is_dir($_SERVER['DOCUMENT_ROOT']."\\".$dir_path) == false)
            mkdir($_SERVER['DOCUMENT_ROOT']."\\".$dir_path);
        
        $fp = fopen($_SERVER['DOCUMENT_ROOT']."\\".$dir_path."\\".$file_name , "a");
        fwrite($fp, $prefix);
        fwrite($fp, "Action: "."".$file."\r\n\r\n");
        fwrite($fp, $log_str."\r\n");
        fwrite($fp, $suffix."\r\n\r\n");
        fclose($fp);
    }

?>