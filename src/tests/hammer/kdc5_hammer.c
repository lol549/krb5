/*
 * tests/hammer/kdc5_hammer.c
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * Initialize a credentials cache.
 */

#include <stdio.h>

#include "k5-int.h"
#include "com_err.h"

#define KRB5_DEFAULT_OPTIONS 0
#define KRB5_DEFAULT_LIFE 60*60*8 /* 8 hours */
#define KRB5_RENEWABLE_LIFE 60*60*2 /* 2 hours */

extern int optind;
extern char *optarg;
char *prog;

static int brief;
static char *cur_realm = 0;

krb5_error_code
krb5_parse_lifetime (time, len)
    char *time;
    long *len;
{
    *len = atoi (time) * 60 * 60; /* XXX stub version */
    return 0;
}
    
krb5_data tgtname = {
    0, 
    KRB5_TGS_NAME_SIZE,
    KRB5_TGS_NAME
};

int verify_cs_pair 
	PROTOTYPE((krb5_context,
		   char *,
		   krb5_principal,
		   char *,
		   char *,
		   int, int, int,
		   krb5_ccache));

int get_tgt 
	PROTOTYPE((krb5_context,
		   char *,
		   krb5_principal *,
		   krb5_ccache));

static void
usage(who, status)
char *who;
int status;
{
    fprintf(stderr,
	    "usage: %s -p prefix -n num_to_check [-d dbpathname] [-r realmname]\n",
	    who);
    fprintf(stderr, "\t [-D depth] [-k keytype] [-e etype] [-M mkeyname]\n");
    fprintf(stderr, "\t [-P preauth type] [-R repeat_count]\n");

    exit(status);
}

static krb5_preauthtype * patype = NULL, patypedata[2] = { 0, -1 };
static krb5_enctype etype = 0xffff;
static krb5_context test_context;
static krb5_keytype keytype;

void
main(argc, argv)
    int argc;
    char **argv;
{
    krb5_ccache ccache = NULL;
    char *cache_name = NULL;		/* -f option */
    int option;
    int errflg = 0;
    krb5_error_code code;
    int num_to_check, n, i, j, repeat_count, counter;
    int n_tried, errors, keytypedone;
    char prefix[BUFSIZ], client[4096], server[4096];
    int depth;
    char ctmp[4096], ctmp2[BUFSIZ], stmp[4096], stmp2[BUFSIZ];
    krb5_principal client_princ;
    krb5_error_code retval;

    krb5_init_context(&test_context);
    krb5_init_ets(test_context);

    if (strrchr(argv[0], '/'))
	prog = strrchr(argv[0], '/')+1;
    else
	prog = argv[0];

    num_to_check = 0;
    depth = 1;
    repeat_count = 1;
    brief = 0;
    n_tried = 0;
    errors = 0;
    keytypedone = 0;

    while ((option = getopt(argc, argv, "D:p:n:c:R:k:P:e:bv")) != EOF) {
	switch (option) {
	case 'b':
	    brief = 1;
	    break;
	case 'v':
	    brief = 0;
	    break;
	case 'R':
	    repeat_count = atoi(optarg); /* how many times? */
	    break;
	case 'r':
	    cur_realm = optarg;
	    break;
	case 'D':
	    depth = atoi(optarg);       /* how deep to go */
	    break;
	case 'p':                       /* prefix name to check */
	    strcpy(prefix, optarg);
	    break;
       case 'n':                        /* how many to check */
	    num_to_check = atoi(optarg);
	    break;
	case 'k':
	    keytype = atoi(optarg);
	    keytypedone++;
	    break;
	case 'e':
	    etype = atoi(optarg);
	    break;
	case 'P':
	    patypedata[0] = atoi(optarg);
	    patype = patypedata;
	    break;
	case 'c':
	    if (ccache == NULL) {
		cache_name = optarg;
		
		code = krb5_cc_resolve (test_context, cache_name, &ccache);
		if (code != 0) {
		    com_err (prog, code, "resolving %s", cache_name);
		    errflg++;
		}
	    } else {
		fprintf(stderr, "Only one -c option allowed\n");
		errflg++;
	    }
	    break;
	case '?':
	default:
	    errflg++;
	    break;
	}
    }

    if (!(num_to_check && prefix[0])) usage(prog, 1);

    if (!keytypedone)
	keytype = DEFAULT_KDC_KEYTYPE;

    if (!cur_realm) {
	if (retval = krb5_get_default_realm(test_context, &cur_realm)) {
	    com_err(prog, retval, "while retrieving default realm name");
	    exit(1);
	}	    
    }

    if (!valid_keytype(keytype)) {
      com_err(prog, KRB5_PROG_KEYTYPE_NOSUPP,
	      "while setting up keytype %d", keytype);
      exit(1);
    }

    if (etype == 0xffff)
	etype = krb5_keytype_array[keytype]->system->proto_enctype;

    if (!valid_etype(etype)) {
	com_err(prog, KRB5_PROG_ETYPE_NOSUPP,
		"while setting up etype %d", etype);
	exit(1);
    }

    if (ccache == NULL) {
	if (code = krb5_cc_default(test_context, &ccache)) {
	    com_err(prog, code, "while getting default ccache");
	    exit(1);
	}
    }

    memset(ctmp, 0, sizeof(ctmp));
    memset(stmp, 0, sizeof(stmp));

    for (counter = 0; counter < repeat_count; counter++) {
      fprintf(stderr, "\nRound %d\n", counter);

      for (n = 1; n <= num_to_check; n++) {
	/* build the new principal name */
	/* we can't pick random names because we need to generate all the names 
	   again given a prefix and count to test the db lib and kdb */
	ctmp[0] = '\0';
	for (i = 1; i <= depth; i++) {
	  ctmp2[0] = '\0';
	  (void) sprintf(ctmp2, "%s%s%d-DEPTH-%d", (i != 1) ? "/" : "",
			 prefix, n, i);
	  strcat(ctmp, ctmp2);
	  sprintf(client, "%s@%s", ctmp, cur_realm);

	  if (get_tgt (test_context, client, &client_princ, ccache)) {
	    errors++;
	    n_tried++;
	    continue;
	  }
	  n_tried++;

	  stmp[0] = '\0';
	  for (j = 1; j <= depth; j++) {
	    stmp2[0] = '\0';
	    (void) sprintf(stmp2, "%s%s%d-DEPTH-%d", (j != 1) ? "/" : "",
			   prefix, n, j);
	    strcat(stmp, stmp2);
	    sprintf(server, "%s@%s", stmp, cur_realm);
	    if (verify_cs_pair(test_context, client, client_princ, 
			       stmp, cur_realm, n, i, j, ccache))
	      errors++;
	    n_tried++;
	  }
	  krb5_free_principal(test_context, client_princ);
	}
      }
    }
    fprintf (stderr, "\nTried %d.  Got %d errors.\n", n_tried, errors);
  }


