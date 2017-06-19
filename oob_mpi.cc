#include <mpi.h>
#include "oob.h"


#define MPI_CHECK(stmt)                                             \
do {                                                                \
    int result = (stmt);                                            \
    if (MPI_SUCCESS != result) {                                    \
        char string[MPI_MAX_ERROR_STRING];                          \
        int resultlen = 0;                                          \
        MPI_Error_string(result, string, &resultlen);               \
        mp_err_msg(" (%s:%d) MPI check failed with %d (%*s)\n",     \
                   __FILE__, __LINE__, result, resultlen, string);  \
        exit(-1);                                                   \
    }                                                               \
} while(0)

MPI_Datatype mp_get_mpi_datatype(mp_data_type type) {
	switch(type) {			
		case MP_CHAR:		
			return MPI_CHAR;
		case MP_BYTE:		
			return MPI_BYTE;
		case MP_INT:		
			return MPI_INT;	
		case MP_LONG:		
			return MPI_LONG;
		case MP_FLOAT:		
			return MPI_FLOAT;
		case MP_DOUBLE:		
			return MPI_DOUBLE;
		default:			
			return MPI_BYTE;
	}						
}

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

			int alltoall(void * sBuf, size_t sSize, mp_data_type sType, void * rBuf, size_t rSize, mp_data_type rType) {

				if(sBuf == NULL)
				{
					MPI_CHECK(MPI_Alltoall(
							MPI_IN_PLACE, 0, 0, 
							rBuf, rSize, mp_get_mpi_datatype(rType),
							comm)
					);
				}
				else
				{
					MPI_CHECK(MPI_Alltoall(
							sBuf, sSize, mp_get_mpi_datatype(sType), 
							rBuf, rSize, mp_get_mpi_datatype(rType),
							comm)
					);
				}

				return OOB_SUCCESS;
			}

			int allgather(void * sBuf, size_t sSize, mp_data_type sType, void * rBuf, size_t rSize, mp_data_type rType) {
				if(sBuf == NULL)
				{
					MPI_CHECK(MPI_Allgather(
							MPI_IN_PLACE, 0, 0, 
							rBuf, rSize, mp_get_mpi_datatype(rType),
							comm));
				}
				else
				{
					MPI_CHECK(MPI_Allgather(
							sBuf, sSize, mp_get_mpi_datatype(sType), 
							rBuf, rSize, mp_get_mpi_datatype(rType),
							comm));
				}
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
