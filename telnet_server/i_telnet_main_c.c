/** @file
*/
/****************************************************************************/
/*                                                                          */
/*                 Copyright (C) ERICSSON RADIO SYSTEMS AB, 2004            */
/*                                                                          */
/*              The copyright to the computer program(s) herein is          */
/*              the property of ERICSSON RADIO SYSTEMS AB, Sweden.          */
/*              The program(s) may be used and/or copied only with          */
/*              the written permission from ERICSSON RADIO SYSTEMS          */
/*              AB or in accordance with the terms and conditions           */
/*              stipulated in the agreement/contract under which            */
/*              the program(s) have been supplied.                          */
/*                                                                          */
/****************************************************************************/

/**************************  IDENTIFICATION  ********************************/
/*                                                                          */
/*      Unit:       RTIPGPHR                                                */
/* @(#) ID          i_telnet_main_c.c                                       */
/* @(#) CLASS       n/190 55                                                */
/* @(#) NUMBER      CAA 204 1141                                            */
/* @(#) REVISION    -                                                       */
/* @(#) DATE        2003-10-22                                              */
/* @(#) DESIGNED    EAB/RJK/M                                               */
/* @(#) RESPONSIBLE EAB/RJK/M                                               */
/* @(#) APPROVED    EAB/RJK/MC                                              */
/****************************************************************************/

/******************  HISTORY OF DEVELOPMENT  ********************************/
/*                                                                          */
/* Date    Sign     Mark  Description                                       */
/* ----    ----     ----  -----------                                       */
/* 031022                 Based on telnet.c from UAB.                       */
/*									    */
/****************************************************************************/

/**************************  GENERAL  ***************************************/
/*                                                                          */
/* Purpose: Telnet Server
 */

/**
 * @file i_telnet_main_c.c
 *
 * @addtogroup ModuleDescriptionTelnet Module Description I_TELNET
 *
 * Module documentation for module TELNET
 *
 * @section telnetMod TELNET module
 *
 * Contents of TELNET module:
 *
 * @li @ref telnetModGeneral
 * @li @ref telnetModProcess
 * @li @ref telnetModSourceFileStructure
 * @li @ref telnetModDataStructure
 * @li @ref telnetModStateTransistion
 * @li @ref telnetModFunction
 *
 *
 * @section telnetModGeneral General
 *
 * The module contains a telnet server that interfaces the
 * OSmonitor function.
 *
 *
 * @section telnetModProcess Processes
 * 
 * The module is executed in the IP_TELNET_SERVER process
 * (implemented in file i_telnetproc_c.c) and the
 * IP_TELNET_CH_n processes (implemented in file i_telnet_main_c.c).
 *
 * @section telnetModSourceFileStructure Source File Structure
 *
 * The module consists of three C-modules:
 * @li @c i_telnet_main_c.c
 * @li @c i_telnetproc_c.c
 * @li @c i_telnet_ledit_c.c
 *
 * @section telnetModDataStructure Data Structures
 *
 * @section telnetModStateTransistion State Event Transitions
 *
 * @section telnetModFunction Function
 *
 * When the telnet server process has been started it is ready to
 * receive an incoming telnet connection request from a client.
 * When a client connects, a child process IP_TELNET_CH_n is created.
 * The ownership of the socket is transferred to the child process
 * and the connection is after this handled by the child process.
 *
 * The TIP stack informs the telnet process when data has been received
 * from the client. The data is fetched from the TIP stack (tip_read) and
 * sent on to the OSmonitor function in the RP (signal APPCMD). 
 * Typically the data is an RP command that the client user has entered.
 * The command will be transferred by OSmonitor to the subscriber of the 
 * command, which can be the OS itself or another application process.
 *
 * When OSmonitor receives a reply from the subscriber, it sends this in
 * signal APPTEXT to the telnet process which then transfers the data
 * to the TIP stack (tip_write) for further sending to the client.
 * Typically the data is a print-out requested by the user.
 * In case the TIP stack reports it couldn't send all the data it was
 * requested to send, the telnet process calls tip_write with the
 * reminder when the TIP stack reports it is ready to send again.
 *
 * The telnet server uses the synchronous non-blocking socket          
 * interface to the TIP stack.                                         
 *
 */

/*lint -elib(14)*/
/*lint -elib(46)*/
/*lint -elib(628)*/

/****************************************************************************/
/*                           INCLUDE                                        */
/****************************************************************************/

/*-------------------------  INTERFACES  -----------------------------------*/

/* Own interface */
#include "i_telnet_main_h.h"

/* Module internal interfaces */
#include "i_telnet_ledit_h.h"

/* Module external interfaces */
#include "i_blockproc_h.h"

#ifdef APT_PBOOT_USER
#include "user_pboot.h"
#else
#include "pboot.h"
#endif

#include "tipsock.h"

/*-------------------------  COMMON DECLARATIONS  --------------------------*/


/****************************************************************************/
/*                           LOCAL DECLARATIONS                             */
/****************************************************************************/

/*-------------------------  CONSTANTS  ------------------------------------*/

/* Max attempts to login before a connection gets closed */
#define MAX_LOGIN_TRIES 3

/* Max time to wait for input from user */
#define CLIENT_LOGIN_TIMEOUT 30000  /* 30 seconds */
#define MAX_LOGIN_NAME_LEN 12
#define MAX_PASSWD_LEN 12
#define TMO_SIG 77665544

/* TELNET PROTOCOL numbers. */
#define IAC     ((char) 255)            /* interpret as command: */
#define DONT    ((char) 254)            /* you are not to use option */
#define DO      ((char) 253)            /* please, you use option */
#define WONT    ((char) 252)            /* I won't use option */
#define WILL    ((char) 251)            /* I will use option */
#define SB      ((char) 250)            /* interpret as subnegotiation */
#define SE      ((char) 240)            /* end sub negotiation */

/* telnet options. */
#define TELOPT_ECHO     1  /* echo */
#define TELOPT_SGA      3  /* suppress go ahead */

#define CLIENT_STATE_LOGIN 0
#define CLIENT_STATE_PASSWORD 1
#define CLIENT_STATE_LOGGEDIN 2

/*-------------------------  MACROS  ---------------------------------------*/

/* TELNET_DEBUG must only be defined when compiling for test purposes, */
/* in all other cases the definition must be within comments! */
/*#define TELNET_DEBUG*/ 
#ifdef TELNET_DEBUG
#define DEBUG_PRINT(a) printf a
#else
#define DEBUG_PRINT(a)
#endif

/*-------------------------  TYPE DEF  -------------------------------------*/

typedef struct LOGIN_st
{
  int hostecho_s;
  int peerecho_s;
  int hostsga_s;
} LOGIN_st;


/****************************************************************************/
/*                           LOCAL SUBROUTINES                              */
/****************************************************************************/

static int AddCharacter(char ch, char *userName, int *position, const int length);
static W32 CalcApptextLength(const char *ptr);
static void CloseConnection(const CLIENT_PROC_DATA_st *const client_p);
static OSTIME GetTimeout(void);
static char HandleEscSeq(const char *const string, U32* i);
static U8 NeedLogin(void);
static PROCESS ReqConsoleHandler(void);
static W32 TelnetServerOptions(LOGIN_st* login, int fd, W32 length,
                               const char *const string);
