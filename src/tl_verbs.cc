#include "tl_verbs.hpp"

void  TL::Verbs::verbs_allocate_requests() {
  int i;
  mem_region_t *mem_region;
  verbs_request_t mp_requests;

  assert (mp_request_free_list == NULL);

  mem_region = (mem_region_t *) calloc (1, sizeof (mem_region_t));
  if (mem_region == NULL) {
    mp_err_msg(oob_rank, "memory allocation for mem_region failed \n");
    exit(EXIT_FAILURE);
  }
  if (mem_region_list == NULL) {
    mem_region_list = mem_region;
    mem_region->next = NULL;
  } else {
    mem_region->next = mem_region_list;
  }

  mem_region->region = (verbs_request_t ) calloc (verbs_request_limit, sizeof(struct verbs_request));
  if (mem_region == NULL) {
    mp_err_msg(oob_rank, "memory allocation for request_region failed \n");
    exit(EXIT_FAILURE);
  }

  mp_requests = (verbs_request_t ) mem_region->region;
  mp_request_free_list = mp_requests;
  for (i=0; i<verbs_request_limit-1; i++) {
    mp_requests[i].next = mp_requests + i + 1;
  }
  mp_requests[i].next = NULL;
}

verbs_request_t  TL::Verbs::verbs_get_request()
{
  verbs_request_t req = NULL;

  if (mp_request_free_list == NULL) {
    verbs_allocate_requests();
    assert(mp_request_free_list != NULL);
  }

  req = mp_request_free_list;
  mp_request_free_list = mp_request_free_list->next;
  
  req->next = NULL;
  req->prev = NULL;

  return req;
}

verbs_request_t  TL::Verbs::verbs_new_request(verbs_client_t client, mp_req_type_t type, mp_state_t state) //, struct CUstream_st *stream)
{
  verbs_request_t req = verbs_get_request();
  //mp_dbg_msg(oob_rank, "new req=%p\n", req);
  if (req) {
      req->peer = client->oob_rank;
      req->sgv = NULL;
      req->next = NULL;
      req->prev = NULL;
      req->trigger = 0;
      req->type = type;
      req->status = state;
      req->id = verbs_get_request_id(client, type);
  }

  return req;
}

void TL::Verbs::verbs_release_request(verbs_request_t req)
{
  req->next = mp_request_free_list;
  req->prev = NULL;
  req->type = MP_NULL;

  mp_request_free_list = req;
}

int TL::Verbs::verbs_get_request_id(verbs_client_t client, mp_req_type_t type)
{
    assert(client->last_req_id < UINT_MAX);
    return ++client->last_req_id;
}

int TL::Verbs::verbs_query_print_qp(struct ibv_qp *qp, verbs_request_t req)
{
    assert(qp);
    struct ibv_qp_attr qp_attr;
    struct ibv_qp_init_attr qp_init_attr;

    memset(&qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));

    if (ibv_query_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_CAP, &qp_init_attr))
    {
        mp_err_msg(oob_rank, "client query qp attr fail\n");
        return MP_FAILURE;
    }
   
    mp_warn_msg(oob_rank, "Init QP attr: max_send_wr=%d, max_recv_wr=%d, max_inline_data=%d, qp_type=%d\nCurrent QP attr: QP State=%d QP Cur State=%d Access Flags=%d max_send_wr=%d, max_recv_wr=%d, max_inline_data=%d\n",
                    qp_init_attr.cap.max_send_wr,
                    qp_init_attr.cap.max_recv_wr,
                    qp_init_attr.cap.max_inline_data,
                    qp_init_attr.qp_type,
                    qp_attr.qp_state,
                    qp_attr.cur_qp_state,
                    qp_attr.qp_access_flags,
                    qp_attr.cap.max_send_wr,
                    qp_attr.cap.max_recv_wr,
                    qp_attr.cap.max_inline_data
    );

#if 0
    if(req != NULL)
    {
        if(req->in.sr.exp_opcode == IBV_EXP_WR_SEND)
            mp_warn_msg("This is an IBV_EXP_WR_SEND\n");
            
        if(req->in.sr.exp_opcode == IBV_EXP_WR_RDMA_WRITE)
            mp_warn_msg("This is an IBV_EXP_WR_RDMA_WRITE\n");
    }
#endif
    return MP_SUCCESS;
}


int TL::Verbs::verbs_post_recv(verbs_client_t client, verbs_request_t req)
{
    int progress_retry=0, ret=0, ret_progress=0;

    if(!client || !req)
        return MP_FAILURE;
    do
    {
    	ret = ibv_post_recv(client->qp, &req->in.rr, &req->out.bad_rr);
        if(ret == ENOMEM)
        {
        	if(qp_query == 0)
            {
                verbs_query_print_qp(client->qp, req);
                qp_query=1;
            }

            ret_progress = verbs_progress_single_flow(RX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg(oob_rank, "verbs_progress_single_flow failed. Error: %d\n", ret_progress);
                break;
            }
            mp_warn_msg(oob_rank, "RX_FLOW was full. verbs_progress_single_flow called %d times (ret=%d)\n", (progress_retry+1), ret);
            progress_retry++;
        }
    } while(ret == ENOMEM && progress_retry <= MP_MAX_PROGRESS_FLOW_TRY);

    qp_query=0;
    return ret;
}


int TL::Verbs::verbs_post_send(verbs_client_t client, verbs_request_t req)
{
    int progress_retry=0, ret=MP_SUCCESS, ret_progress=0;

    if(!client || !req)
    {
      mp_err_msg(oob_rank, "client: %d, req: %d\n", (client) ? 1 : 0, (req) ? 1 : 0);
      return MP_FAILURE;
    }

    do
    {
    	ret = ibv_exp_post_send(client->qp, &req->in.sr, &req->out.bad_sr);
      if(ret == ENOMEM)
      {
        if(qp_query == 0)
        {
            verbs_query_print_qp(client->qp, req);
            qp_query=1;
        }

        ret_progress = verbs_progress_single_flow(TX_FLOW);
        if(ret_progress != MP_SUCCESS)
        {
            mp_err_msg(oob_rank, "verbs_progress_single_flow failed. Error: %d\n", ret_progress);
            break;
        }
        mp_warn_msg(oob_rank, "TX_FLOW was full. verbs_progress_single_flow called %d times (ret=%d)\n", (progress_retry+1), ret);
        progress_retry++;
      }
    } while(ret == ENOMEM && progress_retry <= MP_MAX_PROGRESS_FLOW_TRY);

out:
    qp_query=0;
    return ret;
}

