/*
 *      webhttpd.h
 *
 *      Include file for webhttpd.c 
 *
 *      Specs : http://www.lavrsen.dk/twiki/bin/view/Motion/MotionHttpAPI
 *
 *      Copyright 2004-2005 by Angel Carpintero  (ack@telefonica.net)
 *      This software is distributed under the GNU Public License Version 2
 *      See also the file 'COPYING'.
 *
 */
#ifndef _INCLUDE_WEBHTTPD_H_
#define _INCLUDE_WEBHTTPD_H_

void * motion_web_control(void *arg); 
void httpd_run(struct context **);

#endif