static W8 TelnetWrite(int fd, const char *buf, CLIENT_PROC_DATA_st *client_p);
static void TelnetWriteCmd(int fd, U8 cmd, U8 val);
static int TelnetWriteSimple(int fd, const char *buf, int bufLen);
static Boolean ValidateLogin(const char *userName, const char *passWord, OSUSER *user);
static W32 WrApptextStrSocket(int fd, const char *ptr, CLIENT_PROC_DATA_st *client_p);
static int WrStrSocket(int fd, const char *ptr, CLIENT_PROC_DATA_st *client_p);

/****************************************************************************/
/*                           DATA                                           */
/****************************************************************************/

/*-------------------------  GLOBAL DATA  ----------------------------------*/

/*-------------------------  STATIC DATA  ----------------------------------*/

static SIGSELECT any[1] = {0};



/**************************************************************************
 * Internal function definitions.
 **************************************************************************/

/**
***************************************************************************
* @brief This function encodes ESC sequences and returns them as one
*        character instead.
*
* @param   string    Indata.
* @param   i         Read index.
*
* @return  ESC sequence encoded and returned as one char, 0 if ESC
*          sequence was terminated or unrecognised.
*
***************************************************************************
*/
static char HandleEscSeq(const char *const string, U32* i)
{
  char ch;
  
  ch = string[*i + 1]; /* Get next character */

  if (ch == '[')
  {
    ch = string[*i + 2]; /* Get next character */

    switch (ch)
    {
      case 'A':         /* Arrow up */
        ch = CTRL_P;
        break;
      case 'B':         /* Arrow down */
        ch = CTRL_N;
        break;
      case 'C':         /* Arrow right */
        ch = CTRL_F;
        break;
      case 'D':         /* Arrow left */
        ch = CTRL_B;
        break;
      default:          /* Noone we know of */
        ch = 0;
        break;
    }
  }
  else
    ch = 0;

  if (ch != 0)
    *i += 2;
   
  return ch;
} /* HandleEscSeq */



/**
***************************************************************************
* @brief This function writes a Telnet command to a socket.
*
* @param   fd        File descriptor.
* @param   cmd       Command to write.
* @param   val       Value.
*
***************************************************************************
*/
static void TelnetWriteCmd(int fd, U8 cmd, U8 val)
{
  U8 buf[3];

  buf[0] = (U8) IAC;
  buf[1] = cmd;
  buf[2] = val;

  if (TelnetWriteSimple(fd, (char*) buf, 3) != 0)
  {
    APT_RP_DOTRACE_LEV1(ERROR_ID_R12_1930, (W32) tip_errno,
                        __LINE__, __FILE__,
                        0);

/*  printf("tip_write failed with error code = %d\n", tip_errno);*/
  }
} /* TelnetWriteCmd */



/**
***************************************************************************
* @brief Writes a string to a socket using the SYNCHRONOUS socket interface.
*
* @param   fd        File descriptor.
* @param   buf       Pointer to string.
* @param   bufLen    Length of string.
*
* @return  0 if tip_write is successful,
*          tip_errno if tip_write is unsuccessful.
*
***************************************************************************
*/
static int TelnetWriteSimple(int fd, const char *buf, int bufLen)
{
  static int outOfMemory = 0;
  static int nextPrintout = 0;

  if (tip_write(fd, buf, bufLen) < 0)
  {
    if (tip_errno == (int)TIP_EWOULDBLOCK)
    {
      /* APT_RP_DOTRACE_LEV1(ERROR_ID_R12_1931, (W32) tip_errno, __LINE__, __FILE__, 0);*/
    }
    else if (tip_errno == (int)TIP_ENOMEM)
    { 
      if (nextPrintout == outOfMemory)
      {
        /* Out of memory */
        APT_RP_ERROR(ERROR_ID_R12_1883, (W32) tip_errno);

        /* printf("tip_write failed due to memory shortage for the %d time\n", outOfMemory + 1);*/

        nextPrintout = outOfMemory + 5000;
      }
      outOfMemory++;
    }
    else if (tip_errno == (int)TIP_EINVAL)
    {
      APT_RP_DOTRACE_LEV1(ERROR_ID_R12_1932, (W32) tip_errno,
                          __LINE__, __FILE__,
                          0);
      /*    printf("tip_write failed with EINVAL %d\n", tip_errno);*/
    }
    else
    {
      /* Error in WRITE */
      APT_RP_ERROR(ERROR_ID_R12_1884, (W32) tip_errno);
      /*    printf("tip_write failed with error code = %d\n", tip_errno);*/
    }
    return tip_errno;
  }
  
  return 0;
} /* TelnetWriteSimple */



/**
***************************************************************************
* @brief Writes a string to a socket using the SYNCHRONOUS socket interface.
*        Handles return values from tip_write.
*
* @param   fd        File descriptor.
* @param   buf       Pointer to string.
* @param   client_p  Pointer to client data.
*
* @return  0 if tip_write is successful,
*          1 if tip_write is unsuccessful or cannot send all data.
*
***************************************************************************
*/
static W8 TelnetWrite(int fd, const char *buf, CLIENT_PROC_DATA_st *client_p)
{
  DEBUG_PRINT(("Calling tip_write from TelnetWrite, buflen = %ld\n", client_p -> buflen));

  client_p -> bytesSent = tip_write(fd, buf, client_p -> buflen);
    
  if (client_p -> bytesSent < 0)
  {
    DEBUG_PRINT(("----> bytesSent < 0 in TelnetWrite\n"));

    client_p -> bytesSent = 0;
    
    if (tip_errno == (int)TIP_EWOULDBLOCK)
    {
      /* INETR would block. Get out of here and wait for   */
      /* TIP_SOCKET_CHANGED_EVENT with event TIP_FD_WRITE. */

      DEBUG_PRINT(("----> TIP_EWOULDBLOCK in TelnetWrite\n"));
    }
    else if (tip_errno == (int)TIP_EINVAL)
    {
      APT_RP_DOTRACE_LEV1(ERROR_ID_R12_2100, (W32) tip_errno,
                          __LINE__, __FILE__,
                          0);
    }
    else
    {
      /* Error in WRITE */
      APT_RP_ERROR(ERROR_ID_R12_1885, (W32) tip_errno);
    }
    
    return 1;
  }
  else if ( (W32) client_p -> bytesSent != client_p -> buflen)
  {
    /* We have not sent all data. Get out of here and wait for */
    /* TIP_SOCKET_CHANGED_EVENT with event TIP_FD_WRITE.       */
    
    DEBUG_PRINT(("----> bytesSent = %ld in TelnetWrite\n", client_p -> bytesSent));

    client_p -> bytesSentAcc += client_p -> bytesSent;
    client_p -> buflen = client_p -> buflen - client_p -> bytesSent;

    DEBUG_PRINT(("----> New buflen = %ld in TelnetWrite\n", client_p -> buflen));

    return 1;
  }
  else
  {
    /* We have sent all data */

    DEBUG_PRINT(("++++> bytesSent = %ld in TelnetWrite\n", client_p -> bytesSent));
    
    client_p -> bytesSentAcc = 0;
    client_p -> bytesSent = 0;
    client_p -> buflen = 0;
    client_p -> outBuffer_p = client_p -> startOfOutBuffer_p;
  }
  
  return 0;
} /* TelnetWrite */



