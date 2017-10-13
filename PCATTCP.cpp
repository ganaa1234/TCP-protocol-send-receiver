#include <stdio.h>   
#include <io.h>   
#include <winsock2.h>   
   
#include "getopt.h"    
     
#define  PCATTCP_VERSION   "2.01.01.08"   
   
extern int errno;   
   
BOOL     g_bShutdown = FALSE;               
WSADATA  g_WsaData;   
   
#ifndef IPPORT_TTCP   
#define IPPORT_TTCP          5001   
#endif   
   
typedef   
struct _CMD_BLOCK   
{   
    //   
    //    Коммандын тест/өгөгдлийн тохиргоо
    //   
    BOOLEAN     m_bTransmit;   
    USHORT      m_Protocol;   
   
    SOCKADDR_IN m_RemoteAddr;        // Хост битийн дараалал   
    int         m_nRemoteAddrLen;   
   
    SOCKADDR_IN m_LocalAddress;      // Хост битийн дараалал    
   
    USHORT      m_Port;              // Хост битийн дараалал    
   
    BOOLEAN     m_bOptContinuous;   
    int         m_nOptWriteDelay;    // бичилт тус бүрийн өмнө миллисекунд тасалдал авна.   
   
    BOOLEAN     m_bOptMultiReceiver;   
   
    BOOLEAN     m_bTouchRecvData;   
   
    BOOLEAN     m_bSinkMode;         // False = нормаль оролт/гаралт, TRUE = sink/source горим   
   
    int         m_nNumBuffersToSend; // sink горимд илгээх буфферийн тоо   
   
    int         m_nBufferSize;       // буфферийн урт   
    int         m_nBufOffset;        // буфферийн тохируулга   
    int         m_nBufAlign;         // модуль   
   
    BOOLEAN     m_bUseSockOptBufSize;   
    int         m_nSockOptBufSize;   // сокетийн ашиглах буфферийн хэмжээ   
   
    int         m_nSockOptNoDelay;   // TCP_NODELAY сокет сонголтыг тохируулах   
   
    BOOLEAN     m_bSockOptDebug;     // TRUE = SO_DEBUG сокет сонголтыг тохируулах   
   
    int         m_TokenRate;          // өгөгдлийг дамжуулах шаардлагтай хурд, байт/ceкунд 
   
    SOCKET      m_ExtraSocket;   
}   
    CMD_BLOCK, *PCMD_BLOCK;   
   
typedef   
struct _TEST_BLOCK   
{   
    //   
    //   Комманд/Өгөгдлийн тохиргоо
    //   
    CMD_BLOCK       m_Cmd;   
   
    //   
    // Өгөгдлийн жишээ   
    //   
    PCHAR           m_pBufBase;          // ptr-ийг динамик буфферлуу шилжүүлэх(суурь)
    PCHAR           m_pBuf;              // ptr-ийг динамик буфферлуу шилжүүлэх(тэнцүүлэх)   
   
    SOCKET          m_Socket_fd;         // fd сүлжээнд сокетийг илгээх/хүлээн авах   
   
    //   
    // статистик   
    //   
    DWORD           m_tStart;   
    DWORD           m_tFinish;   
   
    unsigned long   m_numCalls;          // оролт/гаралтын системийг дуудах  
    double          m_nbytes;         // сүлжээний байт   
    int             m_ByteCredits;       // өгөгдлийн хурдыг багасгах   
}   
    TEST_BLOCK, *PTEST_BLOCK;   
   
int b_flag = 0;      // mread() ашиглах
int one = 1;          
int zero = 0;          
   
int verbose = 0;     // 0= энгийн мэдээллийг хэвлэх, 1=cpu-ны хурдыг хэвлэх 
                         // нөөцийг ашиглах   
   
char fmt = 'K';      // гаралтын формат:   
                     // k = kilobits, K = kilobytes,   
                     // m = megabits, M = megabytes,    
                     // g = gigabits, G = gigabytes   
   
#define  UDP_GUARD_BUFFER_LENGTH  4   
   
//   
// Forward Procedure Prototypes   
//   
void TTCP_ExitTest( PTEST_BLOCK pTBlk, BOOLEAN bExitProcess );   
void TTCP_ExitTestOnWSAError( PTEST_BLOCK pTBlk, PCHAR pFnc );   
void TTCP_ExitTestOnCRTError( PTEST_BLOCK pTBlk, PCHAR pFnc );   
   
int TTCP_Nread( PTEST_BLOCK pTBlk, int count );   
int TTCP_Nwrite( PTEST_BLOCK pTBlk, int count );   
int TTCP_mread( PTEST_BLOCK pTBlk, unsigned n );   
   
char *outfmt(double b);   
   
void delay( int us );   
   
/////////////////////////////////////////////////////////////////////////////   
//// Miscellaneous Support Routines   
//   
   
void TTCP_LogMsg( const char *format, ... )   
{   
//   FILE *logfd = NULL;   
   FILE *logfd = stderr;   
   
   char  szBuffer[ 256 ];   
   va_list  marker;   
   
   if( !logfd )   
   {   
//      LogOpen();   
   }   
   
   if( !logfd )   
   {   
      return;   
   }   
   
   va_start( marker, format );   
   
   vsprintf( szBuffer, format, marker );   
   
   fprintf( logfd, szBuffer );   
}   
   
   
void TTCP_InitStatistics( PTEST_BLOCK pTBlk )   
{   
   pTBlk->m_numCalls = 0;       // оролт/гаралтын систем дуудах   
   pTBlk->m_nbytes = 0;      // сүлжээний байтууд   
   
   pTBlk->m_tStart = GetTickCount();   
}   
   
   
void TTCP_LogStatistics( PTEST_BLOCK pTBlk )   
{   
   double realt;                 // хэрэглэгч, бодит хугацаа (секунд)   
   
   pTBlk->m_tFinish = GetTickCount();   
   
   realt = ((double )pTBlk->m_tFinish - (double )pTBlk->m_tStart)/1000;   
   
   if( pTBlk->m_Cmd.m_Protocol == IPPROTO_UDP )   
   {   
      if( pTBlk->m_Cmd.m_bTransmit )   
      {   
         TTCP_LogMsg( "  Statistics  : UDP -> %s:%d\n",   
            inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ),   
            pTBlk->m_Cmd.m_Port );   
      }   
      else   
      {   
         TTCP_LogMsg( "  Statistics  : UDP <- %s:%d\n",   
            inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ),   
            htons( pTBlk->m_Cmd.m_RemoteAddr.sin_port ) );   
      }   
   }   
   else   
   {   
      if( pTBlk->m_Cmd.m_bTransmit )   
      {   
         TTCP_LogMsg( "  Statistics  : TCP -> %s:%d\n",   
            inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ),   
            pTBlk->m_Cmd.m_Port   
            );   
      }   
      else   
      {   
         TTCP_LogMsg( "  Statistics  : TCP <- %s:%d\n",   
            inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ),   
            htons( pTBlk->m_Cmd.m_RemoteAddr.sin_port ) );   
      }   
   }   
   
   TTCP_LogMsg(    
      "%.0f bytes in %.2f real seconds = %s/sec +++\n",   
      pTBlk->m_nbytes,   
      realt,   
      outfmt(pTBlk->m_nbytes/realt)   
      );   
   
   TTCP_LogMsg( "numCalls: %d; msec/call: %.2f; calls/sec: %.2f\n",   
      pTBlk->m_numCalls,   
      1024.0 * realt/((double )pTBlk->m_numCalls),   
      ((double )pTBlk->m_numCalls)/realt   
      );   
}   
   
