/** @file
*/
/****************************************************************************/
/*                                                                          */
/*                 Copyright (C) ERICSSON RADIO SYSTEMS AB, 2003            */
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
/* @(#) ID          i_telneta_main_c.c                                      */
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
/* PA1       030912  EAB/RJK/M ERAPNIL    First issue.                      */
/*									    */
/****************************************************************************/
/**************************  GENERAL  ***************************************/
/*                                                                          */
/* Purpose: Telnet Server Administration
 */

/**
 * @file i_telneta_main_c.c
 *
 * @addtogroup ModuleDescriptionTelneta Module Description I_TELNETA
 *
 * Module documentation for module TELNETA
 *
 * @section telnetaMod TELNETA module
 *
 * Contents of TELNETA module:
 *
 * @li @ref telnetaModGeneral
 * @li @ref telnetaModProcess
 * @li @ref telnetaModSourceFileStructure
 * @li @ref telnetaModDataStructure
 * @li @ref telnetaModStateTransistion
 * @li @ref telnetaModFunction
 *
 *
 * @section telnetaModGeneral General
 *
 * The module handles the administration of the telnet server.
 *
 * @section telnetaModProcess Processes
 * 
 * The module is executed in the RTIPGPHR block process.
 *
 * @section telnetaModSourceFileStructure Source File Structure
 *
 * The module consists of one C-module:
 * @li @c i_telneta_main_c.c
 *
 * @section telnetaModDataStructure Data Structures
 *
 * @section telnetaModStateTransistion State Event Transitions
 *
 * @section telnetaModFunction Function
 *
 * The telnet server can be started and stopped by either apt commands
 * or flash (pboot) parameter.
 * If the flash (pboot) parameter "telnet" is set to "yes" the
 * telnet server will be started at module init.
 * It the flash (pboot) parameter "telnet" is not defined or not set
 * to "yes" the telnet server is not started.
 * The telnet server can be started at any time with the apt command
 * "telneton".
 * The telnet server can be stopped at any time with the apt command
 * "telnetoff".
 *
 */

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
/*  itelneta_Init                  Init module                              */
/*                                                                          */
/*  itelneta_Receive               Receive signals                          */
/*                                                                          */
/****************************************************************************/

/*lint -esym(715, mod2ModSig_p) Symbol not referenced */
/*lint -esym(818, mod2ModSig_p) Pointer parameter could be declared as pointing to const */
/*lint -esym(818, processData_p) Pointer parameter could be declared as pointing to const */

/****************************************************************************/
/*                           INCLUDE                                        */
/****************************************************************************/

/*-------------------------  INTERFACES  -----------------------------------*/
                           
/* Own interface */
#include "i_telneta_main_h.h"

/* Module internal interfaces */
#include "i_blockproc_h.h"
#include "i_telneta_data_def.h"

/* Module external interfaces */
#include "t_rp_h.h"
#ifdef APT_PBOOT_USER
#include "user_pboot.h"
#else
#include "pboot.h"
#endif
#include "i_telnetproc_h.h"

/*-------------------------  COMMON DECLARATIONS  --------------------------*/

#include "i_telnet_def.h"
#include "i_telneta_def.h"
#include "tipsock.h"

/****************************************************************************/
/*                           LOCAL DECLARATIONS                             */
/****************************************************************************/

/*-------------------------  CONSTANTS  ------------------------------------*/


#define TELNET_PARAM_LENGTH 10


/*-------------------------  MACROS  ---------------------------------------*/


/*-------------------------  TYPE DEF  -------------------------------------*/


/****************************************************************************/
/*                           LOCAL SUBROUTINES                              */
/****************************************************************************/

static void  GetModData(PROCESS_DATA_st *processData_p);
static void  StartTelnet(PROCESS_DATA_st *processData_p);
static void  StopTelnet(PROCESS_DATA_st *processData_p);
static void  HandleTelnetStartReply(PROCESS_DATA_st *processData_p);
static void  HandleTelnetStopReply(PROCESS_DATA_st *processData_p);

/****************************************************************************/
/*                           DATA                                           */
/****************************************************************************/

/*-------------------------  GLOBAL DATA  ----------------------------------*/


/*-------------------------  STATIC DATA  ----------------------------------*/
/* Flash (pboot) parameter name string */
static char *telnetConfigParam="telnet";