/**
***************************************************************************
* @brief Writes a string to a socket. Converts line feed to carriage
*        return + line feed.
*
* @param   fd        File descriptor.
* @param   ptr       Pointer to string.
* @param   client_p  Pointer to client data.
*
* @return  0 if TelnetWrite is successful,
*          tip_errno if TelnetWrite is unsuccessful.
*
***************************************************************************
*/
static int WrStrSocket(int fd, const char *ptr, CLIENT_PROC_DATA_st *client_p)
{
  static char buffer[BUFFER_SIZE];
  char buf[SZ];
  W32 tnError = FALSE;

  client_p -> buflen = 0;

  for ( ; *ptr; ptr++)
  {
    if (*ptr == '\f') /* Form feed: clear screen and start from top. */
    {
      /* Flush the buffer */
      if (client_p -> buflen)
      {
        DEBUG_PRINT(("Calling TelnetWrite #1, buffer = \"%s\"\n", buffer));

        if (TelnetWrite(fd, buffer, client_p) != 0)
        {
          tnError = TRUE;
          break;
        }
      }
      
      client_p -> buflen = strlen("\033[2J\033[h");
      if (TelnetWrite(fd, "\033[2J\033[h", client_p) != 0)
      { /* Clears the screen.   */  
        tnError = TRUE;
        break;
      }
      
      sprintf(buf, "\033[%-d;%-dH", 0, 0); /* Top left corner.     */
      client_p -> buflen = strlen(buf);
      if (TelnetWrite(fd, buf, client_p) != 0)
      {
        tnError = TRUE;
        break;
      }
      
      client_p -> buflen = 0;
    }
    else if (*ptr == '\n')
    {
      /* New line */
      /* Add characters to buffer */
      buffer[client_p -> buflen] = '\r';
      client_p -> buflen++;
      buffer[client_p -> buflen] = '\n';
      client_p -> buflen++;
    }
    else
    {
      /* Add character to buffer */
      buffer[client_p -> buflen] = *ptr;
      client_p -> buflen ++;

      /* Flush buffer if buffer is full */
      if (client_p -> buflen >= BUFFER_SIZE)
      {
        DEBUG_PRINT(("Calling TelnetWrite #2, buffer = \"%s\"\n", buffer));

        if (TelnetWrite(fd, buffer, client_p) != 0)
        {
          tnError = TRUE;
          break;
        }
        client_p -> buflen = 0;
      }
    }
  }
  if (tnError == FALSE)
  { /* no errors occured... */

    /* Flush the buffer */
    if (client_p -> buflen)
    {
      DEBUG_PRINT(("Calling TelnetWrite #3, buffer = \"%s\"\n", buffer));

      if (TelnetWrite(fd, buffer, client_p) != 0)
      {
        tnError = TRUE;
      }
      client_p -> buflen = 0;
    }
  }
  if (tnError)
  {
    return tip_errno;
  }

  return 0;  
} /* WrStrSocket */



/**
***************************************************************************
* @brief Writes a string (from signal APPTEXT) to a socket. 
*        Converts line feed to carriage return + line feed.
*
* @param   fd        File descriptor.
* @param   ptr       Pointer to string.
* @param   client_p  Pointer to client data.
*
* @return  0 if TelnetWrite is successful,
*          1 if TelnetWrite is unsuccessful.
*
***************************************************************************
*/
static W32 WrApptextStrSocket(int fd, const char *ptr, CLIENT_PROC_DATA_st *client_p)
{
  char buf[SZ];
  W32 tnError = FALSE;
  
  for ( ; *ptr; ptr++)
  {
    if (*ptr == '\f') /* Form feed: clear screen and start from top. */
    {
      /* Flush the buffer */
      if (client_p -> buflen)
      {
        if (TelnetWrite(fd, client_p -> startOfOutBuffer_p, client_p) != 0)
        {
          tnError = TRUE;
          break;
        }
      }
      
      client_p -> buflen = strlen("\033[2J\033[h");
      if (TelnetWrite(fd, "\033[2J\033[h", client_p) != 0)
      { /* Clears the screen.   */  
        tnError = TRUE;
        break;
      }
      
      sprintf(buf, "\033[%-d;%-dH", 0, 0); /* Top left corner.     */
      client_p -> buflen = strlen(buf);
      if (TelnetWrite(fd, buf, client_p) != 0)
      {
        tnError = TRUE;
        break;
      }
      
      client_p -> buflen = 0;
    }
    else if (*ptr == '\n')
    {
      /* New line */
      /* Add characters to buffer */

      *(client_p -> outBuffer_p) = '\r';
      client_p -> outBuffer_p++;
      client_p -> buflen++;
      *(client_p -> outBuffer_p) = '\n';
      client_p -> outBuffer_p++;
      client_p -> buflen++;
      
      client_p -> ptrInApptext++;

      /* Flush buffer if buffer is full */
      if (client_p -> buflen >= (BUFFER_SIZE - 1) )
      {
        if (TelnetWrite(fd, client_p -> startOfOutBuffer_p, client_p) != 0)
        {
          tnError = TRUE;
          break;
        }
      }
    }
    else
    {
      /* Add character to buffer */
      *(client_p -> outBuffer_p) = *ptr;
      client_p -> outBuffer_p++;
      client_p -> buflen++;

      client_p -> ptrInApptext++;

      /* Flush buffer if buffer is full */
      if (client_p -> buflen >= (BUFFER_SIZE - 1) )
      {
        if (TelnetWrite(fd, client_p -> startOfOutBuffer_p, client_p) != 0)
        {
          tnError = TRUE;
          break;
        }
      }
    }
  }
  if (tnError == FALSE)
  { /* no errors occured... */

    /* Flush the buffer */
    if (client_p -> buflen)
    {
      if (TelnetWrite(fd, client_p -> startOfOutBuffer_p, client_p) != 0)
      {
        tnError = TRUE;
      } 
    }
  }

  return tnError;
} /* WrApptextStrSocket */



/**
***************************************************************************
* @brief This function negotiates telnet server options with a
*        telnet client.
*
* @param   login     Login data.
* @param   fd        File descriptor.
* @param   length    Length of data.
* @param   string    Pointer to string.
*
* @return  i
*
***************************************************************************
*/
/* THIS FUNCTION DOES NOT SEEM TO DO EXACTLY WHAT IT IS SUPPOSED TO DO.
 * It only handles one option and other options provided in the same string
 * are not handled.
 */
