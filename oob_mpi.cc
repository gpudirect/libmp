#include <mpi.h>
#include "oob.h"

namespace OOB
{
	class OOB_MPI : public Communicator {
		private:
			MPI_Comm comm;
			int initialized;
			int numRanks;
			int myRank;

		public:
			OOB_MPI(void){
				initialized=0;
				numRanks=-1;
				myRank=-1;
				comm=MPI_COMM_WORLD;
	    	}

	    	~OOB_MPI() {}

			int init(int argc, char *argv[]) {
				int ret = MPI_Init(&argc,&argv);
				initialized=1;
				comm=MPI_COMM_WORLD;

				return ret;
			}

			int getSize() {
				if(numRanks == -1)
					MPI_Comm_size(comm, &numRanks);

				return numRanks;
			}

			int getMyId() {
				if(myRank == -1)
					MPI_Comm_rank(comm, &myRank);

				return myRank;
			} 

			void sync() {
				MPI_Barrier(comm);
			} 

			int finalize() {
				return MPI_Finalize();
			}

			int bcast(void * sendBuf, size_t sendSize) {
				return 0;
			}
	};
}


static OOB::Communicator *create() { return new OOB::OOB_MPI(); }

static class update_oob_list {
	public: 
		update_oob_list() {
			add_oob_creator(OOB_PRIORITY_MPI, create);
		}
} tmp;