/////////////////////////////////////////////////////////////////////////////   
//// TEST_BLOCK үйл ажилгааг дэмжих   
//   
// Санамж   
// A TEST_BLOCK structure is allocated for each TTCP primary test routine:   
//   
//    TTCP_TransmitTCP   
//    TTCP_TransmitUDP   
//    TTCP_ReceiveTCP   
//    TTCP_ListenTCP   
//    TTCP_ReceiveUDP   
//   
// TEST_BLOCK нь хувийн бүтэцтэй шаардлагатай бүх мэдээллийг агуулсан гүйцэтгэх функц юм.
// Энэ нь Command_BLOCK параметрүүдийн хуулбарийг агуулдаг иймээс сокетууд болон бусад 
// нэгжүүдийн үйл ажилгааг шалгаж, мөн туршилтын функц нь буффер болон статистикуудтай.  
//   
// TTCP эхний туршилтын функц бүр нь өөрийн TEST_BLOCK-ийг хувиарлаж, эцэст нь үүнийгээ чөлөөлдөг.   
//   
// PCATTCP-ийн хувьд нэг урсгалыг хаахад дахин дахин устгах механизмийн хувилбарыг ашигладаг.
// Энэ нь гэхдээ тийм ч тустай арга биш. Глобал өгөгдөл нь илүү үр дүнтэй.
// 
  
   
PTEST_BLOCK   
TTCP_AllocateTestBlock( PCMD_BLOCK pCmdBlk )   
{   
   PTEST_BLOCK pTBlk = NULL;   
   
   pTBlk = (PTEST_BLOCK )malloc( sizeof( TEST_BLOCK ) );   
   
   if( !pTBlk )   
   {   
      TTCP_ExitTestOnCRTError( NULL, "malloc" );   
   
      return( NULL );   
   }   
   
   memset( pTBlk, 0x00, sizeof( TEST_BLOCK ) );   
   
   memcpy( &pTBlk->m_Cmd, pCmdBlk, sizeof( CMD_BLOCK ) );   
   
   pTBlk->m_Socket_fd = INVALID_SOCKET; // fd сүлжээн сокетийг илгээх/хүлээн авах   
   
   return( pTBlk );   
}   
   
   
void TTCP_FreeTestBlock( PTEST_BLOCK pTBlk )   
{   
   if( !pTBlk )   
   {   
      return;   
   }   
   
   if( pTBlk->m_pBufBase )   
   {   
      free( pTBlk->m_pBufBase );   
   }   
   
   pTBlk->m_pBufBase = NULL;   
   
   if( pTBlk->m_Socket_fd != INVALID_SOCKET )   
   {   
      closesocket( pTBlk->m_Socket_fd );   
   
      pTBlk->m_Socket_fd = INVALID_SOCKET;   
   }   
   
   pTBlk->m_Socket_fd = INVALID_SOCKET;   
   
   free( pTBlk );   
}   
   
   
/////////////////////////////////////////////////////////////////////////////   
//// TTCP гарах үйл ажиллагаа   
//   
// Санамж   
// TTCP тестүүдийг нь гарж аль нэгийн дуудна.
//   
//  TTCP_ExitTest           - Call for normal exit.   
//  TTCP_ExitTestOnWSAError - Call to exit when Winsock error is encountered.   
//  TTCP_ExitTestOnCRTError - Call to exit when OS/DOS error is encountered.   
//      
//   
// Тэд аль нэг гаралтийг дуудаж болно ExitThread эсвэл ExitProcess. Ганц урсгалтай хувилбарийн
// хувьд ExitThread дуудхад гол програмын урсгал энгийнээр гардаг. Энэ нь гаралтын үйл ажилгааг
// дуудаж байгаатай адил.
//   
   
void TTCP_ExitTest( PTEST_BLOCK pTBlk, BOOLEAN bExitProcess )   
{   
   if( pTBlk )   
   {   
      TTCP_FreeTestBlock( pTBlk );   
   }   
   
   if( bExitProcess )   
   {   
      WSACleanup();   
   
      ExitProcess( 1 );   
   }   
   
   ExitThread( 1 );   
}   
   
   
void TTCP_ExitTestOnWSAError( PTEST_BLOCK pTBlk, PCHAR pFnc )   
{   
   int nError = WSAGetLastError();   
   
   TTCP_LogMsg( "*** Winsock Error: %s Failed; Error: %d (0x%8.8X)\n",   
      pFnc, nError, nError );   
   
   TTCP_ExitTest( pTBlk, TRUE );   
}   
   
   
void TTCP_ExitTestOnCRTError( PTEST_BLOCK pTBlk, PCHAR pFnc )   
{   
   TTCP_LogMsg( "*** CRT Error: %s Failed; Error: %d (0x%8.8X)\n",   
      pFnc, errno, errno );   
   
   TTCP_ExitTest( pTBlk, TRUE );   
}   
   
   
/////////////////////////////////////////////////////////////////////////////   
//// CtrlHandler   
//   
// Зорилго   
// Ctrl-C консол зохицуулалт   
//   
// Параметрүүд   
//   
// Буцаах утга   
//   
// Санамж   
//   
   
BOOL WINAPI CtrlHandler( DWORD dwCtrlType )   
{   
   //   
   // Sanity Checks   
   //   
   
   switch( dwCtrlType )   
   {   
      case CTRL_C_EVENT:   
         TTCP_LogMsg( "Ctrl+C Event\n" );   
         TTCP_LogMsg( "TTCP shutting down...\n" );   
         g_bShutdown = TRUE;   
   
         //   
         // Бүртгэлтэй мэссеж илгээх   
         // ---------------------   
         // Энэ нь консол дээр гарах үр дүнг дахин чиглүүлэх боломжийг олгоно.
         //   
         fflush( stderr );   
         fflush( stdout );   
   
         ExitProcess(1);   
         return( TRUE );   
   
      case CTRL_BREAK_EVENT:   
      case CTRL_CLOSE_EVENT:   
      case CTRL_SHUTDOWN_EVENT:   
      default:   
         return( FALSE );   
   }   
   
   return( FALSE );   
}   
   
   
void TTCP_SetConfigDefaults( PCMD_BLOCK pCmdBlk )   
{   
   memset( pCmdBlk, 0x00, sizeof( CMD_BLOCK ) );   
   
   pCmdBlk->m_bTransmit = FALSE;   
   pCmdBlk->m_Protocol = IPPROTO_TCP;   
   
   memset(   
      &pCmdBlk->m_RemoteAddr,   
      0x00,   
      sizeof( IN_ADDR )   
      );   
   
   pCmdBlk->m_Port = IPPORT_TTCP;   
   
   pCmdBlk->m_bOptContinuous = FALSE;   
   pCmdBlk->m_nOptWriteDelay = 0;   
   
   pCmdBlk->m_bOptMultiReceiver = FALSE;   
//   pCmdBlk->m_bOptMultiReceiver = TRUE;   
   
   pCmdBlk->m_bTouchRecvData = FALSE;   
   
   pCmdBlk->m_bSinkMode = TRUE;     // FALSE = нормаль оролт/гаралт, TRUE = sink/source горим  
   
   //   
   // SinkMode-ийг анхдагч утгаар нь тохируулах  
   // ----------------------   
   // SinkMode тодорхойлолт:   
   //   TRUE  -> Генератор ашиглана илгээх буфферыг дүүргэдэг. Энэ нь зөвхөн нэг удаа л хийгдэнэ. 
   //            Хүлээн авсан өгөгдөл нь энгийнээр тоологддог.  
   //   FALSE -> Илгээсэн өгөгдлийг std-ээс уншина. Хүлээн авсан өгөгдлийг stdout-д бичнэ.   
   //            
   // g_nNumBuffersToSend нь SinkMode-д илгээсэн буфферийн тоог тодорхойлно.      
   //   
   pCmdBlk->m_bSinkMode = TRUE;     // FALSE = нормаль I/O, TRUE = sink/source горим   
   pCmdBlk->m_nNumBuffersToSend = 2 * 1024; // sinkmode-д илгээх буфферуудын тоо.   
   
   //   
   // Илгээх/Хүлээн авах буфферийн хэмжээг тохируулах
   //   
//   pCmdBlk->m_pBufBase = NULL;   
//   pCmdBlk->m_pBuf = NULL;   
   pCmdBlk->m_nBufferSize = 8 * 1024;   
   
   pCmdBlk->m_nBufOffset = 0;             // буфферийг тохируулах   
   pCmdBlk->m_nBufAlign = 16*1024;        // модуль
   
   pCmdBlk->m_bUseSockOptBufSize = FALSE; // сокетийг ашиглах буфферийн хэмжээ   
   pCmdBlk->m_nSockOptBufSize = 0;        // сокетийг ашиглах буфферийн хэмжээ   
   
   pCmdBlk->m_nSockOptNoDelay = 0;        // TCP_NODELAY сокет сонголтийг тохируулах   
   
   pCmdBlk->m_bSockOptDebug = FALSE;      // TRUE = SO_DEBUG сокет сонголтыг тохируулах 
}   
   
// Тэмдэгтүүдээр буфферийг дүүргэх 
void TTCP_FillPattern( PTEST_BLOCK pTBlk )   
{   
   register char c;   
   UCHAR PBPreamble[] = "PCAUSA PCATTCP Pattern";   // 22 байт   
   int cnt = pTBlk->m_Cmd.m_nBufferSize;   
   char  *cp = pTBlk->m_pBuf;   
   
   c = 0;   
   
   //   
   // Insert "PCAUSA Pattern" Preamble   
   //   
   if( cnt > 22 )   
   {   
      memcpy( cp, PBPreamble, 22 );   
      cnt -= 22;   
      cp += 22;   
   }   
   
   while( cnt-- > 0 )   
   {   
      while( !isprint((c&0x7F)) )   
      {   
         c++;   
      }   
   
      *cp++ = (c++&0x7F);   
   }   
}   
   
