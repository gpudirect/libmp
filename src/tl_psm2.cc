#include "tl_psm2.hpp"

const char * psm2_error_to_string(int psm2_err) {
  switch(psm2_err) {
    case PSM2_OK:
      return "No Errors";
      break;
    case PSM2_OK_NO_PROGRESS:
      return "No events progressed on psm2_poll (not fatal)";
      break;
    case PSM2_PARAM_ERR:
      return "Error in a function parameter";
      break;
    case PSM2_NO_MEMORY:
      return "PSM2 ran out of memory";
      break;
    case PSM2_INIT_NOT_INIT:
      return "PSM2 has not been initialized by @ref psm2_init";
      break;
    case PSM2_INIT_BAD_API_VERSION:
      return "API version passed in @ref psm2_init is incompatible";
      break;
    case PSM2_NO_AFFINITY:
      return "PSM2 Could not set affinity";
      break;
    case PSM2_INTERNAL_ERR:
      return "PSM2 Unresolved internal error";
      break;
    case PSM2_SHMEM_SEGMENT_ERR:
      return "PSM2 could not set up shared memory segment";
      break;
    case PSM2_OPT_READONLY:
      return "PSM2 option is a read-only option";
      break;
    case PSM2_TIMEOUT:
      return "PSM2 operation timed out";
      break;
    case PSM2_TOO_MANY_ENDPOINTS:
      return "Too many endpoints";
      break;
    case PSM2_IS_FINALIZED:
      return "Endpoint was closed";
      break;
    default:
      return "Error code unknown";
      break;
  }
}
#if 0
  /*!  */
  PSM2_EP_WAS_CLOSED = 20,
  /*! PSM2 Could not find an OPA Unit */
  PSM2_EP_NO_DEVICE = 21,
  /*! User passed a bad unit or port number */
  PSM2_EP_UNIT_NOT_FOUND = 22,
  /*! Failure in initializing endpoint */
  PSM2_EP_DEVICE_FAILURE = 23,
  /*! Error closing the endpoing error */
  PSM2_EP_CLOSE_TIMEOUT = 24,
  /*! No free ports could be obtained */
  PSM2_EP_NO_PORTS_AVAIL = 25,
  /*! Could not detect network connectivity */
  PSM2_EP_NO_NETWORK = 26,
  /*! Invalid Unique job-wide UUID Key */
  PSM2_EP_INVALID_UUID_KEY = 27,
  /*! Internal out of resources */
  PSM2_EP_NO_RESOURCES = 28,

  /*! Endpoint connect status unknown (because of other failures or if
   * connect attempt timed out) */
  PSM2_EPID_UNKNOWN = 40,
  /*! Endpoint could not be reached by any PSM2 component */
  PSM2_EPID_UNREACHABLE = 41,
  /*! At least one of the connecting nodes was incompatible in endianess */
  PSM2_EPID_INVALID_NODE = 43,
  /*! At least one of the connecting nodes provided an invalid MTU */
  PSM2_EPID_INVALID_MTU = 44,
  /*! At least one of the connecting nodes provided a bad key */
  PSM2_EPID_INVALID_UUID_KEY = 45,
  /*! At least one of the connecting nodes is running an incompatible
   * PSM2 protocol version */
  PSM2_EPID_INVALID_VERSION = 46,
  /*! At least one node provided garbled information */
  PSM2_EPID_INVALID_CONNECT = 47,
  /*! EPID was already connected */
  PSM2_EPID_ALREADY_CONNECTED = 48,
  /*! EPID is duplicated, network connectivity problem */
  PSM2_EPID_NETWORK_ERROR = 49,
  /*! EPID incompatible partition keys */
  PSM2_EPID_INVALID_PKEY = 50,
  /*! Unable to resolve path for endpoint */
  PSM2_EPID_PATH_RESOLUTION = 51,

  /*! MQ Non-blocking request is incomplete */
  PSM2_MQ_NO_COMPLETIONS = 60,
  /*! MQ Message has been truncated at the receiver */
  PSM2_MQ_TRUNCATION = 61,

  /*! AM reply error */
  PSM2_AM_INVALID_REPLY = 70,

    /*! Reserved Value to indicate highest ENUM value */