static W32 TelnetServerOptions(LOGIN_st* login, int fd, W32 length, const char *const string)
{
  char cmd;
  char opt;
  W32  found = 0;
  W32  i = 0;

  /* Look at one telnet option at a time. */
  while((length >= 3) && (string[0] == IAC) && (found == 0))
  {
    cmd = string[1];
    opt = string[2];
      
    /*********** ECHO ***********/
    if(opt == TELOPT_ECHO)
    {
      switch(cmd)
      {
        case DO:
          if (login->hostecho_s == 0)
          {
            login->hostecho_s = 1;
            TelnetWriteCmd(fd, (U8)WILL, TELOPT_ECHO);
          }
          break;

        case DONT:
          if (login->hostecho_s == 1)
          {
            login->hostecho_s = 0;
            TelnetWriteCmd(fd, (U8)WONT, TELOPT_ECHO);
          }
          break;

        case WILL:
          if (login->peerecho_s == 0)
          {
            login->peerecho_s = 1;
            TelnetWriteCmd(fd, (U8)DO, TELOPT_ECHO);
          }
          break;

        case WONT:
          if (login->peerecho_s == 1)
          {
            login->peerecho_s = 0;
            TelnetWriteCmd(fd, (U8)DONT, TELOPT_ECHO);
          }
          break;
              
        default:
          break;
      }
    }
    else /* not ECHO */
    {
      switch(cmd)
      {
        /* Sender wants receiver to enable option. Receiver says NO. */
        case DO:
          if(opt == TELOPT_SGA)
          {
            if (login->hostsga_s == 0)
            {
              login->hostsga_s = 1;
              TelnetWriteCmd(fd, (U8)WILL, (U8) opt);
            }
          }
          else
            TelnetWriteCmd(fd, (U8)WONT, (U8) opt);
          break;

          /* Sender wants to enable option. Receiver says NO. */
        case WILL:
          TelnetWriteCmd(fd, (U8)DONT, (U8) opt);
          break;
          
          /* Sender wants to disable option. Receiver must say OK. */
        case WONT:
          TelnetWriteCmd(fd, (U8)DONT, (U8) opt);
          break;
          
          /* Sender wants receiver to disable option. Receiver must say OK. */
        case DONT:
          if(opt == TELOPT_SGA)
          {
            login->hostsga_s = 0;
          }
          TelnetWriteCmd(fd, (U8)WONT, (U8) opt);
          break;
          
        default:
          break;
      }
    }
    
    /* Skip to next option. */
    if(cmd == SB) /* Suboption ? */
    {
      for(i = 3; i < length; i++)
        if(string[i] == SE)
        {
          i++;
          break;
        }
    }
    else
    {
      i = 3;
    }
    found = 1;
  }
  
  return i;
} /* TelnetServerOptions */



/**
***************************************************************************
* @brief This function requests a console handler from the OSmonitor.
*
* @return  OSE PID for the console handler.
*
***************************************************************************
*/
static PROCESS ReqConsoleHandler(void)
{
  union SIGNAL *sig_p;
  PROCESS conh_ = 0;
  PROCESS OSmonitor_;
  static SIGSELECT appctrl[] = {1, APPCTRL};

  if (!hunt("OSmonitor", 0, &OSmonitor_, 0) )
  {
    return 0;
  }
  
  sig_p = OS_alloc(APPCTRL_S, APPCTRL);
  sig_p->appctrl.data = APPCTRL_INIT;
  OS_send(&sig_p, OSmonitor_);
  sig_p = OS_receive(appctrl);
   
  switch (sig_p->appctrl.data) 
  {
    case APPCTRL_READY:
      conh_ = OS_sender(&sig_p);
      break;
      
    default:
      break;
  }
  OS_free(&sig_p);
  
  return conh_;
} /* ReqConsoleHandler */



/**
***************************************************************************
* @brief This function closes a connection, returns the socket
*        descriptor and kills the calling process.
*
* @param   client_p  Pointer to client data.
*
***************************************************************************
*/
static void CloseConnection(const CLIENT_PROC_DATA_st *const client_p)
{
  SIGNAL *sig_p;
  
  if (tip_shutdown(client_p -> socketId, (int)TIP_SHUT_RDWR) < 0)
  {
    /* Error when shutting down socket. */
    APT_RP_ERROR(ERROR_ID_R12_1886, (W32) tip_errno);
  }

  if (tip_close(client_p -> socketId) < 0)
  {
    /* Error when closing socket. */
    APT_RP_ERROR(ERROR_ID_R12_1887, (W32) tip_errno);
  }

#ifndef SOFTKERNEL  
  /*
   * The socket_proc_terminate function frees the process context
   * variables used by the specified process and shall be called
   * when a process that has been using the TIP-stack socket interface
   * is to be terminated. Note! No more socket calls has to be made by
   * the process in question after that this function is called.
   * Then it will be inserted again in the context array.
   */
  socket_proc_terminate(current_process() );
#endif
  
  /* Tell the Telnet server process that the client process is closed */
  sig_p = OS_alloc(MISTOPCLIENTR_S, MISTOPCLIENTR);
  sig_p->mistopclientr.clientInd = client_p -> clientInd;
  OS_send(&sig_p, client_p -> serverPid);

  /* Kill the telnet client process */
  kill_proc(current_process());

} /* CloseConnection */



/**
***************************************************************************
* @brief Verifies user name and password for a user.
*
* @param   userName   Login name of user.
* @param   passWord   Password.
* @param   user       OSE user number.
*
* @return  0 if OK, an error code otherwise.
*
***************************************************************************
*/
static Boolean ValidateLogin(const char *userName, const char *passWord,
                             OSUSER *user)
{
#ifdef VALID_LOGIN
  return zzvalidateLogin(userName, passWord, user);
#else
  (void) user;
  
  return ((Boolean)((strcmp(userName, "razor") == 0) &&
                    (strcmp(passWord, "assar") == 0)));
#endif
} /* ValidateLogin */



/**
***************************************************************************
* @brief Calculates length of data text in APPTEXT.
*
* @param   ptr       Pointer to string.
*
* @return  Length of apptext.text.
*
***************************************************************************
*/
static W32 CalcApptextLength(const char *ptr)
{
  W32 i = 0;
  
  for ( ; *ptr; ptr++)
  {
    i++;
  }
  return i;
} /* CalcApptextLength */



/**
***************************************************************************
* @brief Checks if the user needs to login.
*
* @return  TRUE if the user needs to login,
*          FALSE otherwise.
*
***************************************************************************
*/
static U8 NeedLogin(void)
{
  char buffer[20];

  /* Check if the user needs to login. */
#ifdef APT_PBOOT_USER
  if((user_pboot_param_get("telnet_loginenable", buffer, 20) == 0) &&
     (strcmp(buffer, "yes") == 0))
#else
  if((pboot_param_get("telnet_loginenable", buffer, 20) == 0) &&
     (strcmp(buffer, "yes") == 0))
#endif
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
} /* NeedLogin */



