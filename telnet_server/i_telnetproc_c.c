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
/* @(#) ID          i_telnetproc_c.c                                        */
/* @(#) CLASS       102/190 55                                              */
/* @(#) NUMBER      CAA 204 1108                                            */
/* @(#) REVISION    PA1                                                     */
/* @(#) DATE        2003-09-12                                              */
/* @(#) DESIGNED    EAB/RJK/M ERAPNIL                                       */
/* @(#) RESPONSIBLE EAB/RJK/M                                               */
/* @(#) APPROVED    EAB/RJK/MC                                              */
/****************************************************************************/

/**************************  HISTORY OF DEVELOPMENT  ************************/
/*                                                                          */
/* Rev  Mark Date    Sign              Description                          */
/* ---  ---- ----    ----              -----------                          */
/* PA1       030912  EAB/RJK/M THAM    First issue.                         */
/*									    */
/****************************************************************************/

/**************************  GENERAL  ***************************************/
/*                                                                          */
/* Purpose:                                                                 */
/*                                                                          */
/*      Contains the subroutine calls in module i_telnet                    */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*  Notes:                                                                  */
/*                                                                          */
/****************************************************************************/

/**************************  CONTENTS  **************************************/
/*                                                                          */
/*-------------------------  Processes  ------------------------------------*/
/*                                                                          */
/*                                                                          */
/*-------------------------  Subroutines  ----------------------------------*/
/*                                                                          */
/*                                                                          */
/****************************************************************************/

/****************************************************************************/
/*                           INCLUDE                                        */
/****************************************************************************/

/*-------------------------  INTERFACES  -----------------------------------*/
                           
/*lint -elib(14)*/
/*lint -elib(46)*/
/*lint -elib(628)*/

/* Own interface */
#include "i_telnetproc_h.h"

/* Module internal interfaces */
/*#include "i_blockproc_h.h"*/
#include "i_telnet_main_h.h"
#include "i_def.h"

/* Module external interfaces */
#include "t_rp_h.h"
#include "i_telnet_main_h.h"
#include "tipsock.h"

#ifdef APT_PBOOT_USER
#include "user_pboot.h"
#else
#include "pboot.h"
#endif

#include <stdio.h>
#include <stdlib.h>

/*-------------------------  COMMON DECLARATIONS  --------------------------*/

#include "i_telnetproc_def.h"

/****************************************************************************/
/*                           LOCAL DECLARATIONS                             */
/****************************************************************************/

/*-------------------------  CONSTANTS  ------------------------------------*/


#define TCP_TIMESTAMP_OFF 0

#define SEND_BUFFER_SIZE 131072

/* The size of the string used in RTPRINTDATA */
#define SIZE_OF_OUTPUT_STR        200

/*-------------------------  MACROS  ---------------------------------------*/

/*-------------------------  TYPE DEF  -------------------------------------*/


/****************************************************************************/
/*                           LOCAL SUBROUTINES                              */
/****************************************************************************/
static W32 CreateAndActivateServer(PROCESS_DATA_st *processData_p);
static void HandleSocketChangedEvent(PROCESS_DATA_st *processData_p);
static void CreateClient(PROCESS_DATA_st *processData_p, W32 clientInd);
static W32 AllocateClient(PROCESS_DATA_st *processData_p, int clientSockId);
static void InitiateClientTermination(PROCESS_DATA_st *processData_p);
static void HandleStopClientReply(PROCESS_DATA_st *processData_p);
static void ProcessInit(PROCESS_DATA_st *processData_p);
static void CheckAndStopServer(const PROCESS_DATA_st *const processData_p);
static void GetClock1(char *str_p);
static void ReportModuleData (PROCESS_DATA_st *processData_p);
static void SendLine (PROCESS_DATA_st *processData_p,
                      const char *printString,
                      W32 printData1,
                      W32 printData2,
                      W32 typeOfValue);

/****************************************************************************/
/*                           DATA                                           */
/****************************************************************************/

/*-------------------------  GLOBAL DATA  ----------------------------------*/

/*-------------------------  STATIC DATA  ----------------------------------*/

