/*
                  - 2018-07-05 최종 검증 완료 -
    
    소켓 통신에 필요한 링버퍼 및 직렬화버퍼 제작
    컨텐츠와 통신을 쓰레드를 이용하여 분리
    유니티 컨텐츠 부분 일부 버그 존재( 클라이언트부분이라 넘김 )
 
    - contents
        한개의 방에서 두명의 플레이어가 채팅하는 프로그램 ( 방생성한 플레이어가 항상 방장이므로 둘 중에 하나의 플레이어가 나가더라도 둘다 방에서 나가지는 구조 )     
        플레이어 닉네임은 항상 1 ~ 4길이만 허용( 그 이외는 접속이 불가능 )
        입장할 방이없는 경우에는 입장실패로 처리
*/

using UnityEngine;

using System;
using System.Text;
using System.Collections;
using System.Collections.Generic;
using System.Threading;

using System.Net;
using System.Net.Sockets;

using System.Runtime.InteropServices;

using RINGBUFFER;
using SERIALIZE_BUFFER;

public class Chat : MonoBehaviour
{
    private enum ChatState
    {
        ROOM_SELECT = 0,        // 방 선택.
        CHATTING,               // 채팅 중.
        LEAVE,                  // 나가기.
        ERROR,                  // 오류.
    };

    private enum PlayerState : Byte
    {
        NONE = 0,
        CREATE_SEND,
        CREATE_RECV,
        ENTER_SEND,
        ENTER_RECV,
        LEAVE_SEND,
        LEAVE_RECV,
    }

    private enum SendPacketState : Byte
    {
        CS_CREATE_ROOM = 1,
        CS_ENTER_ROOM,
        CS_LEAVE_ROOM,
        CS_CHAT
    };

    private enum RecvPacketState : Byte
    {
        SC_CREATE_ROOM = 10,
        SC_ENTER_ROOM,
        SC_LEAVE_ROOM,
        SC_CHAT,

        CREATE_OK   = 21,
        CREATE_FAIL = 22,
        ENTER_OK    = 31,
        ENTER_FAIL  = 32,
    };

    [StructLayout(LayoutKind.Sequential, Pack = 1)] // c++ 의 #pragma pack() 과 동일한 역할( 단 해당 구조체 한개만 적용됨 )
    public struct Header
    {
        public Byte s_code, s_type;
        public Int32 s_payload_size;
    }

    // 이미지 텍스쳐
    public Texture texture_title = null;
    public Texture texture_bg = null;

    public Texture texture_main = null;
    public Texture texture_belo = null;
    public Texture texture_kado_lu = null;
    public Texture texture_kado_ru = null;
    public Texture texture_kado_ld = null;
    public Texture texture_kado_rd = null;
    public Texture texture_tofu = null;
    public Texture texture_daizu = null;

    private Byte m_code = 0x41;
    private bool m_is_left_login = false, m_is_right_login = false, m_is_left_user = false, m_is_right_user = false;
    private bool m_send_thread_flag = true, m_recv_thread_flag = true, m_chat_packet_ready = false;
    private const int m_port = 9000, m_header_size = 6;
    private string m_my_nickname = "", m_other_nickname = "", m_sendComment = "", m_Address = "192.168.10.15";
    private ChatState m_chat_state = ChatState.ROOM_SELECT;
    private PlayerState m_player_state = PlayerState.NONE;
    private Socket m_client_socket;
    private RingBuffer m_sendQ, m_recvQ;

    private static float KADO_SIZE = 16.0f;
    private static float FONT_SIZE = 13.0f;
    private static float FONG_HEIGHT = 18.0f;
    private static int MESSAGE_LINE = 18;

    private Thread m_send_packet_thread, m_recv_packet_thread;
    private List<string>[] m_message_list;
  
    private void Start()
    {
        m_message_list = new List<string>[2];
        for (int i = 0; i < 2; ++i)
            m_message_list[i] = new List<string>();      
    }

