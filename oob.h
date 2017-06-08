#ifndef OOB_H
#define OOB_H

#include "common.h"

namespace OOB
{
    class Communicator {
	    public:	 
			Communicator(){};
			virtual ~Communicator()=0;
	        virtual int init(int argc, char *argv[])=0;
	      	virtual int getSize()=0;
	        virtual int getMyId()=0;
   	        virtual void sync()=0;
	        virtual int finalize()=0;
			virtual int alltoall(void * sBuf, size_t sSize, mp_data_type sType, void * rBuf, size_t rSize, mp_data_type rType)=0;
    };

}

OOB::Communicator * getBestOOB();
typedef OOB::Communicator*(*oob_creator)();
void add_oob_creator(int id, oob_creator c);

#endif