static W32 sendBufferSize = SEND_BUFFER_SIZE;
static const char dotStr[] = ".";



/****************************************************************************/
/*                                                                          */
/*                   PROCESS                                                */
/*                                                                          */
/*                   IP_TELNET_SERVER                                       */
/*                                                                          */
/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Purpose: This is the telnet server process which listens to TCP port    */
/*           23 (telnet server).                                            */
/*                                                                          */
/****************************************************************************/

APT_RP_PROCESS(IP_TELNET_SERVER)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  PROCESS_DATA_st processData;
  PROCESS_DATA_st *processData_p = &processData;
  
  SIGNAL* sig_p;
  W32 result;
  static SIGSELECT startTelnetReq[] = {1,MITELNETSTART};
  static SIGSELECT allSignals[] = {0};     /* Signal select array */
  static char buf[32] ="";	/* Text buffer. */

/*-------------------------  CODE  -----------------------------------------*/

#ifdef APT_PBOOT_USER
  if(!user_pboot_param_get("send_buffer_size", buf, 31))  
#else
  if(!pboot_param_get("send_buffer_size", buf, 31))
#endif  
  {
    sendBufferSize = atoi(buf);
    printf("i_telnetproc, send_buffer_size: %ld\n", sendBufferSize);
  }
  
  ProcessInit(processData_p);
  
  /* Wait for start signal */
  RECSIG = OS_receive(startTelnetReq);
  /* Save the Process ID for the creator */
  processData_p->parentPid = OS_sender(&RECSIG);
  /* Save server IP address and port number */
  processData_p->ipAddress = RECSIG->mitelnetstart.ipAddress;
  processData_p->portNumber = RECSIG->mitelnetstart.portNumber;  

  OS_free(&RECSIG);
  
  /* Create and activate the server socket */
  result = CreateAndActivateServer(processData_p);
  
  /* Telnet server was started send reply signal to our creator (parent) */
  sig_p = OS_alloc(MITELNETSTARTR_S,MITELNETSTARTR);
  sig_p->mitelnetstartr.resultCode = result;
  
  OS_send(&sig_p, processData_p->parentPid);
  
  /* Enter main loop */
  while(1)                                              /*lint !e716*/
  {
    RECSIG = OS_receive(allSignals);
    switch(RECSIG->sig_no)
    {
      case TIP_SOCKET_CHANGED_EVENT:
      {
        /* Handle Socket Changed Event */
        HandleSocketChangedEvent(processData_p);
        break;
      }

      case SOCKET_ASEND_CFM:
      {
        /* If we receive this signal from the TIP stack
         * it indicates that we have made an erronous call to 
         * an asynchronous socket interface function
         */
        APT_RP_ERROR(ERROR_ID_R12_1896,RECSIG->socket_asend_cfm.Error_code);
        break;
      }

      case MITELNETSTOP:
        /* Stop the telnet server */
        InitiateClientTermination(processData_p);
        break;
        
      case MISTOPCLIENTR:
        HandleStopClientReply(processData_p);
        break;
        
      case RTGETMODDATA:
        ReportModuleData(processData_p);
        break;
        
      default:
        /* Unexpected signal */
        APT_RP_ERROR(ERROR_ID_R12_1897,RECSIG->sig_no);
        break;
    }

    if(RECSIG)
    {
      OS_free(&RECSIG);
    }
  }

} /* IP_TELNET_SERVER */



