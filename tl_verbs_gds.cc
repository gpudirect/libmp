#include "tl_verbs.cc"

namespace TL
{
	class Verbs_GDS : public Verbs {
		private:
			int dummy;

		public:
			int dummy2;

	};
}


static TL::Communicator *create_gds() { return new TL::Verbs_GDS(); }

static class update_tl_list_gds {
	public: 
		update_tl_list_gds() {
			add_tl_creator(TL_INDEX_VERBS_GDS, create_gds);
		}
} list_tl_gds;