int TL::Verbs::verbs_progress_single_flow(mp_flow_t flow)
{
    int i, ne = 0, ret = 0;
    struct ibv_cq *cq = NULL; 
    int cqe_count = 0;

    if (!wc) {
        wc = (struct ibv_wc*)calloc(cq_poll_count, sizeof(struct ibv_wc));
    }

    const char *flow_str = verbs_flow_to_str(flow);

    //printf("flow=%s\n", flow_str);

    //useful only for sync_event
    for (i=0; i<peer_count; i++) {
        verbs_client_t client = &clients[i];
        cq = (flow == TX_FLOW) ? client->send_cq : client->recv_cq; 

        // WARNING: can't progress a CQE if it is associated to an RX req
        // which is dependent upon GPU work which has not been triggered yet
        //Async only!!!
        //cqe_count = verbs_client_can_poll(client, flow);
        cqe_count = cq_poll_count; //MIN(cqe_count, cq_poll_count);
        if (!cqe_count) {
            printf("cannot poll client[%d] flow=%s\n", client->oob_rank, flow_str);
            continue;
        }
        ne = ibv_poll_cq(cq, cqe_count, wc);
        //printf("client[%d] flow=%s cqe_count=%d nw=%d\n", client->oob_rank, flow_str, cqe_count, ne);
        if (ne == 0) {
            //if (errno) printf("client[%d] flow=%s errno=%s\n", client->oob_rank, flow_str, strerror(errno));
        }
        else if (ne < 0) {
            mp_err_msg(oob_rank, "error %d(%d) in ibv_poll_cq\n", ne, errno);
            ret = MP_FAILURE;
            goto out;
        } else if (ne) {
            int j;
            for (j=0; j<ne; j++) {
                struct ibv_wc *wc_curr = wc + j;
                mp_dbg_msg(oob_rank, "client:%d wc[%d]: status=%x(%s) opcode=%x byte_len=%d wr_id=%"PRIx64"\n",
                           client->oob_rank, j,
                           wc_curr->status, ibv_wc_status_str(wc_curr->status), 
                           wc_curr->opcode, wc_curr->byte_len, wc_curr->wr_id);
				
                verbs_request_t req = (verbs_request_t ) wc_curr->wr_id;

                if (wc_curr->status != IBV_WC_SUCCESS) {
                    mp_err_msg(oob_rank, "ERROR!!! completion error, status:'%s' client:%d rank:%d req:%p flow:%s\n",
                               ibv_wc_status_str(wc_curr->status),
                               i, client->oob_rank,
                               req, flow_str);
                    exit(EXIT_FAILURE);
                    //continue;
                }

                if (req) { 
                    mp_dbg_msg(oob_rank, "polled new CQE for req:%p flow:%s id=%d peer=%d type=%d\n", req, flow_str, req->id, req->peer, req->type);

                    if (!(req->status == MP_PENDING_NOWAIT || req->status == MP_PENDING))
                        mp_err_msg(oob_rank, "status not pending, value: %d \n", req->status);

                    if (req->status == MP_PENDING_NOWAIT) {
                    } else if (req->status != MP_PENDING) {
                        mp_err_msg(oob_rank, "status not pending, value: %d \n", req->status);
                        exit(EXIT_FAILURE);
                    }

                    ACCESS_ONCE(client->last_done_id) = req->id;
                    verbs_progress_request(req);
                } else {
                    mp_warn_msg(oob_rank, "received completion with null wr_id \n");
                }
            }
        }
    }

out: 
    return ret;
}

int TL::Verbs::verbs_progress_request(verbs_request_t req)
{
	switch(req->status) {
		case MP_PREPARED:
		    mp_dbg_msg(oob_rank, "req=%p id=%d PREPARED\n", req, req->id);
		    break;
		case MP_PENDING_NOWAIT:
		    mp_dbg_msg(oob_rank, "req=%p id=%d NOWAIT\n", req, req->id);
		case MP_PENDING:
		    mp_dbg_msg(oob_rank, "req=%p id=%d PENDING->COMPLETE\n", req, req->id);
		    req->status = MP_COMPLETE;
		    cleanup_request(req);
		    break;
		case MP_COMPLETE:
		    mp_warn_msg(oob_rank, "attempt at progressing a complete req:%p \n", req);
		    break;
		default:
		    mp_err_msg(oob_rank, "invalid status %d for req:%p \n", req->status, req);
		    break;
	}
	return MP_SUCCESS;
}

int TL::Verbs::cleanup_request(verbs_request_t req)
{
    if (req->sgv) {
        free(req->sgv);
        req->sgv = NULL;
    }

    return MP_SUCCESS;
}

void TL::Verbs::verbs_env_vars() {
	//Which HCA
	char *value = NULL;
	value = getenv("VERBS_IB_HCA"); 
	if (value != NULL) {
		ib_req_dev = value;
	}

	value = getenv("VERBS_ENABLE_UD"); 
	if (value != NULL) {
		verbs_enable_ud = atoi(value);
	}

	value = getenv("MP_ENABLE_DEBUG");
    if (value) {
        int en = atoi(value);
        mp_dbg_is_enabled = !!en;
        //printf("MP_ENABLE_DEBUG=%s\n", value);
    } else
        mp_dbg_is_enabled = 0;

    value = getenv("MP_ENABLE_WARN");
    if (value) {
        int en = atoi(value);
        mp_warn_is_enabled = !!en;
        //printf("MP_ENABLE_DEBUG=%s\n", value);
    } else
        mp_warn_is_enabled = 0;


    value = getenv("VERBS_CQ_POLL_COUNT"); 
	if (value != NULL) {
		cq_poll_count = atoi(value);
	}

	value = getenv("VERBS_IB_CQ_DEPTH");
	if (value != NULL) {
		num_cqes = atoi(value);
		mp_dbg_msg(oob_rank, "setting num_cqes=%d\n", num_cqes);
	}

	value = getenv ("VERBS_IB_MAX_SGL"); 
	if (value != NULL) { 
		ib_max_sge = atoi(value);
	}
#if 0
	value = getenv ("VERBS_ENABLE_IPC"); 
	if (value != NULL) { 
		mp_enable_ipc = atoi(value);
	}
#endif
}

int TL::Verbs::setupOOB(OOB::Communicator * input_comm) {

	int i,j;

	if(!input_comm)
		return MP_FAILURE;

	oob_comm=input_comm;
	oob_size=oob_comm->getSize();
	oob_rank=oob_comm->getMyId();

	// init peers_list_list    
	for (i=0, j=0; i<oob_size; ++i) {
	        // self reference is forbidden
	    if (i!=oob_rank) {
	        peers_list[j] = i;
	       // rank_to_peer[i] = j;
	        ++j;
	    }
	    /* else {
	        rank_to_peer[i] = bad_peer;
	    }*/
	}
	peer_count = j;
	if(oob_size-1 != peer_count)
		return MP_FAILURE;

	return MP_SUCCESS;
}