/*
******************************************************************************
*
*                   SUBROUTINE  CreateAndActivateServer
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Create the and activate the telnet server socket
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*
*  Return value: void
*
*****************************************************************************
*/
static W32 CreateAndActivateServer(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/
  struct tip_sockaddr_in addr;
  tip_socklen_t size;
  int returnValue;
  W32 result = TELNET_START_FAIL;
  W32 flag;
/*-------------------------  CODE  -----------------------------------------*/
  processData_p->serverSockId = tip_socket((int)TIP_AF_INET, (int)TIP_SOCK_STREAM, 0);
  if(processData_p->serverSockId < 0)
  {
    /* Error when creating socket. */
    APT_RP_ERROR(ERROR_ID_R12_1898, (W32) tip_errno);
  }
  else
  {
  
    /* Create and fill in address structure. */
    size = sizeof(struct tip_sockaddr_in);
    addr.sin_family      = (int)TIP_AF_INET;
    addr.sin_addr.s_addr = tip_htonl(processData_p->ipAddress); 
    addr.sin_port        = tip_htons(processData_p->portNumber);
  
    /* Bind the socket to the address. */
    returnValue = tip_bind(processData_p->serverSockId, (struct tip_sockaddr *) &addr, size);
    if(returnValue < 0)
    {
      /* Error when binding socket. */
      APT_RP_DOTRACE_LEV1(ERROR_ID_R12_2102,
                          (W32) tip_errno,
                          __LINE__,
                          __FILE__,
                          3,           /* Num of extra parameters 0-65535 */
                          addr.sin_addr.s_addr,
                          addr.sin_port,
                          size);
      
      result = TELNET_START_FAIL_BIND;
    }
    else
    {
      /* Subscribe on client connection request and socket closed events */
      returnValue = tip_asyncselect(processData_p->serverSockId, TIP_FD_CLOSE|TIP_FD_ACCEPT); /*lint !e641 !e655*/
      if (returnValue < 0)
      {
        APT_RP_ERROR(ERROR_ID_R12_1899, (W32) tip_errno);
      }
      else
      {
        
        /* Disable the TCP Timestamp option */
        flag = TCP_TIMESTAMP_OFF;
        if(tip_setsockopt(processData_p->serverSockId,
                          TIP_IPPROTO_TCP, TIP_TCP_TIMESTAMP,      /*lint !e641*/
                          (void *)&flag,
                          sizeof(flag)) < 0)
        {
          APT_RP_ERROR(ERROR_ID_R12_1900, (W32) tip_errno);
        }

        /* Set the Send buffer size */
        flag = sendBufferSize;
        if(tip_setsockopt(processData_p->serverSockId,
                          TIP_SOL_SOCKET, TIP_SO_SNDBUF,          /*lint !e641*/
                          (void *)&flag,
                          sizeof(flag)) < 0)
        {
          APT_RP_ERROR(ERROR_ID_R12_1901, (W32) tip_errno);
        }

        /* Start listening for connections. */
        returnValue = tip_listen(processData_p->serverSockId, MAX_CONNECTIONS);
        if(returnValue < 0)
        {
          /* Error when listening to socket. */
          APT_RP_ERROR(ERROR_ID_R12_1902, (W32) tip_errno);
        }
        else
        {
          result = TELNET_START_OK;
        }
      }
    }
  }

  return(result);
} /* CreateAndActivateServer */