/**
***************************************************************************
* @brief Get login timeout value.
*
* @return  Timeout value.
*
***************************************************************************
*/
static OSTIME GetTimeout(void)
{
  char *parameter_p, buffer[20];
  OSTIME timeOut;
  
  /* Request a timer used to supervise inactive users */
#ifdef APT_PBOOT_USER
  if(user_pboot_param_get("telnet_autologout", buffer, 20) == 0)
#else
  if(pboot_param_get("telnet_autologout", buffer, 20) == 0)
#endif    
  {
    /* Time found in pboot parameter. */
    timeOut = (OSTIME) strtol(buffer,0,0);
  }
  else
  {
    /* No pboot parameter found. */

    /* Check environment to find the parameter. */
    parameter_p = get_env(get_bid(current_process()), "telnet_autologout");
    
    if(parameter_p)
    {
      timeOut = (OSTIME) strtol(buffer,0,0);
      
      OS_free((union SIGNAL**) buffer);
    }
    else
    {
      /* If no environment and no pboot parameter set, dont ever timeout. */
      timeOut = 0;
    }
  }

  return timeOut;
} /* GetTimeout */



/**
***************************************************************************
* @brief Add character to a string.
*
* @return  1 if CR encountered or maximum string length reached,
*          0 otherwise.
*
***************************************************************************
*/
static int AddCharacter(char ch, char *userName, int *position, const int length)
{
  if((ch != CR) && (*position < length))
  {
    userName[*position++] = ch;
    
    return 0;
  }
  else
  {
    userName[*position] = '\0';
    
    *position = 0;
    
    return 1;
  }
} /* AddCharacter */


 
/**************************************************************************
 * External function definitions.
 **************************************************************************/