/*
******************************************************************************
*                                            
*                   SUBROUTINE   itelneta_Init
*                                            
*                   Initialize module
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Initialize module data
*                                            
*  Parameters:                               
*                                            
*      parameter       in/out   description  
*
*      *processData_p   in      Pointer to the process data.
*                                            
*  Return value:  void                       
*
*****************************************************************************
*/

void itelneta_Init (PROCESS_DATA_st   *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  char telnetConfig[TELNET_PARAM_LENGTH];
/*-------------------------  CODE  -----------------------------------------*/

  /*
   *  Initialize module data
   */

  TELNETA_P->state = ST_TELNETA_STOPPED;
  TELNETA_P->telnetPid = (PROCESS) NULL;


  /*
   * Check if Telnet shall be started, defined by flash parameter setting
   */
#ifdef APT_PBOOT_USER
  if (user_pboot_param_get(telnetConfigParam,telnetConfig,
                           TELNET_PARAM_LENGTH) == 0)
#else  
  if (pboot_param_get(telnetConfigParam,telnetConfig,
                      TELNET_PARAM_LENGTH) == 0)
#endif    
  {
    /* Parameter found */

    /* Check if telnet should be started */
    if ( (strcmp(telnetConfig, "YES") == 0) ||
         (strcmp(telnetConfig, "yes") == 0))
    {
      StartTelnet(processData_p);
    }
    
  }
} /* itelneta_Init */

/*
******************************************************************************
*                                            
*                   SUBROUTINE   itelneta_Receive
*                                            
*                   Receives signals 
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Receive calls from the unit module for every event that
*           this module should process
*                                            
*  Parameters:                               
*                                            
*      parameter       in/out   description  
*
*      *processData_p   in      Pointer to the process data.
*                                            
*  Return value:  void                       
*
*****************************************************************************
*/

void itelneta_Receive (PROCESS_DATA_st *processData_p)

{
/*-------------------------  LOCAL DATA   ----------------------------------*/

/*-------------------------  CODE  -----------------------------------------*/
  
    switch (RECSIG -> signo)
    {
      /* Receive and handle apt commands telneton and telnetoff */
      case RTGETMODDATA:
        GetModData(processData_p);
        break;
        
      /* Receive and handle telnet server start reply signal */
      case MITELNETSTARTR:
        HandleTelnetStartReply(processData_p);
        break; 

        /* Receive and handle telnet server stop reply signal */
      case MITELNETSTOPR:
        HandleTelnetStopReply(processData_p);
        break; 
       
      default:
        /* Unknown signal */
        APT_RP_ERROR(ERROR_ID_R12_1913, RECSIG -> signo);                
        break;
    }
} /* itelneta_Receive */

/*
******************************************************************************
*
*                   SUBROUTINE  GetModData
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Handles apt commands telneton and telnetoff
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

static void  GetModData(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

/*-------------------------  CODE  -----------------------------------------*/

  TELNETA_P -> printReceiverPid = OS_sender(&RECSIG);
  
  switch(RECSIG -> rtgetmoddata.sigVersion)
  {
    case RTGETMODDATA_FIRST_VERSION:

      switch(RECSIG -> rtgetmoddata.orderCode)
      {
        case OC_MOD_SET_TELNET_ON:
          /* If state is IDLE start telnet */
          if (TELNETA_P->state == ST_TELNETA_STOPPED)
          {
            StartTelnet(processData_p);
          }
          break;

        case OC_MOD_SET_TELNET_OFF:
          /* If state is STARTED stop telnet */
          if (TELNETA_P->state == ST_TELNETA_RUNNING)
          {
            StopTelnet(processData_p);
          }
          break;

        default:
          break;          
      }
      break;      

    default:
      /* Unknown signal version */
      APT_RP_ERROR(ERROR_ID_R12_1914, RECSIG -> rtgetmoddata.sigVersion);                
      break;
  }
} /* GetModData */

/*
******************************************************************************
*
*                   SUBROUTINE  StartTelnet
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Start the telnet server
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

static void  StartTelnet(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  SIGNAL *sig_p;
  
/*-------------------------  CODE  -----------------------------------------*/


