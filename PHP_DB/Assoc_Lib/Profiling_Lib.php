<?php
    $GLOBALS["_SERVER"];
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Log/Log_Config.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Log/Socket_Http.php";
    include_once $_SERVER['DOCUMENT_ROOT']."/Assoc_Lib/Error_Handler.php";

    define("PF_TYPE_START", 1);
    define("PF_PAGE", 1);          //  전체 페이지 처리
    define("PF_MYSQL_CONNECT", 2); // MySQL 연결 처리
    define("PF_MYSQL_QUERY", 3);   // MySQL 쿼리 처리
    define("PF_EXTAPI", 4);        // 외부 API 처리
    define ("PF_LOG", 5);          // 로그 처리
    define("PF_TYPE_END", 5);

    class Profile
    {
        private $m_log_url = '';
        private $m_log_flag = false;
        private $m_action = '';
        private $m_profile = array();
        private $m_query = '';
        private $m_comment = '';

        function __construct()
        {
            $this->m_profile[PF_PAGE]['start']  = 0;           $this->m_profile[PF_PAGE]['sum']  = 0;
            $this->m_profile[PF_MYSQL_CONNECT]['start']  = 0;  $this->m_profile[PF_MYSQL_CONNECT]['sum']  = 0;
            $this->m_profile[PF_MYSQL_QUERY]['start']  = 0;    $this->m_profile[PF_MYSQL_QUERY]['sum']  = 0;
            $this->m_profile[PF_EXTAPI]['start']  = 0;         $this->m_profile[PF_EXTAPI]['sum']  = 0;
            $this->m_profile[PF_LOG]['start']  = 0;            $this->m_profile[PF_LOG]['sum']  = 0;
        }

        static function ProfileLogInstance($actionPath)
        {
            global $g_config_profile_log_URL;

            static $s_profile_instance;

            if(isset($s_profile_instance) === false)
                $s_profile_instance = new Profile();

            if($actionPath != '')
                $s_profile_instance->m_action = $actionPath;

            $s_profile_instance->m_log_url = $g_config_profile_log_URL;

            if(rand() % 100 < $g_config_profile_log_rate)
                $s_profile_instance->m_log_flag = true;

            return $s_profile_instance;
        }

        function BeginProfile($type)
        {
            if($this->m_log_flag == false)
                return;

            if($type < PF_TYPE_START || PF_TYPE_END < $type)
                return;

            $this->m_profile[$type]['start'] = microtime(true);
        }

        function EndProfile($type, $query = '')
        {
            if($this->m_log_flag == false)
                return;

            if($type < PF_TYPE_START || PF_TYPE_END < $type)
                return;

            $endTime = microtime(true);
            $this->m_profile[$type]['sum'] += ($endTime - $this->m_profile[$type]['start']);

            if($type == PF_MYSQL_QUERY)
                $this->m_query .= $query."\n";
        }

        function SaveLog()
        {
            $ip = null;

            if($this->m_log_flag == false)
                return;
                
            if(array_key_exists('HTTP_X_FORWARDED_FOR', $_SERVER))
                $ip = $_SERVER['HTTP_X_FORWARDED_FOR'];
            else if(array_key_exists('REMOTE_ADDR',$_SERVER))
                $ip = $_SERVER["REMOTE_ADDR"];
            else
                $ip = 'local';

            $post_string = array(
            "IP"                  => $ip,
            "action"              => $this->m_action,
            "page_time"           => $this->m_profile[PF_PAGE]['sum'],
            "mysql_conn_time"     => $this->m_profile[PF_MYSQL_CONNECT]['sum'],
            "mysql_query_time"    => $this->m_profile[PF_MYSQL_QUERY]['sum'],
            "ExtAPI_time"         => $this->m_profile[PF_EXTAPI]['sum'],
            "log_time"            => $this->m_profile[PF_LOG]['sum'],
            "query"               => $this->m_query,
                                );

            Request_Http($this->m_log_url, Encode($post_string), "POST");
        }
    }

?>