BOOLEAN TTCP_AllocateBuffer( PTEST_BLOCK pTBlk )   
{   
   //   
   // Буфферийн тохиргоог тохируулах   
   //   
   if( (pTBlk->m_pBufBase = (PCHAR )malloc(   
               pTBlk->m_Cmd.m_nBufferSize + pTBlk->m_Cmd.m_nBufAlign)) == (PCHAR )NULL   
      )   
   {   
      return( FALSE );  // Амжилтгүй болсон  
   }   
   
   //   
   // Буфферийн хэмжээг тохируулах   
   //   
   pTBlk->m_pBuf = pTBlk->m_pBufBase;   
   
   if (pTBlk->m_Cmd.m_nBufAlign != 0)   
      pTBlk->m_pBuf += (   
         pTBlk->m_Cmd.m_nBufAlign   
         - ((int)pTBlk->m_pBuf % pTBlk->m_Cmd.m_nBufAlign   
         ) + pTBlk->m_Cmd.m_nBufOffset) % pTBlk->m_Cmd.m_nBufAlign;   
   
   TTCP_LogMsg( "  Buffer Size : %d; Alignment: %d/%d\n",   
      pTBlk->m_Cmd.m_nBufferSize,   
      pTBlk->m_Cmd.m_nBufAlign,   
      pTBlk->m_Cmd.m_nBufOffset   
      );   
   
   return( TRUE );  // Амжилттай болсон   
}   
   
   
/////////////////////////////////////////////////////////////////////////////   
//// TTCP чухал функцууд   
//   
   
/////////////////////////////////////////////////////////////////////////////   
//// TTCP_TransmitTCP   
//   
// Зорилго  
// TTCP TCP дамжуулагч   
//   
// Parameters   
//   pCmdBlk -          Заагч CMD_BLOCK нь дамжуулгийн тестэнд ашиглах тохируулга болон 
//                      бусад тохиргоо
//                     
//   pRemoteHostName -  Заагч нь алсын хостын нэрийг null-terminated гэнэ.  
//                       
//   
// Буцаах утга  
//  
//   
// Санамж 
//   
   
DWORD WINAPI TTCP_TransmitTCP( PCMD_BLOCK pCmdBlk )   
{   
    PTEST_BLOCK pTBlk = NULL;   
    int   optlen;   
   
    //   
    // Hello илгээх   
    //   
    TTCP_LogMsg( "TCP Transmit Test\n" );   
   
    //   
    // Тестийн өгөгдлийн жишээг өгөгдлийг хувиарлах  
    // ---------------------------   
    // Энэ нь онцгой тохиолдолийн хувьд TEST_BLOCK-ийг хувиарлана.
    //   
    pTBlk = TTCP_AllocateTestBlock( pCmdBlk );   
   
    if( !pTBlk )   
    {   
        TTCP_ExitTestOnCRTError( NULL, "malloc" );   
   
        return( 1 );   
    }   
   
    //   
    // Дотоод айпи хаягийг тохируулах   
    //   
    pTBlk->m_Cmd.m_LocalAddress.sin_family = AF_INET;   
    pTBlk->m_Cmd.m_LocalAddress.sin_port = 0;        /* чөлөөтэй сонгох */   
   
    TTCP_LogMsg( "  Transmit    : TCP -> %s:%d\n",   
        inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ),   
        pTBlk->m_Cmd.m_Port   
        );   
   
    //   
    // Буффер илгээх/хүлээн авах өгөгдлийг хувиарлах   
    //   
    if( !TTCP_AllocateBuffer( pTBlk ) )   
    {   
        TTCP_ExitTestOnCRTError( pTBlk, "malloc" );   
    }   
   
    //   
    // Туршилтын нээлттэй сокет   
    //   
    if( ( pTBlk->m_Socket_fd = socket( AF_INET, SOCK_STREAM, 0 ) )   
        == INVALID_SOCKET   
        )   
    {   
        TTCP_ExitTestOnWSAError( pTBlk, "socket" );   
    }   
   
    //   
    // Дотоод хаягтай blind сокет   
    //   
    if( bind(   
        pTBlk->m_Socket_fd,   
        (PSOCKADDR )&pTBlk->m_Cmd.m_LocalAddress,   
        sizeof(pTBlk->m_Cmd.m_LocalAddress)   
        ) == SOCKET_ERROR   
        )   
    {   
        TTCP_ExitTestOnWSAError( pTBlk, "bind" );   
    }   
   
    if( pTBlk->m_Cmd.m_bUseSockOptBufSize )   
    {   
        if( setsockopt(   
            pTBlk->m_Socket_fd,   
            SOL_SOCKET,   
            SO_SNDBUF,   
            (char * )&pTBlk->m_Cmd.m_nSockOptBufSize,   
            sizeof pTBlk->m_Cmd.m_nSockOptBufSize   
            ) == SOCKET_ERROR   
            )   
        {   
            TTCP_ExitTestOnWSAError( pTBlk, "setsockopt: SO_SNDBUF" );   
        }   
   
        TTCP_LogMsg( "  SO_SNDBUF   : %d\n", pTBlk->m_Cmd.m_nSockOptBufSize );   
    }   
   
    //   
    // TCP холболтыг эхлүүлэх  
    //   
    optlen = sizeof( pTBlk->m_Cmd.m_nSockOptNoDelay );   
   
    //   
    // Клиентэд дамжуулах   
    //   
    if( pTBlk->m_Cmd.m_bSockOptDebug )   
    {   
        if( setsockopt(   
            pTBlk->m_Socket_fd,   
            SOL_SOCKET,   
            SO_DEBUG,   
            (PCHAR )&one,     // Boolean   
            sizeof(one)   
            ) == SOCKET_ERROR   
            )   
        {   
            TTCP_ExitTestOnWSAError( pTBlk, "setsockopt: SO_DEBUG" );   
        }   
    }   
   
    //   
    // TCP_NODELAY илгээх тохируулгийг тохируулах
    //   
    if( pTBlk->m_Cmd.m_nSockOptNoDelay )   
    {   
        if( setsockopt(   
            pTBlk->m_Socket_fd,   
            IPPROTO_TCP,   
            TCP_NODELAY,    
            (PCHAR )&one,     // Boolean   
            sizeof(one)   
            ) == SOCKET_ERROR   
            )   
        {   
            int nError = WSAGetLastError();   
            TTCP_LogMsg( "  Error: 0x%8.8X\n", nError );   
            TTCP_LogMsg("setsockopt: TCP_NODELAY option failed");   
        }   
    }   
   
    //   
    // Лавлах болон TCP_NODELAY илгээх тохируулгийг харуулах   
    //   
    if( getsockopt(   
        pTBlk->m_Socket_fd,   
        IPPROTO_TCP,   
        TCP_NODELAY,    
        (PCHAR )&pTBlk->m_Cmd.m_nSockOptNoDelay,   
        &optlen   
        ) != SOCKET_ERROR   
        )   
    {   
        if( pTBlk->m_Cmd.m_nSockOptNoDelay )   
        {   
            TTCP_LogMsg( "  TCP_NODELAY : ENABLED (%d)\n",   
                pTBlk->m_Cmd.m_nSockOptNoDelay );   
        }   
        else   
        {   
            TTCP_LogMsg( "  TCP_NODELAY : DISABLED (%d)\n",   
                pTBlk->m_Cmd.m_nSockOptNoDelay );   
        }   
    }   
    else   
    {   
        int nError = WSAGetLastError();   
        TTCP_LogMsg( "  Error: 0x%8.8X\n", nError );   
        TTCP_LogMsg("getsockopt: TCP_NODELAY option failed\n");   
    }   
   
    //   
    // Алсийн сервертэй холбогдох   
    //   
    if(connect( pTBlk->m_Socket_fd, (PSOCKADDR )&pTBlk->m_Cmd.m_RemoteAddr,   
        sizeof(pTBlk->m_Cmd.m_RemoteAddr) ) == SOCKET_ERROR)   
    {   
        TTCP_ExitTestOnWSAError( pTBlk, "connect" );   
    }   
   
    TTCP_LogMsg( "  Connect     : Connected to %s:%d\n",   
        inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ), pTBlk->m_Cmd.m_Port );   
   
    TTCP_InitStatistics( pTBlk );   
   
    errno = 0;   
   
    if( pTBlk->m_Cmd.m_nOptWriteDelay )   
    {   
        TTCP_LogMsg( "  Write Delay : %d milliseconds\n",   
            pTBlk->m_Cmd.m_nOptWriteDelay   
            );   
    }   
   
    if( pTBlk->m_Cmd.m_bSinkMode )   
    {         
        TTCP_FillPattern( pTBlk );   
   
        if( pTBlk->m_Cmd.m_bOptContinuous )   
        {   
            TTCP_LogMsg( "  Send Mode   : Sending Pattern CONTINUOUS\n" );   
   
            if( pTBlk->m_Cmd.m_TokenRate > 0 )   
            {   
                int LastTickCount = GetTickCount();   
                int ThisTickCount;   
   
                TTCP_LogMsg( "  Data Rate   : %d bytes/second (%.2f KBps)\n", pTBlk->m_Cmd.m_TokenRate, pTBlk->m_Cmd.m_TokenRate/1024.0 );   
   
                Sleep( 1 );   
                ThisTickCount = GetTickCount();   
                pTBlk->m_ByteCredits += ( pTBlk->m_Cmd.m_TokenRate/1000 ) * (ThisTickCount - LastTickCount );   
   
                LastTickCount = ThisTickCount;   
   
                while( pTBlk->m_ByteCredits >= pTBlk->m_Cmd.m_nBufferSize )   
                {   
                    if( TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize )   
                    {   
                        pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
                        pTBlk->m_ByteCredits -= pTBlk->m_Cmd.m_nBufferSize;   
                    }   
                    else   
                    {   
                        break;   
                    }   
                }   
            }   
            else   
            {   
                while (TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize)   
                    pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
            }   
        }   
        else   
        {   
            TTCP_LogMsg( "  Send Mode   : Send Pattern; Number of Buffers: %d\n",   
                pTBlk->m_Cmd.m_nNumBuffersToSend   
                );   
   
            if( pTBlk->m_Cmd.m_TokenRate > 0 )   
            {   
                int LastTickCount = GetTickCount();   
   
                TTCP_LogMsg( "  Data Rate   : %d bytes/second (%.2f KBps)\n", pTBlk->m_Cmd.m_TokenRate, pTBlk->m_Cmd.m_TokenRate/1024.0 );   
   
                // Өгөгдлийг дамжуулах хурдны хэмжээ   
                while( pTBlk->m_Cmd.m_nNumBuffersToSend )   
                {   
                    int ThisTickCount;   
   
                    Sleep( 1 );   
                    ThisTickCount = GetTickCount();   
                    pTBlk->m_ByteCredits += ( pTBlk->m_Cmd.m_TokenRate/1000 ) * (ThisTickCount - LastTickCount );   
   
                    LastTickCount = ThisTickCount;   
   
                    while( pTBlk->m_Cmd.m_nNumBuffersToSend > 0   
                        && pTBlk->m_ByteCredits >= pTBlk->m_Cmd.m_nBufferSize   
                        )   
                    {   
                        if( TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize )   
                        {   
                            --pTBlk->m_Cmd.m_nNumBuffersToSend;   
                            pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
                            pTBlk->m_ByteCredits -= pTBlk->m_Cmd.m_nBufferSize;   
                        }   
                        else   
                        {   
                            break;   
                        }   
                    }   
                }   
            }   
            else   
            {   
                while (pTBlk->m_Cmd.m_nNumBuffersToSend--   
                    && TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize)   
                    pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
            }   
        }   
    }   
    else   
    {   
        register int cnt;   
   
        TTCP_LogMsg( "  Send Mode   : Read from STDIN\n" );   
      
        while( ( cnt = read(0, pTBlk->m_pBuf,pTBlk->m_Cmd.m_nBufferSize ) ) > 0   
            && TTCP_Nwrite( pTBlk, cnt) == cnt   
            )   
        {   
            pTBlk->m_nbytes += cnt;   
        }   
    }   
   
    if(errno)   
        TTCP_ExitTestOnCRTError( pTBlk, "IO" );   
   
    TTCP_LogStatistics( pTBlk );   
   
    TTCP_FreeTestBlock( pTBlk );   
   
    return( 0 );   
}   
   
   
/////////////////////////////////////////////////////////////////////////////   
//// TTCP_TransmitUDP   
//   
// Зорилго  
// TTCP UDP дамжуулах.   
//   
// Параметрүүд   
//   pCmdBlk -         Заагч CMD_BLOCK нь дамжуулах тестэнд ашиглах тохируулга болон тохируулгийн    
//                     мэдээллийг агуулдаг
//                     
//   pRemoteHostName - Заагч нь алсийн хостийн нэрийг null-terminated гэнэ.  
//                     Магадгүй айпи хаяг эсвэл DNS нэр тогтоосон байж болно.   
//     
//   
// Санамж   
//   
   