int TL::Verbs::setupNetworkDevices() {
	int i;

	/*pick the right device*/
	dev_list = ibv_get_device_list (&num_devices);
	if (dev_list == NULL) {
		mp_err_msg(oob_rank, "ibv_get_device_list returned NULL \n");
		return MP_FAILURE;
	}

	ib_dev = dev_list[0];
	if (ib_req_dev != NULL) {
		for (i=0; i<num_devices; i++) {
		  select_dev = ibv_get_device_name(dev_list[i]);
		  if (strstr(select_dev, ib_req_dev) != NULL) {
		    ib_dev = dev_list[i];
		    mp_dbg_msg(oob_rank, "using IB device: %s \n", ib_req_dev);
		    break;
		  }
		}
		if (i == num_devices) {
		  select_dev = ibv_get_device_name(dev_list[0]);
		  ib_dev = dev_list[0];
		  printf("request device: %s not found, defaulting to %s \n", ib_req_dev, select_dev);
		}
	}
	mp_warn_msg(oob_rank, "HCA dev: %s\n", ibv_get_device_name(ib_dev));

	/*create context, pd, cq*/
	ib_ctx = (ib_context_t *) calloc (1, sizeof (ib_context_t));
	if (ib_ctx == NULL) {
		mp_err_msg(oob_rank, "ib_ctx allocation failed \n");
		return MP_FAILURE;
	}

	ib_ctx->context = ibv_open_device(ib_dev);
	if (ib_ctx->context == NULL) {
		mp_err_msg(oob_rank, "ibv_open_device failed \n");
		return MP_FAILURE;
	}

	/*get device attributes and check relevant leimits*/
	if (ibv_query_device(ib_ctx->context, &dev_attr)) {
		mp_err_msg(oob_rank, "query_device failed \n"); 	 
		return MP_FAILURE;	
	}

	if (ib_max_sge > dev_attr.max_sge) {
		mp_err_msg(oob_rank, "warning!! requested sgl length longer than supported by the adapter, reverting to max, requested: %d max: %d \n", ib_max_sge, dev_attr.max_sge);
		ib_max_sge = dev_attr.max_sge;
	}

	ib_ctx->pd = ibv_alloc_pd (ib_ctx->context);
	if (ib_ctx->pd == NULL) {
		fprintf(stderr ,"ibv_alloc_pd failed \n");
		return MP_FAILURE;
	}

	ibv_query_port (ib_ctx->context, ib_port, &ib_port_attr);

	/*allocate requests*/
	verbs_allocate_requests();
	assert(mp_request_free_list != NULL);

	return MP_SUCCESS;
}

int TL::Verbs::createEndpoints() {
	int ret, i;
	int old_errno=0;
	struct ibv_cq *send_cq = NULL;
	struct ibv_cq *recv_cq = NULL;
	struct ibv_exp_cq_init_attr cq_attr;
	struct ibv_exp_qp_init_attr ib_qp_init_attr;

	/*establish connections*/
	client_index = (int *)calloc(oob_size, sizeof(int));
	if (client_index == NULL) {
		mp_err_msg(oob_rank, "allocation failed \n");
		return MP_FAILURE;
	}
	memset(client_index, -1, sizeof(int)*oob_size);

	clients = (verbs_client_t )calloc(peer_count, sizeof(struct verbs_client));
	if (clients == NULL) {
		mp_err_msg(oob_rank, "allocation failed \n");
		return MP_FAILURE;
	}
	memset(clients, 0, sizeof(struct verbs_client)*peer_count);

	qpinfo_all =(qpinfo_t *)calloc(oob_size, sizeof(qpinfo_t));
	if (qpinfo_all == NULL) {
		mp_err_msg(oob_rank, "qpinfo allocation failed \n");
		return MP_FAILURE;
	}
	//client_count = peer_count
	/*creating qps for all peers_list*/
	for (i=0; i<peer_count; i++)
	{
		// MPI rank of i-th peer
		peer = peers_list[i];
		/*rank to peer id mapping */
		client_index[peer] = i;
		mp_dbg_msg(oob_rank, "Creating client %d, peer %d, client_index[peer]: %d\n", i, peer, client_index[peer]);
		/*peer id to rank mapping */
		clients[i].oob_rank = peer;
		clients[i].last_req_id = 0;
		clients[i].last_done_id = 0;

		memset(clients[i].last_posted_trigger_id, 0, sizeof(clients[0].last_posted_trigger_id));
		memset(clients[i].last_posted_tracked_id, 0, sizeof(clients[0].last_posted_tracked_id));
		memset(clients[i].last_tracked_id,        0, sizeof(clients[0].last_tracked_id));
		memset(clients[i].last_trigger_id,        0, sizeof(clients[0].last_trigger_id));
		memset(&ib_qp_init_attr, 0, sizeof(ib_qp_init_attr));

		ib_qp_init_attr.cap.max_send_wr  = ib_tx_depth;
		ib_qp_init_attr.cap.max_recv_wr  = ib_rx_depth;
		ib_qp_init_attr.cap.max_send_sge = ib_max_sge;
		ib_qp_init_attr.cap.max_recv_sge = ib_max_sge;

		//create QP, set to INIT state and exchange QPN information
		if (verbs_enable_ud) {
		  ib_qp_init_attr.qp_type = IBV_QPT_UD;
		  ib_qp_init_attr.cap.max_inline_data = ib_inline_size;
		} else {
		  ib_qp_init_attr.qp_type = IBV_QPT_RC;
		  ib_qp_init_attr.cap.max_inline_data = ib_inline_size;
		}

		// ================= CQs creation =================
        cq_attr.comp_mask = 0; //IBV_CREATE_CQ_ATTR_PEER_DIRECT;
        //CQE Compressing IBV_EXP_CQ_COMPRESSED_CQE? https://www.mail-archive.com/netdev@vger.kernel.org/msg110152.html
        cq_attr.flags = 0; // see ibv_exp_cq_create_flags
        cq_attr.res_domain = NULL;
        //Not Async case, no need of peer attrs
        cq_attr.peer_direct_attrs = NULL; //peer_attr;

        old_errno = errno;

        send_cq = ibv_exp_create_cq(ib_ctx->context, ib_qp_init_attr.cap.max_send_wr /*num cqe*/, NULL /* cq_context */, NULL /* channel */, 0 /*comp_vector*/, &cq_attr);
        if (!send_cq) {
            mp_err_msg(oob_rank, "error %d in ibv_exp_create_cq, old errno %d\n", errno, old_errno);
            return MP_FAILURE;
        }

        recv_cq = ibv_exp_create_cq(ib_ctx->context, ib_qp_init_attr.cap.max_recv_wr /*num cqe*/, NULL /* cq_context */, NULL /* channel */, 0 /*comp_vector*/, &cq_attr);
        if (!recv_cq) {
            mp_err_msg(oob_rank, "error %d in ibv_exp_create_cq, old errno %d\n", errno, old_errno);
            goto err_free_tx_cq;
        }

		    // ================= QP creation =================
        ib_qp_init_attr.send_cq = send_cq;
        ib_qp_init_attr.recv_cq = recv_cq;
        ib_qp_init_attr.pd = ib_ctx->pd;
        ib_qp_init_attr.comp_mask |= IBV_EXP_QP_INIT_ATTR_PD;
        //todo: No need of IBV_QP_INIT_ATTR_PEER_DIRECT (IBV_EXP_QP_INIT_ATTR_PEER_DIRECT)
        //ib_qp_init_attr.comp_mask |= IBV_QP_INIT_ATTR_PEER_DIRECT;
        ib_qp_init_attr.peer_direct_attrs = NULL; //peer_attr;
 
        clients[i].qp = ibv_exp_create_qp(ib_ctx->context, &ib_qp_init_attr);
        if (!clients[i].qp)  {
                ret = EINVAL;
                mp_err_msg(oob_rank, "error in ibv_exp_create_qp\n");
                goto err_free_rx_cq;
		}

    //======== QP CREATED
    clients[i].send_cq = clients[i].qp->send_cq;
    clients[i].send_cq_curr_offset = 0;
    clients[i].recv_cq = clients[i].qp->recv_cq;
    clients[i].recv_cq_curr_offset = 0;

		//clients[i].send_cq = &clients[i].qp->send_cq;
		//clients[i].recv_cq = &clients[i].qp->recv_cq;

		assert(clients[i].qp);
		assert(clients[i].send_cq);
		assert(clients[i].recv_cq);
		
		struct ibv_qp_attr ib_qp_attr;
		memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
		ib_qp_attr.qp_state        = IBV_QPS_INIT;
		ib_qp_attr.pkey_index      = 0;
		ib_qp_attr.port_num        = ib_port;
		int flags = 0;
		if (verbs_enable_ud) { 
		  ib_qp_attr.qkey            = 0;
		  flags                      = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
		} else {
		  ib_qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE;
		  flags                      = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
		}

		ret = ibv_modify_qp (clients[i].qp, &ib_qp_attr, flags);
		if (ret != 0) {
		  mp_err_msg(oob_rank, "Failed to modify QP to INIT: %d, %s\n", ret, strerror(errno));
		  goto err_free_qps;
		}

		qpinfo_all[peer].lid = ib_port_attr.lid;
		qpinfo_all[peer].qpn = clients[i].qp->qp_num;
		qpinfo_all[peer].psn = 0;
		mp_dbg_msg(oob_rank, "QP lid:%04x qpn:%06x psn:%06x\n", 
		         qpinfo_all[peer].lid,
		         qpinfo_all[peer].qpn,
		         qpinfo_all[peer].psn);
	}

	return MP_SUCCESS;

	//======== ERROR CASES ========
	err_free_qps:
		for (i=0; i<peer_count; i++)
		{
			mp_dbg_msg(oob_rank, "destroying QP client %d\n", i);
			ret = ibv_destroy_qp(clients[i].qp);
			if (ret)
				mp_err_msg(oob_rank, "error %d destroying QP client %d\n", ret, i);
		}
	
	err_free_rx_cq:
		ret = ibv_destroy_cq(recv_cq);
		if (ret)
			mp_err_msg(oob_rank, "error %d destroying RX CQ\n", ret);

	err_free_tx_cq:
		mp_dbg_msg(oob_rank, "destroying TX CQ\n");
		ret = ibv_destroy_cq(send_cq);
		if (ret)
			mp_err_msg(oob_rank, "error %d destroying TX CQ\n", ret);

	return MP_FAILURE;
	//============================
}

