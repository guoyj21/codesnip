/*
*****************************************************************************
**                            PROGRAM MODULE
**
** @(#) Id:             1/190 55-7/CAA 204 03
** @(#) File:           ledit.c
** @(#) Subsystem:      RAZOR
** @(#) Date:           01/04/03
** @(#) Time:           08:55:07
** @(#) Revision:       1.1
** @(#) Author:         Hans Feldt
**
**       Copyright (C) 2000 Ericsson Utvecklings AB. All rights reserved.
**
*****************************************************************************

        CONTENTS
        --------

        1  Description
        2  History of development
        3  Include files
           3.1 Blockwide definitions
           3.2 Libraries
           3.3 Modulewide definitions
           3.4 Global signals
           3.5 Local signals
        4  Local definitions
           4.1 Manifest constants
           4.2 Type definitions
           4.3 Macros
           4.4 Signal composition
        5  External references
           5.1 Functions
           5.2 Variables
           5.3 Forward references
           5.4 Processes
        6  Global variables
        7  Local variables
           7.1 General variables
           7.2 Receive specifications
        8  Local functions
        9  Global functions
        10 Process entrypoints

*****************************************************************************
*/

/*
*****************************************************************************
** 1  DESCRIPTION.
*****************************************************************************
** This file is part of the telnet module.
**
*/

/*
*****************************************************************************
** 2  HISTORY OF DEVELOPMENT.
*****************************************************************************
** date      responsible           notes
** --------  --------------------  ------------------------------------------
** 00/04/19  Hans Feldt            Now uses CHGIs ledit function
** 00/07/26  Hans Feldt            Bug fix in push_to_stack
*/

/*
*****************************************************************************
** 3  INCLUDE FILES.
*****************************************************************************
*/

/*
**===========================================================================
** 3.1  Blockwide definitions.
**===========================================================================
*/

#include "i_telnet_ledit_h.h"

/*
**===========================================================================
** 3.2  Libraries.
**===========================================================================
*/

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

/*
**===========================================================================
** 3.3  Modulewide definitions.
**===========================================================================
*/

/*
**===========================================================================
** 3.4  Global signals.
**      These signals are visible outside the current module.
**===========================================================================
*/

/*
The sigunion.h file is a wrapper for all signal definition files.
It also defines the SIGNAL union.
*/

#include "sigunion.h"

/*
**===========================================================================
** 3.5  Local signals.
**      These signals are used only within the current module.
**===========================================================================
*/

/*
*****************************************************************************
** 4  LOCAL DEFINITIONS.
*****************************************************************************
*/

/*
**===========================================================================
** 4.1  Manifest constants.
**===========================================================================
*/

/*
**===========================================================================
** 4.2  Type definitions.
**===========================================================================
*/

/*
**===========================================================================
** 4.3  Macros.
**===========================================================================
*/

/*
**===========================================================================
** 4.4  Signal composition.
**===========================================================================
*/

/* The SIGNAL union is declared in sigunion.h */

/*
*****************************************************************************
** 5  EXTERNAL REFERENCES.
*****************************************************************************
*/

/*
**===========================================================================
** 5.1  Functions.
**===========================================================================
*/

/*
**===========================================================================
** 5.2  Variables.
**===========================================================================
*/

/*
**===========================================================================
** 5.3  Forward references.
**===========================================================================
*/

/*
**===========================================================================
** 5.4  Processes.
**===========================================================================
*/

/*
*****************************************************************************
** 6  GLOBAL VARIABLES.
*****************************************************************************
*/

/*
*****************************************************************************
** 7  LOCAL VARIABLES.
*****************************************************************************
*/

/*
**===========================================================================
** 7.1  General variables.
**===========================================================================
*/

/*
**===========================================================================
** 7.2 Receive specifications.
**===========================================================================
*/

/*
*****************************************************************************
** 8  LOCAL FUNCTIONS.
*****************************************************************************
*/

/*
**===========================================================================
** 8.0                 flush_buf()
**===========================================================================
** Description: Flush (write) the output buffer.
**
** Parameters:  stack - info pointer
**
** Returns:     -
**
** Globals:     -
*/