DWORD WINAPI TTCP_TransmitUDP( PCMD_BLOCK pCmdBlk )   
{   
    PTEST_BLOCK pTBlk = NULL;   
   
    //   
    // Hello илгээх   
    //   
    TTCP_LogMsg( "UDP Transmit Test\n" );   
   
    //   
    // Тестийн жишээ өгөгдлийг хувиарлах   
    // ---------------------------   
    // Энэ нь онцгой тохиолдолд TEST_BLOCK-ийг хувиарлана. 
    //   
    pTBlk = TTCP_AllocateTestBlock( pCmdBlk );   
   
    if( !pTBlk )   
    {   
        TTCP_ExitTestOnCRTError( NULL, "malloc" );   
   
        return( 1 );   
    }   
   
    //   
    // Дотоод айпи хаягийг тохируулах   
    //   
    pTBlk->m_Cmd.m_LocalAddress.sin_family = AF_INET;   
    pTBlk->m_Cmd.m_LocalAddress.sin_port = 0;        /* чөлөөтэй сонгох */   
   
    TTCP_LogMsg( "  Transmit    : UDP -> %s:%d\n",   
        inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ),   
        pTBlk->m_Cmd.m_Port );   
   
    //   
    // Буфферийн тохиргоог тохируулах   
    //   
    if( pTBlk->m_Cmd.m_nBufferSize <= UDP_GUARD_BUFFER_LENGTH )   
    {   
        pTBlk->m_Cmd.m_nBufferSize = UDP_GUARD_BUFFER_LENGTH + 1; // Илгээсэн хэмжээнээс илүү ихийг   
    }   
   
    //   
    // Буффер илгээх/хүлээн авах өгөгдлийг хувиарлах   
    //   
    if( !TTCP_AllocateBuffer( pTBlk ) )   
    {   
        TTCP_ExitTestOnCRTError( pTBlk, "malloc" );   
    }   
   
    //   
    // Туршилтийн нээлттэй сокет   
    //   
    if( (pTBlk->m_Socket_fd = socket( AF_INET, SOCK_DGRAM, 0 ) ) == INVALID_SOCKET   
        )   
    {   
        TTCP_ExitTestOnWSAError( pTBlk, "socket" );   
    }   
   
    //   
    // Дотоод хаягтай blind сокет  
    //   
    if( bind(   
        pTBlk->m_Socket_fd,   
        (PSOCKADDR )&pTBlk->m_Cmd.m_LocalAddress,   
        sizeof(pTBlk->m_Cmd.m_LocalAddress)   
        ) == SOCKET_ERROR   
        )   
    {   
        TTCP_ExitTestOnWSAError( pTBlk, "bind" );   
    }   
   
    if( pTBlk->m_Cmd.m_bUseSockOptBufSize )   
    {   
        if( setsockopt(   
            pTBlk->m_Socket_fd,   
            SOL_SOCKET,   
            SO_SNDBUF,   
            (char * )&pTBlk->m_Cmd.m_nSockOptBufSize,   
            sizeof pTBlk->m_Cmd.m_nSockOptBufSize   
            ) == SOCKET_ERROR   
            )   
        {   
            TTCP_ExitTestOnWSAError( pTBlk, "setsockopt: SO_SNDBUF" );   
        }   
   
        TTCP_LogMsg( "  SO_SNDBUF   : %d\n", pTBlk->m_Cmd.m_nSockOptBufSize );   
    }   
   
    TTCP_InitStatistics( pTBlk );   
    errno = 0;   
   
    if( pTBlk->m_Cmd.m_nOptWriteDelay )   
    {   
        TTCP_LogMsg( "  Write Delay : %d milliseconds\n",   
            pTBlk->m_Cmd.m_nOptWriteDelay   
            );   
    }   
   
    if( pTBlk->m_Cmd.m_bSinkMode )   
    {         
        TTCP_FillPattern( pTBlk );   
   
        TTCP_Nwrite( pTBlk, UDP_GUARD_BUFFER_LENGTH ); /* rcvr эхлэх */   
   
        if( pTBlk->m_Cmd.m_bOptContinuous )   
        {   
            int LastTickCount = GetTickCount();   
   
            TTCP_LogMsg( "  Send Mode   : Send Pattern CONTINUOUS\n" );   
   
            if( pTBlk->m_Cmd.m_TokenRate > 0 )   
            {   
                TTCP_LogMsg( "  Data Rate   : %d bytes/second (%.2f KBps)\n", pTBlk->m_Cmd.m_TokenRate, pTBlk->m_Cmd.m_TokenRate/1024.0 );   
   
                while( 1 )   
                {   
                    int ThisTickCount;   
   
                    Sleep( 1 );   
                    ThisTickCount = GetTickCount();   
                    pTBlk->m_ByteCredits += ( pTBlk->m_Cmd.m_TokenRate/1000 ) * (ThisTickCount - LastTickCount );   
   
                    LastTickCount = ThisTickCount;   
   
                    while( pTBlk->m_ByteCredits >= pTBlk->m_Cmd.m_nBufferSize )   
                    {   
                        if( TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize )   
                        {   
                            pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
                            pTBlk->m_ByteCredits -= pTBlk->m_Cmd.m_nBufferSize;   
                        }   
                        else   
                        {   
                            break;   
                        }   
                    }   
                }   
            }   
            else   
            {   
                while (TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize)   
                    pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
            }   
        }   
        else   
        {   
            TTCP_LogMsg( "  Send Mode   : Send Pattern; Number of Buffers: %d\n",   
                pTBlk->m_Cmd.m_nNumBuffersToSend   
                );   
   
            if( pTBlk->m_Cmd.m_TokenRate > 0 )   
            {   
                int LastTickCount = GetTickCount();   
   
                TTCP_LogMsg( "  Data Rate   : %d bytes/second (%.2f KBps)\n", pTBlk->m_Cmd.m_TokenRate, pTBlk->m_Cmd.m_TokenRate/1024.0 );   
   
                // Өгөгдлийг дамжуулах хурд   
                while( pTBlk->m_Cmd.m_nNumBuffersToSend )   
                {   
                    int ThisTickCount;   
   
                    Sleep( 1 );   
                    ThisTickCount = GetTickCount();   
                    pTBlk->m_ByteCredits += ( pTBlk->m_Cmd.m_TokenRate/1000 ) * (ThisTickCount - LastTickCount );   
   
                    LastTickCount = ThisTickCount;   
   
                    while( pTBlk->m_Cmd.m_nNumBuffersToSend > 0   
                        && pTBlk->m_ByteCredits >= pTBlk->m_Cmd.m_nBufferSize   
                        )   
                    {   
                        if( TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize )   
                        {   
                            --pTBlk->m_Cmd.m_nNumBuffersToSend;   
                            pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
                            pTBlk->m_ByteCredits -= pTBlk->m_Cmd.m_nBufferSize;   
                        }   
                        else   
                        {   
                            break;   
                        }   
                    }   
                }   
            }   
            else   
            {   
                while (pTBlk->m_Cmd.m_nNumBuffersToSend--   
                    && TTCP_Nwrite( pTBlk, pTBlk->m_Cmd.m_nBufferSize) == pTBlk->m_Cmd.m_nBufferSize)   
                    pTBlk->m_nbytes += pTBlk->m_Cmd.m_nBufferSize;   
            }   
        }   
   
        TTCP_Nwrite( pTBlk, UDP_GUARD_BUFFER_LENGTH ); /* rcvr дуусах */   
    }   
    else   
    {   
        register int cnt;   
   
        TTCP_LogMsg( "  Send Mode   : Read from STDIN\n" );   
   
         
        while( ( cnt = read(0, pTBlk->m_pBuf,pTBlk->m_Cmd.m_nBufferSize ) ) > 0   
            && TTCP_Nwrite( pTBlk, cnt) == cnt   
            )   
        {   
            pTBlk->m_nbytes += cnt;   
        }   
    }   
   
    if(errno)   
        TTCP_ExitTestOnCRTError( pTBlk, "IO" );   
   
    TTCP_LogStatistics( pTBlk );   
   
    //   
    // Pause To Allow Receiver To Flush Receive Buffers   
    // ------------------------------------------------   
    // PCATTCP Version 2.01.01.06 and prior (as well as the original TTCP   
    // application)were subject to failure at various points. This was due   
    // to the fact that UDP has no flow control, so a UDP transfer overwhelms   
    // the receiving computer. The reason it was failing was that the end marker   
    // of the data transfer was getting lost in the network while the receiving   
    // computer was being overwhelmed with data. This would cause the receiving   
    // computer to hang, waiting for more data, but the transmitting computer   
    // had already finished sending all the data along with the end markers.   
    //   
    // The folks at Clarkson University identified a simple fix for this   
    // weakness which PCAUSA has adopted in V2.01.01.07. See the URL:   
    //   
    // http://www.clarkson.edu/projects/itl/HOWTOS/mcnair_summer2002/Paper/User_Manual_PCATTCP_Controller_1.00.htm   
    //   
    // A simple pause by the transmitter before the end markers were sent would   
    // allow the receiving computer time to free its internal receiving buffers   
    // and obtain an end marker.   
    //   
    Sleep( 1000 );   
   
    TTCP_Nwrite( pTBlk, UDP_GUARD_BUFFER_LENGTH );   // rcvr end   
    TTCP_Nwrite( pTBlk, UDP_GUARD_BUFFER_LENGTH );   // rcvr end   
    TTCP_Nwrite( pTBlk, UDP_GUARD_BUFFER_LENGTH );   // rcvr end   
    TTCP_Nwrite( pTBlk, UDP_GUARD_BUFFER_LENGTH );   // rcvr end   
   
    TTCP_FreeTestBlock( pTBlk );   
   
    return( 0 );   
}   
   
   
/////////////////////////////////////////////////////////////////////////////   
//// TTCP_ReceiveTCP   
//   
// Зорилго  
// TTCP TCP stream receiver.   
//   
// Параметрүүд   
//   pCmdBlk - Заагч CMD_BLOCK нь хүлээн авах урсгалыг ашиглах тохируулга болон бусад
//             тохируулгийн мэдээллийг агуулдаг.
//              
//   conn_fd - The connection socket, which returned from a call to   
//             accept which is made in TTCP_ListenTCP.   
//    
// Санамж
// TTCP_ListenTCP үйл ажилгаа нь TTCP порт дээрхи холболтийг сонсдог.
// Холболтыг TTCP_ListenTCP-д хүлээн зөвшөөрсөн байгаа үед энэ холболтийн  
// сокет дээр TCP хүлээн авах тестийг гүйцэтгэхийн тулд энэ функцийг дууддаг.
//   
// Conn_fd сокет-ийн эзэмшигчийг энгийн журмаар тогтоодог, эцэст нь үүнийг хаах
//  үүрэгтэй.
//   
   