int TL::Verbs::exchangeEndpoints() {
	int ret = oob_comm->alltoall(NULL, sizeof(qpinfo_t), MP_BYTE, qpinfo_all, sizeof(qpinfo_t), MP_BYTE);
	return ret;
}	

int TL::Verbs::verbs_update_qp_rtr(verbs_client_t client, int index, struct ibv_qp * qp) {
  int ret,flags;
  struct ibv_qp_attr ib_qp_attr;
  assert(qp);
  
  peer = peers_list[index];
  memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
  if (verbs_enable_ud) { 
    ib_qp_attr.qp_state = IBV_QPS_RTR;
    flags = IBV_QP_STATE;
  } else { 
    ib_qp_attr.qp_state           = IBV_QPS_RTR;
    ib_qp_attr.path_mtu           = ib_port_attr.active_mtu;
    ib_qp_attr.dest_qp_num        = qpinfo_all[peer].qpn;
    ib_qp_attr.rq_psn             = qpinfo_all[peer].psn;
    ib_qp_attr.ah_attr.dlid       = qpinfo_all[peer].lid;
    ib_qp_attr.max_dest_rd_atomic     = 1;
    ib_qp_attr.min_rnr_timer          = 12;
    ib_qp_attr.ah_attr.is_global      = 0;
    ib_qp_attr.ah_attr.sl             = 0;
    ib_qp_attr.ah_attr.src_path_bits  = 0;
    ib_qp_attr.ah_attr.port_num       = ib_port;
    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU
        | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN
        | IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC;
  }

  ret = ibv_modify_qp(qp, &ib_qp_attr, flags);
  if (ret != 0) {
    printf("Failed to modify RC QP to RTR\n");
    return MP_FAILURE;
  }

  return MP_SUCCESS;
}

int TL::Verbs::verbs_update_qp_rts(verbs_client_t client, int index, struct ibv_qp * qp) {
  int ret, flags;
  struct ibv_qp_attr ib_qp_attr;
  assert(qp);
  peer = peers_list[index];
  memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
  if (verbs_enable_ud) { 
    ib_qp_attr.qp_state       = IBV_QPS_RTS;
    ib_qp_attr.sq_psn         = 0;
    flags = IBV_QP_STATE | IBV_QP_SQ_PSN; 
  } else { 
    ib_qp_attr.qp_state       = IBV_QPS_RTS;
    ib_qp_attr.sq_psn         = 0;
    ib_qp_attr.timeout        = 20;
    ib_qp_attr.retry_cnt      = 7;
    ib_qp_attr.rnr_retry      = 7;
    ib_qp_attr.max_rd_atomic  = 1;
    flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT
      | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY
      | IBV_QP_MAX_QP_RD_ATOMIC;
  }

  ret = ibv_modify_qp(qp, &ib_qp_attr, flags);
  if (ret != 0) {
    printf("Failed to modify RC QP to RTR\n");
    return MP_FAILURE;
  }

  if (verbs_enable_ud) {
    mp_err_msg(oob_rank, "setting up connection with peer: %d lid: %d qpn: %d \n", peer, qpinfo_all[peer].lid, qpinfo_all[peer].qpn);

    struct ibv_ah_attr ib_ah_attr;
    memset(&ib_ah_attr, 0, sizeof(ib_ah_attr));
    ib_ah_attr.is_global     = 0;
    ib_ah_attr.dlid          = qpinfo_all[peer].lid;
    ib_ah_attr.sl            = 0;
    ib_ah_attr.src_path_bits = 0;
    ib_ah_attr.port_num      = ib_port;

    client->ah = ibv_create_ah(ib_ctx->pd, &ib_ah_attr);
    if (!client->ah) {
        mp_err_msg(oob_rank, "Failed to create AH\n");
        return MP_FAILURE;
    }

    client->qpn = qpinfo_all[peer].qpn; 
  }

  return MP_SUCCESS;
}

