<?php
	$GLOBALS["_SERVER"];
	include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Config.php";
	include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_DB/DB_Control.php";
	include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/FileLog_Lib.php";
	include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";

	$parsing_data = array();
	Decode($parsing_data);

	$check = DB_Connection($g_profile_db_handle, $g_log_DB_ip, $g_log_DB_id, $g_log_DB_pass, $g_log_DB_name, $g_log_DB_port);
	if($check === false)
	{
		$errno = mysqli_errno($g_profile_db_handle);

		$log .= "Errno: {$errno}";
		$log .= "DB_IP: {$g_log_DB_ip}\r\n";        $log .= "DB_ID: {$g_log_DB_id}\r\n";
		$log .= "DB_Pass: {$g_log_DB_pass}\r\n";    $log .= "DB_Name: {$g_log_DB_name}\r\n";
		$log .= "DB_Port: {$g_log_DB_port}\r\n";
		
		File_Log(__FILE__, __LINE__, $log);
		exit;
	}
	
	mysqli_set_charset($g_profile_db_handle, "utf8");

	# 현재날짜 테이블에 로그 삽입
	$errno = 0;
	$table_name = "ProfilingLog_".date("Ym");
	$insert_query[] = "INSERT INTO `{$table_name}` (`no`, `date`, `IP`, `action`, `page_time`, `mysql_conn_time`, `mysql_query_time`, `ExtAPI_time`, `log_time`, `query`) 
					      VALUES (NULL, NOW(), '{$parsing_data[0]}', '{$parsing_data[1]}', '{$parsing_data[2]}', '{$parsing_data[3]}', '{$parsing_data[4]}', '{$parsing_data[5]}', '{$parsing_data[6]}', '{$parsing_data[7]}')";

	$result = DB_WriteQuery($g_profile_db_handle, $insert_query, $errno);
	if ($result === false)
	{
		# 해당 테이블이 존재하지 않는 경우 생성 후 입력
		if($errno == 1146)
		{
			$errno = 0;

			$create_and_insert_query[] = "CREATE TABLE `{$table_name}` LIKE ProfilingLog_template";
			$create_and_insert_query[] = "INSERT INTO `{$table_name}` (`no`, `date`, `IP`, `action`, `page_time`, `mysql_conn_time`, `mysql_query_time`, `ExtAPI_time`, `log_time`, `query`) 
											 VALUES (NULL, NOW(), '{$parsing_data[0]}', '{$parsing_data[1]}', '{$parsing_data[2]}', '{$parsing_data[3]}', '{$parsing_data[4]}', '{$parsing_data[5]}', '{$parsing_data[6]}', '{$parsing_data[7]}')";

			DB_WriteQuery($g_profile_db_handle, $create_and_insert_query, $errno);
			if($errno != 0)
			{
				$log = "Query Error Code: {$errno}\r\n";
				$log .= "Query: {$create_and_insert_query[0]}\r\n";
				$log .= "Query: {$create_and_insert_query[1]}";
				File_Log(__FILE__, __LINE__, $log);
			}
		}
		else
		{
			$log = "Create System_Log_Table Error Code: {$errno}\r\n";
			$log .= "Query: {$insert_query[0]}";
			File_Log(__FILE__, __LINE__, $log);
		}
	}

	DB_Disconnection($g_profile_db_handle);
?>