static void
flush_buf(cmd_hist* stack)
{
   stack->output(stack->fd, stack->wr_buf, stack->wr_index);
   stack->wr_index = 0;
   stack->wr_buf[0] = '\0';
}

/*
**===========================================================================
** 8.1                 wr_char()
**===========================================================================
** Description: Writes a char to the output buffer.
**
** Parameters:  stack - info pointer, c - char to write
**
** Returns:     -
**
** Globals:     -
*/

static void
wr_char(cmd_hist* stack, char c)
{
   if (stack->wr_index >= MAX_LINE)
      flush_buf(stack);

   if (c == '\n')
      stack->wr_buf[stack->wr_index++] = '\r';

   if (c == '\f') /* Form feed: clear screen and start from top. */
   {
      char buf[SZ];
      stack->output(stack->fd, "\033[2J\033[h", strlen("\033[2J\033[h")); /* Clears the screen.   */
      sprintf(buf, "\033[%-d;%-dH", 0, 0); /* Top left corner.     */
      stack->output(stack->fd, buf, strlen(buf));
   }
   else
      stack->wr_buf[stack->wr_index++] = c;
}

/*===========================================================================
** 8.2                  push_to_stack()
**===========================================================================
** Description: Pushes a command buffer onto the history stack.
**
** Parameters:  new = command buffer to push
**              stack = root to the history command
**
** Returns:     
**
** Globals:
*/

static void
push_to_stack(hist_stack* new, cmd_hist* stack)
{
   if (stack->last)
      (*stack->last).next = new;

   new->prev = stack->last;
   new->next = 0;
   stack->last = new;

   if (!stack->first)
      stack->first = new;
}

/*===========================================================================
** 8.3                  pop_from_stack()
**===========================================================================
** Description: pops a history buffer from the history stack
**
** Parameters:  old = command buffer to unlink
**              stack = root to command history
**
** Returns:     unlinked history buffer
**
** Globals:
*/

static hist_stack*
pop_from_stack ( hist_stack* old, cmd_hist* stack )
{
   hist_stack *prev,*next;

   prev = old->prev;
   next = old->next;

   if (next)
      next->prev = prev;
   else
      stack->last = prev;

   if (prev)
      prev->next = next;
   else
      stack->first = next;
   
   return old;
}

/*
*****************************************************************************
** 9  GLOBAL FUNCTIONS.
*****************************************************************************
*/

/*
**===========================================================================
** 8.5                  ledit()
**===========================================================================
** Description: Edit a command line and add the result to the command history.
**              Several emacs control characters are recognised.
**              First call will init editor.
**
** Parameters:  character from keyboard
**              Clear history and skip edit if ==0.
**
** Returns:    0 - still working on string, or string too long
**             >0 - CR detected , edit complete.
**             The retuned value is the length of the string in buff +1.
**             The CR is not part of the string.
**             The string is terminated. 
**
** Globals:    many*
*/

