<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/FileLog_Lib.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";

    # 데이터베이스에 연결하는 함수
    function DB_Connection(&$DB_handle, $DB_ip, $DB_id, $DB_pass, $DB_name, $DB_port)
    {
        $DB_handle = mysqli_connect('p:'.$DB_ip, $DB_id, $DB_pass, $DB_name, $DB_port);
        if($DB_handle === false)
        {
            $log = null;
            $log .= mysqli_connect_error()."\r\n";
            $log .= "DB_IP: {$DB_ip}\r\n";        
            $log .= "DB_ID: {$DB_id}\r\n";
            $log .= "DB_Pass: {$DB_pass}\r\n";    
            $log .= "DB_Name: {$DB_name}\r\n";
            $log .= "DB_Port: {$DB_port}\r\n";
        
            FileLog(__FILE__, __LINE__, $log);
            return false;
        }

        return true;
    }

    # 데이터베이스와 연결을 종료
    function DB_Disconnection(&$DB_handle)
    {
        mysqli_close($DB_handle);
    }

    # 데이터베이스에 Select 쿼리문을 실행하는 함수
    function DB_ReadQuery(&$DB_handle, $DB_query, &$query_result)
    {
        $query_result = mysqli_query($DB_handle, $DB_query);
        if($query_result === false)
            return false;
          
        return true;
    }

    # 데이터베이스에 데이터의 변화와 관련된 쿼리문을 실행하는 함수
    function DB_WriteQuery(&$DB_handle, $DB_querys, &$errno = 0, &$recent_AI = 0)
    {
        mysqli_begin_transaction($DB_handle);
        foreach($DB_querys as $query)
        {
            if(is_string($query) === true)
            {
                if(mysqli_query($DB_handle, $query) === false)
                {
                    $errno = mysqli_errno($DB_handle); 
                    mysqli_rollback($DB_handle);
                    return false;
                }
            }
            else
            {
                $errno = 10;
                mysqli_rollback($DB_handle);
                return false;
            }
        }

        # 최근 AutoIncrement 값을 얻어옴
        $recent_AI = mysqli_insert_id($DB_handle);
        mysqli_commit($DB_handle);

        return true;
    }

 ?>