DWORD WINAPI TTCP_ReceiveTCP( PCMD_BLOCK pCmdBlk )   
{   
   PTEST_BLOCK pTBlk = NULL;   
   
   //   
   // Тестийн жишээ өгөгдлийг хувиарлах   
   // ---------------------------   
   // Энэ нь онцгой тохиолдол TEST_BLOCK-ийг хувиарлах. 
   //   
   pTBlk = TTCP_AllocateTestBlock( pCmdBlk );   
   
   if( !pTBlk )   
   {   
      closesocket( pCmdBlk->m_ExtraSocket );   
   
      TTCP_ExitTestOnCRTError( NULL, "malloc" );   
   
      return( 1 );   
   }   
   
   //   
   // Сокетийн холбогтийг хадгалах  
   //   
   pTBlk->m_Socket_fd = pCmdBlk->m_ExtraSocket;   
   
   pTBlk->m_Cmd.m_Protocol = IPPROTO_TCP;   
   
   //   
   // Буффер илгээх/хүлээн авах өгөгдлийг хувиарлах   
   //   
   if( !TTCP_AllocateBuffer( pTBlk ) )   
   {   
      TTCP_ExitTestOnCRTError( pTBlk, "malloc" );   
   
      return( 1 );   
   }   
   
   TTCP_InitStatistics( pTBlk );   
   errno = 0;   
   
   if( pTBlk->m_Cmd.m_bSinkMode )   
   {         
      register int cnt;   
   
      //   
      // Хүлээн авсан өгөгдлийг хасах   
      //   
      TTCP_LogMsg( "  Receive Mode: Sinking (discarding) Data\n" );   
   
      while( (cnt=TTCP_Nread( pTBlk, pTBlk->m_Cmd.m_nBufferSize) ) > 0)   
      {   
         pTBlk->m_nbytes += cnt;   
      }   
   }   
   else   
   {   
      register int cnt;   
   
      //   
      // Алсаас унших болон stdout-d бичих  
      //   
      TTCP_LogMsg( "  Receive Mode: Writing Received Data To STDOUT\n" );   
   
      while( ( cnt = TTCP_Nread( pTBlk, pTBlk->m_Cmd.m_nBufferSize ) ) > 0   
         && write(1, pTBlk->m_pBuf,cnt) == cnt   
         )   
      {   
         pTBlk->m_nbytes += cnt;   
      }   
   }   
   
    if(errno)   
      TTCP_ExitTestOnCRTError( pTBlk, "IO" );   
   
   TTCP_LogStatistics( pTBlk );   
   
   TTCP_FreeTestBlock( pTBlk );   
   
   return( 0 );   
}   
   
   