static krb5_error_code 
get_server_key(context, server, keytype, key)
    krb5_context context;
    krb5_principal server;
    krb5_keytype keytype;
    krb5_keyblock ** key;
{
    krb5_error_code retval;
    krb5_encrypt_block eblock;
    char * string;
    krb5_data salt;
    krb5_data pwd;

    if (retval = krb5_principal2salt(context, server, &salt))
	return retval;

    if (retval = krb5_unparse_name(context, server, &string))
	goto cleanup_salt;

    pwd.data = string;
    pwd.length = strlen(string);

    if (*key = (krb5_keyblock *)malloc(sizeof(krb5_keyblock))) {
    	krb5_use_keytype(context, &eblock, keytype);
    	if (retval = krb5_string_to_key(context, &eblock, keytype, 
				        *key, &pwd, &salt))
	    free(*key);
    } else 
        retval = ENOMEM;

    free(string);

cleanup_salt:
    free(salt.data);
    return retval;
}

extern krb5_flags krb5_kdc_default_options;

int verify_cs_pair(context, p_client_str, p_client, service, hostname, 
		   p_num, c_depth, s_depth, ccache)
    krb5_context context;
    char *p_client_str;
    krb5_principal p_client;
    char * service;
    char * hostname;
    int p_num, c_depth, s_depth;
    krb5_ccache ccache;
{
    krb5_error_code 	  retval;
    krb5_creds 	 	  creds;
    krb5_creds 		* credsp;
    krb5_ticket 	* ticket = NULL;
    krb5_keyblock 	* keyblock = NULL;
    krb5_auth_context 	* auth_context = NULL;
    krb5_data		  request_data;

    if (brief)
      fprintf(stderr, "\tprinc (%d) client (%d) for server (%d)\n", 
	      p_num, c_depth, s_depth);
    else
      fprintf(stderr, "\tclient %s for server %s\n", p_client_str, 
	      hostname);

    /* Initialize variables */
    memset((char *)&creds, 0, sizeof(creds));

    /* Do client side */
    if (retval = krb5_build_principal(context, &creds.server, strlen(hostname), 
				      hostname, service, NULL, NULL)) {
	com_err(prog, retval, "while building principal for %s", hostname);
	return retval;
    }

    /* obtain ticket & session key */
    if (retval = krb5_cc_get_principal(context, ccache, &creds.client)) {
	com_err(prog, retval, "while getting client princ for %s", hostname);
    	krb5_free_cred_contents(context, &creds);
	return retval;
    }

    if (retval = krb5_get_credentials(context, krb5_kdc_default_options,
                                      ccache, &creds, &credsp)) {
	com_err(prog, retval, "while getting creds for %s", hostname);
    	krb5_free_cred_contents(context, &creds);
	return retval;
    }

    krb5_free_cred_contents(context, &creds);

    if (retval = krb5_mk_req_extended(context, &auth_context, 0, NULL,
			            credsp, &request_data)) {
	com_err(prog, retval, "while preparing AP_REQ for %s", hostname);
        krb5_auth_con_free(context, auth_context);
	return retval;
    }

    krb5_auth_con_free(context, auth_context);
    auth_context = NULL;

    /* Do server side now */
    if (retval = get_server_key(context, credsp->server,
				credsp->keyblock.keytype, &keyblock)) {
	com_err(prog, retval, "while getting server key for %s", hostname);
	goto cleanup_rdata;
    }

    if (krb5_auth_con_init(context, &auth_context)) {
	com_err(prog, retval, "while creating auth_context for %s", hostname);
	goto cleanup_keyblock;
    }

    if (krb5_auth_con_setuseruserkey(context, auth_context, keyblock)) {
	com_err(prog, retval, "while setting auth_context key %s", hostname);
	goto cleanup_keyblock;
    }

    if (retval = krb5_rd_req(context, &auth_context, &request_data, 
			     NULL /* server */, 0, NULL, &ticket)) { 
	com_err(prog, retval, "while decoding AP_REQ for %s", hostname); 
        krb5_auth_con_free(context, auth_context);
	goto cleanup_keyblock;
    }

    krb5_auth_con_free(context, auth_context);

    if (!(krb5_principal_compare(context,ticket->enc_part2->client,p_client))){
    	char *returned_client;
        if (retval = krb5_unparse_name(context, ticket->enc_part2->client, 
			       	       &returned_client)) 
	    com_err (prog, retval, 
		     "Client not as expected, but cannot unparse client name");
      	else
	    com_err (prog, 0, "Client not as expected (%s).", returned_client);
	retval = KRB5_PRINC_NOMATCH;
      	free(returned_client);
    } else {
	retval = 0;
    }

    krb5_free_ticket(context, ticket);

cleanup_keyblock:
    krb5_free_keyblock(context, keyblock);

cleanup_rdata:
    krb5_xfree(request_data.data);

    return retval;
}

