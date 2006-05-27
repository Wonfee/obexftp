#include <common.h>
#if HAVE_BLUETOOTH && HAVE_SDPLIB

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include "client.h"

char **obexftp_discover_bt()
{
	char **res;
  inquiry_info *info = NULL;
  bdaddr_t bdaddr, bdswap;
  char name[248];
  int dev_id = 0;
  int num_rsp = 10;
  int flags = 0;
  int length = 8;
  int dd, i;


	DEBUG(1, "%s: Scanning ...\n", __func__);
  num_rsp = hci_inquiry(dev_id, length, num_rsp, NULL, &info, flags);

  if(num_rsp < 0) 
    {
      perror("Inquiry failed.");
      exit(1);
    }

  if ((dd = hci_open_dev(dev_id)) < 0) 
    {
      perror("HCI device open failed");
      free(info);
      exit(1);
    }
  
  res = calloc(num_rsp + 1, sizeof(char *));
  for(i = 0; i < num_rsp; i++) 
    {
      memset(name, 0, sizeof(name));

      if(hci_read_remote_name(dd, &(info+i)->bdaddr, sizeof(name), name, 100000) < 0)
	{
	  strcpy(name, "n/a");
	}

      bacpy(&bdaddr, &(info+i)->bdaddr);
      baswap(&bdswap, &(info+i)->bdaddr);

	DEBUG(1, "%s: Found\t%s\t%s\n", __func__, batostr(&bdswap), name);
      res[i] = strdup(batostr(&bdswap));
  }
  
  close(dd);
  free(info);
  
  return res;
}

int obexftp_scan_bt(char *addr, int svclass)
{
	int res = -1;
  struct hci_dev_info di;
  sdp_session_t *sess;
  sdp_list_t *attrid, *search, *seq, *loop;
  uint32_t range = SDP_ATTR_PROTO_DESC_LIST;
  /* 0x0000ffff for SDP_ATTR_REQ_RANGE */
  uuid_t root_uuid;
  bdaddr_t bdaddr;

  if (!addr || strlen(addr) != 17)
	  return -1;
  str2ba(addr, &bdaddr);
//  baswap(&bdswap, &bdaddr);
//  *res_bdaddr = batostr(&bdswap);
//  fprintf(stderr, "Browsing %s ...\n", *res_bdaddr);

  // Get local bluetooth address
  if(hci_devinfo(0, &di) < 0) 
    {
      perror("HCI device info failed");
      exit(1);
    }

  // Connect to remote SDP server
  sess = sdp_connect(&di.bdaddr, &bdaddr, SDP_RETRY_IF_BUSY);

  if(!sess) 
    {
      perror("Failed to connect to SDP server");
      exit(1);
    }


  // Build linked lists
  if ((svclass != IRMC_SYNC_SVCLASS_ID) &&
      (svclass != OBEX_OBJPUSH_SVCLASS_ID) &&
      (svclass != OBEX_FILETRANS_SVCLASS_ID))
  {
    svclass = OBEX_FILETRANS_SVCLASS_ID;
  }
  sdp_uuid16_create(&root_uuid, svclass);
  /* or OBEX_FILETRANS_PROFILE_ID? */
  attrid = sdp_list_append(0, &range);
  search = sdp_list_append(0, &root_uuid);

  // Get a linked list of services
  if(sdp_service_search_attr_req(sess, search, SDP_ATTR_REQ_INDIVIDUAL, attrid, &seq) < 0)
    {
      perror("SDP service search");
      sdp_close(sess);
      exit(1);
    }

  sdp_list_free(attrid, 0);
  sdp_list_free(search, 0);

  // Loop through the list of services
  for(loop = seq; loop; loop = loop->next)
    {
      sdp_record_t *rec = (sdp_record_t *) loop->data;
      sdp_list_t *access = NULL;
      int channel;

      // Print the RFCOMM channel
      sdp_get_access_protos(rec, &access);

      if(access)
	{
	  channel = sdp_get_proto_port(access, RFCOMM_UUID);
//	  fprintf(stderr, "Channel: %d\n", channel);
	  res = channel;
	}
    }

    sdp_list_free(seq, 0);
    sdp_close(sess);

    return res;
}

#else
#warning "no bluetooth discovery available"
#include "client.h"
char **obexftp_discover_bt()
{
    return NULL;
}
int obexftp_scan_bt(char *addr, int svclass)
{
    return 0;
}

#endif /* HAVE_BLUETOOTH && HAVE_SDPLIB */