//// TTCP_ListenTCP   
//   
// Зорилго   
// TTCP TCP холболтийг сонсох.   
//   
// Параметрүүд   
//   pCmdBlk - Заагч CMD_BLOCK нь холболтийг сонсох ашиглах тохируулга болон бусад тохируулгийн
//             мэдээллийг агуулдаг.
//                
//      
//   
// Санамж 
// TTCP_ListenTCP port дээрхи холболтийг сонсдог
//   
// Холболтийг хүлээн авах үед TTCP_ListenTCP нь TCP хүлээн авах тестийг гүйцэтгэх
// TTCP_ReceiveTCP функцийг дуудна.   
//   
// Conn_fd socket-ийн өмчлөл нь TTCP_ReceiveTCP горимд шилжсэн бөгөөд эцэст нь conn_fd-г хаах үүрэгтэй.
// 
//   
   
DWORD WINAPI TTCP_ListenTCP( PCMD_BLOCK pCmdBlk )   
{   
   PTEST_BLOCK    pTBlk = NULL;   
   SOCKET         conn_fd;   
   BOOLEAN        bDoAccept = TRUE;   
   CHAR           szLocalHostName[ 128 ];   
   
   TTCP_LogMsg( "TCP Receive Test\n" );   
   
   //   
   // Тестийн жишээ өгөгдлийг хувиарлах   
   // ---------------------------   
   // Энэ нь онцгой тохиолдол TEST_BLOCK-ийг хувиарлах
   //   
   pTBlk = TTCP_AllocateTestBlock( pCmdBlk );   
   
   if( !pTBlk )   
   {   
      TTCP_ExitTestOnCRTError( NULL, "malloc" );   
   
      return( 1 );   
   }   
   
   sprintf( szLocalHostName, "Unknown" );   
   gethostname( szLocalHostName, 128 );   
   TTCP_LogMsg( "  Local Host  : %s\n", szLocalHostName );   
   
   //   
   // Дотоод айпи хаягийг тохируулах  
   //   
   pTBlk->m_Cmd.m_LocalAddress.sin_family = AF_INET;   
   pTBlk->m_Cmd.m_LocalAddress.sin_port =  htons( pTBlk->m_Cmd.m_Port );   
   
   //   
   // Нээлттэй сокетийг сонсох   
   //   
   if( (pTBlk->m_Socket_fd = socket( AF_INET, SOCK_STREAM, 0 ) ) == INVALID_SOCKET )   
   {   
      TTCP_ExitTestOnWSAError( pTBlk, "socket" );   
   }   
   
   //   
   // Дотоод хаягтай blind сокет   
   //   
   if( bind(   
         pTBlk->m_Socket_fd,   
         (PSOCKADDR )&pTBlk->m_Cmd.m_LocalAddress,   
         sizeof(pTBlk->m_Cmd.m_LocalAddress)   
         ) == SOCKET_ERROR   
      )   
   {   
      TTCP_ExitTestOnWSAError( pTBlk, "bind" );   
   }   
   
   if( pTBlk->m_Cmd.m_bUseSockOptBufSize )   
   {   
      if( setsockopt(   
            pTBlk->m_Socket_fd,   
            SOL_SOCKET,   
            SO_RCVBUF,   
            (char * )&pTBlk->m_Cmd.m_nSockOptBufSize,   
            sizeof pTBlk->m_Cmd.m_nSockOptBufSize   
            ) == SOCKET_ERROR   
         )   
      {   
         TTCP_ExitTestOnWSAError( pTBlk, "setsockopt: SO_RCVBUF" );   
      }   
   
      TTCP_LogMsg( "  SO_RCVBUF   : %d\n", pTBlk->m_Cmd.m_nSockOptBufSize );   
   }   
   
   //   
   // TCP холболтыг эхлүүлэх    
   //   
   listen( pTBlk->m_Socket_fd, 0 );    
   
   if( pTBlk->m_Cmd.m_bSockOptDebug )   
   {   
      if( setsockopt(   
            pTBlk->m_Socket_fd,   
            SOL_SOCKET,   
            SO_DEBUG,   
            (PCHAR )&one,        // Boolean   
            sizeof(one)   
            ) == SOCKET_ERROR   
         )   
      {   
         TTCP_ExitTestOnWSAError( pTBlk, "setsockopt: SO_DEBUG" );   
      }   
   }   
      
   pTBlk->m_Cmd.m_nRemoteAddrLen = sizeof( pTBlk->m_Cmd.m_RemoteAddr );   
   
   while( bDoAccept )   
   {   
      DWORD    tid;     // Thread ID   
   
      TTCP_LogMsg( "**************\n" );   
      TTCP_LogMsg( "  Listening...: On port %d\n", pTBlk->m_Cmd.m_Port );   
   
      if( ( conn_fd = accept( pTBlk->m_Socket_fd, (PSOCKADDR )&pTBlk->m_Cmd.m_RemoteAddr,   
         &pTBlk->m_Cmd.m_nRemoteAddrLen) ) == SOCKET_ERROR)   
      {   
         TTCP_ExitTestOnWSAError( pTBlk, "accept" );   
      }   
      else   
      {   
         SOCKADDR_IN peer;   
         int peerlen = sizeof(peer);   
   
         if (getpeername( conn_fd, (PSOCKADDR ) &peer,    
               &peerlen) == SOCKET_ERROR)   
         {   
            TTCP_ExitTestOnWSAError( pTBlk, "getpeername" );   
         }   
   
         pCmdBlk->m_RemoteAddr.sin_addr.s_addr = peer.sin_addr.s_addr;   
         pCmdBlk->m_RemoteAddr.sin_port = peer.sin_port;   
   
         TTCP_LogMsg( "\n  Accept      : TCP <- %s:%d\n",   
            inet_ntoa( pCmdBlk->m_RemoteAddr.sin_addr ),   
            htons( pCmdBlk->m_RemoteAddr.sin_port ) );   
      }   
   
      pTBlk->m_Cmd.m_ExtraSocket = conn_fd;   
   
      if( pCmdBlk->m_bOptMultiReceiver )   
      {   
         CreateThread(   
            NULL,   
            0,   
            (LPTHREAD_START_ROUTINE )TTCP_ReceiveTCP,   
            &pTBlk->m_Cmd,   
            0,   
            &tid   
            );   
      }   
      else   
      {   
         TTCP_ReceiveTCP( &pTBlk->m_Cmd );   
      }   
   
      if( !pTBlk->m_Cmd.m_bOptContinuous )   
      {   
         bDoAccept = FALSE;   
      }   
   }   
   
   TTCP_FreeTestBlock( pTBlk );   
   
   return( 0 );   
}   
   
  
//// TTCP_ReceiveUDP   
//   
// Purpose   
// TTCP UDP датаграм хүлээн авагч.   
//   
// Параметрүүд  
//   pCmdBlk - Заагч CMD_BLOCK тохируулгийн мэдээлэл болон бусад тохируулгийн мэдээллийг агуулдаг.
//               
//      
// Санамж   
// The TTCP_ReceiveUDP routine performs the TTCP UDP receive test.   
//   
   
