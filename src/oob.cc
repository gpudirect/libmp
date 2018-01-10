#include "oob.hpp"

OOB::Communicator::~Communicator() {}

static oob_creator oob_array[MAX_OOB];
void add_oob_creator(int id, oob_creator c)
{
        oob_array[id] = c;
}

OOB::Communicator *getBestOOB()
{
        OOB::Communicator * c;
        for (int i=0; i<MAX_OOB; ++i) {
                c = (oob_array[i])();
                if (c)
                        break;
        }
        return c;
}
