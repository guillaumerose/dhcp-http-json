#include "dhcpd.h"

int
find_haddr_with_remotedb(struct host_decl **hp, int htype, unsigned hlen,
		const unsigned char *haddr, const char *file, int line)
{
	struct host_decl * host;
	isc_result_t status;
	char *type_str;

	switch (htype) 
	{
		case HTYPE_ETHER:
			type_str = "ethernet";
			break;
		case HTYPE_IEEE802:
			type_str = "token-ring";
			break;
		case HTYPE_FDDI:
			type_str = "fddi";
			break;
		default:
			type_str = "something else";
	}

	printf("MAC = %s %s\n", type_str, print_hw_addr(htype, hlen, haddr));

	char option_buffer[1024] = "fixed-address 192.168.1.2;";

	host = (struct host_decl *)0;

	status = host_allocate (&host, MDL);
	if (status != ISC_R_SUCCESS)
	{
		log_fatal ("can't allocate host decl struct: %s", 
				isc_result_totext (status)); 
		return (0);
	}

	host->name = "Testing";      
	if (host->name == NULL)
	{
		host_dereference (&host, MDL);
		return (0);
	}

	if (!clone_group (&host->group, root_group, MDL))
	{
		log_fatal ("can't clone group for host %s", host->name);
		host_dereference (&host, MDL);
		return (0);
	}

	int declaration, lease_limit;
	enum dhcp_token token;
	struct parse *cfile;
	isc_result_t res;
	const char *val;

	lease_limit = 1000;

	cfile = (struct parse *) NULL;
	res = new_parse (&cfile, -1, option_buffer, strlen(option_buffer), 
			"Testing", 0);

	if (res != ISC_R_SUCCESS)
		return (lease_limit);

	printf("Sending the following options: '%s'\n", option_buffer);

	declaration = 0;
	do
	{
		token = peek_token (&val, NULL, cfile);
		if (token == END_OF_FILE)
			break;
		declaration = parse_statement (cfile, host->group, HOST_DECL, host, declaration);
	} while (1);

	end_parse (&cfile);

	*hp = host;

	return (lease_limit);
}