DWORD WINAPI TTCP_ReceiveUDP( PCMD_BLOCK pCmdBlk )   
{   
   PTEST_BLOCK pTBlk = NULL;   
   BOOLEAN     bContinue = TRUE;   
   
   TTCP_LogMsg( "UDP Receive Test\n" );   
   
   //   
   // Тестийн жишээ өгөгдлийг хувиарлах   
   // ---------------------------   
   // Энэ нь онцгой тохиолдол TEST_BLOCK-ийг хувиарлах. 
   //   
   pTBlk = TTCP_AllocateTestBlock( pCmdBlk );   
   
   if( !pTBlk )   
   {   
      TTCP_ExitTestOnCRTError( NULL, "malloc" );   
   
      return( 1 );   
   }   
   
   TTCP_LogMsg( "  Protocol   : UDP\n" );   
   TTCP_LogMsg( "  Port       : %d\n", pTBlk->m_Cmd.m_Port );   
   
   //   
   // Дотоод айпи хаягийг тохируулах   
   //   
   pTBlk->m_Cmd.m_LocalAddress.sin_family = AF_INET;   
   pTBlk->m_Cmd.m_LocalAddress.sin_port =  htons( pTBlk->m_Cmd.m_Port );   
   
   //   
   // Буфферийн тохиргоог тохируулах  
   //   
   if( pTBlk->m_Cmd.m_nBufferSize <= UDP_GUARD_BUFFER_LENGTH   
      )   
   {   
      pTBlk->m_Cmd.m_nBufferSize = UDP_GUARD_BUFFER_LENGTH + 1; // send more than the sentinel size   
   }   
   
   //   
   // Buffer илгээх/хүлээн авах өгөгдлийг хувиарлах   
   //   
   if( !TTCP_AllocateBuffer( pTBlk ) )   
   {   
      TTCP_ExitTestOnCRTError( pTBlk, "malloc" );   
   }   
   
   //   
   // Туршилтийн нээлттэй сокет  
   //   
   if( (pTBlk->m_Socket_fd = socket( AF_INET, SOCK_DGRAM, 0 ) ) == INVALID_SOCKET )   
   {   
      TTCP_ExitTestOnWSAError( pTBlk, "socket" );   
   }   
   
   //   
   // Дотоод хаягтай blind сокет  
   //   
   if( bind(   
         pTBlk->m_Socket_fd,   
         (PSOCKADDR )&pTBlk->m_Cmd.m_LocalAddress,   
         sizeof(pTBlk->m_Cmd.m_LocalAddress)   
         ) == SOCKET_ERROR   
      )   
   {   
      TTCP_ExitTestOnWSAError( pTBlk, "bind" );   
   }   
   
   if( pTBlk->m_Cmd.m_bUseSockOptBufSize )   
   {   
      if( setsockopt(   
            pTBlk->m_Socket_fd,   
            SOL_SOCKET,   
            SO_RCVBUF,   
            (char * )&pTBlk->m_Cmd.m_nSockOptBufSize,   
            sizeof pTBlk->m_Cmd.m_nSockOptBufSize   
            ) == SOCKET_ERROR   
         )   
      {   
         TTCP_ExitTestOnWSAError( pTBlk, "setsockopt: SO_RCVBUF" );   
      }   
   
      TTCP_LogMsg( "  SO_RCVBUF   : %d\n", pTBlk->m_Cmd.m_nSockOptBufSize );   
   }   
   
   while( bContinue )   
   {   
      TTCP_InitStatistics( pTBlk );   
      errno = 0;   
   
      memset(   
         &pTBlk->m_Cmd.m_RemoteAddr,   
         0x00,   
         sizeof( IN_ADDR )   
         );   
   
      if( pTBlk->m_Cmd.m_bSinkMode )   
      {         
         register int cnt;   
         int going = 0;   
         BOOLEAN bIsFirst = TRUE;   
   
         while( (cnt=TTCP_Nread( pTBlk, pTBlk->m_Cmd.m_nBufferSize)) > 0 )   
         {   
            if( cnt <= UDP_GUARD_BUFFER_LENGTH )   
            {   
               if( going )   
               {   
                  going = 0;   
                  break;    /* "EOF" */   
               }   
   
               going = 1;   
               TTCP_InitStatistics( pTBlk );   
            }   
            else   
            {   
               if( bIsFirst )   
               {   
                  TTCP_LogMsg( "  recvfrom    : UDP <- %s:%d\n",   
                     inet_ntoa( pTBlk->m_Cmd.m_RemoteAddr.sin_addr ),   
                     htons( pTBlk->m_Cmd.m_RemoteAddr.sin_port ) );   
   
                  bIsFirst = FALSE;   
               }   
   
               pTBlk->m_nbytes += cnt;   
            }   
         }   
      }   
      else   
      {   
         register int cnt;   
   
         //   
         // Алсаас унших болон stdout-д бичих  
         //   
         while( ( cnt = TTCP_Nread( pTBlk, pTBlk->m_Cmd.m_nBufferSize ) ) > 0   
            && write(1, pTBlk->m_pBuf,cnt) == cnt   
            )   
         {   
            pTBlk->m_nbytes += cnt;   
         }   
      }   
   
       if(errno)   
         TTCP_ExitTestOnCRTError( pTBlk, "IO" );   
   
      if( pTBlk->m_nbytes > 0 )   
      {   
         TTCP_LogStatistics( pTBlk );   
      }   
   
      if( !pTBlk->m_Cmd.m_bOptContinuous )   
      {   
         bContinue = FALSE;   
      }   
   }   
   
   TTCP_FreeTestBlock( pTBlk );   
   
   return( 0 );   
}   
   
//   
// Ашиглалтийн мэдээлэл   
//   
char Usage[] = "\   
Usage: pcattcp -t [-options] host [ < in ]\n\   
       pcattcp -r [-options > out]\n\   
Common options:\n\   
   -l ##  length of bufs read from or written to network (default 8192)\n\   
   -u     use UDP instead of TCP\n\   
   -p ##  port number to send to or listen at (default 5001)\n\   
   -s     toggle sinkmode (enabled by default)\n\   
            sinkmode enabled:\n\   
               -t: source (transmit) fabricated pattern\n\   
               -r: sink (discard) all received data\n\   
            sinkmode disabled:\n\   
               -t: reads data to be transmitted from stdin\n\   
               -r: writes received data to stdout\n\   
   -A     align the start of buffers to this modulus (default 16384)\n\   
   -O     start buffers at this offset from the modulus (default 0)\n\   
   -v     verbose: print more statistics\n\   
   -d     set SO_DEBUG socket option\n\   
   -b ##  set socket buffer size (if supported)\n\   
   -f X   format for rate: k,K = kilo{bit,byte}; m,M = mega; g,G = giga\n\   
   -c       -t: send continuously\n\   
            -r: accept multiple connections sequentially\n\   
   -R     concurrent TCP/UDP multithreaded receiver\n\   
Options specific to -t:\n\   
   -n ##  number of source bufs written to network (default 2048)\n\   
   -D     don't buffer TCP writes (sets TCP_NODELAY socket option)\n\   
   -w ##  milliseconds of delay before each write (default 0)\n\   
   -L ##  desired transmit data rate in bytes/second\n\   
Options specific to -r:\n\   
   -B     for -s, only output full blocks as specified by -l (for TAR)\n\   
   -T     \"touch\": access each byte as it's read\n\   
";   
   
   
 
//// TTCP_ParseCommandLine   
//   
// Зорилго  
// Parse the console application command-line arguments and setup the   
// CMD_BLOCK/   
//   
// Параметрүүд  
//   pCmdBlk - CMD_BLOCK мэдээллийн тохиргоог задлан дүүргэдэгв
//                 
// Санамж   
//   
   
int TTCP_ParseCommandLine( PCMD_BLOCK pCmdBlk, int argc, char **argv )   
{   
    int   c;   
   
    if (argc < 2)   
    {   
        fprintf( stderr, Usage );   
        return( !0 );   
    }   
   
    while (optind != argc)   
    {   
        c = getopt(argc, argv, "cdrstuvBDRTb:f:l:n:p:A:O:w:L:" );   
   
        switch (c)   
        {   
        case EOF:   
            optarg = argv[optind];   
            optind++;   
            break;   
   
        case 'B':   
            b_flag = 1;   
            break;   
   
        case 't':   
            pCmdBlk->m_bTransmit = TRUE;   
            break;   
   
        case 'r':   
            pCmdBlk->m_bTransmit = FALSE;   
            break;   
   
        case 'c':   
            pCmdBlk->m_bOptContinuous = TRUE;   
            break;   
   
        case 'd':   
            pCmdBlk->m_bSockOptDebug = 1;   
            break;   
   
        case 'D':   
            pCmdBlk->m_nSockOptNoDelay = 1;   
            break;   
   
        case 'R':   
            pCmdBlk->m_bOptContinuous = TRUE;   
            pCmdBlk->m_bTransmit = FALSE;   
            pCmdBlk->m_bOptMultiReceiver = TRUE;   
            break;   
   
        case 'n':   
            pCmdBlk->m_nNumBuffersToSend = atoi(optarg);   
            break;   
   
        case 'l':   
            pCmdBlk->m_nBufferSize = atoi(optarg);   
            break;   
   
        case 's':   
            pCmdBlk->m_bSinkMode = !pCmdBlk->m_bSinkMode;   
            break;   
   
        case 'p':   
            pCmdBlk->m_Port = atoi(optarg);   
            break;   
   
        case 'u':   
            pCmdBlk->m_Protocol = IPPROTO_UDP;   
            break;   
   
        case 'v':   
            verbose = 1;   
            break;   
   
        case 'A':   
            pCmdBlk->m_nBufAlign = atoi(optarg);   
            break;   
   
        case 'O':   
            pCmdBlk->m_nBufOffset = atoi(optarg);   
            break;   
   
        case 'b':   
            pCmdBlk->m_bUseSockOptBufSize = TRUE;   
            pCmdBlk->m_nSockOptBufSize = atoi(optarg);   
            if( pCmdBlk->m_nSockOptBufSize < 0 )   
            {   
                pCmdBlk->m_nSockOptBufSize = 0;   
            }   
            break;   
   
        case 'f':   
            fmt = *optarg;   
            break;   
   
        case 'T':   
            pCmdBlk->m_bTouchRecvData = 1;   
            break;   
   
        case 'w':   
            pCmdBlk->m_nOptWriteDelay = atoi(optarg);   
            break;   
   
        case 'L':   
            pCmdBlk->m_TokenRate = atoi(optarg);   
            if( pCmdBlk->m_TokenRate < 0 )   
            {   
                pCmdBlk->m_TokenRate = 0;   
            }   
            break;   
   
        default:   
            fprintf( stderr, Usage );   
            return( !0 );   
        }   
    }   
   
    return( 0 );   
}   
   
   
//  
// Зорилго   
// Програмийн үндсэн нэвтрэх цэг нь консол аппликашион.   
//   
// Параметрүүд  
//   pCmdBlk - CMD_BLOCK нь мэдээллийг тохиргоог задлан дүүргэдэг.
//                 
// Санамж   
//   
   