PSM2_ERROR_LAST = 80
}
#endif

#define PSM2_CHECK(result)                              \
do {                                                    \
    if (PSM2_OK != result) {                            \
        fprintf(stderr, "[%s:%d] psm2 call failed [%d: %s]\n",    \
         __FILE__, __LINE__, result, psm2_error_to_string(result));                           \
        exit(EXIT_FAILURE);                             \
    }                                                   \
    assert(PSM2_OK == result);                                \
} while (0)


int TL::PSM2::setupOOB(OOB::Communicator * input_comm) {

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

int TL::PSM2::setupNetworkDevices() {

  int  ver_major = PSM2_VERNO_MAJOR;
  int  ver_minor = PSM2_VERNO_MINOR;
  psm2_ep_open_opts epopts;
  psm2_uuid_t my_uuid;
  psm2_ep_t  my_ep;
  int unit = -1;
  int port = 0;

  /* Try to initialize PSM2 with the requested library version.
   * In this example, given the use of the PSM2_VERNO_MAJOR and MINOR
   * as defined in the PSM2 headers, ensure that we are linking with
   * the same version of PSM2 as we compiled against. */
  PSM2_CHECK(psm2_init(&ver_major, &ver_minor));

  /* Use a UUID of zero */
  //Change this!!!
  memset(my_uuid, 0, sizeof(psm2_uuid_t)); 

  /* Setup the endpoint options struct */
  PSM2_CHECK(psm2_ep_open_opts_get_defaults(&epopts));
  // We want a stricter timeout and a specific unit
  epopts.timeout = 15*1e9;  // 15 second timeout
  epopts.unit = unit;  // We want a specific unit, -1 would let PSM choose the unit for us.
  epopts.port = port;  // We want a specific unit, <= 0 would let PSM choose the port for us.
  // We've already set affinity, don't let PSM2 do so if it wants to.
  if (epopts.affinity == PSM2_EP_OPEN_AFFINITY_SET)
    epopts.affinity = PSM2_EP_OPEN_AFFINITY_SKIP;

  /* Attempt to open a PSM2 endpoint. This allocates hardware resources. */
  PSM2_CHECK(psm2_ep_open(my_uuid, &epopts, &my_ep, &my_epid));

  return MP_SUCCESS;
}

int TL::PSM2::createEndpoints() {

  client_index = (int *)calloc(oob_size, sizeof(int));
  if (client_index == NULL) {
    mp_err_msg(oob_rank, "allocation failed \n");
    return MP_FAILURE;
  }
  memset(client_index, -1, sizeof(int)*oob_size);

  clients = (psm2_client_t )calloc(peer_count, sizeof(struct psm2_client));
  if (clients == NULL) {
    mp_err_msg(oob_rank, "allocation failed \n");
    return MP_FAILURE;
  }
  memset(clients, 0, sizeof(struct psm2_client)*peer_count);

  epinfo_all =(epid_info_t)calloc(oob_size, sizeof(epid_info));
  if (epinfo_all == NULL) {
    mp_err_msg(oob_rank, "qpinfo allocation failed \n");
    return MP_FAILURE;
  }

  if(i == oob_rank)
  {
    epinfo_all[i].epid = my_epid;
    mp_dbg_msg(oob_rank, "My ep id:%lx\n", epinfo_all[peer].epid);
  }


  return MP_SUCCESS;
}

int TL::PSM2::exchangeEndpoints() {
  int ret = oob_comm->alltoall(NULL, sizeof(epid_info), MP_BYTE, epinfo_all, sizeof(qpinfo_t), MP_BYTE);
  /*creating endpoints for all peers_list*/
  for (int i=0; i<peer_count; i++)
  {
    // MPI rank of i-th peer
    peer = peers_list[i];
    /*rank to peer id mapping */
    client_index[peer] = i;
    mp_dbg_msg(oob_rank, "Creating client %d, peer %d, client_index[peer]: %d\n", i, peer, client_index[peer]);
    /*peer id to rank mapping */
    clients[i].oob_rank = peer;
    clients[i].epid = epinfo_all[i].epid;
    mp_dbg_msg(oob_rank, "Peer %d, EP ID:%lx\n", clients[i].epid);
  }

  return ret;
} 

int TL::PSM2::updateEndpoints() {

  psm2_error_t *epid_errors = (psm2_error_t *) calloc(oob_size, sizeof(psm2_error_t));
  if (epid_errors == NULL) return MP_FAILURE;

  psm2_epaddr_t all_epaddrs_in = (psm2_epaddr_t) calloc(oob_size, sizeof(psm2_epaddr_t));
  if (all_epaddrs == NULL) return MP_FAILURE;

  psm2_epaddr_t *all_epaddrs_out = (psm2_epaddr_t *) calloc(oob_size, sizeof(psm2_epaddr_t));
  if (all_epaddrs == NULL) return MP_FAILURE;

  for (int i=0; i<peer_count; i++)
    all_epaddrs_in[i].epid = clients[i].epid
  
  PSM2_CHECK(psm2_ep_connect(clients[oob_rank].epid, oob_size, all_epaddrs_in,
  NULL, // We want to connect all epids, no mask needed
  epid_errors, all_epaddrs_out, 30*e9)); // 30 second timeout, <1 ns is forever

  for (int i=0; i<peer_count; i++)
    PSM2_CHECK(epid_errors[i]);

  free(epid_errors);

  for (int i=0; i<peer_count; i++)
  {
    //That's the correct way?
    memcpy(clients[i].epaddr, all_epaddrs_out[i], sizeof(all_epaddrs_out[i]));
    free(all_epaddrs_out[i]);
//    clients[i].epaddr.epid = all_epaddrs_out[peer]->epid
    clients[i].epid = clients[i].epaddr.epid;
    mp_dbg_msg(oob_rank, "Peer %d, EP ID:%lx\n", clients[i].epid);
  }
  mp_dbg_msg(oob_rank, "PSM2 connect request processed.\n");
  
  free(all_epaddrs_in);

  oob_comm->barrier();

//  for (int i=0; i<peer_count; i++)
//  {
    //I have to create my own MQ
    PSM2_CHECK(psm2_mq_init(clients[oob_rank].epid, PSM2_MQ_ORDERMASK_NONE, NULL, 0, &(clients[oob_rank].mq)));
    mp_dbg_msg(oob_rank, "PSM2 MQ created.\n");
//  }

  oob_comm->barrier();

  return MP_SUCCESS;
}

void TL::PSM2::cleanupInit() {
  if(epinfo_all)
    free(epinfo_all);
}

int TL::PSM2::finalize() {

  //oob_comm->barrier();

  /* Close down the MQ */
  PSM2_CHECK(psm2_mq_finalize(clients[oob_rank].mq));
  mp_dbg_msg(oob_rank,"PSM2 MQ finalized.\n");
 
  /* Close our ep, releasing all hardware resources.
   * Try to close all connections properly */
  PSM2_CHECK(psm2_ep_close(clients[oob_rank].epid, PSM2_EP_CLOSE_GRACEFUL, 0 /* no timeout */));
  mp_dbg_msg(oob_rank,"PSM2 ep closed.\n");

  /* Release all local PSM2 resources */
  PSM2_CHECK(psm2_finalize());

  free(client_index);  
  free(clients);

  return MP_SUCCESS;     
}

int TL::PSM2::pt2pt_nb_recv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
  int ret = 0;
  psm2_request_t req = NULL;
  psm2_client_t client = &clients[client_index[peer]];
  psm2_mq_req_t req_mq;
  uint64_t msg_tag = 0xABCD;
  uint64_t msg_tag_mask = (uint64_t)-1;
#if 0
  assert(reg);
  req = verbs_new_request(client, MP_RECV, MP_PENDING_NOWAIT);
  assert(req);
#endif

  mp_dbg_msg(oob_rank, "peer=%d req=%p buf=%p size=%zd req id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);
  PSM2_CHECK(psm2_mq_irecv(clients[oob_rank].mq, msg_tag, msg_tag_mask, 
           0,  /* no flags */
           buf, size,
           NULL,  /* no context to add */
           &req_mq  /* track irecv status */
         ));

  *mp_req = (mp_request_t) req_mq; 
  out:
    //if (ret && req) verbs_release_request((verbs_request_t) req);
    return ret;
}

int TL::PSM2::pt2pt_b_send(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
  int ret = 0;
  psm2_request_t req = NULL;
  psm2_client_t client = &clients[client_index[peer]];
  psm2_mq_req_t req_mq;
  uint64_t msg_tag = 0xABCD;
#if 0
  assert(reg);
  req = verbs_new_request(client, MP_RECV, MP_PENDING_NOWAIT);
  assert(req);
#endif

  mp_dbg_msg(oob_rank, "peer=%d req=%p buf=%p size=%zd req id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);
  PSM2_CHECK(psm2_mq_send(clients[oob_rank].mq, 
                        &(clients[oob_rank].epaddr),
                          0, /* no flags */
                          msg_tag, /* tag */
         ));

  *mp_req = (mp_request_t) req_mq; 
  out:
    //if (ret && req) verbs_release_request((verbs_request_t) req);
    return ret;
}

int TL::PSM2::wait(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
{
  psm2_mq_req_t req_mq = mp_req;
  PSM2_CHECK(psm2_mq_wait(&req_mq, NULL));
}

#if 0
int main(int argc, char **argv){
  struct psm2_ep_open_opts o;
  psm2_uuid_tuuid;
  psm2_ep_t  myep;
  psm2_epid_tmyepid;
  psm2_epid_tserver_epid;
  psm2_epid_tepid_array[CONNECT_ARRAY_SIZE];
  int  epid_array_mask[CONNECT_ARRAY_SIZE];
  psm2_error_t   epid_connect_errors[CONNECT_ARRAY_SIZE];
  psm2_epaddr_t  epaddr_array[CONNECT_ARRAY_SIZE];
  int  rc;
  int  ver_major = PSM2_VERNO_MAJOR;
  int  ver_minor = PSM2_VERNO_MINOR;
  char msgbuf[BUFFER_LENGTH];
  psm2_mq_t  q;
  psm2_mq_req_t  req_mq;
  int     is_server = 0;

  if (argc > 2){
    die("To run in server mode, invoke as ./psm2-demo -s\n" \
      "or run in client mode, invoke as ./psm2-demo\n" \
      "Wrong number of args", argc);
  }
  is_server = argc - 1; 
  /* Assume any command line argument is -s */
  memset(uuid, 0, sizeof(psm2_uuid_t)); 
  /* Use a UUID of zero */
  /* Try to initialize PSM2 with the requested library version.
   * In this example, given the use of the PSM2_VERNO_MAJOR and MINOR
   * as defined in the PSM2 headers, ensure that we are linking with
   * the same version of PSM2 as we compiled against. */
  if ((rc = psm2_init(&ver_major, &ver_minor)) != PSM2_OK){
  die("couldn't init", rc);
  }
  printf("PSM2 init done.\n");
  /* Setup the endpoint options struct */
  if ((rc = psm2_ep_open_opts_get_defaults(&o)) != PSM2_OK){
  die("couldn't set default opts", rc);
  }
  printf("PSM2 opts_get_defaults done.\n");
  /* Attempt to open a PSM2 endpoint. This allocates hardware resources. */
  if ((rc = psm2_ep_open(uuid, &o, &myep, &myepid)) != PSM2_OK){
  die("couldn't psm2_ep_open()", rc);
  }
  printf("PSM2 endpoint open done.\n");
  if (is_server){
  write_epid_to_file(myepid);
  } else {
  server_epid = find_server();
  }
  if (is_server){
    /* Server does nothing here. A connection does not have to be
     * established to receive messages. */
    printf("PSM2 server up.\n");
  } else {
    /* Setup connection request info */
    /* PSM2 can connect to a single epid per request,
     * or an arbitrary number of epids in a single connect call.
     * For this example, use part of an array of
     * connection requests. */
    memset(epid_array_mask, 0, sizeof(int) * CONNECT_ARRAY_SIZE);
    epid_array[0] = server_epid; 
    epid_array_mask[0] = 1;
    /* Begin the connection process.
     * note that if a requested epid is not responding,
     * the connect call will still return OK.  
     * The errors array will contain the state of individual 
     * connection requests. */
    if ((rc = psm2_ep_connect(myep,
     CONNECT_ARRAY_SIZE,
     epid_array,
     epid_array_mask,
     epid_connect_errors,
     epaddr_array,
     0 /* no timeout */
    )) != PSM2_OK) { die("couldn't ep_connect", rc); }

    printf("PSM2 connect request processed.\n");
    /* Now check if our connection to the server is ready */
    if (epid_connect_errors[0] != PSM2_OK){
      die("couldn't connect to server", epid_connect_errors[0]);
    }

    printf("PSM2 client-server connection established.\n");
  }

  /* Setup our PSM2 message queue */
  if ((rc = psm2_mq_init(myep, PSM2_MQ_ORDERMASK_NONE, NULL, 0, &q)) != PSM2_OK) { die("couldn't initialize PSM2 MQ", rc); }
  printf("PSM2 MQ init done.\n");

  if (is_server)
  {
    /* Post the receive request */
    if ((rc = psm2_mq_irecv(q,
           0xABCD, 
    /* message tag */
           (uint64_t)-1, 
    /* message tag mask */
           0, 
    /* no flags */
           msgbuf, BUFFER_LENGTH,
           NULL, 
    /* no context to add */
           &req_mq 
    /* track irecv status */
         )) != PSM2_OK){ die("couldn't post psm2_mq_irecv()", rc); }

    printf("PSM2 MQ irecv() posted\n");
    /* Wait until the message arrives */
    if ((rc = psm2_mq_wait(&req_mq, NULL)) != PSM2_OK){
    die("couldn't wait for the irecv", rc);
  }

  printf("PSM2 MQ wait() done.\n");
  printf("Message from client:\n");
  printf("%s", msgbuf);
  unlink("psm2-demo-server-epid");
  } else {
    /* Say hello */
    snprintf(msgbuf, BUFFER_LENGTH,
     "Hello world from epid=0x%lx, pid=%d.\n",
     myepid, getpid());
    if ((rc = psm2_mq_send(q,
          epaddr_array[0], /* destination epaddr */
          0, /* no flags */
          0xABCD, /* tag */
        msgbuf, BUFFER_LENGTH)) != PSM2_OK){ die("couldn't post psm2_mq_isend", rc); }
    printf("PSM2 MQ send() done.\n");
  }

  /* Close down the MQ */
  if ((rc = psm2_mq_finalize(q)) != PSM2_OK){
    die("couldn't psm2_mq_finalize()", rc);
  }

  printf("PSM2 MQ finalized.\n");
  /* Close our ep, releasing all hardware resources.
   * Try to close all connections properly */
  if ((rc = psm2_ep_close(myep, PSM2_EP_CLOSE_GRACEFUL, 0 /* no timeout */)) != PSM2_OK){ die("couldn't psm2_ep_close()", rc); }
  printf("PSM2 ep closed.\n");
  /* Release all local PSM2 resources */
  if ((rc = psm2_finalize()) != PSM2_OK){
    die("couldn't psm2_finalize()", rc);
  }
  
  printf("PSM2 shut down, exiting.\n");
  return 0;
}
#endif