/**
***************************************************************************
* @brief This is a child process forked by the telnet server process
*        to handle one telnet connection.
*
***************************************************************************
*/
APT_RP_PROCESS(IP_TELNET_CH)
{
  CLIENT_PROC_DATA_st clientProcData;
  U8 clientState;
  
  char ch;
  int position = 0;
  char userName[MAX_LOGIN_NAME_LEN + 1];
  char password[MAX_PASSWD_LEN + 1];
  U8 loginTries = 0;

  W32 i;
  W32 noOfBufCmds = 0;
  W32 cmdIdx = 0;
  W32 isCmdRunning = FALSE;

  char *data_p;
  int dataLength;
  int commandSize;

  union SIGNAL* signal_p;
  union SIGNAL* savedSig_p = NULL;
  union SIGNAL* outSig_p;
  union SIGNAL* bufCmds[MAX_BUF_CMDS];

  PROCESS       conh_ = 0;             /* Console handler PID */
  LOGIN_st      login;
  cmd_hist*     root;
  CANCEL_INFO   canTmo;
  OSTIME        timeOut;
  char          syncBuf[1500];

  static SIGSELECT startClientReq[] = {1,MISTARTCLIENT};

  /*
   * Wait for start signal from IP_TELNET_SERVER
   */
  signal_p = OS_receive(startClientReq);

  /*
   * Save the Telnet Server process ID,
   * the client socket ID and client individual
   */
  clientProcData.serverPid = OS_sender(&signal_p);
  clientProcData.socketId = (int) signal_p->mistartclient.clientSockId;
  clientProcData.clientInd = signal_p->mistartclient.clientInd;
  clientProcData.buflen = 0;
  clientProcData.bytesSent = 0;
  clientProcData.bytesSentAcc = 0;
  clientProcData.ptrInApptext = 0;
  clientProcData.apptextLength = 0;
  clientProcData.outBuffer_p = NULL;
  clientProcData.startOfOutBuffer_p = NULL;

  OS_free(&signal_p);

  /* Check the socket validity. */
  if(!clientProcData.socketId)
  {
    /* Received socket is not ok. */
    APT_RP_ERROR(ERROR_ID_R12_1888, (W32) clientProcData.socketId);
  }

  /*
   * Subscribe on socket closed events, socket read events and 
   * socket write events
   */
  if (tip_asyncselect(clientProcData.socketId, TIP_FD_CLOSE | TIP_FD_READ | TIP_FD_WRITE) < 0) /*lint !e641 !e655*/
  {
    APT_RP_ERROR(ERROR_ID_R12_1889, (W32) tip_errno);
  }

  /* Reset login structure and make server echo by default. */
  login.hostecho_s = 1;
  login.peerecho_s = 0;
  login.hostsga_s = 0;

  /* Send initial telnet setup commands to client. */
  TelnetWriteCmd(clientProcData.socketId, (U8)WILL, TELOPT_SGA);
  if(tip_errno !=0)
  {
    CloseConnection(&clientProcData);
  }
  TelnetWriteCmd(clientProcData.socketId, (U8)WILL, TELOPT_ECHO);
  if(tip_errno !=0)
  {
    CloseConnection(&clientProcData);
  }
  /* Setup command history. */
  root = (cmd_hist *) OS_alloc(sizeof(cmd_hist), 0);
  memset(root, 0, sizeof(cmd_hist));
  root->fd = clientProcData.socketId;
  root->output = TelnetWriteSimple;

  /* Initialize buffer for commands */
  for (i=0; i<MAX_BUF_CMDS; i++)
  {
    bufCmds[i] = NULL;
  }

  /* Check if login is needed. */
  if(NeedLogin())
  {
    /* Login needed. */
    clientState = CLIENT_STATE_LOGIN;

    timeOut = CLIENT_LOGIN_TIMEOUT;

    /* Write login to screen. */
    if (WrStrSocket(clientProcData.socketId, "\nlogin: ", &clientProcData) != 0)
    {
/*    printf("tip_write failed with error code = %d\n", tip_errno);*/
      CloseConnection(&clientProcData);
    }
  }
  else
  {
    /* Login not needed. */
    clientState = CLIENT_STATE_LOGGEDIN;
    
    /* Get the timeout value. */
    timeOut = GetTimeout();
    
    /* Get the console process. */
    conh_ = ReqConsoleHandler();
    
    /* Write welcome to screen. */
    if (WrStrSocket(clientProcData.socketId,
                    "\n\nWelcome to the RAZOR Telnet shell, type 'help' for a\n\r"
                    "list of available commands, 'exit' to end the session.",
                    &clientProcData) != 0)
    {
/*    printf("tip_write failed with error code = %d\n", tip_errno);*/
      CloseConnection(&clientProcData);
    }
  }
  
  /* Request timeout if needed. */
  if(timeOut)
  {
    APT_RP_FREQUEST_TMO(&canTmo, timeOut, current_process(), TMO_SIG);
  }
  
  /* Enter main loop. */
  while(1)                                              /*lint !e716*/
  {
    signal_p = OS_receive(any);

    switch(signal_p->sig_no)
    {
      case TIP_SOCKET_CHANGED_EVENT:
      {
        if (signal_p->tip_socket_changed_event.event == (U16)TIP_FD_CLOSE)
        {
          if ((long)signal_p->tip_socket_changed_event.socket == clientProcData.socketId)
          {
            if(signal_p != NULL)
            {
              OS_free(&signal_p);
            }
            CloseConnection(&clientProcData);
          }
          else
          {
            /* Bad socket */
            APT_RP_ERROR(ERROR_ID_R12_1890,
                         signal_p->tip_socket_changed_event.socket);
          }
          /* The socket was closed by peer */
        }
        else if (signal_p->tip_socket_changed_event.event == (U16)TIP_FD_WRITE)
        {
          /* The socket is writable */
          if (clientProcData.buflen)
          {
            /* There is still data in the buffer to send */
            if (TelnetWrite(clientProcData.socketId,
                            (clientProcData.startOfOutBuffer_p + clientProcData.bytesSentAcc),
                            &clientProcData) != 0)
            {
              if ( (tip_errno == (int)TIP_EWOULDBLOCK) OR (tip_errno == (int)TIP_ESUCCESS) )
              {
                /* INETR would block or INETR couldn't send what we requested. */
                /* Keep savedSig_p. Get out of here and wait for               */
                /* TIP_SOCKET_CHANGED_EVENT with event TIP_FD_WRITE.           */
                break;
              }
              else
              {
                /* INETR has reported an error we cannot handle. Close connection. */
                if (clientProcData.outBuffer_p != NULL)
                {
                  OS_free((SIGNAL**) &(clientProcData.outBuffer_p));
                }
                if (savedSig_p != NULL)
                {
                  OS_free(&savedSig_p);
                }
                if(signal_p != NULL)
                {
                  OS_free(&signal_p);
                }
                CloseConnection(&clientProcData);
              }
            } 
          }
          
          if ( (clientProcData.buflen == 0) AND
               (savedSig_p != NULL) )
          {
            if (clientProcData.ptrInApptext < clientProcData.apptextLength)
            {
              /* There is still data in APPTEXT to send */

              DEBUG_PRINT(("Calling WrApptextStrSocket after TIP_FD_WRITE, apptext = \"%s\"\n, clientProcData.bytesSent = %ld\n", savedSig_p->apptext.text, clientProcData.bytesSent));

              if (WrApptextStrSocket(clientProcData.socketId,
                                     &(savedSig_p -> apptext.text[clientProcData.ptrInApptext]),
                                     &clientProcData) != 0)
              {
                if (tip_errno == (int)TIP_ESUCCESS)
                {
                  /* INETR couldn't send what we requested. Keep savedSig_p. */
                  /* Get out of here and wait for                      */
                  /* TIP_SOCKET_CHANGED_EVENT with event TIP_FD_WRITE. */
                  
                  DEBUG_PRINT(("----> TIP_ESUCCESS after TIP_FD_WRITE\n"));
                  break;
                }
                else if (tip_errno == (int)TIP_EWOULDBLOCK)
                {
                  /* INETR would block. Keep savedSig_p.               */
                  /* Get out of here and wait for                      */
                  /* TIP_SOCKET_CHANGED_EVENT with event TIP_FD_WRITE. */

                  DEBUG_PRINT(("----> TIP_EWOULDBLOCK after TIP_FD_WRITE\n"));
                  break;
                }
                else
                {
                  /* INETR has reported an error we cannot handle. Close connection. */
                  if (clientProcData.outBuffer_p != NULL)
                  {
                    OS_free((SIGNAL**) &(clientProcData.outBuffer_p));
                  }
                  if (savedSig_p != NULL)               /*lint !e774*/
                  {
                    OS_free(&savedSig_p);
                  }
                  if(signal_p != NULL)
                  {
                    OS_free(&signal_p);
                  }
                  CloseConnection(&clientProcData);
                }
              }
            }

            if (clientProcData.outBuffer_p != NULL)
            {
              OS_free((SIGNAL**) &(clientProcData.outBuffer_p));
              clientProcData.outBuffer_p = NULL;
              clientProcData.startOfOutBuffer_p = NULL;
            }
            
            if (savedSig_p != NULL)
            {
              /* Check if confirm is requested. */
              if (savedSig_p->apptext.ctrl & APPTEXT_ACK)
              {
/*              printf("APPCTRL_ACK sent after TIP_FD_WRITE handled\n");*/
                
                /* ACK (flow control) requested. */
                outSig_p = OS_alloc(APPCTRL_S, APPCTRL);
                outSig_p->appctrl.data = APPCTRL_ACK;
                OS_send(&outSig_p, conh_);
              }

              /* Check if command is completed.
                 Note: due to a probable fault in consolehandler, when executing
                 help command "?" the apptext signal with APPTEXT_RDY is not
                 send when command is completed. For that reason it's necessary
                 to additionally check if prompt text without RDY flag is
                 received, and if so behave as if command is completed. */
              if ((savedSig_p->apptext.ctrl & APPTEXT_RDY)
                  OR
                  ((clientProcData.apptextLength == 9)
                   AND
                   (strcmp(savedSig_p->apptext.text, "\r\nOSmon> ") == 0))
                  OR
                  ((clientProcData.apptextLength == 8)
                   AND
                   (strcmp(savedSig_p->apptext.text, "\r\nOSmon>") == 0)))
              {
                /* The command has been completed (all output signals have
                   been received). */

                /* Unset flag indicating ongoing command */
                isCmdRunning = FALSE;

                /* Check the if there are any buffered (awaiting) commands
                   and if so execute the next command. */
                if (noOfBufCmds > 0)
                {
                  /* Check if previous command was not the last one */
                  if (cmdIdx < noOfBufCmds)
                  {
                    /* For safety check if stored command is not NULL */
                    if (bufCmds[cmdIdx] != NULL)
                    {
                      commandSize = strlen((char*)(bufCmds[cmdIdx]->appcmd.cmd));

                      /* Print the command on the telnet console */
                      if (TelnetWriteSimple(clientProcData.socketId,
                                            (char*)(bufCmds[cmdIdx]->appcmd.cmd),
                                            commandSize) != 0)
                      {
                        APT_RP_DOTRACE_LEV1(ERROR_ID_G12B_RTIPGPHR_6, (W32)tip_errno,
                                            __LINE__, __FILE__,
                                            0);
                      }

                      /* Send command to OSmonitor */
                      OS_send(&(bufCmds[cmdIdx]), conh_);

                      /* Set flag indicating ongoing command */
                      isCmdRunning = TRUE;

                      /* Set command index to point to the next command */
                      cmdIdx++;
                    }
                    else
                    {
                      /* Stored command is NULL, this should never happen and
                         indicates internal fault in RTIPGPHR. Report RPTERROR
                         and free all remaining stored commands (if any)
                         to avoid memory leaks. */
                      APT_RP_ERROR2(ERROR_ID_G12B_RTIPGPHR_8, 0,
                                    __LINE__, __FILE__,
                                    2,           /* Num of extra parameters 0-65535 */
                                    cmdIdx,
                                    noOfBufCmds);

                      for (cmdIdx = 0; cmdIdx < MAX_BUF_CMDS; cmdIdx++)
                      {
                        if (bufCmds[cmdIdx] != NULL)
                        {
                          OS_free(&(bufCmds[cmdIdx]));
                        }
                      }

                      noOfBufCmds = 0;
                      cmdIdx = 0;
                    }
                  }
                  else
                  {
                    /* No more commands to execute, clear the variables used
                       for handling of buffered commands */
                    noOfBufCmds = 0;
                    cmdIdx = 0;
                  }
                }
              }
           
              OS_free(&savedSig_p);
              savedSig_p = NULL;
            }
          }
        }
        else if (signal_p->tip_socket_changed_event.event == (int)TIP_FD_READ)
        {
          data_p = &syncBuf[0];

          if ((dataLength = tip_read(clientProcData.socketId, data_p, 1500)) < 0)
          {
            /* READ error */
            APT_RP_ERROR(ERROR_ID_R12_1891, (W32) tip_errno);
            break;
          }
          
          /* TELNET options to server from client. */
          if((dataLength >= 3) && (data_p[0] == IAC))
          {
            i = TelnetServerOptions(&login, clientProcData.socketId, (W32) dataLength, data_p);

            /*
             * Since TelnetServerOptions does not seem to handle 
             * suboptions correctly, avoid processing the suboptions
             * as printable characters
             */
            i = (W32) dataLength;
          }
          else
          {
            i = 0;
          }
          
          /* Process received data as a character stream. */
          while(i < (W32) dataLength)
          {
            /* Get character. */
            ch = data_p[i++];
              
            /* Check what to do with the data. */
            switch(clientState)
            {
              case CLIENT_STATE_LOGIN:
                if(AddCharacter(ch, userName, &position, MAX_LOGIN_NAME_LEN))
                {
                  /* Prompt user for password. */
                  if (WrStrSocket(clientProcData.socketId, "\nPassword: ",
                                  &clientProcData) == 0)
                  {
                    /* Got username, need password. */
                    clientState = CLIENT_STATE_PASSWORD;
                  }
                }
                
                /* Echo the character. */
                if (TelnetWriteSimple(clientProcData.socketId, &ch, 1) != 0)
                {
                  APT_RP_DOTRACE_LEV1(ERROR_ID_R12_2101, (W32) tip_errno,
                                      __LINE__, __FILE__,
                                      0);

/*                printf("TelnetWriteSimple failed at echo, with error code = %d\n", tip_errno);*/
                }
                
                break;
                  
              case CLIENT_STATE_PASSWORD:
                if(AddCharacter(ch, password, &position, MAX_PASSWD_LEN))
                {
                  if(ValidateLogin(userName, password, 0)) /*lint !e645*/
                  {
                    /* Login accepted. */
                    
                    timeOut = GetTimeout();
                    
                    clientState = CLIENT_STATE_LOGGEDIN;
                    
                    /* Get the console process. */
                    conh_ = ReqConsoleHandler();
                    
                    /* Write welcome to screen. */
                    if (WrStrSocket(clientProcData.socketId,
                                    "\n\nWelcome to the RAZOR Telnet shell, type 'help' for a\n\r"
                                    "list of available commands, 'exit' to end the session.",
                                    &clientProcData) != 0)
                    {
/*                    printf("TelnetWrite failed at pwd, with error code = %d\n", tip_errno);*/
                    }
                  }
                  else
                  {
                    /* Login denied. */
                    
                    WrStrSocket(clientProcData.socketId, "\nLogin incorrect\n",
                                &clientProcData); /*lint !e534*/ /* Safe??? */
                    
                    loginTries++;
                    if(loginTries < MAX_LOGIN_TRIES)
                    {
                      clientState = CLIENT_STATE_LOGIN;
                      
                      WrStrSocket(clientProcData.socketId, "\nlogin: ",
                                  &clientProcData);         /*lint !e534*/ /* Safe??? */
                    }
                    else
                    {
                      CloseConnection(&clientProcData);
                    }
                  }
                }
                
                break;
                
              case CLIENT_STATE_LOGGEDIN:
                /* Look for VT 100 command/control sequence. */
                if(ch == ESC)
                {
                  /* Preprocess the control sequence. */
                  ch = HandleEscSeq(data_p, &i);
                }

                /* Call command line edit function. Returned size includes
                   terminating character (in case size is > 0). */
                commandSize = itelnet_LEdit(root, (U8)ch, isCmdRunning);

                /* Check if command completed. */
                if(commandSize > 0)
                {
                  /*
                  ** A command has been completed, check for 'exit' before
                  ** sending it to the console handler.
                  */
                  if((root->cmdbuf[0] == 'e') && !strcmp(root->cmdbuf, "exit"))
                  {
                    /* User wants to quit. */
                    WrStrSocket(clientProcData.socketId, "\nlogout\n",
                                &clientProcData); /*lint !e534*/ /* Safe??? */
                    if(signal_p != NULL)
                    {
                      OS_free(&signal_p);
                    }

                    /* Free remaining buffered commands if any */
                    if (noOfBufCmds > 0)
                    {
                      /* Free all remaining commands in the buffer */
                      while (cmdIdx < noOfBufCmds)
                      {
                        if (bufCmds[cmdIdx] != NULL)
                        {
                          OS_free(&(bufCmds[cmdIdx]));
                        }
                        cmdIdx++;
                      }
                    }

                    CloseConnection(&clientProcData);
                  }

                  /* Check if another command is not already running */
                  if (isCmdRunning == FALSE)
                  { 
                    /* Send complete command to OSmonitor. */
                    outSig_p = OS_alloc(APPCMD_S + commandSize, APPCMD); /*lint !e737*/
                    strcpy((char*) outSig_p->appcmd.cmd, root->cmdbuf);
                    OS_send(&outSig_p, conh_);

                    /* Set flag indicating ongoing command */
                    isCmdRunning = TRUE;
                  }
                  else
                  {
                    /* Another command is already running. This can happen in
                       case of copy-paste of multiple commands into the telnet
                       console. Store the new command, it will be executed after
                       all preceding commands are finished. */

                    /* Make sure there is a place in the buffer for commands */
                    if (noOfBufCmds < MAX_BUF_CMDS)
                    {
                      /* Allocate the signal for the command and store it */
                      bufCmds[noOfBufCmds] = OS_alloc(APPCMD_S + commandSize, APPCMD); /*lint !e737*/
                      strcpy((char*)(bufCmds[noOfBufCmds]->appcmd.cmd), root->cmdbuf);
                      noOfBufCmds++;
                    }
                    else
                    {
                      APT_RP_DOTRACE_LEV1(ERROR_ID_G12B_RTIPGPHR_4, 0,
                                          __LINE__, __FILE__,
                                          0);
                    }
                  }

                  /* Clear command buffer after copying it. */
                  root->cmdbuf[0] = '\0';
                }
                else if(ch == CTRL_C)
                {
                  /* Abort job. */
                  outSig_p= OS_alloc(APPCTRL_S, APPCTRL);
                  outSig_p->appctrl.data = APPCTRL_ABORT;
                  OS_send(&outSig_p, conh_);
                }
                
                break;
                
              default:
                
                break;
            }
          }
          
          /* Check if timeout is active. */
          if(timeOut)
          {
            /* Timeout is restarted. */
            APT_RP_RESET_TMO(&canTmo, timeOut);
          } 
        }
        else
        {
          /* Unexpected event */
          APT_RP_ERROR(ERROR_ID_R12_1892,
                       signal_p->tip_socket_changed_event.event);
        }
        break;
      }

      /* Request to print text from monitor application. */
      case APPTEXT:
      {
        /* Check if logged in. */
        if(clientState == CLIENT_STATE_LOGGEDIN)
        {
          /* Client logged in. */
          if (savedSig_p != NULL)
          {
            DEBUG_PRINT(("Calling WrApptextStrSocket after APPTEXT, apptext = \"%s\"\n, clientProcData.bytesSent = %ld\n", savedSig_p->apptext.text, clientProcData.bytesSent));
          }

          clientProcData.ptrInApptext = 0;
          clientProcData.buflen = 0;
          clientProcData.apptextLength = CalcApptextLength(signal_p -> apptext.text);

          /* Allocate memory for the out buffer used when sending data to the client */
          clientProcData.outBuffer_p = (char*) OS_alloc(sizeof(char) * BUFFER_SIZE, 0);
          clientProcData.startOfOutBuffer_p = clientProcData.outBuffer_p;
          
          /* Print the message to the client. */          
          if (WrApptextStrSocket(clientProcData.socketId,
                                 signal_p->apptext.text, &clientProcData) != 0)
          {
            if (tip_errno == (int)TIP_ESUCCESS)
            {
              /* INETR couldn't send what we requested. Save signal. */
              /* Get out of here and wait for                      */
              /* TIP_SOCKET_CHANGED_EVENT with event TIP_FD_WRITE. */
              
              DEBUG_PRINT(("----> TIP_ESUCCESS after APPTEXT\n"));
              savedSig_p = signal_p;
              signal_p = NULL; /* to prevent it from being freed at the end */
              break;
            }
            else if (tip_errno == (int)TIP_EWOULDBLOCK)
            {
              /* INETR would block. Save signal.                   */
              /* Get out of here and wait for                      */
              /* TIP_SOCKET_CHANGED_EVENT with event TIP_FD_WRITE. */
              
              DEBUG_PRINT(("----> TIP_EWOULDBLOCK after APPTEXT\n"));
              savedSig_p = signal_p;
              signal_p = NULL; /* to prevent it from being freed at the end */
              break;
            }
            else
            {
              
              /* INETR has reported an error we cannot handle. Close connection. */
              if (clientProcData.outBuffer_p != NULL)
              {
                /* OS_free((SIGNAL**) &(clientProcData.outBuffer_p)); */
                APT_RP_ERROR(ERROR_ID_R12_1893,(W32) &(clientProcData.outBuffer_p) );
              }
              if(signal_p != NULL)
              {
                OS_free(&signal_p);
              }
              if (savedSig_p != NULL)
              {
                OS_free(&savedSig_p);
              }
              CloseConnection(&clientProcData);
            }
          }
          
          if (clientProcData.outBuffer_p != NULL)
          {
            OS_free((SIGNAL**) &(clientProcData.outBuffer_p));
            clientProcData.outBuffer_p = NULL;
            clientProcData.startOfOutBuffer_p = NULL;
          }
          
          if (signal_p != NULL)
          {
            /* Check if confirm is requested. */
            if (signal_p->apptext.ctrl & APPTEXT_ACK)
            {
              /* ACK (flow control) requested. */
              outSig_p = OS_alloc(APPCTRL_S, APPCTRL);
              outSig_p->appctrl.data = APPCTRL_ACK;
              OS_send(&outSig_p, conh_);
            }

            /* Check if command is completed.
               Note: due to a probable fault in consolehandler, when executing
               help command "?" the apptext signal with APPTEXT_RDY is not
               send when command is completed. For that reason it's necessary
               to additionally check if prompt text without RDY flag is
               received, and if so behave as if command is completed. */
            if ((signal_p->apptext.ctrl & APPTEXT_RDY)
                OR
                ((clientProcData.apptextLength == 9)
                 AND
                 (strcmp(signal_p->apptext.text, "\r\nOSmon> ") == 0))
                OR
                ((clientProcData.apptextLength == 8)
                 AND
                 (strcmp(signal_p->apptext.text, "\r\nOSmon>") == 0)))
            {
              /* The command has been completed (all output signals have
                 been received). */

              /* Unset flag indicating ongoing command */
              isCmdRunning = FALSE;

              /* Check the if there are any buffered (awaiting) commands
                 and if so execute the next command. */
              if (noOfBufCmds > 0)
              {
                /* Check if previous command was not the last one */
                if (cmdIdx < noOfBufCmds)
                {
                  /* For safety check if stored command is not NULL */
                  if (bufCmds[cmdIdx] != NULL)
                  {
                    commandSize = strlen((char*)(bufCmds[cmdIdx]->appcmd.cmd));

                    /* Print the command on the telnet console */
                    if (TelnetWriteSimple(clientProcData.socketId,
                                          (char*)(bufCmds[cmdIdx]->appcmd.cmd),
                                          commandSize) != 0)
                    {
                      APT_RP_DOTRACE_LEV1(ERROR_ID_G12B_RTIPGPHR_5, (W32)tip_errno,
                                          __LINE__, __FILE__,
                                          0);
                    }

                    /* Send command to OSmonitor */
                    OS_send(&(bufCmds[cmdIdx]), conh_);

                    /* Set flag indicating ongoing command */
                    isCmdRunning = TRUE;

                    /* Set command index to point to the next command */
                    cmdIdx++;
                  }
                  else
                  {
                    /* Stored command is NULL, this should never happen and
                       indicates internal fault in RTIPGPHR. Report RPTERROR
                       and free all remaining stored commands (if any)
                       to avoid memory leaks. */
                    APT_RP_ERROR2(ERROR_ID_G12B_RTIPGPHR_7, 0,
                                  __LINE__, __FILE__,
                                  2,           /* Num of extra parameters 0-65535 */
                                  cmdIdx,
                                  noOfBufCmds);

                    for (cmdIdx = 0; cmdIdx < MAX_BUF_CMDS; cmdIdx++)
                    {
                      if (bufCmds[cmdIdx] != NULL)
                      {
                        OS_free(&(bufCmds[cmdIdx]));
                      }
                    }

                    noOfBufCmds = 0;
                    cmdIdx = 0;
                  }
                }
                else
                {
                  /* No more commands to execute, clear the variables used
                     for handling of buffered commands */
                  noOfBufCmds = 0;
                  cmdIdx = 0;
                }
              }
            }
          }
        }
        break;
      }
      
      /* User inactive for too long time. */
      case TMO_SIG: 
      {
        /* Unexpected close */
        APT_RP_ERROR(ERROR_ID_R12_1894, 0);
        if(signal_p != NULL)
        {
          OS_free(&signal_p);
        }
        
        CloseConnection(&clientProcData); /* no return... */
        break;
      }
      
      case MISTOPCLIENT:
        /* Telnet server process request to terminate client connection */
        if(signal_p != NULL)
        {
          OS_free(&signal_p);
        }
        CloseConnection(&clientProcData);
        break;
        
      case RTGETMODDATA:
        /* Do nothing */
        break;
        
      default:
        /* Unexpected signal */
        APT_RP_ERROR(ERROR_ID_R12_1895, signal_p->sig_no);
        break;
    }
    
    if(signal_p != NULL)
    {
      OS_free(&signal_p);
    }
  }
} /* IP_TELNET_CH */