int get_tgt (context, p_client_str, p_client, ccache)
    krb5_context context;
    char *p_client_str;
    krb5_principal *p_client;
    krb5_ccache ccache;
{
    char *cache_name = NULL;		/* -f option */
    long lifetime = KRB5_DEFAULT_LIFE;	/* -l option */
    int options = KRB5_DEFAULT_OPTIONS;
    krb5_error_code code;
    krb5_creds my_creds;
    krb5_timestamp start;
    krb5_principal tgt_server;

    if (!brief)
      fprintf(stderr, "\tgetting TGT for %s\n", p_client_str);

    if (code = krb5_timeofday(context, &start)) {
	com_err(prog, code, "while getting time of day");
	return(-1);
    }

    memset((char *)&my_creds, 0, sizeof(my_creds));
    
    if (code = krb5_parse_name (context, p_client_str, p_client)) {
	com_err (prog, code, "when parsing name %s", p_client_str);
	return(-1);
    }


    if (code = krb5_build_principal_ext(context, &tgt_server,
				krb5_princ_realm(context, *p_client)->length,
				krb5_princ_realm(context, *p_client)->data,
				tgtname.length,
				tgtname.data,
				krb5_princ_realm(context, *p_client)->length,
				krb5_princ_realm(context, *p_client)->data,
				0)) {
	com_err(prog, code, "when setting up tgt principal");
	return(-1);
    }

    my_creds.client = *p_client;
    my_creds.server = tgt_server;

    code = krb5_cc_initialize (context, ccache, *p_client);
    if (code != 0) {
	com_err (prog, code, "when initializing cache %s",
		 cache_name?cache_name:"");
	return(-1);
    }

    my_creds.times.starttime = 0;	/* start timer when request
					   gets to KDC */
    my_creds.times.endtime = start + lifetime;
    my_creds.times.renew_till = 0;

    code = krb5_get_in_tkt_with_password(context, options, 0,
					 NULL, patype, p_client_str, ccache,
					 &my_creds, 0);
    my_creds.server = my_creds.client = 0;
    krb5_free_principal(context, tgt_server);
    krb5_free_cred_contents(context, &my_creds);
    if (code != 0) {
	com_err (prog, code, "while getting initial credentials");
	return(-1);
      }

    return(0);
}
