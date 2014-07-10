/*
 * portmacros.h
 *
 * Created: 11/16/2011 11:05:46 AM
 *  Author: steve
 */ 


#ifndef PORTMACROS_H_
#define PORTMACROS_H_

#define PORT_(port) PORT ## port 
#define DDR_(port)  DDR  ## port 
#define PIN_(port)  PIN  ## port 

#define PORT(port) PORT_(port) 
#define DDR(port)  DDR_(port) 
#define PIN(port)  PIN_(port) 

#endif /* PORTMACROS_H_ */