    private void Update()
    {
        switch (m_chat_state)
        {
            case ChatState.ROOM_SELECT:
                 UpdateSelect();
                 break;

            case ChatState.CHATTING:
                UpdateChatting();
                break;

            case ChatState.LEAVE:
                UpdateLeave();
                break;
        }
    }

    unsafe private bool Connect(PlayerState p_status)
    {
        m_client_socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);

        try
        {
            m_client_socket.Connect(m_Address, m_port);
        }
        catch
        {
            m_chat_state = ChatState.ERROR;
            return false;
        }

        m_recv_thread_flag = true;
        m_send_thread_flag = true;

        m_sendQ = new RingBuffer();
        m_recvQ = new RingBuffer();

        // 패킷 송신담당할 쓰레드 생성 및 실행
        m_send_packet_thread = new Thread(new ThreadStart(SendPacketThread));
        m_send_packet_thread.Start();

        // 패킷 수신담당할 쓰레드 생성 및 실행
        m_recv_packet_thread = new Thread(new ThreadStart(RecvPacketThread));
        m_recv_packet_thread.Start();

        SerializeBuffer serialQ = new SerializeBuffer(m_header_size);
        Header head = new Header();
        Byte[] nickname = Encoding.Unicode.GetBytes(m_my_nickname); // 한글입력이 존재하므로 유니코드로 인코딩

        fixed (void* fix_nickname = nickname)
            serialQ.Enqueue((IntPtr)fix_nickname, nickname.Length * 2);
        int payload_size = serialQ.GetUsingSize();

        head.s_code = m_code;
        head.s_payload_size = payload_size;

        if (p_status == PlayerState.CREATE_SEND)
            head.s_type = (Byte)SendPacketState.CS_CREATE_ROOM;
        else if(p_status == PlayerState.ENTER_SEND)
            head.s_type = (Byte)SendPacketState.CS_ENTER_ROOM;

        void* head_ptr = (void*)&head;
        Byte[] head_array = new byte[m_header_size];
        System.Runtime.InteropServices.Marshal.Copy((IntPtr)head_ptr, head_array, 0, m_header_size);

        serialQ.MakeHeader(head_array, m_header_size);
        m_sendQ.Enqueue((IntPtr)serialQ.GetBufferPtr(), payload_size + m_header_size);
        