int TL::Verbs::updateEndpoints() {
  for (int i=0; i<peer_count; i++) {
    verbs_update_qp_rtr(&clients[i], i, clients[i].qp);
  }

  oob_comm->barrier();

  for (int i=0; i<peer_count; i++) {
    verbs_update_qp_rts(&clients[i], i, clients[i].qp);
  }

  if (verbs_enable_ud) {
    int result = register_region_buffer(ud_padding, UD_ADDITION, &ud_padding_reg);
    assert(result == MP_SUCCESS);
  }

  oob_comm->barrier();

  #ifdef HAVE_IPC
    //ipc connection setup
    node_info_all = malloc(sizeof(struct node_info)*oob_size);
    if (!node_info_all) {
      mp_err_msg(oob_rank, "Failed to allocate node info array \n");
    return MP_FAILURE;
    }

    if(!gethostname(node_info_all[oob_rank].hname, 20)) {
      mp_err_msg(oob_rank, "gethostname returned error \n");
    return MP_FAILURE;
    }

    CUDA_CHECK(cudaGetDevice(&node_info_all[oob_rank].gpu_id));

    oob_comm->allgather(NULL, 0, MP_CHAR, node_info_all, sizeof(struct node_info), MP_CHAR);

    int cidx, can_access_peer; 
    for (i=0; i<oob_size; i++) {
      can_access_peer = 0;
      cidx = client_index[i];

      if (i == oob_size) { 
        /*pick first rank on the node as the leader*/
        if (!smp_num_procs) smp_leader = i;
        smp_local_rank = smp_num_procs;       
        smp_num_procs++;
        ipc_num_procs++;
        continue;
      }

      if (!strcmp(node_info_all[i].hname, node_info_all[oob_rank].hname)) {
        /*pick first rank on the node as the leader*/
        if (!smp_num_procs) smp_leader = i; 
        clients_async[cidx].is_local = 1;
        clients_async[cidx].local_rank = smp_num_procs;
        smp_num_procs++; 
        CUDA_CHECK(cudaDeviceCanAccessPeer(&can_access_peer, node_info_all[oob_rank].gpu_id, node_info_all[i].gpu_id));
      }

      if (can_access_peer) { 
        ipc_num_procs++;
        clients_async[cidx].can_use_ipc = 1;
      } 
    }

    if (smp_num_procs > 1) {
      shm_client_bufsize = sizeof(smp_buffer_t)*smp_depth;
      shm_proc_bufsize = shm_client_bufsize*smp_num_procs;
      shm_filesize = sizeof(smp_buffer_t)*smp_depth*smp_num_procs*smp_num_procs;

      //setup shared memory buffers 
      sprintf(shm_filename, "/dev/shm/libmp_shmem-%s-%d.tmp", node_info_all[oob_rank].hname, getuid());
      mp_dbg_msg(oob_rank, "shemfile %s\n", shm_filename);

      shm_fd = open(shm_filename, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
      if (shm_fd < 0) {
        mp_err_msg(oob_rank, "opening shm file failed \n");
        return MP_FAILURE;
      }

      if (smp_leader == oob_rank) {
        if (ftruncate(shm_fd, 0)) {
          mp_err_msg(oob_rank, "clearning up shm file failed \n");
          /* to clean up tmp shared file */
          return MP_FAILURE;
        }

        if (ftruncate(shm_fd, shm_filesize)) {
          mp_err_msg(oob_rank, "setting up shm file failed \n");
          /* to clean up tmp shared file */
          return MP_FAILURE;
        }
      }
    }

    oob_comm->barrier();

    if (smp_num_procs > 1) {
      struct stat file_status;

      /* synchronization between local processes */
      do {
        if (fstat(shm_fd, &file_status) != 0) {
          mp_err_msg(oob_rank, "fstat on shm file failed \n");
          /* to clean up tmp shared file */
          return MP_FAILURE;
        }
        usleep(1);
      } while (file_status.st_size != shm_filesize);

      /* mmap of the shared memory file */
      shm_mapptr = mmap(0, shm_filesize, (PROT_READ | PROT_WRITE), (MAP_SHARED), shm_fd, 0);
      if (shm_mapptr == (void *) -1) {
        mp_err_msg(oob_rank, "mmap on shm file failed \n");
        /* to clean up tmp shared file */
        return MP_FAILURE;
      }
    }

    for (i=0; i<oob_size; i++) {
      int j, cidx;

      cidx = client_index[i]; 

      if (clients_async[cidx].is_local) {
        assert(smp_local_rank >= 0);

        clients_async[cidx].smp.local_buffer = (void *)((char *)shm_mapptr 
        + shm_proc_bufsize*smp_local_rank 
        + shm_client_bufsize*clients_async[cidx].local_rank);

        memset(clients_async[cidx].smp.local_buffer, 0, shm_client_bufsize);

        for (j=0; j<smp_depth; j++) { 
          clients_async[cidx].smp.local_buffer[j].free = 1;
        }

        clients_async[cidx].smp.remote_buffer = (void *)((char *)shm_mapptr 
                          + shm_proc_bufsize*clients[cidx].local_rank 
                          + shm_client_bufsize*smp_local_rank);
      }
    }
  #endif

  return MP_SUCCESS;
}

void TL::Verbs::cleanupInit() {
	if(qpinfo_all)
		free(qpinfo_all);
}


int TL::Verbs::finalize() {
	int i, retcode=MP_SUCCESS;
	mem_region_t *mem_region = NULL;

	oob_comm->barrier();

	/*destroy IB resources*/
	for (i=0; i<peer_count; i++) {
	  	int ret=0;
        
        assert(clients[i].qp);
        ret = ibv_destroy_qp(clients[i].qp);
        if (ret) {
            mp_err_msg(oob_rank, "error %d in destroy_qp\n", ret);
            retcode = ret;
        }


        assert(clients[i].send_cq);
        ret = ibv_destroy_cq(clients[i].send_cq);
        if (ret) {
            mp_err_msg(oob_rank, "error %d in destroy_cq send_cq\n", ret);
            retcode = ret;
        }

        assert(clients[i].recv_cq);
        ret = ibv_destroy_cq(clients[i].recv_cq);
        if (ret) {
            mp_err_msg(oob_rank, "error %d in destroy_cq recv_cq\n", ret);
            retcode = ret;
        }


        //free(clients[i]);
	}

	ibv_dealloc_pd (ib_ctx->pd);
	ibv_close_device (ib_ctx->context);

	while (mem_region_list != NULL) {
		mem_region = mem_region_list;
		mem_region_list = mem_region_list->next;
		free(mem_region->region);
		free(mem_region);
	}

	/*free all buffers*/
	free(ib_ctx);
	free(client_index);
	free(clients);

	return retcode;			
}

// ===== COMMUNICATION
int TL::Verbs::register_region_buffer(void * addr, size_t length, mp_region_t * mp_reg) {

	int flags=1;
	assert(mp_reg);

	verbs_region_t reg = (verbs_region_t)calloc(1, sizeof(struct verbs_region));
	if (!reg) {
	  mp_err_msg(oob_rank, "malloc returned NULL while allocating struct mp_reg\n");
	  return MP_FAILURE;
	}

//GPUDirect RDMA
#ifdef HAVE_CUDA
	/*set SYNC MEMOPS if its device buffer*/
	unsigned int type;
	size_t size;
	CUdeviceptr base;
	CUresult curesult; 
	curesult = cuPointerGetAttribute((void *)&type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)addr);
	if ((curesult == CUDA_SUCCESS) && (type == CU_MEMORYTYPE_DEVICE)) { 
		CU_CHECK(cuMemGetAddressRange(&base, &size, (CUdeviceptr)addr));
		CU_CHECK(cuPointerSetAttribute(&flags, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, base)); 
			mp_dbg_msg(oob_rank, "Addr:%p Memory type: CU_MEMORYTYPE_DEVICE\n", addr);
	}
#endif

	if (verbs_enable_ud) {
	  mp_warn_msg(oob_rank, "UD enabled, registering buffer for LOCAL_WRITE\n");
	  flags = IBV_ACCESS_LOCAL_WRITE;
	} else { 
	  flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
	          IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;
	}

	mp_dbg_msg(oob_rank, "ibv_reg_mr addr:%p size:%zu flags=0x%08x\n", addr, length, flags);
	// note: register addr, not base. no advantage in registering the whole buffer as we don't
	// maintain a registration cache yet
	reg->mr = ibv_reg_mr(ib_ctx->pd, addr, length, flags);
	if (!reg->mr) {
		mp_err_msg(oob_rank, "ibv_reg_mr returned NULL for addr:%p size:%zu errno=%d(%s)\n", addr, length, errno, strerror(errno));
		return MP_FAILURE;
	}

	reg->key = reg->mr->lkey;
	mp_dbg_msg(oob_rank, "Registered key: key=%p value=%x buf=%p\n", reg, reg->key, addr);
	*mp_reg = (mp_region_t)reg;

	return MP_SUCCESS;
}