/*
******************************************************************************
*
*                   SUBROUTINE  HandleSocketChangedEvent
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Handle the socket changed event
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*
*  Return value: void
*
*****************************************************************************
*/
static void HandleSocketChangedEvent(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/
  struct tip_sockaddr_in addr;
  tip_socklen_t size;
  int clientSockId;
  W32 clientInd;

/*-------------------------  CODE  -----------------------------------------*/
  
  if ((long)RECSIG->tip_socket_changed_event.socket == processData_p->serverSockId)
  {
    switch(RECSIG->tip_socket_changed_event.event)
    {
      case TIP_FD_CLOSE:
        APT_RP_ERROR(ERROR_ID_R12_1903,RECSIG->tip_socket_changed_event.error);

        if (tip_close(processData_p->serverSockId) < 0)
        {
          /* Error when closing socket. */
          APT_RP_ERROR(ERROR_ID_R12_1904, (W32) tip_errno);
        }
        break;
              
      case TIP_FD_ACCEPT:
        /* Get incoming connection request. */
        size = sizeof(addr);
        clientSockId = tip_accept(processData_p->serverSockId,
                                  (struct tip_sockaddr *) &addr, &size);
        
        /* Check if returned socket is valid. */

        if(clientSockId != -1)
        {
          /* Socket is valid. */
          /* Check if not too many connections. */
          if(processData_p->connections < MAX_CONNECTIONS)
          {
            /* Not too many connections. */
                  
            clientInd = AllocateClient(processData_p, clientSockId);
            
            /* Build telnet process name and create the process (not started yet). */
            if(clientInd < MAX_CONNECTIONS)
            { 
              /* Save the client's IP address and port. */
              processData_p->clientData[clientInd].clientIpAddress =
                tip_ntohl(addr.sin_addr.s_addr);
              processData_p->clientData[clientInd].clientPortNumber =
                tip_ntohs(addr.sin_port);

              /* Get real time to remember when client was started. */
              GetClock1(processData_p->clientData[clientInd].clientStartTime);
          
              CreateClient(processData_p, clientInd);

              /* Increment the number of active connections. */
              processData_p->connections++;
            }
            else
            {
              APT_RP_DOTRACE_LEV1(ERROR_ID_R12_2103, clientInd,
                                  __LINE__, __FILE__,
                                  0);

              /* Too many connections. */
              /* Close the socket */
              if (tip_close(clientSockId) < 0)
              {
                /* Error when closing socket. */
                APT_RP_ERROR(ERROR_ID_R12_1905, (W32) tip_errno);
              }
            } 
          }
          else
          {
            APT_RP_DOTRACE_LEV1(ERROR_ID_R12_2104, processData_p->connections,
                                __LINE__, __FILE__,
                                0);

            /* Too many connections. */
            /* Close the socket */
            if (tip_close(clientSockId) < 0)
            {
              /* Error when closing socket. */
              APT_RP_ERROR(ERROR_ID_R12_1906, (W32) tip_errno);
            }
          }
        }
        else
        {
          /* Accept Error */
          APT_RP_ERROR2(ERROR_ID_R12_1907,
                        (W32) tip_errno,
                        __LINE__,
                        __FILE__,
                        3,           /* Num of extra parameters 0-65535 */
                        addr.sin_addr.s_addr,
                        addr.sin_port,
                        size);
          
          /* Close the socket */
          if (tip_close(clientSockId) < 0)
          {
            /* Error when closing socket. */
            APT_RP_ERROR(ERROR_ID_R12_1908, (W32) tip_errno);
          }
        }
        break;
            
      default:
        APT_RP_ERROR(ERROR_ID_R12_1909,
                     RECSIG->tip_socket_changed_event.event);
        break;
    }
  }
  else
  {
    /* Bad socket */
    APT_RP_ERROR(ERROR_ID_R12_1910,
                 RECSIG->tip_socket_changed_event.socket);
  }

} /* HandleSocketChangedEvent */



/*
******************************************************************************
*
*                   SUBROUTINE  ProcessInit
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Initalize the process data
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*
*  Return value: void
*
*****************************************************************************
*/
static void ProcessInit(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  W32 i;
  W32 j;

/*-------------------------  CODE  -----------------------------------------*/
  processData_p-> serverSockId = 0;
  processData_p-> terminationPending = FALSE;
  processData_p-> connections = 0;
  processData_p-> parentPid = (PROCESS) NULL;
  processData_p-> ipAddress = TIP_INADDR_ANY;
  processData_p-> portNumber = TELNET_SERVER_PORT;

  for (i = 0; i < MAX_CONNECTIONS; i++)
  {
    processData_p->clientData[i].status = TELNET_CLIENT_IDLE;
    processData_p->clientData[i].clientPid = (PROCESS) NULL;
    processData_p->clientData[i].clientSockId = 0;        
    processData_p->clientData[i].clientIpAddress = 0;
    processData_p->clientData[i].clientPortNumber = 0;

    for (j = 0; j < CLIENT_START_TIME_STR_LEN; j++)
    {
      processData_p->clientData[i].clientStartTime[j] = 0;
    }
  }

} /* ProcessInit */



/*
******************************************************************************
*
*                   SUBROUTINE  CreateClient
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Creates a telnet client process
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*      clientInd        in      Client individual
*
*  Return value: void
*
*****************************************************************************
*/
static void CreateClient(PROCESS_DATA_st *processData_p, W32 clientInd)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  char buffer[30];
  SIGNAL *sig_p;