        return true;
    }

    public void UpdateSelect()
    {
        if(m_player_state == PlayerState.CREATE_SEND)
        {
            bool check = Connect(PlayerState.CREATE_SEND);
            if (check == true)
            {
                m_player_state = PlayerState.CREATE_RECV;
                m_is_left_login = true;
            }
        }                   
        else if(m_player_state == PlayerState.ENTER_SEND)
        {
            bool check = Connect(PlayerState.ENTER_SEND);
            if (check == true)
            {
                m_player_state = PlayerState.ENTER_RECV;
                m_is_right_login = true;
            }
        }
    }

    unsafe void UpdateChatting()
    {
        if(m_chat_packet_ready == true)
        {
            Byte[] comment = Encoding.Unicode.GetBytes(m_sendComment);
            SerializeBuffer serialQ = new SerializeBuffer(m_header_size);
            Header head = new Header();

            fixed(void* fix_comment = comment)
                serialQ.Enqueue((IntPtr)fix_comment, m_sendComment.Length * 2);
            int payload_size = serialQ.GetUsingSize();

            head.s_code = m_code;
            head.s_payload_size = payload_size;
            head.s_type = (Byte)SendPacketState.CS_CHAT;

            void* head_ptr = (void*)&head;
            Byte[] head_array = new byte[m_header_size];
            System.Runtime.InteropServices.Marshal.Copy((IntPtr)head_ptr, head_array, 0, m_header_size);

            serialQ.MakeHeader(head_array, m_header_size);
            m_sendQ.Enqueue((IntPtr)serialQ.GetBufferPtr(), payload_size + m_header_size);

            string message = "[" + DateTime.Now.ToString("HH:mm:ss") + "] " + m_sendComment;
            if(m_is_left_user == true)
                AddMessage(ref m_message_list[0], message);
            else if(m_is_right_user ==  true)
                AddMessage(ref m_message_list[1], message);

            m_sendComment = "";
        }
    }

    unsafe void UpdateLeave()
    {
        if (m_player_state != PlayerState.LEAVE_SEND)
            return;

        SerializeBuffer serialQ = new SerializeBuffer(m_header_size);
        Header head = new Header();

        int payload_size = serialQ.GetUsingSize();

        head.s_code = m_code;
        head.s_type = (Byte)SendPacketState.CS_LEAVE_ROOM;
        head.s_payload_size = payload_size;

        void* head_ptr = (void*)&head;
        Byte[] head_array = new byte[m_header_size];
        System.Runtime.InteropServices.Marshal.Copy((IntPtr)head_ptr, head_array, 0, m_header_size);

        serialQ.MakeHeader(head_array, m_header_size);
        m_sendQ.Enqueue((IntPtr)serialQ.GetBufferPtr(), payload_size + m_header_size);
        m_player_state = PlayerState.LEAVE_RECV;
    }

    void OnGUI()
    {
        switch (m_chat_state)
        {
            case ChatState.ROOM_SELECT:
                 GUI.DrawTexture(new Rect(0, 0, 800, 600), this.texture_title);
                 SelectRoomGUI();
                 break;

            case ChatState.CHATTING:
                 GUI.DrawTexture(new Rect(0, 0, 800, 600), this.texture_bg);
                 ChattingGUI();
                 break;

            case ChatState.ERROR:
                 GUI.DrawTexture(new Rect(0, 0, 800, 600), this.texture_title);
                 ErrorGUI();
                 break;
        }
    }

    void SelectRoomGUI()
    {
        float width = 800.0f, height = 600.0f;
        float calc_width = width * 0.5f - 100.0f, calc_height = height * 0.75f;

        Input.imeCompositionMode = IMECompositionMode.On; // 한글 입력받을 수 있도록 처리

        // 닉네임 입력받을 공간 생성
        Rect nickname_label = new Rect(calc_width, calc_height - 80, 200, 30);
        GUIStyle nickname_style = new GUIStyle();
        nickname_style.fontStyle = FontStyle.Bold;
        nickname_style.normal.textColor = Color.black;
        GUI.Label(nickname_label, "닉네임", nickname_style);

        Rect nameRect = new Rect(calc_width, calc_height - 60, 200, 30);
        m_my_nickname = GUI.TextField(nameRect, m_my_nickname);

        // IP 입력받을 공간 생성
        Rect ip_label = new Rect(calc_width, calc_height + 80, 200, 30);
        GUIStyle ip_style = new GUIStyle();
        ip_style.fontStyle = FontStyle.Bold;
        ip_style.normal.textColor = Color.black;
        GUI.Label(ip_label, "상대방 IP 주소", ip_style);

        Rect textRect = new Rect(calc_width, calc_height + 100, 200, 30);
        m_Address = GUI.TextField(textRect, m_Address);
        
        if (GUI.Button(new Rect(calc_width, calc_height, 200, 30), "채팅방 만들기"))
        {
            if (0 < m_my_nickname.Length && m_my_nickname.Length <= 4)
            {
                m_player_state = PlayerState.CREATE_SEND;
                m_is_left_user = true;
            }
            else
                m_my_nickname = "";
        }
        else if (GUI.Button(new Rect(calc_width, calc_height + 40, 200, 30), "채팅방 들어가기"))
        {
            if (0 < m_my_nickname.Length && m_my_nickname.Length <= 4)
            {
                m_player_state = PlayerState.ENTER_SEND;
                m_is_right_user = true;
            }
            else
                m_my_nickname = "";
        }
    }

    void ChattingGUI()
    {
        bool isSent = false;
        string head = "대_", tail = "최종병기";
        float width = 800.0f, height = 600.0f;
        float calc_width = width * 0.5f - 100.0f, calc_height = height * 0.75f;

        Rect commentRect = new Rect(220, 450, 300, 30);
        m_sendComment = GUI.TextField(commentRect, m_sendComment, 15);

        if (m_is_left_login == true)
        {
            Vector2 position = new Vector2(200.0f, 200.0f), size = new Vector2(340.0f, 360.0f);

            // 말풍선 테두리
            DrawBaloonFrame(position, size, Color.cyan, true);
            GUI.DrawTexture(new Rect(50.0f, 370.0f, 145.0f, 200.0f), this.texture_tofu);

            Rect nickname_label = new Rect(70, 570, 200, 30);
            GUIStyle nickname_style = new GUIStyle();
            nickname_style.fontStyle = FontStyle.Bold;
            nickname_style.normal.textColor = Color.black;

            if (m_is_left_user == true)
                GUI.Label(nickname_label, head + m_my_nickname + tail, nickname_style);
            else
                GUI.Label(nickname_label, head + m_other_nickname + tail, nickname_style);

            foreach (string s in m_message_list[0])
            {
                DrawText(s, position, size);
                position.y += FONG_HEIGHT;
            }
        }

        if (m_is_right_login == true)
        {
            Vector2 position = new Vector2(600.0f, 200.0f), size = new Vector2(340.0f, 360.0f);

            // 말풍선 테두리
            DrawBaloonFrame(position, size, Color.green, false);
            GUI.DrawTexture(new Rect(600.0f, 370.0f, 145.0f, 200.0f), this.texture_daizu);

            Rect nickname_label = new Rect(620, 570, 200, 30);
            GUIStyle nickname_style = new GUIStyle();
            nickname_style.fontStyle = FontStyle.Bold;
            nickname_style.normal.textColor = Color.black;

            if (m_is_left_user == true)
                GUI.Label(nickname_label, head + m_other_nickname + tail, nickname_style); 
            else
                GUI.Label(nickname_label, head + m_my_nickname + tail, nickname_style);

            foreach (string s in m_message_list[1])
            {
                DrawText(s, position, size);
                position.y += FONG_HEIGHT;
            }
        }

        isSent = GUI.Button(new Rect(530, 450, 100, 30), "말하기");
        if (isSent == true || Event.current.keyCode == KeyCode.Return)
        {   
            if(m_is_left_user == true && m_is_right_login == true)
            {
                if(m_sendComment.Length != 0)
                    m_chat_packet_ready = true;
            }
            else if(m_is_right_user == true && m_is_left_login == true)
            {
                if (m_sendComment.Length != 0)
                    m_chat_packet_ready = true;
            }
        }

        if (GUI.Button(new Rect(380, 560, 80, 30), "나가기"))
        {
            m_chat_state = ChatState.LEAVE;
            m_player_state = PlayerState.LEAVE_SEND;   
        }
    }

    void ErrorGUI()
    {
        float width = 800.0f, height = 600.0f;
        float calc_width = width * 0.5f - 150.0f, calc_height = height * 0.5f;

        if (GUI.Button(new Rect(calc_width, calc_height, 300, 80), "접속에 실패했습니다.\n\n버튼을 누르세요."))
        {
            m_chat_state = ChatState.ROOM_SELECT;
            m_player_state = PlayerState.NONE;
            m_my_nickname = "";
        }
  
        m_send_packet_thread.Join();
        m_recv_packet_thread.Join();
    }

    // 말풍선 그리는 함수
    void DrawBaloonFrame(Vector2 position, Vector2 size, Color color, bool left)
    {
        GUI.color = color;

        float kado_size = KADO_SIZE;

        Vector2 p, s;

        s.x = size.x - kado_size * 2.0f;
        s.y = size.y;

        // 한 가운데.
        p = position - s / 2.0f;
        GUI.DrawTexture(new Rect(p.x, p.y, s.x, s.y), this.texture_main);

        // 좌.
        p.x = position.x - s.x / 2.0f - kado_size;
        p.y = position.y - s.y / 2.0f + kado_size;
        GUI.DrawTexture(new Rect(p.x, p.y, kado_size, size.y - kado_size * 2.0f), this.texture_main);

        // 우.
        p.x = position.x + s.x / 2.0f;
        p.y = position.y - s.y / 2.0f + kado_size;
        GUI.DrawTexture(new Rect(p.x, p.y, kado_size, size.y - kado_size * 2.0f), this.texture_main);

        // 좌상.
        p.x = position.x - s.x / 2.0f - kado_size;
        p.y = position.y - s.y / 2.0f;
        GUI.DrawTexture(new Rect(p.x, p.y, kado_size, kado_size), this.texture_kado_lu);

        // 우상.
        p.x = position.x + s.x / 2.0f;
        p.y = position.y - s.y / 2.0f;
        GUI.DrawTexture(new Rect(p.x, p.y, kado_size, kado_size), this.texture_kado_ru);

        // 좌하.
        p.x = position.x - s.x / 2.0f - kado_size;
        p.y = position.y + s.y / 2.0f - kado_size;
        GUI.DrawTexture(new Rect(p.x, p.y, kado_size, kado_size), this.texture_kado_ld);

        // 우하.
        p.x = position.x + s.x / 2.0f;
        p.y = position.y + s.y / 2.0f - kado_size;
        GUI.DrawTexture(new Rect(p.x, p.y, kado_size, kado_size), this.texture_kado_rd);

        // 말풍선 기호.
        p.x = position.x - kado_size;
        p.y = position.y + s.y / 2.0f;
        GUI.DrawTexture(new Rect(p.x, p.y, kado_size, kado_size), this.texture_belo);

        GUI.color = Color.white;
    }

   // 저장된 채팅 내용을 말풍선위에 출력하는 함수 
    void DrawText(string message, Vector2 position, Vector2 size)
    {
        if (message.Length == 0)
            return;

        Vector2 balloon_size, text_size, p;
        GUIStyle style = new GUIStyle();
        style.fontSize = 16;
        style.normal.textColor = Color.black;

        text_size.x = message.Length * FONT_SIZE;
        text_size.y = FONG_HEIGHT;

        balloon_size.x = text_size.x + KADO_SIZE * 2.0f;
        balloon_size.y = text_size.y + KADO_SIZE;

        p.x = position.x - size.x / 2.0f + KADO_SIZE;
        p.y = position.y - size.y / 2.0f + KADO_SIZE;

        GUI.Label(new Rect(p.x, p.y, text_size.x, text_size.y), message, style);
    }

    private void SendPacketThread()
    {
        // 패킷 송신만 하는 쓰레드
        while (m_send_thread_flag == true)
        {
            if (m_sendQ.GetUseSize() > 0 && m_client_socket.Poll(0, SelectMode.SelectWrite) == true)
            {
                int sendQ_size = m_sendQ.LinearRemainFrontSize();
                Byte[] sendQ_array = new byte[sendQ_size];
                System.Runtime.InteropServices.Marshal.Copy((IntPtr)m_sendQ.GetFrontPtr(), sendQ_array, 0, sendQ_size);

                int send_size = m_client_socket.Send(sendQ_array, sendQ_size, SocketFlags.None);
                m_sendQ.MoveFront(send_size);

                
                m_chat_packet_ready = false; // 채팅 패킷이 정상적으로 전송됨을 의미
            }
            Thread.Sleep(10);
        }
    }

    unsafe private void RecvPacketThread()
    {
        // 패킷 수신 및 처리하는 쓰레드
        while (m_recv_thread_flag == true)
        {
            if (m_client_socket.Poll(0, SelectMode.SelectRead) == true)
            {
                int size = m_recvQ.LinearRemainRearSize();
                Byte[] recv_data = new byte[size];

                int read_size = m_client_socket.Receive(recv_data, size, SocketFlags.None);
                fixed (void* fix_data = recv_data)
                    m_recvQ.Enqueue((IntPtr)fix_data, read_size);

                while (true)
                {
                    if (m_recvQ.GetUseSize() < m_header_size)
                        break;

                    int return_value = 0;
                    Header head = new Header();
                    SerializeBuffer serialQ = new SerializeBuffer(m_header_size);

                    void* head_ptr = (void*)&head;
                    bool check = m_recvQ.Peek((IntPtr)head_ptr, m_header_size, ref return_value);
                    if (check == false)
                    {
                        // error
                    }

                    if (head.s_code != m_code)
                    {
                        // error
                    }

                    if (m_recvQ.GetUseSize() < head.s_payload_size + m_header_size)
                        break;

                    m_recvQ.MoveFront(return_value);
                    m_recvQ.Dequeue(serialQ.GetBufferPtr(), head.s_payload_size);

                    serialQ.MoveRear(head.s_payload_size);
                    switch (head.s_type)
                    {
                        case (Byte)RecvPacketState.SC_CREATE_ROOM:
                            CreateRoom(ref serialQ);
                            break;

                        case (Byte)RecvPacketState.SC_ENTER_ROOM:
                            EnterRoom(ref serialQ);
                            break;

                        case (Byte)RecvPacketState.SC_LEAVE_ROOM:
                            LeaveRoom();
                            break;

                        case (Byte)RecvPacketState.SC_CHAT:
                            Chatting(ref serialQ);
                            break;
                    }
                }
            }

            Thread.Sleep(10);
        }
    }

    unsafe private void CreateRoom(ref SerializeBuffer p_serialQ)
    {
        Byte result = 0;
        void* result_ptr = (void*)&result;
        p_serialQ.Dequeue((IntPtr)result_ptr, sizeof(Byte));

        if (result == (Byte)RecvPacketState.CREATE_OK)
        {
            m_chat_state = ChatState.CHATTING;
            m_player_state = PlayerState.NONE;
        }
        else
            m_chat_state = ChatState.ERROR;
    }

    unsafe private void EnterRoom(ref SerializeBuffer p_serialQ)
    {
        Byte result = 0;
        Byte[] name = new byte[10];

        void* result_ptr = (void*)&result;
        p_serialQ.Dequeue((IntPtr)result_ptr, sizeof(Byte));

        if (result == (Byte)RecvPacketState.ENTER_OK)
        {
            fixed (void* fix_name = name)
                p_serialQ.Dequeue((IntPtr)fix_name, p_serialQ.GetUsingSize());

            m_other_nickname = Encoding.Unicode.GetString(name);

            if (m_is_left_login == false)
                m_is_left_login = true;
            else
                m_is_right_login = true;

            m_chat_state = ChatState.CHATTING;
        }
        else
        {
            m_is_right_login = false;
            m_send_thread_flag = false;
            m_recv_thread_flag = false;

            m_client_socket.Close();
            m_chat_state = ChatState.ERROR;
        }    
    }

    unsafe private void Chatting(ref SerializeBuffer p_serialQ)
    {
        Byte[] comment = new byte[p_serialQ.GetUsingSize()];
        fixed (void* fix_name = comment)
            p_serialQ.Dequeue((IntPtr)fix_name, p_serialQ.GetUsingSize());
        
        string message = "[" + DateTime.Now.ToString("HH:mm:ss") + "] " + Encoding.Unicode.GetString(comment);
        if (m_is_left_user == true)
            AddMessage(ref m_message_list[1], message);
        else if (m_is_right_user == true)
            AddMessage(ref m_message_list[0], message);
    }

    unsafe private void LeaveRoom()
    {
        m_chat_state = ChatState.ROOM_SELECT;
        m_player_state = PlayerState.NONE;

        m_send_thread_flag = false; m_recv_thread_flag = false;
        m_is_left_login = false; m_is_right_login = false;
        m_is_left_user = false;  m_is_right_user = false;
        m_my_nickname = ""; m_other_nickname = "";
        m_client_socket.Close();
        
        for (int i = 0; i < 2; i++)
            m_message_list[i].Clear();
    }

    void AddMessage(ref List<string> messages, string str)
    {
        while (messages.Count >= MESSAGE_LINE)
            messages.RemoveAt(0);
        
        messages.Add(str);
    }

    // 완료 ( 최종 프로그램이 종료될때 호출되는 함수 )
    private void OnApplicationQuit()
    {
        // 스레드 종료하기 위한 코드 삽입
        m_send_thread_flag = false;
        m_recv_thread_flag = false;
    }
}