int TL::Verbs::unregister_region(mp_region_t *reg_)
{
	verbs_region_t reg = (verbs_region_t) *reg_; 

	assert(reg);
	assert(reg->mr);
	ibv_dereg_mr(reg->mr);
	free(reg);

	return MP_SUCCESS;
}

mp_region_t * TL::Verbs::create_regions(int number) {

	verbs_region_t * reg;

	if(number <= 0) {
		mp_err_msg(oob_rank, "erroneuos requests number specified (%d)\n", number);
		return NULL;
	}

	reg = (verbs_region_t *)calloc(number, sizeof(verbs_region_t));
	if (!reg) {
	  mp_err_msg(oob_rank, "malloc returned NULL while allocating struct mp_reg\n");
	  return NULL;
	}

	return (mp_region_t *) reg;
}


mp_request_t * TL::Verbs::create_requests(int number) {
	verbs_request_t * req;
	if(number <= 0) {
		mp_err_msg(oob_rank, "erroneuos requests number specified (%d)\n", number);
		return NULL;
	}

	req = (verbs_request_t *) calloc(number, sizeof(verbs_request_t));
	if(!req) {
		mp_err_msg(oob_rank, "calloc returned NULL while allocating struct verbs_request_t\n");
		return NULL;
	}
	
	return (mp_request_t *) req;
}

int TL::Verbs::verbs_fill_recv_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_recv_wr * rr, struct ibv_sge * sg_entry, struct ibv_sge ud_sg_entry[2])
{
  int ret = MP_SUCCESS;

    rr->next = NULL;
    rr->wr_id = req_id;

    if (verbs_enable_ud) { 
      verbs_region_t ud_reg = (verbs_region_t ) ud_padding_reg;

      rr->num_sge = 2;
      rr->sg_list = (ud_sg_entry);
      ud_sg_entry[0].length = UD_ADDITION;
      ud_sg_entry[0].lkey = ud_reg->key;
      ud_sg_entry[0].addr = (uintptr_t)(ud_padding);
      ud_sg_entry[1].length = size;
      ud_sg_entry[1].lkey = reg->key;
      ud_sg_entry[1].addr = (uintptr_t)(buf);  
    } else { 
      rr->num_sge = 1;
      rr->sg_list = sg_entry;
      sg_entry->length = size;
      sg_entry->lkey = reg->key;
      sg_entry->addr = (uintptr_t)(buf);
    }
    return ret;
}


int TL::Verbs::pt2pt_nb_recv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
	int ret = 0;
	verbs_request_t req = NULL;
	verbs_region_t reg = (verbs_region_t) *mp_reg;
	verbs_client_t client = &clients[client_index[peer]];


	assert(reg);
	req = verbs_new_request(client, MP_RECV, MP_PENDING_NOWAIT);
	assert(req);

	mp_dbg_msg(oob_rank, "peer=%d req=%p buf=%p size=%zd req id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);


  #ifdef HAVE_IPC
      if ((*client)->can_use_ipc)
          track_ipc_stream_rreq(peer, (*req));
  #else
    ret = verbs_fill_recv_request(buf, size, peer, reg, (uintptr_t) req, &(req->in.rr), &(req->sg_entry), req->ud_sg_entry);
    if (ret) goto out;
  	//progress (remove) some request on the RX flow if is not possible to queue a recv request
  	ret = verbs_post_recv(client, req);
  	if (ret) {
  		mp_err_msg(oob_rank, "Posting recv failed: %s \n", strerror(errno));
  		goto out;
  	}
  #endif
	*mp_req = (mp_request_t) req; 
  out:
    if (ret && req) verbs_release_request((verbs_request_t) req);
  	return ret;
}

int TL::Verbs::pt2pt_nb_recvv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
  int ret = 0;
  verbs_request_t req = NULL;
  verbs_region_t reg = (verbs_region_t) *mp_reg;
  verbs_client_t client = &clients[client_index[peer]];

  if (nvecs > ib_max_sge) {
    mp_err_msg(oob_rank, "exceeding max supported vector size: %d \n", ib_max_sge);
    ret = MP_FAILURE;
    goto out;
  }

  assert(reg);
  req = verbs_new_request(client, MP_RECV, MP_PENDING_NOWAIT);
  assert(req);

  mp_dbg_msg(oob_rank, "peer=%d req=%p v=%p nvecs=%d req id=%d reg=%p key=%x\n", peer, req, v, nvecs, req->id, reg, reg->key);

  for (int i=0; i < nvecs; ++i) {
    req->sgv[i].length = v[i].iov_len;
    req->sgv[i].lkey = reg->key;
    req->sgv[i].addr = (uint64_t)(v[i].iov_base);
  }

  req->in.rr.next = NULL;
  req->in.rr.wr_id = (uintptr_t) req;
  req->in.rr.num_sge = nvecs;
  req->in.rr.sg_list = req->sgv;

  //progress (remove) some request on the RX flow if is not possible to queue a recv request
  ret = verbs_post_recv(client, req);
  if (ret) {
    mp_err_msg(oob_rank, "Posting recv failed: %s \n", strerror(errno));
    goto out;
  }

  *mp_req = (mp_request_t) req; 
  out:
    return ret;
}

int TL::Verbs::verbs_fill_send_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      struct ibv_ah *ah, uint32_t qpn)
{
  int ret = MP_SUCCESS;

    sr->next = NULL;
    sr->exp_send_flags = IBV_EXP_SEND_SIGNALED;
    sr->exp_opcode = IBV_EXP_WR_SEND;
    sr->wr_id = req_id; //(uintptr_t) (*req);
    sr->num_sge = 1;
    sr->sg_list = sg_entry;

    if (verbs_enable_ud) {
        sr->wr.ud.ah = ah;
        sr->wr.ud.remote_qpn = qpn; 
        sr->wr.ud.remote_qkey = 0;
    }

    sg_entry->length = size;
    sg_entry->lkey = reg->key;
    sg_entry->addr = (uintptr_t)(buf);

    return ret;
}