/*-------------------------  CODE  -----------------------------------------*/

  /* Give the telnet client process a name */
  sprintf(buffer, "IP_TELNET_CH_%d", (W8) clientInd);

  /* Create the process */

  processData_p->clientData[clientInd].clientPid =
    OS_create_proc((OSADDRESS) IP_TELNET_CH,
                   OWN_REF,
                   buffer,
                   15,
                   3000,
                   2,
                   TRH_USER_MODE);
  

  /* Make the telnet client process owner of the socket. */
  if(tip_setsockopt(processData_p->clientData[clientInd].clientSockId,
                    TIP_SOL_SOCKET, TIP_SO_CHOWNER,                         /*lint !e641*/
                    (char *)&processData_p->clientData[clientInd].clientPid,
                    sizeof(PROCESS)) < 0)
  {
    APT_RP_ERROR(ERROR_ID_R12_1911, (W32) tip_errno);
  }

  /* Request the client to start and provide the socket ID and
   * client individual to the client */
  sig_p = OS_alloc(MISTARTCLIENT_S,MISTARTCLIENT);
  sig_p->mistartclient.clientSockId =
    (W32) processData_p->clientData[clientInd].clientSockId;
  sig_p->mistartclient.clientInd = clientInd;

  OS_send(&sig_p, processData_p->clientData[clientInd].clientPid);

} /* CreateClient */



/*
******************************************************************************
*
*                   SUBROUTINE AllocateClient  
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Allocate a client individual and store the file descriptor
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*      clientSockId     in      Socket ID for the client
*
*  Return value: Client Individual
*
*****************************************************************************
*/
static W32 AllocateClient(PROCESS_DATA_st *processData_p, int clientSockId)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  W32 clientInd=0;
  W32 found = FALSE;
/*-------------------------  CODE  -----------------------------------------*/

  /* Search the client data structure for an idle individual */
  while ((!found) && (clientInd < MAX_CONNECTIONS))
  {
    if (processData_p->clientData[clientInd].status == TELNET_CLIENT_IDLE)
    {
      found = TRUE;
    }
    else
    {
      clientInd++;
    }
  }

  if (found)
  {
    /* Save the Socket ID and set the individual to used */
    processData_p->clientData[clientInd].clientSockId = clientSockId; /*lint !e661*/
    processData_p->clientData[clientInd].status = TELNET_CLIENT_USED; /*lint !e661*/
    return(clientInd);
  }
  else
  {
    return(MAX_CONNECTIONS);
  }
} /* AllocateClient */



/*
******************************************************************************
*
*                   SUBROUTINE InitiateClientTermination  
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Initate client termination
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*
*  Return value: void
*
*****************************************************************************
*/
static void InitiateClientTermination(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/
  SIGNAL *sig_p;
  W32 i;
/*-------------------------  CODE  -----------------------------------------*/

  /* Indicate the Telnet server termination is pending */
  processData_p->terminationPending = TRUE;
  
  /* Request all active clients to terminate */
  for (i = 0; i < MAX_CONNECTIONS; i++)
  {
    /* If client connected */
    if ( processData_p->clientData[i].status == TELNET_CLIENT_USED)
    {
      /* Send a client stop request */
      sig_p = OS_alloc(MISTOPCLIENT_S,MISTOPCLIENT);
      OS_send(&sig_p, processData_p->clientData[i].clientPid);
      
    }
  }

  /* Check if telnet server can terminate */
  CheckAndStopServer(processData_p);

} /* InitiateClientTermination */