/*
 *  Start the telnet server process.
 */
  
  TELNETA_P->telnetPid = OS_create_proc((OSADDRESS) IP_TELNET_SERVER,
                                       OWN_REF,
                                       "IP_TELNET_SERVER",
                                       15,
                                       2000,
                                       2,
                                       TRH_USER_MODE);

  if (TELNETA_P->telnetPid)
  {
    /* Request the IP_TELNET_SERVER to start */
    sig_p = OS_alloc(MITELNETSTART_S,MITELNETSTART);
    
    sig_p->mitelnetstart.ipAddress = TIP_INADDR_ANY;
    sig_p->mitelnetstart.portNumber = TELNET_SERVER_PORT;

    OS_send(&sig_p,TELNETA_P->telnetPid);


    /* State change
     * New state STARTING
     * The telnet server starts up.
     * Wait for MITELNETSTARTR from the IP_TELNET_SERVER process
     */
    TELNETA_P->state = ST_TELNETA_STARTING;
  }
  
} /* StartTelnet */

/*
******************************************************************************
*
*                   SUBROUTINE  StopTelnet
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Initiates the stop procedure for the telnet server
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

static void  StopTelnet(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  SIGNAL *sig_p;
  
/*-------------------------  CODE  -----------------------------------------*/


/*
 *  Stop the telnet server process.
 */
  
  /* Request the IP_TELNET_SERVER to stop */
  sig_p = OS_alloc(MITELNETSTOP_S,MITELNETSTOP);

  OS_send(&sig_p,TELNETA_P->telnetPid);
  
  /* State change
   * New state STOPPING
   * The telnet server terminates
   * Wait for MITELNETSTOPR from the IP_TELNET_SERVER process
   *
   */
  TELNETA_P->state = ST_TELNETA_STOPPING;
  
} /* StopTelnet */

/*
******************************************************************************
*
*                   SUBROUTINE  HandleTelnetStartReply
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Handles the telnet start reply signal, MITELNETSTARTR
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

static void HandleTelnetStartReply(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  char *printString;
  SIGNAL *sig_p;
  
/*-------------------------  CODE  -----------------------------------------*/

  if (TELNETA_P->state == ST_TELNETA_STARTING)
  {
    /* Check result code */

    if (RECSIG->mitelnetstartr.resultCode == TELNET_START_OK)
    {
      /* State change
       * New state RUNNING
       * The telnet server is up and running
       */
    
      TELNETA_P->state = ST_TELNETA_RUNNING;
    }
    else if (RECSIG->mitelnetstartr.resultCode == TELNET_START_FAIL_BIND)
    {
      /* Stop telnet server */
      StopTelnet(processData_p);

      if (TELNETA_P->printReceiverPid != (PROCESS) NULL)
      {
        printString = "Error: Socket bind failed. Try again in 4 minutes.";
      
        sig_p = OS_alloc(sizeof(struct rtprintdata_s) +
                         strlen(printString), RTPRINTDATA);
        sig_p->rtprintdata.sigVersion = RTPRINTDATA_SECOND_VERSION;
        sig_p->rtprintdata.typeOfValue = TYPE_ONLY_STRING;
        sig_p->rtprintdata.noOfPrintValues = 0;
        sig_p->rtprintdata.printValue[0] = 0;
        sig_p->rtprintdata.printValue[1] = 0;
        
        strcpy( (char *)sig_p->rtprintdata.outPutStr, printString); /*lint !e419*/
      
        OS_send(&sig_p, TELNETA_P -> printReceiverPid);
      }
    }
    else
    {
      /* Stop telnet server */
      StopTelnet(processData_p);
    }
  }  
  else
  {
    /* Bad state */
    APT_RP_ERROR(ERROR_ID_R12_1915, (W32)TELNETA_P->state);
  }
} /* HandleTelnetStartReply */

/*
******************************************************************************
*
*                   SUBROUTINE  HandleTelnetStopReply
*                                            
*-----------------------------------------------------------------------------
*                                            
*  Purpose: Handles the telnet stop reply signal, MITELNETSTOPR
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

static void  HandleTelnetStopReply(PROCESS_DATA_st *processData_p)
{
/*-------------------------  LOCAL DATA   ----------------------------------*/

  
/*-------------------------  CODE  -----------------------------------------*/

  if (TELNETA_P->state == ST_TELNETA_STOPPING)
  {
    /* The Telnet Server process is gone*/
    TELNETA_P->telnetPid = (PROCESS) NULL;
    
    /* State change
     * New state STOPPED
     * The telnet server is now stopped
     */
  
    TELNETA_P->state = ST_TELNETA_STOPPED;
  }
  else
  {
    /* Bad state */
    APT_RP_ERROR(ERROR_ID_R12_1916,(W32)TELNETA_P->state);
  }
} /* HandleTelnetStopReply */
