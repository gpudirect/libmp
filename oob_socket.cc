#include "oob.h"

namespace OOB
{
	class OOB_Socket : public Communicator {
		private:
			int initialized;
			int numRanks;
			int myRank;

		public:
			OOB_Socket(void){
				initialized=0;
				numRanks=-1;
				myRank=-1;
				printf("ERROR: Socket-mode not implemented yet");
	    	}

	    	~OOB_Socket() {}

			int init(int argc, char *argv[]) {
				return 0;
			}

			int getSize() {
				return 0;
			}

			int getMyId() {
				return 0;
			} 

			void sync() {
			} 

			int finalize() {
				return 0;
			}

			int alltoall(void * sBuf, size_t sSize, mp_data_type sType, void * rBuf, size_t rSize, mp_data_type rType) {
				return 0;
			}
	};
}


static OOB::Communicator *create() { return new OOB::OOB_Socket(); }

static class update_oob_list {
	public: 
		update_oob_list() {
			add_oob_creator(OOB_PRIORITY_SOCKET, create);
		}
} tmp;