int main( int argc, char **argv )   
{   
    CMD_BLOCK cmdBlock;   
   
    //   
    // Hello илгээх   
    //   
    TTCP_LogMsg( "PCAUSA Test TCP Utility V%s\n", PCATTCP_VERSION );   
   
    //   
    // Энэ тест нь комманд/тохируулгийг дууддаг. 
    //   
    TTCP_SetConfigDefaults( &cmdBlock );   
   
    if( TTCP_ParseCommandLine( &cmdBlock, argc, argv ) != 0 )   
    {   
        return( 0 );   
    }   
   
  
    if( WSAStartup( MAKEWORD(0x02,0x00), &g_WsaData ) == SOCKET_ERROR )   
    {   
        fprintf( stderr, Usage );   
        TTCP_ExitTestOnWSAError( NULL, "WSAStartup" );   
    }   
   
    //   
    // Set The Ctrl-C Handler   
    //   
    SetConsoleCtrlHandler( CtrlHandler, TRUE );   
   
    if( cmdBlock.m_bOptMultiReceiver )   
    {   
        TTCP_LogMsg( "  Threading   : Multithreaded\n" );   
    }   
   
   
    if( cmdBlock.m_bTransmit )   
    {   
        DWORD nResult;   
   
   
        if( atoi( argv[argc - 1] ) > 0 )   
        {   
  
            cmdBlock.m_RemoteAddr.sin_family = AF_INET;   
            cmdBlock.m_RemoteAddr.sin_addr.s_addr = inet_addr( argv[argc - 1] );   
        }   
        else   
        {   
            struct hostent *addr;   
            unsigned long addr_tmp;   
   
            if ((addr=gethostbyname( argv[argc - 1] )) == NULL)   
            {   
                TTCP_ExitTestOnWSAError( NULL, "gethostbyname" );   
            }   
   
            cmdBlock.m_RemoteAddr.sin_family = addr->h_addrtype;   
   
            memcpy((char*)&addr_tmp, addr->h_addr, addr->h_length );   
            cmdBlock.m_RemoteAddr.sin_addr.s_addr = addr_tmp;   
        }   
   
        cmdBlock.m_RemoteAddr.sin_port = htons( cmdBlock.m_Port );   
   
        if( cmdBlock.m_Protocol == IPPROTO_UDP )   
        {   
            nResult = TTCP_TransmitUDP( &cmdBlock );   
        }   
        else   
        {   
            nResult = TTCP_TransmitTCP( &cmdBlock );   
        }   
    }   
    else   
    {   
        DWORD nResult;   
   
        if( cmdBlock.m_bOptMultiReceiver )   
        {   
            if( cmdBlock.m_Protocol == IPPROTO_UDP )   
            {   
                nResult = TTCP_ReceiveUDP( &cmdBlock );   
            }   
            else   
            {   
                HANDLE   RxThreadHandles[ 2 ];      
                DWORD    tid[2];                  
   
                   
                cmdBlock.m_Protocol = IPPROTO_TCP;   
   
                RxThreadHandles[0] = CreateThread(   
                                        NULL,   
                                        0,   
                                        (LPTHREAD_START_ROUTINE )TTCP_ListenTCP,   
                                        &cmdBlock,   
                                        0,   
                                        &tid[0]   
                                        );   
   
                cmdBlock.m_Protocol = IPPROTO_UDP;   
                nResult = TTCP_ReceiveUDP( &cmdBlock );   
            }   
        }   
        else   
        {   
            if( cmdBlock.m_Protocol == IPPROTO_UDP )   
            {   
                nResult = TTCP_ReceiveUDP( &cmdBlock );   
            }   
            else   
            {   
                nResult = TTCP_ListenTCP( &cmdBlock );   
            }   
        }   
    }   
   
    WSACleanup();   
   
    return( 0 );   
}   
   
/*  
 *          N R E A D  
 */   
int TTCP_Nread( PTEST_BLOCK pTBlk, int count )   
{   
   SOCKADDR_IN from;   
   int len = sizeof(from);   
   register int cnt;   
   
   if( pTBlk->m_Cmd.m_Protocol == IPPROTO_UDP )   
   {   
        cnt = recvfrom( pTBlk->m_Socket_fd,   
               pTBlk->m_pBuf, count, 0,   
               (PSOCKADDR )&from,   
               &len   
               );   
   
      pTBlk->m_Cmd.m_RemoteAddr.sin_addr.s_addr = from.sin_addr.s_addr;   
      pTBlk->m_Cmd.m_RemoteAddr.sin_port = from.sin_port;   
   
        pTBlk->m_numCalls++;   
   }   
   else   
   {   
        if( b_flag )   
      {   
         cnt = TTCP_mread( pTBlk, count );  /* fill buf */   
      }   
      else   
      {   
         cnt = recv( pTBlk->m_Socket_fd, pTBlk->m_pBuf, count, 0 );   
   
         if( cnt == SOCKET_ERROR )   
         {   
            int nError = WSAGetLastError();   
         }   
   
         pTBlk->m_numCalls++;   
      }   
    }   
   
    if( pTBlk->m_Cmd.m_bTouchRecvData && cnt > 0 )   
   {   
        register int c = cnt, sum;   
        register char *b = pTBlk->m_pBuf;   
      sum = 0;   
        while (c--)   
            sum += *b++;   
    }   
   
    return(cnt);   
}   
   
 
  
int TTCP_Nwrite( PTEST_BLOCK pTBlk, int count )   
{   
   register int cnt;   
   
   
   if( pTBlk->m_Cmd.m_nOptWriteDelay && pTBlk->m_numCalls > 0 )   
   {   
      Sleep( pTBlk->m_Cmd.m_nOptWriteDelay );   
   }   
   
   if( pTBlk->m_Cmd.m_Protocol == IPPROTO_UDP )   
   {   
again:   
      cnt = sendto( pTBlk->m_Socket_fd, pTBlk->m_pBuf, count, 0,   
               (PSOCKADDR )&pTBlk->m_Cmd.m_RemoteAddr,   
               sizeof(pTBlk->m_Cmd.m_RemoteAddr)   
               );   
   
      pTBlk->m_numCalls++;   
   
      if( cnt == SOCKET_ERROR && WSAGetLastError() == WSAENOBUFS )   
      {   
         delay(18000);   
         errno = 0;   
         goto again;   
      }   
   }   
   else   
   {   
      cnt = send( pTBlk->m_Socket_fd, pTBlk->m_pBuf, count, 0 );   
      pTBlk->m_numCalls++;   
   }   
   
   return(cnt);   
}   
   
void delay( int us )   
{   
    struct timeval tv;   
   
    tv.tv_sec = 0;   
    tv.tv_usec = us;   
    select( 1, (fd_set *)0, (fd_set *)0, (fd_set *)0, &tv );   
}   
   
   
int TTCP_mread( PTEST_BLOCK pTBlk, unsigned n)   
{   
   char *bufp = pTBlk->m_pBuf;   
   register unsigned    count = 0;   
   register int     nread;   
   
   do   
   {   
        nread = recv( pTBlk->m_Socket_fd, bufp, n-count, 0);   
   
        pTBlk->m_numCalls++;   
   
        if(nread < 0)  {   
            perror("ttcp_mread");   
            return(-1);   
        }   
        if(nread == 0)   
            return((int)count);   
   
        count += (unsigned)nread;   
   
        bufp += nread;   
     }   
      while(count < n);   
   
    return((int)count);   
}   
   
#define END(x)  {while(*x) x++;}   
   
char *outfmt( double b )   
{   
   static char obuf[50];   
   
   switch (fmt) {   
    case 'G':   
        sprintf(obuf, "%.2f GB", b / 1024.0 / 1024.0 / 1024.0);   
        break;   
    default:   
    case 'K':   
        sprintf(obuf, "%.2f KB", b / 1024.0);   
        break;   
    case 'M':   
        sprintf(obuf, "%.2f MB", b / 1024.0 / 1024.0);   
        break;   
    case 'g':   
        sprintf(obuf, "%.2f Gbit", b * 8.0 / 1024.0 / 1024.0 / 1024.0);   
        break;   
    case 'k':   
        sprintf(obuf, "%.2f Kbit", b * 8.0 / 1024.0);   
        break;   
    case 'm':   
        sprintf(obuf, "%.2f Mbit", b * 8.0 / 1024.0 / 1024.0);   
        break;   
    }   
   
    return obuf;   
}   