/*
******************************************************************************
*
*                   SUBROUTINE HandleStopClientReply  
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Handle Stop client reply, MISTOPCLIENTR
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*
*  Return value: void
*
*****************************************************************************
*/
static void HandleStopClientReply(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  W32 clientInd;
  W32 i;

/*-------------------------  CODE  -----------------------------------------*/
  /* Get the client index */
  clientInd = RECSIG->mistopclientr.clientInd;
  
  /* Indicate that this client is stopped */
  processData_p->clientData[clientInd].status = TELNET_CLIENT_IDLE;

  processData_p->connections--;
  
  processData_p->clientData[clientInd].clientPid = (PROCESS) NULL;
  processData_p->clientData[clientInd].clientSockId     = 0;
  processData_p->clientData[clientInd].clientIpAddress  = 0;
  processData_p->clientData[clientInd].clientPortNumber = 0;

  for (i = 0; i < CLIENT_START_TIME_STR_LEN; i++)
  {
    processData_p->clientData[clientInd].clientStartTime[i] = 0;
  }

  /* Check if telnet server can terminate */
  CheckAndStopServer(processData_p);

} /* HandleStopClientReply */



/*
******************************************************************************
*
*                   SUBROUTINE  CheckAndStopServer
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Check
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *processData_p   in      Pointer to the process data.
*
*  Return value: void
*
*****************************************************************************
*/
static void CheckAndStopServer(const PROCESS_DATA_st *const processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/
  SIGNAL *sig_p;
  W32 i;
  W32 allStopped = TRUE;

/*-------------------------  CODE  -----------------------------------------*/
  /* If Telnet server termination is pending */
  if (processData_p->terminationPending)
  {
  /* Check if all clients are stopped
   */
    i = 0;
    while ((allStopped) && (i < MAX_CONNECTIONS))
    {
      if (processData_p->clientData[i].status == TELNET_CLIENT_USED)
      {
        allStopped = FALSE;
      }
      else
      {
        i++;
      }
    }

    if (allStopped)
    {
      /* All clients are stopped
       * Send reply to the creator of the Telnet server (parent) i_telneta
       */
      if (tip_close(processData_p->serverSockId) < 0)
      {
        /* Error when closing socket. */
        APT_RP_ERROR(ERROR_ID_R12_1912, (W32) tip_errno);
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

      sig_p = OS_alloc(MITELNETSTOPR_S,MITELNETSTOPR);
      OS_send(&sig_p, processData_p->parentPid);

      /* Kill the telnet server process */
      kill_proc(current_process());
    }
  }

} /* CheckAndStopServer */



/*
******************************************************************************
*
*                   SUBROUTINE  GetClock1
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Appends the current time to string.
*                                            
*  Parameters:                               
*                                            
*      parameter        in/out  description  
*
*      *str_p           in/out  The string to append the time to.
*
*  Return value: void
*
*****************************************************************************
*/
/** @todo Consider using function GetClock in rp_common instead */
static void GetClock1(char *str_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  SIGSELECT    realtimeSiglist[] = {1, REALTIMEGETR};
  SIGNAL *sig_p;

/*-------------------------  CODE  -----------------------------------------*/

  /* Request time for process start */
  /* Print real time (same as CP) */
  sig_p = OS_alloc(REALTIMEGET_S, REALTIMEGET);
  sig_p->realtimeget.version_no = 0;
  OS_send(&sig_p, BDT.REOS);

  /* Receive the real time */
  sig_p = OS_receive_w_tmo(100, realtimeSiglist);

  if (sig_p != NULL)
  {
    switch(sig_p->signo)
    {
      case REALTIMEGETR:
        /* Insert year */
        sprintf(str_p, "%4d", sig_p->realtimegetr.year);
        sprintf(str_p, "%s%s", str_p, "-");
        /* Insert month */
        sprintf(str_p, "%s%02d", str_p, sig_p->realtimegetr.month);
        sprintf(str_p, "%s%s", str_p, "-");
        /* Insert day */
        sprintf(str_p, "%s%02d", str_p, sig_p->realtimegetr.day);
        sprintf(str_p, "%s%s", str_p, " ");
        /* Insert hour */
        sprintf(str_p, "%s%02d", str_p, sig_p->realtimegetr.hour);
        sprintf(str_p, "%s%s", str_p, ":");
        /* Insert minute */
        sprintf(str_p, "%s%02d", str_p, sig_p->realtimegetr.minute);
        sprintf(str_p, "%s%s", str_p, ":");
        /* Insert second */
        sprintf(str_p, "%s%02d", str_p, sig_p->realtimegetr.second);
        break;
                    
      default:
        /* Do nothing */
        break;
    }
    OS_free(&sig_p);
  }
  else
  {
    sprintf(str_p, "2003-01-01 00:00:00");
  }
} /* GetClock1 */



/*
******************************************************************************
*
*                   SUBROUTINE  ReportModuleData
*
*-----------------------------------------------------------------------------
*
*  Purpose: Print statistics of the telnet module
*
*  Parameters:
*
*      parameter                      in/out   description
*
*      processData_p    in      Pointer to the process data
*
*  Return value: void
*
*****************************************************************************
*/

static void ReportModuleData (PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  W32 i;           /* Loop variable, W32 used to match */
  W8  ipLswLsb;
  W8  ipLswMsb;
  W8  ipMswLsb;
  W8  ipMswMsb;

  char outPutStr[SIZE_OF_OUTPUT_STR];

/*-------------------------  CODE  -----------------------------------------*/
  
                    
            SendLine (processData_p,
"=======================================================================",
                      0,
                      0,
                      TYPE_ONLY_STRING);

            SendLine (processData_p,
"'telnet' TELNET STATUS INFORMATION                                     ",
                      0,
                      0,
                      TYPE_ONLY_STRING);
            
            SendLine (processData_p,
"'telnet' -------------------------------------------------------------",
                      0,
                      0,
                      TYPE_ONLY_STRING);
            
           SendLine (processData_p,
"'telnet' Server Socket ID                            ",
                     processData_p-> serverSockId,
                     0,
                     TYPE_UNSIGNED_LONG);

           SendLine (processData_p,
"'telnet' Termination Pending                         ",
                     processData_p-> terminationPending,
                     0,
                     TYPE_UNSIGNED_LONG);
           
           SendLine (processData_p,
"'telnet' Connections                                 ",
                     processData_p-> connections,
                     0,
                     TYPE_UNSIGNED_LONG);
                      
           SendLine (processData_p,
"'telnet' Parent Process ID                           ",
                     processData_p-> parentPid,
                     0,
                     TYPE_UNSIGNED_LONG);

           /* Build address string for the IP address */
           if(processData_p-> ipAddress != 0)
           {
             ipMswMsb = APT_RP_TO_W8((processData_p-> ipAddress) >> THREE_BYTE);
             ipMswLsb = APT_RP_TO_W8((processData_p-> ipAddress) >> TWO_BYTE);
             ipLswMsb = APT_RP_TO_W8((processData_p-> ipAddress) >> ONE_BYTE);
             ipLswLsb = APT_RP_TO_W8(processData_p-> ipAddress);
             
             sprintf(outPutStr,
"'telnet' Bound IP Address                             = %d%s%d%s%d%s%d"
                     ,ipMswMsb
                     ,dotStr
                     ,ipMswLsb
                     ,dotStr
                     ,ipLswMsb
                     ,dotStr
                     ,ipLswLsb);
           }
           else
           {
             sprintf(outPutStr,
"'telnet' Bound IP Address                             = ANY");
           }
           
           SendLine (processData_p,
                     outPutStr,
                     0,
                     0,
                     TYPE_ONLY_STRING);

           SendLine (processData_p,
"'telnet' Port Number                                 ",
                     processData_p-> portNumber,
                     0,
                     TYPE_UNSIGNED_LONG);

           
           /*
            * Print client data
            */
           
           for (i = 0; i < MAX_CONNECTIONS; i++)
           {
            SendLine (processData_p,
 "'telnet'                                            ",
                      0,
                      0,
                      TYPE_ONLY_STRING);

            if (processData_p->clientData[i].status == TELNET_CLIENT_USED)
            {
              SendLine (processData_p,
"'telnet' Client Status                                = USED",
                        0,
                        0,
                        TYPE_ONLY_STRING);
            }
            else
            {
              SendLine (processData_p,
"'telnet' Client status                                = IDLE",
                        0,
                        0,
                        TYPE_ONLY_STRING);
            }
              
            SendLine (processData_p,
"'telnet' Client Process ID                           ",
                      processData_p->clientData[i].clientPid,
                      0,
                      TYPE_UNSIGNED_LONG);
             
            SendLine (processData_p,
"'telnet' Client Socket ID                            ",
                      processData_p->clientData[i].clientSockId,
                      0,
                      TYPE_UNSIGNED_LONG);


            /* Build address string for the IP address */
            if(processData_p->clientData[i].clientIpAddress != 0)
            {
              ipMswMsb = APT_RP_TO_W8(
                (processData_p->clientData[i].clientIpAddress) >> THREE_BYTE);
              ipMswLsb = APT_RP_TO_W8(
                (processData_p->clientData[i].clientIpAddress) >> TWO_BYTE);
              ipLswMsb = APT_RP_TO_W8(
                (processData_p->clientData[i].clientIpAddress) >> ONE_BYTE);
              ipLswLsb = APT_RP_TO_W8(processData_p->clientData[i].clientIpAddress);
              
              sprintf(outPutStr,
"'telnet' Client IP Address                            = %d%s%d%s%d%s%d"
                      ,ipMswMsb
                      ,dotStr
                      ,ipMswLsb
                      ,dotStr
                      ,ipLswMsb
                      ,dotStr
                      ,ipLswLsb);
            }
            else
            {
              sprintf(outPutStr,
"'telnet' Client IP Address                            = 0");
            }
      
            SendLine (processData_p,
                      outPutStr,
                      0,
                      0,
                      TYPE_ONLY_STRING);

            SendLine (processData_p,
"'telnet' Client Port Number                          ",
                      processData_p->clientData[i].clientPortNumber,
                      0,
                      TYPE_UNSIGNED_LONG);

            /* Print time when client was started */
            if (strlen(processData_p->clientData[i].clientStartTime) > 0)
            {
              sprintf(outPutStr,
"'telnet' Client Start Time                            = %s",
                      processData_p->clientData[i].clientStartTime);

              SendLine (processData_p,
                        outPutStr,
                        0,
                        0,
                        TYPE_ONLY_STRING);
            }
            else
            {
              sprintf(outPutStr,
"'telnet' Client Start Time                            = 0");

              SendLine (processData_p,
                        outPutStr,
                        0,
                        0,
                        TYPE_ONLY_STRING);
            }
            
           } /* for i */


           SendLine (processData_p,
 "'telnet' -------------------------------------------------------------",
                     0,
                     0,
                     TYPE_ONLY_STRING);
  return;

} /* ReportModuleData */



/*
******************************************************************************
*
*                   SUBROUTINE  SendLine
*
*-----------------------------------------------------------------------------
*
*  Purpose: Send printouts to RGSERVR
*
*  Parameters:
*
*      parameter                in/out   description
*
*      processData_p    in      Pointer to the process data
*      printString      in      Printout string to send
*      printData1       in      Printout data to send
*      printData2       in      Printout data to send
*      typeOfValue      in      Type of printout
*
*  Return value: void
*
*****************************************************************************
*/

static void SendLine (PROCESS_DATA_st *processData_p,
                      const char *printString,
                      W32 printData1,
                      W32 printData2,
                      W32 typeOfValue)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  SIGNAL            *sig_p;

/*-------------------------  CODE  -----------------------------------------*/

  sig_p = OS_alloc(sizeof(struct rtprintdata_s) +
                   strlen(printString), RTPRINTDATA);
  sig_p->rtprintdata.sigVersion = RTPRINTDATA_SECOND_VERSION;
  sig_p->rtprintdata.typeOfValue = typeOfValue;
  sig_p->rtprintdata.noOfPrintValues = 1;
  sig_p->rtprintdata.printValue[0] = printData1;
  sig_p->rtprintdata.printValue[1] = printData2;

  strcpy((char *)sig_p->rtprintdata.outPutStr, 
         printString);                           /*lint !e419*/
  OS_send(&sig_p,OS_sender(&RECSIG));

  return;

} /* SendLine */