int TL::Verbs::pt2pt_nb_send(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
	int ret = 0;
	verbs_request_t req = NULL;
	verbs_region_t reg = (verbs_region_t) *mp_reg;
	verbs_client_t client = &clients[client_index[peer]];

	assert(reg);
	req = verbs_new_request(client, MP_SEND, MP_PENDING_NOWAIT);
	assert(req);

	mp_dbg_msg(oob_rank, "peer=%d req=%p buf=%p size=%zd req id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);

  #ifdef HAVE_IPC
    if (*client->can_use_ipc)
    {
      ipc_handle_cache_entry_t *entry = NULL;
      smp_buffer_t *smp_buffer = NULL;

      //try to find in local handle cache
      ipc_handle_cache_find (buf, size, &entry, oob_rank);
      if (!entry) { 
        entry = malloc(sizeof(ipc_handle_cache_entry_t));
        if (!entry) { 
          mp_err_msg(oob_rank, "cache entry allocation failed \n"); 
          ret = MP_FAILURE;
          goto out;
        }

        CU_CHECK(cuMemGetAddressRange((CUdeviceptr *)&entry->base, &entry->size, (CUdeviceptr) buf));
        CU_CHECK(cuIpcGetMemHandle (&entry->handle, (CUdeviceptr)entry->base));

        ipc_handle_cache_insert(entry, oob_rank);
      }

      assert(entry != NULL);
      smp_buffer = client->smp.remote_buffer + client->smp.remote_head;
      assert(smp_buffer->free == 1);  

      memcpy((void *)&smp_buffer->handle, (void *)&entry->handle, sizeof(CUipcMemHandle));  
      smp_buffer->base_addr = entry->base;
      smp_buffer->base_size = entry->size;
      smp_buffer->addr = buf;
      smp_buffer->size = size;
      smp_buffer->offset = (uintptr_t)buf - (uintptr_t)entry->base;
      smp_buffer->sreq = req; 
      smp_buffer->free = 0; 
      smp_buffer->busy = 1;
      (*client)->smp.remote_head = ((*client)->smp.remote_head + 1)%smp_depth;   
    }
  #else
    ret = verbs_fill_send_request(buf, size, peer, reg, (uintptr_t) req, &(req->in.sr), &(req->sg_entry), client->ah, client->qpn);
    if (ret) goto out;

    ret = verbs_post_send(client, req);
    if (ret) {
    	mp_err_msg(oob_rank, "posting send failed: %s \n", strerror(errno));
    	goto out;
    }
  #endif
	*mp_req = (mp_request_t) req;

  out:
    if (ret && req) verbs_release_request((verbs_request_t) req);
    return ret;
}

int TL::Verbs::pt2pt_nb_sendv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req)
{
  int ret = 0;
  verbs_request_t req = NULL;
  verbs_region_t reg = (verbs_region_t) *mp_reg;
  verbs_client_t client = &clients[client_index[peer]];

  if (nvecs > ib_max_sge) {
    mp_err_msg(oob_rank, "exceeding max supported vector size: %d \n", ib_max_sge);
    ret = MP_FAILURE;
    goto out;
  }

  assert(reg);
  req = verbs_new_request(client, MP_SEND, MP_PENDING_NOWAIT);
  assert(req);
  req->sgv = (struct ibv_sge *) calloc(nvecs, sizeof(struct ibv_sge));
  assert(req->sgv);

  mp_dbg_msg(oob_rank, "req=%p id=%d\n", req, req->id);

  for (int i=0; i < nvecs; ++i) {
    req->sgv[i].length = v[i].iov_len;
    req->sgv[i].lkey = reg->key;
    req->sgv[i].addr = (uint64_t)(v[i].iov_base);
  }

  if (verbs_enable_ud) {
      req->in.sr.wr.ud.ah = client->ah;
      req->in.sr.wr.ud.remote_qpn = client->qpn;
      req->in.sr.wr.ud.remote_qkey = 0;
  }

  req->in.sr.next = NULL;
  req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
  req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
  req->in.sr.wr_id = (uintptr_t) req;
  req->in.sr.num_sge = nvecs;
  req->in.sr.sg_list = req->sgv;

  ret = verbs_post_send(client, req);

 out:
  return ret;
}

int TL::Verbs::wait_word(uint32_t *ptr, uint32_t value, int flags)
{
    int ret = MP_SUCCESS;
    int cond = 0;
    int cnt = 0;
    assert(ptr);
    
    while (1) {
        switch(flags) {
        case VERBS_WAIT_EQ:   cond = (ACCESS_ONCE(*ptr) >  value); break;
        case VERBS_WAIT_GEQ:  cond = (ACCESS_ONCE(*ptr) >= value); break;
        case VERBS_WAIT_AND:  cond = (ACCESS_ONCE(*ptr) &  value); break;
        default: ret = EINVAL; goto out; break;
        }
        if (cond) break;
        arch_cpu_relax();
        ++cnt;
        if (cnt > 10000) {
            sched_yield();
            cnt = 0;
        }
    }
  out:
    return ret;
}

int TL::Verbs::wait(mp_request_t *req)
{
  int ret = 0;

  ret = wait_all(1, req);

  return ret;
}

int TL::Verbs::wait_all(int count, mp_request_t *req_)
{
  int complete = 0, ret = 0;
  /*poll until completion*/
  while (complete < count)
  {
    verbs_request_t req = (verbs_request_t) req_[complete];
    if (!verbs_req_can_be_waited(req))
    {
      mp_dbg_msg(oob_rank, "cannot wait req:%p status:%d id=%d peer=%d type=%d flags=%08x\n", req, req->status, req->id, req->peer, req->type, req->flags);
      ret = EINVAL;
      goto out;
    }
    if (req->status == MP_PENDING_NOWAIT) {
      mp_dbg_msg(oob_rank, "PENDING_NOWAIT->PENDING req:%p status:%d id=%d peer=%d type=%d\n", req, req->status, req->id, req->peer, req->type);
      req->status = MP_PENDING;
    }

    complete++;
  }

  complete=0;

  while (complete < count) {
      verbs_request_t req = (verbs_request_t) req_[complete];
      
      while (req->status != MP_COMPLETE) {
          ret = verbs_progress_single_flow (TX_FLOW);
          if (ret) {
              goto out;
          }
          ret = verbs_progress_single_flow (RX_FLOW);
          if (ret) {
              goto out;
          }
      }
      
      complete++;
  }

  if(!ret)
  {
    complete=0;
    while (complete < count) {
      verbs_request_t req = (verbs_request_t) req_[complete];
      if (req->status == MP_COMPLETE)
          verbs_release_request((verbs_request_t) req);
      else
          ret = MP_FAILURE;

      complete++;
    }
  }

out:
    return ret;
}

//====================== ONE-SIDED ======================
/*one-sided operations: window creation, put and get*/
exchange_win_info * TL::Verbs::verbs_window_create(void *addr, size_t size, verbs_window_t *window_t) 
{
  int result = MP_SUCCESS;
  verbs_window_t window;

  exchange_win_info *exchange_win = NULL; 

  window = (verbs_window_t) calloc(1, sizeof(struct verbs_window));
  assert(window != NULL); 

  window->base_ptr = (void ** ) calloc(peer_count, sizeof(void *));
  assert(window->base_ptr != NULL);
  window->rkey = (uint32_t * ) calloc(peer_count, sizeof(uint32_t));
  assert(window->rkey != NULL);
  window->rsize = (uint64_t * ) calloc(peer_count, sizeof(uint64_t));
  assert(window->rsize != NULL);

  exchange_win = (exchange_win_info * ) calloc(oob_size, sizeof(exchange_win_info));
  assert(exchange_win != NULL); 

  result = register_region_buffer(addr, size, (mp_region_t *) &window->reg);  
  assert(result == MP_SUCCESS); 

  exchange_win[oob_rank].base_addr = addr;
  exchange_win[oob_rank].rkey = window->reg->mr->rkey; 
  exchange_win[oob_rank].size = size;

  oob_comm->allgather(NULL, 0, MP_CHAR, exchange_win, sizeof(exchange_win_info), MP_CHAR);
  //loop
  *window_t = window;

  return exchange_win;
}

