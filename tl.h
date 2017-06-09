#ifndef TL_H
#define TL_H

#include "common.h"
#include "oob.h"

namespace TL
{
    class Communicator {
	    public:	 
	    	Communicator(){ }
	    	virtual ~Communicator()=0;
	    	// ===== SETUP
  		    virtual int setupOOB(OOB::Communicator * input_comm)=0;
  		    virtual void getEnvVars()=0;
	        virtual int setupNetworkDevices()=0;

	        // ===== CONNECTION
	        virtual int createEndpoints()=0;
	        virtual int exchangeEndpoints()=0;
	        virtual int updateEndpoints()=0;

	        // ===== CLEANUP
	        virtual void cleanupInit()=0;
	        virtual int finalize()=0;

	        // ===== PREPARE COMMUNICATION
			virtual int register_buffer(void * addr, size_t length, mp_key_t * mp_req)=0;
			virtual mp_request_t * create_requests(int number)=0;
			virtual mp_key_t * create_keys(int number)=0;

	        // ===== COMMUNICATION
	        virtual int send()=0;
	        virtual int receive(void * rBuf, size_t size, int client_id, mp_request_t * mp_req, mp_key_t * mp_mem_key)=0;
    };
}

TL::Communicator * getTLObj(int tl_index);
typedef TL::Communicator*(*tl_creator)();
void add_tl_creator(int id, tl_creator c);

#endif