<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/SystemLog_Lib.php";

    # 옵션 설정
    ignore_user_abort(true);
    error_reporting(E_ALL);

    # php 가 종료될 때 항상 호출되는 함수 [ c++ 에는 AtExit() 존재 ]
    function sys_Shutdown() {}

    # syntax error 을 제외한 모든 에러 및 예외 발생시 callback
    set_error_handler("ERROR_Handler");
    set_exception_handler("EXCEPTION_Handler"); 

    function ERROR_Handler($errno, $errstr, $errfile, $errline)
    {
        $ErrorMsg = "Errno: $errno FILE: $errfile / LINE: $errline / MSG: $errstr";
        SystemLOG(-1, "ERROR_Handler", $ErrorMsg);
    }

    function EXCEPTION_Handler($exception)
    {
        SystemLOG(-1, $exception->getMessage(), "Exception_Error");
    }

 ?>