int itelnet_LEdit(cmd_hist *stack, unsigned char c, W32 outputLock)
{
   unsigned len, i, j;
   unsigned line_complete;                 /* 0=incomplete , >0 =complete */
   hist_stack *newcmd;

   len = (unsigned short)strlen(stack->cmdbuf);
   line_complete = 0;

   switch ( c )
   {
      case CTRL_N:
      {
         if (stack->curr)
         {
            for (i = stack->pos; i; i--) /* Cursor to start of line */
               wr_char(stack, BS);
          
            for (i = 0; i<len; i++)     /* Erase characters on line */
               wr_char(stack, SP);
          
            for (i = len; i; i--)       /* Cursor to start of line */
               wr_char(stack, BS);
          
            stack->curr = (*stack->curr).next;

            if (stack->curr)
            {
               len = (*stack->curr).len;
               stack->pos = (*stack->curr).pos;
               stack->mark = (*stack->curr).mark;
               strcpy(stack->cmdbuf, (*stack->curr).buf);

               for (i = 0; i < len; i++)
                  wr_char(stack, stack->cmdbuf[i]);

               for (i = len; i > stack->pos; i--) 
                  wr_char(stack, BS);
            }
            else
            {
               stack->pos = 0;
               stack->mark = 0;
               stack->cmdbuf[0]=0;
            }
         }
         break;
      }
      case CTRL_P:
      {
         if (stack->last)
         {
            for (i = stack->pos; i; i--) /* Cursor to start of line */
               wr_char(stack, BS);
              
            for (i = 0; i<len; i++)     /* Erase characters on line */
               wr_char(stack, SP);
              
            for (i = len; i; i--)       /* Cursor to start of line */
               wr_char(stack, BS);

            if (!stack->curr)
            {
               stack->curr = stack->last;
            }
            else
            {   
               strcpy(stack->cmdbuf, (*stack->curr).buf);
               stack->curr =(*stack->curr).prev;
            }
          
            if (!stack->curr)
            {
               wr_char(stack, BEEP);
               stack->curr = stack->first;
            }

            len = (*stack->curr).len;
            stack->pos = (*stack->curr).pos;
            stack->mark = (*stack->curr).mark;
            strcpy((char *) stack->cmdbuf, (const char *) (*stack->curr).buf);
            for (i = 0; i < len; i++)
               wr_char(stack, stack->cmdbuf[i]);
         }
         break;
      }   
      case CTRL_A:
      {
         while (stack->pos) 
         {
            stack->pos--;
            wr_char(stack, BS);
         }
         break;
      }
      case CTRL_B:
      {
         if (stack->pos)
         {
            stack->pos--;
            wr_char(stack, BS);
         }
         break;
      }
      case CTRL_C:
      {
         stack->cmdbuf[0] = '\0';
         stack->pos = 0;
         stack->mark = 0;
         break;
      }
      case CTRL_E:
      {
         while (stack->pos < len)
         {
            wr_char(stack, stack->cmdbuf[stack->pos]);
            stack->pos++;
         }     

         break;
      } 
      case CTRL_F:
      {
         if (stack->pos < len)
         {
            wr_char(stack, stack->cmdbuf[stack->pos]);
            stack->pos++;
         }
         break;
      }
      case CTRL_K:
      {
         strcpy((char *) stack->killbuf, (const char *) &stack->cmdbuf[stack->pos]);

         for (i = stack->pos; i < len; i++) 
            wr_char(stack, SP);

         for (i = stack->pos; i < len; i++)
            wr_char(stack, BS);

         stack->cmdbuf[stack->pos] = '\0';
         break;
      }   
      case CTRL_Y:
      {
         j = (unsigned short)strlen((char *) stack->killbuf);

         if (j != 0  && (j + len) < MAX_LINE) 
         {
            stack->mark = stack->pos;

            for (i = len + j + 1; i >= stack->pos + j; i--) 
               stack->cmdbuf[i] = stack->cmdbuf[i - j];

            for (i = stack->pos; i < stack->pos + j; i++) 
               stack->cmdbuf[i] = stack->killbuf[i - stack->pos];

            len += j;

            for (i = stack->pos; i < len; i++) 
               wr_char(stack, stack->cmdbuf[i]);

            stack->pos += j;

            for (i = stack->pos; i < len; i++) 
               wr_char(stack, BS);
         }
         break;
      }
      case CTRL_W:
      {
         if (stack->mark != stack->pos)
         {
            if (stack->mark > stack->pos)         /* Make mark < pos. */
            {
               j = stack->pos;
               while (stack->mark > stack->pos) 
               {
                  stack->pos++;
                  wr_char(stack, stack->cmdbuf[stack->pos]);
               }
               stack->mark = j;
            }

            len = len - (stack->pos - stack->mark);

            for (i = stack->mark; i < stack->pos; i++) 
               stack->killbuf[i - stack->mark] = stack->cmdbuf[i];

            stack->killbuf[i - stack->mark] = '\0';

            strcpy((char *) &stack->cmdbuf[stack->mark],
                   (const char *) &stack->cmdbuf[stack->pos]);

            for (i = stack->pos - stack->mark; i; i--) 
               wr_char(stack, BS);

            for (i = stack->mark; i < len; i++) 
               wr_char(stack, stack->cmdbuf[i]);

            for (i = stack->pos - stack->mark; i; i--)
               wr_char(stack, ' ');

            for (i = stack->pos - stack->mark; i; i--) 
               wr_char(stack, BS);

            for (i = stack->mark; i < len; i++)
               wr_char(stack, BS);

            stack->pos = stack->mark;
         }
         break;
      }
      case CR:
      {
         if (len)
         {
            if (!stack->curr)
            {
               if (stack->hist_size < 10)
               {
                  newcmd = (hist_stack *)OS_alloc(sizeof(hist_stack),0);
                  stack->hist_size++;
                  push_to_stack(newcmd,stack);
               }
               else
               {
                  newcmd = pop_from_stack(stack->first,stack);
                  push_to_stack(newcmd,stack);
               }

               if (!stack->first)
                  stack->first = stack->last;

               newcmd->len = len;
               newcmd->pos = len; /* Cursor always to end */
               newcmd->mark = stack->mark;
               strcpy((char *) newcmd->buf, (const char *) stack->cmdbuf);
            }
            else
            {
               push_to_stack(pop_from_stack(stack->curr,stack),stack);
               stack->curr=(hist_stack *)0;
            }
         }
         else 
         {                   /* Don't save empty */
            stack->curr = (hist_stack *)0;
         }
 
         line_complete = len+1;
         stack->pos = 0;
         stack->mark = 0;
         break;
      }
      case CTRL_D:
         /* Fall through to case DELETE */
      case DELETE:
      {
         if (stack->pos < len)
         {
            stack->curr = (hist_stack *)0;
            len--;
            for (i = stack->pos; i < len; i++)
            {
               stack->cmdbuf[i] = stack->cmdbuf[i + 1];
               wr_char(stack, stack->cmdbuf[i]);
            }
            stack->cmdbuf[i] = stack->cmdbuf[i + 1];
            stack->cmdbuf[len] = '\0';
            wr_char(stack, SP);
            for (i = stack->pos; i < len; i++) 
            {
               wr_char(stack, BS);
            }
            wr_char(stack, BS);
         }

         break;
      }
      case BS:
      {
         if (stack->pos)
         {
            stack->curr = (hist_stack *)0;
            stack->pos--;
            len--;
            wr_char(stack, BS);
            for (i = stack->pos; i < len; i++)
            {
               stack->cmdbuf[i] = stack->cmdbuf[i + 1];
               wr_char(stack, stack->cmdbuf[i]);
            }
            stack->cmdbuf[i] = stack->cmdbuf[i + 1];
            stack->cmdbuf[len] = '\0';
            wr_char(stack, SP);

            for (i = stack->pos; i < len; i++)
               wr_char(stack, BS);

            wr_char(stack, BS);
         }
         break;
      }
      default:
      {
         if ( 31<c && c<127)
         {
            if (isprint(c) && ((1 + len) < MAX_LINE))
            {
               wr_char(stack, c);
               stack->pos++;
               len++;

               /* make room for char   */
               for (i = len; i >= stack->pos; i--)
                  stack->cmdbuf[i] = stack->cmdbuf[i - 1];

               /* echo new ending      */
               for (i = stack->pos; i < len; i++)
                  wr_char(stack, stack->cmdbuf[i]);

               /* put cursor back     */
               for (i = stack->pos; i < len; i++)
                  wr_char(stack, BS);

               stack->cmdbuf[stack->pos - 1] = c; /* put char in buf     */
               stack->curr = (hist_stack *)0;
            }
         }
         break;
      }
   }             /* end char switch */

   /* Check that output buffer isn't empty before flushing it */
   if (strlen(stack->wr_buf) > 0)
   {
     /* Check if output buffer should be printed on telnet console.
        In case of executing multiple commands that were copy-pasted
        into the telnet console, each command should be printed right
        before it is executed, instead of printing each character
        when reading it from socket, to not interfere with the output
        for preceding commands. */
     if (outputLock == FALSE)
     {
       flush_buf(stack);
     }
     else
     {
       if (line_complete > 0)
       {
         stack->wr_index = 0;
         stack->wr_buf[0] = '\0';
       }
     }
   }
   
   return(line_complete);  
}

/*
*****************************************************************************
** 10 PROCESS ENTRYPOINTS.
*****************************************************************************
*/
