#include "tl.hpp"

TL::Communicator::~Communicator() {}

static tl_creator tl_array[MAX_TL];
void add_tl_creator(int id, tl_creator c)
{
        tl_array[id] = c;
}

TL::Communicator *getTLObj(int tl_index)
{
        if((tl_array[tl_index])())
                return ((tl_array[tl_index])());

        return NULL;
}