int TL::Verbs::onesided_window_create(void *addr, size_t size, mp_window_t *window_t)
{
  int i, peer;
  verbs_window_t window;
  exchange_win_info * exchange_win = verbs_window_create(addr, size, &window);
  assert(exchange_win);

  /*populate window address info*/
  for (i=0; i<peer_count; i++) { 
    peer = clients[i].oob_rank;

    window->base_ptr[i] = exchange_win[peer].base_addr;
    window->rkey[i] = exchange_win[peer].rkey;
    window->rsize[i] = exchange_win[peer].size;
  }
  *window_t = (mp_window_t) window;
  free(exchange_win);
  //Required ??
  oob_comm->barrier();

  return MP_SUCCESS;
}

int TL::Verbs::onesided_window_destroy(mp_window_t *window_t)
{
  verbs_window_t window = (verbs_window_t) *window_t;
  int result = MP_SUCCESS;

  unregister_region((mp_region_t *) &window->reg);
  
  free(window->base_ptr);
  free(window->rkey);

  free(window);

  return result;
}

int TL::Verbs::verbs_fill_put_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      int client_id, verbs_window_t window, size_t displ, int * req_flags, int flags)
{
  int ret = MP_SUCCESS;

  *req_flags = flags;
  sr->next = NULL;
  
  if (flags & MP_PUT_NOWAIT)
    sr->exp_send_flags = 0;
  else
    sr->exp_send_flags = IBV_EXP_SEND_SIGNALED;

  if (flags & MP_PUT_INLINE)
    sr->exp_send_flags |= IBV_EXP_SEND_INLINE;
  
  sr->exp_opcode = IBV_EXP_WR_RDMA_WRITE;
  sr->wr_id = req_id;
  sr->num_sge = 1;
  sr->sg_list = sg_entry;

  sg_entry->length = size;
  sg_entry->lkey = reg->key;
  sg_entry->addr = (uintptr_t)buf;

  sr->wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
  sr->wr.rdma.rkey = window->rkey[client_id];

  return ret;
}

int TL::Verbs::onesided_nb_put (void *buf, int size, mp_region_t *mp_reg, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, int flags) 
{
	int ret = MP_SUCCESS;
	verbs_request_t req;
	verbs_region_t reg = (verbs_region_t) *mp_reg;
	verbs_window_t window = (verbs_window_t) *window_t;
	int client_id = client_index[peer];
	verbs_client_t client = &clients[client_id];

  if (verbs_enable_ud) { 
    mp_err_msg(oob_rank, "put/get not supported with UD \n");
    ret = MP_FAILURE;
    goto out;
  }

	assert(displ < window->rsize[client_id]);
	req = verbs_new_request(client, MP_RDMA, MP_PENDING_NOWAIT);
	assert(req);

  ret = verbs_fill_put_request(buf, size, peer, reg, (uintptr_t) req, &(req->in.sr), &(req->sg_entry), client_id, window, displ, &(req->flags), flags);
  if(ret)
  {
    mp_err_msg(oob_rank, "verbs_fill_put_request error: %s \n", strerror(errno));
    goto out;
  }

	ret = verbs_post_send(client, req);
	if (ret) {
		mp_err_msg(oob_rank, "posting send failed: %s \n", strerror(errno));
		goto out;
	}

	*mp_req = req;

  out:
    if (ret && req) verbs_release_request((verbs_request_t) req);
  	return ret;
}


int TL::Verbs::verbs_fill_get_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      int client_id, verbs_window_t window, size_t displ)
{
  int ret = MP_SUCCESS;

  sr->next = NULL;
  sr->exp_send_flags = IBV_EXP_SEND_SIGNALED;
  sr->exp_opcode = IBV_EXP_WR_RDMA_READ;
  sr->wr_id = req_id;
  sr->num_sge = 1;
  sr->sg_list = sg_entry;

  sg_entry->length = size;
  sg_entry->lkey = reg->key;
  sg_entry->addr = (uintptr_t)buf;

  sr->wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
  sr->wr.rdma.rkey = window->rkey[client_id];


  return ret;
}


int TL::Verbs::onesided_nb_get(void *buf, int size, mp_region_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req) 
{
  int ret = 0;
  verbs_request_t req;
  verbs_region_t reg = (verbs_region_t) *reg_t;
  verbs_window_t window = (verbs_window_t) *window_t;
  int client_id = client_index[peer];
  verbs_client_t client = &clients[client_id];

  if (verbs_enable_ud) { 
  	mp_err_msg(oob_rank, "put/get not supported with UD \n");
  	ret = MP_FAILURE;
  	goto out;
  }

  assert(displ < window->rsize[client_id]);
  req = verbs_new_request(client, MP_RDMA, MP_PENDING_NOWAIT);
  assert(req);

  ret = verbs_fill_get_request(buf, size, peer, reg, (uintptr_t) req, &(req->in.sr), &(req->sg_entry), client_id, window, displ);
  if(ret) goto out;

  ret = verbs_post_send(client, req);
  if (ret) {
  	mp_err_msg(oob_rank, "posting send failed: %s \n", strerror(errno));
  	goto out;
  }

  *mp_req = req;

  out:
    if (ret && req) verbs_release_request((verbs_request_t) req);
    return ret;
}

//============== GPUDirect Async - Verbs_Async child class ==============
int TL::Verbs::setup_sublayer(int par1) { return MP_SUCCESS; }
int TL::Verbs::pt2pt_nb_send_async(void * rBuf, size_t size, int client_id, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream async_stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_nb_sendv_async(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_b_send_async(void * rBuf, size_t size, int client_id, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream async_stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_send_prepare(void *buf, int size, int peer, mp_region_t *reg_t, mp_request_t *req_t) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t *mp_req) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_b_send_post_async(mp_request_t *req_t, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_b_send_post_all_async(int count, mp_request_t *req_t, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_nb_send_post_async(mp_request_t *req_t, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::pt2pt_nb_send_post_all_async(int count, mp_request_t *req_t, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::wait_async (mp_request_t *req_t, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::wait_all_async(int count, mp_request_t *req_t, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::wait_word_async(uint32_t *ptr, uint32_t value, int flags, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::onesided_nb_put_async(void *src, int size, mp_region_t *mp_reg, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, int flags, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::onesided_nb_get_async(void *buf, int size, mp_region_t *mp_reg, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::onesided_put_prepare (void *src, int size, mp_region_t *mp_reg, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t, int flags) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::onesided_nb_put_post_async(mp_request_t *mp_req, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::onesided_nb_put_post_all_async (int count, mp_request_t *mp_req, asyncStream stream) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::progress_requests (int count, mp_request_t *mp_req) { fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_alloc(mp_comm_descriptors_queue_t *pdq){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_free(mp_comm_descriptors_queue_t *pdq){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_add_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_add_wait_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_add_wait_recv(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_add_wait_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value, int flags){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_add_write_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_queue_post_async(asyncStream stream, mp_comm_descriptors_queue_t *pdq, int flags){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_kernel(KERNEL_DESCRIPTOR_SEM *psem, uint32_t *ptr, uint32_t value){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_kernel_send(mp_kernel_send_desc_t * sinfo, mp_request_t *mp_req){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
int TL::Verbs::descriptors_kernel_wait(mp_kernel_wait_desc_t * winfo, mp_request_t *req_t){ fprintf(stderr, "Need to use Verbs_Async child class\n"); return MP_FAILURE; }
//================================================================

static TL::Communicator *create_verbs() { return new TL::Verbs(); }

static class update_tl_list_verbs {
	public: 
		update_tl_list_verbs() {
			add_tl_creator(TL_INDEX_VERBS, create_verbs);
		}
} list_tl_verbs;
