/* A utility program originally written for the Linux OS SCSI subsystem.
 *  Copyright (C) 2004-2014 D. Gilbert
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program issues the SCSI PERSISTENT IN and OUT commands.
 */

/* Added timers - Spyros Papageorgiou (spapageo0x01@gmail.com) */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>

#include <time.h>
#include <sys/time.h>

#define __STDC_FORMAT_MACROS 1

#include <inttypes.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_cmds_basic.h"
#include "sg_cmds_extra.h"

static const char * version_str = "0.49 20141007";


#define PRIN_RKEY_SA     0x0
#define PRIN_RRES_SA     0x1
#define PRIN_RCAP_SA     0x2
#define PRIN_RFSTAT_SA   0x3
#define PROUT_REG_SA     0x0
#define PROUT_RES_SA     0x1
#define PROUT_REL_SA     0x2
#define PROUT_CLEAR_SA   0x3
#define PROUT_PREE_SA    0x4
#define PROUT_PREE_AB_SA 0x5
#define PROUT_REG_IGN_SA 0x6
#define PROUT_REG_MOVE_SA 0x7
#define PROUT_REPL_LOST_SA 0x8
#define MX_ALLOC_LEN 8192
#define MX_TIDS 32
#define MX_TID_LEN 256

#define SG_PERSIST_IN_RDONLY "SG_PERSIST_IN_RDONLY"

struct opts_t {
    unsigned int prout_type;
    uint64_t param_rk;
    uint64_t param_sark;
    unsigned int param_rtp;
    int prin;
    int prin_sa;
    int prout_sa;
    int param_alltgpt;
    int param_aptpl;
    int param_unreg;
    int inquiry;
    int hex;
    int readonly;
    unsigned char transportid_arr[MX_TIDS * MX_TID_LEN];
    int num_transportids;
    unsigned int alloc_len;
    int verbose;
};


static struct option long_options[] = {
    {"alloc-length", required_argument, 0, 'l'},
    {"clear", no_argument, 0, 'C'},
    {"device", required_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {"hex", no_argument, 0, 'H'},
    {"in", no_argument, 0, 'i'},
    {"no-inquiry", no_argument, 0, 'n'},
    {"out", no_argument, 0, 'o'},
    {"param-alltgpt", no_argument, 0, 'Y'},
    {"param-aptpl", no_argument, 0, 'Z'},
    {"param-rk", required_argument, 0, 'K'},
    {"param-sark", required_argument, 0, 'S'},
    {"param-unreg", no_argument, 0, 'U'},
    {"preempt", no_argument, 0, 'P'},
    {"preempt-abort", no_argument, 0, 'A'},
    {"prout-type", required_argument, 0, 'T'},
    {"read-full-status", no_argument, 0, 's'},
    {"read-keys", no_argument, 0, 'k'},
    {"readonly", no_argument, 0, 'y'},
    {"read-reservation", no_argument, 0, 'r'},
    {"read-status", no_argument, 0, 's'},
    {"register", no_argument, 0, 'G'},
    {"register-ignore", no_argument, 0, 'I'},
    {"register-move", no_argument, 0, 'M'},
    {"release", no_argument, 0, 'L'},
    {"relative-target-port", required_argument, 0, 'Q'},
    {"replace-lost", no_argument, 0, 'z'},
    {"report-capabilities", no_argument, 0, 'c'},
    {"reserve", no_argument, 0, 'R'},
    {"transport-id", required_argument, 0, 'X'},
    {"unreg", no_argument, 0, 'U'},
    {"verbose", no_argument, 0, 'v'},
    {"version", no_argument, 0, 'V'},
    {0, 0, 0, 0}
};

static const char * prin_sa_strs[] = {
    "Read keys",
    "Read reservation",
    "Report capabilities",
    "Read full status",
    "[reserved 0x4]",
    "[reserved 0x5]",
    "[reserved 0x6]",
    "[reserved 0x7]",
};
static const int num_prin_sa_strs = sizeof(prin_sa_strs) /
                                    sizeof(prin_sa_strs[0]);

static const char * prout_sa_strs[] = {
    "Register",
    "Reserve",
    "Release",
    "Clear",
    "Preempt",
    "Preempt and abort",
    "Register and ignore existing key",
    "Register and move",
    "Replace lost reservation",
    "[reserved 0x9]",
};
static const int num_prout_sa_strs = sizeof(prout_sa_strs) /
                                     sizeof(prout_sa_strs[0]);

static const char * pr_type_strs[] = {
    "obsolete [0]",
    "Write Exclusive",
    "obsolete [2]",
    "Exclusive Access",
    "obsolete [4]",
    "Write Exclusive, registrants only",
    "Exclusive Access, registrants only",
    "Write Exclusive, all registrants",
    "Exclusive Access, all registrants",
    "obsolete [9]", "obsolete [0xa]", "obsolete [0xb]", "obsolete [0xc]",
    "obsolete [0xd]", "obsolete [0xe]", "obsolete [0xf]",
};


#ifdef __GNUC__
static int pr2serr(const char * fmt, ...)
        __attribute__ ((format (printf, 1, 2)));
#else
static int pr2serr(const char * fmt, ...);
#endif

static int
pr2serr(const char * fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = vfprintf(stderr, fmt, args);
    va_end(args);
    return n;
}

static void
usage(int help)
{
    if (help < 2) {
        pr2serr("Usage: sg_persist [OPTIONS] [DEVICE]\n"
                "  where the main OPTIONS are:\n"
                "    --clear|-C                 PR Out: Clear\n"
                "    --help|-h                  print usage message, "
                "twice for more\n"
                "    --in|-i                    request PR In command "
                "(default)\n"
                "    --out|-o                   request PR Out command\n"
                "    --param-rk=RK|-K RK        PR Out parameter reservation "
                "key\n"
                "                               (RK is in hex)\n"
                "    --param-sark=SARK|-S SARK    PR Out parameter service "
                "action\n"
                "                                 reservation key (SARK is "
                "in hex)\n"
                "    --preempt|-P               PR Out: Preempt\n"
                "    --preempt-abort|-A         PR Out: Preempt and Abort\n"
                "    --prout-type=TYPE|-T TYPE    PR Out type field (see "
                "'-hh')\n"
                "    --read-full-status|-s      PR In: Read Full Status\n"
                "    --read-keys|-k             PR In: Read Keys "
                "(default)\n");
        pr2serr("    --read-reservation|-r      PR In: Read Reservation\n"
                "    --read-status|-s           PR In: Read Full Status\n"
                "    --register|-G              PR Out: Register\n"
                "    --register-ignore|-I       PR Out: Register and Ignore\n"
                "    --register-move|-M         PR Out: Register and Move\n"
                "                               for '--register-move'\n"
                "    --release|-L               PR Out: Release\n"
                "    --replace-lost|-x          PR Out: Replace Lost "
                "Reservation\n"
                "    --report-capabilities|-c   PR In: Report Capabilities\n"
                "    --reserve|-R               PR Out: Reserve\n"
                "    --unreg|-U                 optional with PR Out "
                "Register and Move\n\n"
                "Performs a SCSI PERSISTENT RESERVE (IN or OUT) command. "
                "Invoking\n'sg_persist DEVICE' will do a PR In Read Keys "
                "command. Use '-hh'\nfor more options and TYPE meanings.\n");
    } else {
        pr2serr("Usage: sg_persist [OPTIONS] [DEVICE]\n"
                "  where the other OPTIONS are:\n"
                "    --alloc-length=LEN|-l LEN    allocation length hex "
                "value (used with\n"
                "                                 PR In only) (default: 8192 "
                "(2000 in hex))\n"
                "    --device=DEVICE|-d DEVICE    supply DEVICE as an option "
                "rather than\n"
                "                                 an argument\n"
                "    --hex|-H                   output response in hex (for "
                "PR In commands)\n"
                "    --no-inquiry|-n            skip INQUIRY (default: do "
                "INQUIRY)\n"
                "    --param-alltgpt|-Y         PR Out parameter "
                "'ALL_TG_PT'\n"
                "    --param-aptpl|-Z           PR Out parameter 'APTPL'\n"
                "    --readonly|-y              open DEVICE read-only (def: "
                "read-write)\n"
                "    --relative-target-port=RTPI|-Q RTPI    relative target "
                "port "
                "identifier\n"
                "    --transport-id=TIDS|-X TIDS    one or more "
                "TransportIDs can\n"
                "                                   be given in several "
                "forms\n"
                "    --verbose|-v               output additional debug "
                "information\n"
                "    --version|-V               output version string\n\n"
                "For the main options use '--help' or '-h' once.\n\n\n");
        pr2serr("PR Out TYPE field value meanings:\n"
                "  0:    obsolete (was 'read shared' in SPC)\n"
                "  1:    write exclusive\n"
                "  2:    obsolete (was 'read exclusive')\n"
                "  3:    exclusive access\n"
                "  4:    obsolete (was 'shared access')\n"
                "  5:    write exclusive, registrants only\n"
                "  6:    exclusive access, registrants only\n"
                "  7:    write exclusive, all registrants\n"
                "  8:    exclusive access, all registrants\n");
    }
}

/* If num_tids==0 then only one TransportID is assumed with len bytes in
 * it. If num_tids>0 then that many TransportIDs is assumed, each in an
 * element that is MX_TID_LEN bytes long (and the 'len' argument is
 * ignored). */
static void
decode_transport_id(const char * leadin, unsigned char * ucp, int len,
                    int num_tids)
{
    int format_code, proto_id, num, j, k;
    uint64_t ull;
    int bump;

    if (num_tids > 0)
        len = num_tids * MX_TID_LEN;
    for (k = 0, bump = MX_TID_LEN; k < len; k += bump, ucp += bump) {
        if ((len < 24) || (0 != (len % 4)))
            printf("%sTransport Id short or not multiple of 4 "
                   "[length=%d]:\n", leadin, len);
        else
            printf("%sTransport Id of initiator:\n", leadin);
        format_code = ((ucp[0] >> 6) & 0x3);
        proto_id = (ucp[0] & 0xf);
        switch (proto_id) {
        case TPROTO_FCP: /* Fibre channel */
            printf("%s  FCP-2 World Wide Name:\n", leadin);
            if (0 != format_code)
                printf("%s  [Unexpected format code: %d]\n", leadin,
                       format_code);
            dStrHex((const char *)&ucp[8], 8, -1);
            break;
        case TPROTO_SPI: /* Parallel SCSI */
            printf("%s  Parallel SCSI initiator SCSI address: 0x%x\n",
                   leadin, ((ucp[2] << 8) | ucp[3]));
            if (0 != format_code)
                printf("%s  [Unexpected format code: %d]\n", leadin,
                       format_code);
            printf("%s  relative port number (of corresponding target): "
                   "0x%x\n", leadin, ((ucp[6] << 8) | ucp[7]));
            break;
        case TPROTO_SSA:
            printf("%s  SSA (transport id not defined):\n", leadin);
            printf("%s  format code: %d\n", leadin, format_code);
            dStrHex((const char *)ucp, ((len > 24) ? 24 : len), -1);
            break;
        case TPROTO_1394: /* IEEE 1394 */
            printf("%s  IEEE 1394 EUI-64 name:\n", leadin);
            if (0 != format_code)
                printf("%s  [Unexpected format code: %d]\n", leadin,
                       format_code);
            dStrHex((const char *)&ucp[8], 8, -1);
            break;
        case TPROTO_SRP:
            printf("%s  RDMA initiator port identifier:\n", leadin);
            if (0 != format_code)
                printf("%s  [Unexpected format code: %d]\n", leadin,
                       format_code);
            dStrHex((const char *)&ucp[8], 16, -1);
            break;
        case TPROTO_ISCSI:
            printf("%s  iSCSI ", leadin);
            num = ((ucp[2] << 8) | ucp[3]);
            if (0 == format_code)
                printf("name: %.*s\n", num, &ucp[4]);
            else if (1 == format_code)
                printf("name and session id: %.*s\n", num, &ucp[4]);
            else {
                printf("  [Unexpected format code: %d]\n", format_code);
                dStrHex((const char *)ucp, num + 4, -1);
            }
            break;
        case TPROTO_SAS:
            ull = 0;
            for (j = 0; j < 8; ++j) {
                if (j > 0)
                    ull <<= 8;
                ull |= ucp[4 + j];
            }
            printf("%s  SAS address: 0x%016" PRIx64 "\n", leadin, ull);
            if (0 != format_code)
                printf("%s  [Unexpected format code: %d]\n", leadin,
                       format_code);
            break;
        case TPROTO_ADT:
            printf("%s  ADT:\n", leadin);
            printf("%s  format code: %d\n", leadin, format_code);
            dStrHex((const char *)ucp, ((len > 24) ? 24 : len), -1);
            break;
        case TPROTO_ATA:
            printf("%s  ATAPI:\n", leadin);
            printf("%s  format code: %d\n", leadin, format_code);
            dStrHex((const char *)ucp, ((len > 24) ? 24 : len), -1);
            break;
        case TPROTO_UAS:
            printf("%s  UAS:\n", leadin);
            printf("%s  format code: %d\n", leadin, format_code);
            dStrHex((const char *)ucp, ((len > 24) ? 24 : len), -1);
            break;
        case TPROTO_SOP:
            printf("%s  SOP ", leadin);
            num = ((ucp[2] << 8) | ucp[3]);
            if (0 == format_code)
                printf("Routing ID: 0x%x\n", num);
            else {
                printf("  [Unexpected format code: %d]\n", format_code);
                dStrHex((const char *)ucp, ((len > 24) ? 24 : len), -1);
            }
            break;
        case TPROTO_NONE:
            pr2serr("%s  No specified protocol\n", leadin);
            /* dStrHexErr((const char *)ucp, ((len > 24) ? 24 : len), -1); */
            break;
        default:
            pr2serr("%s  unknown protocol id=0x%x  format_code=%d\n",
                    leadin, proto_id, format_code);
            dStrHexErr((const char *)ucp, ((len > 24) ? 24 : len), -1);
            break;
        }
    }
}

static int
prin_work(int sg_fd, const struct opts_t * op)
{
    int k, j, num, res, add_len, add_desc_len, rel_pt_addr;
    unsigned int pr_gen;
    uint64_t ull;
    unsigned char * ucp;
    unsigned char pr_buff[MX_ALLOC_LEN];
    struct timeval start, end;

    memset(pr_buff, 0, sizeof(pr_buff));

    gettimeofday(&start, NULL);
    res = sg_ll_persistent_reserve_in(sg_fd, op->prin_sa, pr_buff,
                                      op->alloc_len, 1, op->verbose);
    gettimeofday(&end, NULL);
    printf("Time: %ld microseconds\n", ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)));
    if (res) {
        char b[64];
        char bb[80];

        if (op->prin_sa < num_prin_sa_strs)
            snprintf(b, sizeof(b), "%s", prin_sa_strs[op->prin_sa]);
        else
            snprintf(b, sizeof(b), "service action=0x%x", op->prin_sa);

        if (SG_LIB_CAT_INVALID_OP == res)
            pr2serr("PR in (%s): command not supported\n", b);
        else if (SG_LIB_CAT_ILLEGAL_REQ == res)
            pr2serr("PR in (%s): bad field in cdb or parameter list (perhaps "
                    "unsupported service action)\n", b);
        else {
            sg_get_category_sense_str(res, sizeof(bb), bb, op->verbose);
            pr2serr("PR in (%s): %s\n", b, bb);
        }
        return res;
    }
    if (PRIN_RCAP_SA == op->prin_sa) {
        if (8 != pr_buff[1]) {
            pr2serr("Unexpected response for PRIN Report Capabilities\n");
            if (op->hex)
                dStrHex((const char *)pr_buff, pr_buff[1], 1);
            return SG_LIB_CAT_MALFORMED;
        }
        if (op->hex)
            dStrHex((const char *)pr_buff, 8, 1);
        else {
            printf("Report capabilities response:\n");
            printf("  Compatible Reservation Handling(CRH): %d\n",
                   !!(pr_buff[2] & 0x10));
            printf("  Specify Initiator Ports Capable(SIP_C): %d\n",
                   !!(pr_buff[2] & 0x8));
            printf("  All Target Ports Capable(ATP_C): %d\n",
                   !!(pr_buff[2] & 0x4));
            printf("  Persist Through Power Loss Capable(PTPL_C): %d\n",
                   !!(pr_buff[2] & 0x1));
            printf("  Type Mask Valid(TMV): %d\n", !!(pr_buff[3] & 0x80));
            printf("  Allow Commands: %d\n", (pr_buff[3] >> 4) & 0x7);
            printf("  Persist Through Power Loss Active(PTPL_A): %d\n",
                   !!(pr_buff[3] & 0x1));
            if (pr_buff[3] & 0x80) {
                printf("    Support indicated in Type mask:\n");
                printf("      %s: %d\n", pr_type_strs[7],
                       !!(pr_buff[4] & 0x80));
                printf("      %s: %d\n", pr_type_strs[6],
                       !!(pr_buff[4] & 0x40));
                printf("      %s: %d\n", pr_type_strs[5],
                       !!(pr_buff[4] & 0x20));
                printf("      %s: %d\n", pr_type_strs[3],
                       !!(pr_buff[4] & 0x8));
                printf("      %s: %d\n", pr_type_strs[1],
                       !!(pr_buff[4] & 0x2));
                printf("      %s: %d\n", pr_type_strs[8],
                       !!(pr_buff[5] & 0x1));
            }
        }
    } else {
        pr_gen = ((pr_buff[0] << 24) | (pr_buff[1] << 16) |
                  (pr_buff[2] << 8) | pr_buff[3]);
        add_len = ((pr_buff[4] << 24) | (pr_buff[5] << 16) |
                   (pr_buff[6] << 8) | pr_buff[7]);
        if (op->hex) {
            if (op->hex > 1)
                dStrHex((const char *)pr_buff, add_len + 8,
                        ((2 == op->hex) ? 1 : -1));
            else {
                printf("  PR generation=0x%x, ", pr_gen);
                if (add_len <= 0)
                    printf("Additional length=%d\n", add_len);
                if (add_len > ((int)sizeof(pr_buff) - 8)) {
                    printf("Additional length too large=%d, truncate\n",
                           add_len);
                    dStrHex((const char *)(pr_buff + 8), sizeof(pr_buff) - 8,
                            1);
                } else {
                    printf("Additional length=%d\n", add_len);
                    dStrHex((const char *)(pr_buff + 8), add_len, 1);
                }
            }
        } else if (PRIN_RKEY_SA == op->prin_sa) {
            printf("  PR generation=0x%x, ", pr_gen);
            num = add_len / 8;
            if (num > 0) {
                if (1 == num)
                    printf("1 registered reservation key follows:\n");
                else
                    printf("%d registered reservation keys follow:\n", num);
                ucp = pr_buff + 8;
                for (k = 0; k < num; ++k, ucp += 8) {
                    ull = 0;
                    for (j = 0; j < 8; ++j) {
                        if (j > 0)
                            ull <<= 8;
                        ull |= ucp[j];
                    }
                    printf("    0x%" PRIx64 "\n", ull);
                }
            } else
                printf("there are NO registered reservation keys\n");
        } else if (PRIN_RRES_SA == op->prin_sa) {
            printf("  PR generation=0x%x, ", pr_gen);
            num = add_len / 16;
            if (num > 0) {
                printf("Reservation follows:\n");
                ucp = pr_buff + 8;
                ull = 0;
                for (j = 0; j < 8; ++j) {
                    if (j > 0)
                        ull <<= 8;
                    ull |= ucp[j];
                }
                printf("    Key=0x%" PRIx64 "\n", ull);
                j = ((ucp[13] >> 4) & 0xf);
                if (0 == j)
                    printf("    scope: LU_SCOPE, ");
                else
                    printf("    scope: %d ", j);
                j = (ucp[13] & 0xf);
                printf(" type: %s\n", pr_type_strs[j]);
            } else
                printf("there is NO reservation held\n");
        } else if (PRIN_RFSTAT_SA == op->prin_sa) {
            printf("  PR generation=0x%x\n", pr_gen);
            ucp = pr_buff + 8;
            if (0 == add_len) {
                printf("  No full status descriptors\n");
                if (op->verbose)
                printf("  So there are no registered IT nexuses\n");
            }
            for (k = 0; k < add_len; k += num, ucp += num) {
                add_desc_len = ((ucp[20] << 24) | (ucp[21] << 16) |
                                (ucp[22] << 8) | ucp[23]);
                num = 24 + add_desc_len;
                ull = 0;
                for (j = 0; j < 8; ++j) {
                    if (j > 0)
                        ull <<= 8;
                    ull |= ucp[j];
                }
                printf("    Key=0x%" PRIx64 "\n", ull);
                if (ucp[12] & 0x2)
                    printf("      All target ports bit set\n");
                else {
                    printf("      All target ports bit clear\n");
                    rel_pt_addr = ((ucp[18] << 8) | ucp[19]);
                    printf("      Relative port address: 0x%x\n",
                           rel_pt_addr);
                }
                if (ucp[12] & 0x1) {
                    printf("      << Reservation holder >>\n");
                    j = ((ucp[13] >> 4) & 0xf);
                    if (0 == j)
                        printf("      scope: LU_SCOPE, ");
                    else
                        printf("      scope: %d ", j);
                    j = (ucp[13] & 0xf);
                    printf(" type: %s\n", pr_type_strs[j]);
                } else
                    printf("      not reservation holder\n");
                if (add_desc_len > 0)
                    decode_transport_id("      ", &ucp[24], add_desc_len, 0);
            }
        }
    }
    return 0;
}

/* Compact the 2 dimensional transportid_arr into a one dimensional
 * array in place returning the length. */
static int
compact_transportid_array(struct opts_t * op)
{
    int k, off, protocol_id, len;
    int compact_len = 0;
    unsigned char * ucp = op->transportid_arr;

    for (k = 0, off = 0; ((k < op->num_transportids) && (k < MX_TIDS));
         ++k, off += MX_TID_LEN) {
        protocol_id = ucp[off] & 0xf;
        if (TPROTO_ISCSI == protocol_id) {
            len = (ucp[off + 2] << 8) + ucp[off + 3] + 4;
            if (len < 24)
                len = 24;
            if (off > compact_len)
                memmove(ucp + compact_len, ucp + off, len);
            compact_len += len;

        } else {
            if (off > compact_len)
                memmove(ucp + compact_len, ucp + off, 24);
            compact_len += 24;
        }
    }
    return compact_len;
}

static int
prout_work(int sg_fd, struct opts_t * op)
{
    int j, len, res, t_arr_len;
    unsigned char pr_buff[MX_ALLOC_LEN];
    uint64_t param_rk;
    uint64_t param_sark;
    struct timeval start, end;
    char b[64];
    char bb[80];

    t_arr_len = compact_transportid_array(op);
    param_rk = op->param_rk;
    memset(pr_buff, 0, sizeof(pr_buff));
    for (j = 7; j >= 0; --j) {
        pr_buff[j] = (param_rk & 0xff);
        param_rk >>= 8;
    }
    param_sark = op->param_sark;
    for (j = 7; j >= 0; --j) {
        pr_buff[8 + j] = (param_sark & 0xff);
        param_sark >>= 8;
    }
    if (op->param_alltgpt)
        pr_buff[20] |= 0x4;
    if (op->param_aptpl)
        pr_buff[20] |= 0x1;
    len = 24;
    if (t_arr_len > 0) {
        pr_buff[20] |= 0x8;     /* set SPEC_I_PT bit */
        memcpy(&pr_buff[28], op->transportid_arr, t_arr_len);
        len += (t_arr_len + 4);
        pr_buff[24] = (unsigned char)((t_arr_len >> 24) & 0xff);
        pr_buff[25] = (unsigned char)((t_arr_len >> 16) & 0xff);
        pr_buff[26] = (unsigned char)((t_arr_len >> 8) & 0xff);
        pr_buff[27] = (unsigned char)(t_arr_len & 0xff);
    }
    
    gettimeofday(&start, NULL);
    res = sg_ll_persistent_reserve_out(sg_fd, op->prout_sa, 0,
                                       op->prout_type, pr_buff, len, 1,
                                       op->verbose);
    gettimeofday(&end, NULL);
    printf(">>Time: %ld microseconds\n", ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)));

    if (res || op->verbose) {
        if (op->prout_sa < num_prout_sa_strs)
            snprintf(b, sizeof(b), "%s", prout_sa_strs[op->prout_sa]);
        else
            snprintf(b, sizeof(b), "service action=0x%x", op->prout_sa);
        if (res) {
            if (SG_LIB_CAT_INVALID_OP == res)
                pr2serr("PR out (%s): command not supported\n", b);
            else if (SG_LIB_CAT_ILLEGAL_REQ == res)
                pr2serr("PR out (%s): bad field in cdb or parameter list "
                        "(perhaps unsupported service action)\n", b);
            else {
                sg_get_category_sense_str(res, sizeof(bb), bb, op->verbose);
                pr2serr("PR out (%s): %s\n", b, bb);
            }
            return res;
        } else if (op->verbose)
            pr2serr("PR out: command (%s) successful\n", b);
    }
    return 0;
}

static int
prout_reg_move_work(int sg_fd, struct opts_t * op)
{
    int j, len, res, t_arr_len;
    unsigned char pr_buff[MX_ALLOC_LEN];
    uint64_t param_rk;
    uint64_t param_sark;
    struct timeval start, end;


    t_arr_len = compact_transportid_array(op);
    param_rk = op->param_rk;
    memset(pr_buff, 0, sizeof(pr_buff));
    for (j = 7; j >= 0; --j) {
        pr_buff[j] = (param_rk & 0xff);
        param_rk >>= 8;
    }
    param_sark = op->param_sark;
    for (j = 7; j >= 0; --j) {
        pr_buff[8 + j] = (param_sark & 0xff);
        param_sark >>= 8;
    }
    if (op->param_unreg)
        pr_buff[17] |= 0x2;
    if (op->param_aptpl)
        pr_buff[17] |= 0x1;
    pr_buff[18] = (unsigned char)((op->param_rtp >> 8) & 0xff);
    pr_buff[19] = (unsigned char)(op->param_rtp & 0xff);
    len = 24;
    if (t_arr_len > 0) {
        memcpy(&pr_buff[24], op->transportid_arr, t_arr_len);
        len += t_arr_len;
        pr_buff[20] = (unsigned char)((t_arr_len >> 24) & 0xff);
        pr_buff[21] = (unsigned char)((t_arr_len >> 16) & 0xff);
        pr_buff[22] = (unsigned char)((t_arr_len >> 8) & 0xff);
        pr_buff[23] = (unsigned char)(t_arr_len & 0xff);
    }

    gettimeofday(&start, NULL);
    res = sg_ll_persistent_reserve_out(sg_fd, PROUT_REG_MOVE_SA, 0,
                                       op->prout_type, pr_buff, len, 1,
                                       op->verbose);
    gettimeofday(&end, NULL);
    printf("Time: %ld microseconds\n", ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)));
    if (res) {
       if (SG_LIB_CAT_INVALID_OP == res)
            pr2serr("PR out (register and move): command not supported\n");
        else if (SG_LIB_CAT_ILLEGAL_REQ == res)
            pr2serr("PR out (register and move): bad field in cdb or "
                    "parameter list (perhaps unsupported service action)\n");
        else {
            char bb[80];

            sg_get_category_sense_str(res, sizeof(bb), bb, op->verbose);
            pr2serr("PR out (register and move): %s\n", bb);
        }
        return res;
    } else if (op->verbose)
        pr2serr("PR out: 'register and move' command successful\n");
    return 0;
}

/* Decode various symbolic forms of TransportIDs into SPC-4 format.
 * Returns 1 if one found, else returns 0. */
static int
decode_sym_transportid(const char * lcp, unsigned char * tidp)
{
    int k, j, n, b, c, len, alen;
    unsigned int ui;
    const char * ecp;
    const char * isip;

    memset(tidp, 0, 24);
    if ((0 == memcmp("sas,", lcp, 4)) || (0 == memcmp("SAS,", lcp, 4))) {
        lcp += 4;
        k = strspn(lcp, "0123456789aAbBcCdDeEfF");
        if (16 != k) {
            pr2serr("badly formed symbolic SAS TransportID: %s\n", lcp);
            return 0;
        }
        tidp[0] = TPROTO_SAS;
        for (k = 0, j = 0, b = 0; k < 16; ++k) {
            c = lcp[k];
            if (isdigit(c))
                n = c - 0x30;
            else if (isupper(c))
                n = c - 0x37;
            else
                n = c - 0x57;
            if (k & 1) {
                tidp[4 + j] = b | n;
                ++j;
            } else
                b = n << 4;
        }
        return 1;
    } else if ((0 == memcmp("spi,", lcp, 4)) ||
               (0 == memcmp("SPI,", lcp, 4))) {
        lcp += 4;
        if (2 != sscanf(lcp, "%d,%d", &b, &c)) {
            pr2serr("badly formed symbolic SPI TransportID: %s\n", lcp);
            return 0;
        }
        tidp[0] = TPROTO_SPI;
        tidp[2] = (b >> 8) & 0xff;
        tidp[3] = b & 0xff;
        tidp[6] = (c >> 8) & 0xff;
        tidp[7] = c & 0xff;
        return 1;
    } else if ((0 == memcmp("fcp,", lcp, 4)) ||
               (0 == memcmp("FCP,", lcp, 4))) {
        lcp += 4;
        k = strspn(lcp, "0123456789aAbBcCdDeEfF");
        if (16 != k) {
            pr2serr("badly formed symbolic FCP TransportID: %s\n", lcp);
            return 0;
        }
        tidp[0] = TPROTO_FCP;
        for (k = 0, j = 0, b = 0; k < 16; ++k) {
            c = lcp[k];
            if (isdigit(c))
                n = c - 0x30;
            else if (isupper(c))
                n = c - 0x37;
            else
                n = c - 0x57;
            if (k & 1) {
                tidp[8 + j] = b | n;
                ++j;
            } else
                b = n << 4;
        }
        return 1;
    } else if ((0 == memcmp("sbp,", lcp, 4)) ||
               (0 == memcmp("SBP,", lcp, 4))) {
        lcp += 4;
        k = strspn(lcp, "0123456789aAbBcCdDeEfF");
        if (16 != k) {
            pr2serr("badly formed symbolic SBP TransportID: %s\n", lcp);
            return 0;
        }
        tidp[0] = TPROTO_1394;
        for (k = 0, j = 0, b = 0; k < 16; ++k) {
            c = lcp[k];
            if (isdigit(c))
                n = c - 0x30;
            else if (isupper(c))
                n = c - 0x37;
            else
                n = c - 0x57;
            if (k & 1) {
                tidp[8 + j] = b | n;
                ++j;
            } else
                b = n << 4;
        }
        return 1;
    } else if ((0 == memcmp("srp,", lcp, 4)) ||
               (0 == memcmp("SRP,", lcp, 4))) {
        lcp += 4;
        k = strspn(lcp, "0123456789aAbBcCdDeEfF");
        if (16 != k) {
            pr2serr("badly formed symbolic SRP TransportID: %s\n", lcp);
            return 0;
        }
        tidp[0] = TPROTO_SRP;
        for (k = 0, j = 0, b = 0; k < 32; ++k) {
            c = lcp[k];
            if (isdigit(c))
                n = c - 0x30;
            else if (isupper(c))
                n = c - 0x37;
            else
                n = c - 0x57;
            if (k & 1) {
                tidp[8 + j] = b | n;
                ++j;
            } else
                b = n << 4;
        }
        return 1;
    } else if (0 == memcmp("iqn.", lcp, 4)) {
        ecp = strpbrk(lcp, " \t");
        isip = strstr(lcp, ",i,0x");
        if (ecp && (isip > ecp))
            isip = NULL;
        len = ecp ? (ecp - lcp) : (int)strlen(lcp);
        tidp[0] = TPROTO_ISCSI | (isip ? 0x40 : 0x0);
        alen = len + 1; /* at least one trailing null */
        if (alen < 20)
            alen = 20;
        else if (0 != (alen % 4))
            alen = ((alen / 4) + 1) * 4;
        if (alen > 241) { /* sam5r02.pdf A.2 (Annex) */
            pr2serr("iSCSI name too long, alen=%d\n", alen);
            return 0;
        }
        tidp[3] = alen & 0xff;
        memcpy(tidp + 4, lcp, len);
        return 1;
    } else if ((0 == memcmp("sop,", lcp, 4)) ||
               (0 == memcmp("SOP,", lcp, 4))) {
        lcp += 4;
        if (2 != sscanf(lcp, "%x", &ui)) {
            pr2serr("badly formed symbolic SOP TransportID: %s\n", lcp);
            return 0;
        }
        tidp[0] = TPROTO_SOP;
        tidp[2] = (ui >> 8) & 0xff;
        tidp[3] = ui & 0xff;
        return 1;
    }
    pr2serr("unable to parse symbolic TransportID: %s\n", lcp);
    return 0;
}

/* Read one or more TransportIDs from the given file or from stdin.
 * Returns 0 if successful, 1 otherwise. */
static int
decode_file_tids(const char * fnp, struct opts_t * op)
{
    FILE * fp = stdin;
    int in_len, k, j, m, split_line;
    unsigned int h;
    const char * lcp;
    char line[1024];
    char carry_over[4];
    int off = 0;
    int num = 0;
    unsigned char * tid_arr = op->transportid_arr;

    if (fnp) {
        fp = fopen(fnp, "r");
        if (NULL == fp) {
            pr2serr("decode_file_tids: unable to open %s\n", fnp);
            return 1;
        }
    }
    carry_over[0] = 0;
    for (j = 0, off = 0; j < 512; ++j) {
        if (NULL == fgets(line, sizeof(line), fp))
            break;
        in_len = strlen(line);
        if (in_len > 0) {
            if ('\n' == line[in_len - 1]) {
                --in_len;
                line[in_len] = '\0';
                split_line = 0;
            } else
                split_line = 1;
        }
        if (in_len < 1) {
            carry_over[0] = 0;
            continue;
        }
        if (carry_over[0]) {
            if (isxdigit(line[0])) {
                carry_over[1] = line[0];
                carry_over[2] = '\0';
                if (1 == sscanf(carry_over, "%x", &h))
                    tid_arr[off - 1] = h;       /* back up and overwrite */
                else {
                    pr2serr("decode_file_tids: carry_over error ['%s'] "
                            "around line %d\n", carry_over, j + 1);
                    goto bad;
                }
                lcp = line + 1;
                --in_len;
            } else
                lcp = line;
            carry_over[0] = 0;
        } else
            lcp = line;
        m = strspn(lcp, " \t");
        if (m == in_len)
            continue;
        lcp += m;
        in_len -= m;
        if ('#' == *lcp)
            continue;
        if (decode_sym_transportid(lcp, tid_arr + off))
            goto my_cont_a;
        k = strspn(lcp, "0123456789aAbBcCdDeEfF ,\t");
        if ((k < in_len) && ('#' != lcp[k])) {
            pr2serr("decode_file_tids: syntax error at line %d, pos %d\n",
                    j + 1, m + k + 1);
            goto bad;
        }
        for (k = 0; k < 1024; ++k) {
            if (1 == sscanf(lcp, "%x", &h)) {
                if (h > 0xff) {
                    pr2serr("decode_file_tids: hex number larger than 0xff "
                            "in line %d, pos %d\n", j + 1,
                            (int)(lcp - line + 1));
                    goto bad;
                }
                if (split_line && (1 == strlen(lcp))) {
                    /* single trailing hex digit might be a split pair */
                    carry_over[0] = *lcp;
                }
                if ((off + k) >= (int)sizeof(op->transportid_arr)) {
                    pr2serr("decode_file_tids: array length exceeded\n");
                    goto bad;
                }
                tid_arr[off + k] = h;
                lcp = strpbrk(lcp, " ,\t");
                if (NULL == lcp)
                    break;
                lcp += strspn(lcp, " ,\t");
                if ('\0' == *lcp)
                    break;
            } else {
                if ('#' == *lcp) {
                    --k;
                    break;
                }
                pr2serr("decode_file_tids: error in line %d, at pos %d\n",
                        j + 1, (int)(lcp - line + 1));
                goto bad;
            }
        }
my_cont_a:
        off += MX_TID_LEN;
        if (off >= (MX_TIDS * MX_TID_LEN)) {
            pr2serr("decode_file_tids: array length exceeded\n");
            goto bad;
        }
        ++num;
    }
    op->num_transportids = num;
    return 0;

bad:
   if (fnp)
        fclose(fp);
   return 1;
}

/* Build transportid array which may contain one or more TransportIDs.
 * A single TransportID can appear on the command line either as a list of
 * comma (or single space) separated ASCII hex bytes, or in some transport
 * protocol specific form (e.g. "sas,5000c50005b32001"). One or more
 * TransportIDs may be given in a file (syntax: "file=<name>") or read from
 * stdin in (when "-" is given). Fuller description in manpage of
 * sg_persist(8). Returns 0 if successful, else 1 .
 */
static int
build_transportid(const char * inp, struct opts_t * op)
{
    int in_len;
    int k = 0;
    unsigned int h;
    const char * lcp;
    unsigned char * tid_arr = op->transportid_arr;
    char * cp;
    char * c2p;

    lcp = inp;
    in_len = strlen(inp);
    if (0 == in_len) {
        op->num_transportids = 0;
    }
    if (('-' == inp[0]) ||
        (0 == memcmp("file=", inp, 5)) ||
        (0 == memcmp("FILE=", inp, 5))) {
        if ('-' == inp[0])
            lcp = NULL;         /* read from stdin */
        else
            lcp = inp + 5;      /* read from given file */
        return decode_file_tids(lcp, op);
    } else {        /* TransportID given directly on command line */
        if (decode_sym_transportid(lcp, tid_arr))
            goto my_cont_b;
        k = strspn(inp, "0123456789aAbBcCdDeEfF, ");
        if (in_len != k) {
            pr2serr("build_transportid: error at pos %d\n", k + 1);
            return 1;
        }
        for (k = 0; k < (int)sizeof(op->transportid_arr); ++k) {
            if (1 == sscanf(lcp, "%x", &h)) {
                if (h > 0xff) {
                    pr2serr("build_transportid: hex number larger than 0xff "
                            "at pos %d\n", (int)(lcp - inp + 1));
                    return 1;
                }
                tid_arr[k] = h;
                cp = (char *)strchr(lcp, ',');
                c2p = (char *)strchr(lcp, ' ');
                if (NULL == cp)
                    cp = c2p;
                if (NULL == cp)
                    break;
                if (c2p && (c2p < cp))
                    cp = c2p;
                lcp = cp + 1;
            } else {
                pr2serr("build_transportid: error at pos %d\n",
                        (int)(lcp - inp + 1));
                return 1;
            }
        }
my_cont_b:
        op->num_transportids = 1;
        if (k >= (int)sizeof(op->transportid_arr)) {
            pr2serr("build_transportid: array length exceeded\n");
            return 1;
        }
    }
    return 0;
}


int
main(int argc, char * argv[])
{
    int sg_fd, c, res;
    const char * device_name = NULL;
    char buff[48];
    int help =0;
    int num_prin_sa = 0;
    int num_prout_sa = 0;
    int num_prout_param = 0;
    int want_prin = 0;
    int want_prout = 0;
    int peri_type = 0;
    int ret = 0;
    struct sg_simple_inquiry_resp inq_resp;
    const char * cp;
    struct opts_t opts;
    struct opts_t * op;

    op = &opts;
    memset(op, 0, sizeof(opts));
    op->prin = 1;
    op->prin_sa = -1;
    op->prout_sa = -1;
    op->inquiry = 1;
    op->alloc_len = MX_ALLOC_LEN;

    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "AcCd:GHhiIkK:l:LMnoPQ:rRsS:T:UvVX:yYzZ",
                        long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'A':
            op->prout_sa = PROUT_PREE_AB_SA;
            ++num_prout_sa;
            break;
        case 'c':
            op->prin_sa = PRIN_RCAP_SA;
            ++num_prin_sa;
            break;
        case 'C':
            op->prout_sa = PROUT_CLEAR_SA;
            ++num_prout_sa;
            break;
        case 'd':
            device_name = optarg;
            break;
        case 'G':
            op->prout_sa = PROUT_REG_SA;
            ++num_prout_sa;
            break;
        case 'h':
            ++help;
            break;
        case 'H':
            ++op->hex;
            break;
        case 'i':
            want_prin = 1;
            break;
        case 'I':
            op->prout_sa = PROUT_REG_IGN_SA;
            ++num_prout_sa;
            break;
        case 'k':
            op->prin_sa = PRIN_RKEY_SA;
            ++num_prin_sa;
            break;
        case 'K':
            if (1 != sscanf(optarg, "%" SCNx64 "", &op->param_rk)) {
                pr2serr("bad argument to '--param-rk'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            ++num_prout_param;
            break;
        case 'l':
            if (1 != sscanf(optarg, "%x", &op->alloc_len)) {
                pr2serr("bad argument to '--alloc-length'\n");
                return SG_LIB_SYNTAX_ERROR;
            } else if (MX_ALLOC_LEN < op->alloc_len) {
                pr2serr("'--alloc-length' argument exceeds maximum value "
                        "(%d)\n", MX_ALLOC_LEN);
                return SG_LIB_SYNTAX_ERROR;
            }
            break;
        case 'L':
            op->prout_sa = PROUT_REL_SA;
            ++num_prout_sa;
            break;
        case 'M':
            op->prout_sa = PROUT_REG_MOVE_SA;
            ++num_prout_sa;
            break;
        case 'n':
            op->inquiry = 0;
            break;
        case 'o':
            want_prout = 1;
            break;
        case 'P':
            op->prout_sa = PROUT_PREE_SA;
            ++num_prout_sa;
            break;
        case 'Q':
            if (1 != sscanf(optarg, "%x", &op->param_rtp)) {
                pr2serr("bad argument to '--relative-target-port'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            if (op->param_rtp > 0xffff) {
                pr2serr("argument to '--relative-target-port' 0 to ffff "
                        "inclusive\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            ++num_prout_param;
            break;
        case 'r':
            op->prin_sa = PRIN_RRES_SA;
            ++num_prin_sa;
            break;
        case 'R':
            op->prout_sa = PROUT_RES_SA;
            ++num_prout_sa;
            break;
        case 's':
            op->prin_sa = PRIN_RFSTAT_SA;
            ++num_prin_sa;
            break;
        case 'S':
            if (1 != sscanf(optarg, "%" SCNx64 "", &op->param_sark)) {
                pr2serr("bad argument to '--param-sark'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            ++num_prout_param;
            break;
        case 'T':
            if (1 != sscanf(optarg, "%x", &op->prout_type)) {
                pr2serr("bad argument to '--prout-type'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            ++num_prout_param;
            break;
        case 'U':
            op->param_unreg = 1;
            break;
        case 'v':
            ++op->verbose;
            break;
        case 'V':
            pr2serr("version: %s\n", version_str);
            return 0;
        case 'X':
            if (0 != build_transportid(optarg, op)) {
                pr2serr("bad argument to '--transport-id'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            ++num_prout_param;
            break;
        case 'y':
            ++op->readonly;
            break;
        case 'Y':
            op->param_alltgpt = 1;
            ++num_prout_param;
            break;
        case 'z':
            op->prout_sa = PROUT_REPL_LOST_SA;
            ++num_prout_sa;
            break;
        case 'Z':
            op->param_aptpl = 1;
            ++num_prout_param;
            break;
        case '?':
            usage(1);
            return 0;
        default:
            pr2serr("unrecognised switch code 0x%x ??\n", c);
            usage(1);
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    if (optind < argc) {
        if (NULL == device_name) {
            device_name = argv[optind];
            ++optind;
        }
        if (optind < argc) {
            for (; optind < argc; ++optind)
                pr2serr("Unexpected extra argument: %s\n", argv[optind]);
            usage(1);
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    if (help > 0) {
        usage(help);
        return 0;
    }

    if (NULL == device_name) {
        pr2serr("No device name given\n");
        usage(1);
        return SG_LIB_SYNTAX_ERROR;
    }
    if ((want_prout + want_prin) > 1) {
        pr2serr("choose '--in' _or_ '--out' (not both)\n");
        usage(1);
        return SG_LIB_SYNTAX_ERROR;
    } else if (want_prout) { /* syntax check on PROUT arguments */
        op->prin = 0;
        if ((1 != num_prout_sa) || (0 != num_prin_sa)) {
            pr2serr(">> For Persistent Reserve Out one and only one "
                    "appropriate\n>> service action must be chosen (e.g. "
                    "'--register')\n");
            return SG_LIB_SYNTAX_ERROR;
        }
    } else { /* syntax check on PRIN arguments */
        if (num_prout_sa > 0) {
            pr2serr(">> When a service action for Persistent Reserve Out "
                    "is chosen the\n>> '--out' option must be given (as a "
                    "safeguard)\n");
            return SG_LIB_SYNTAX_ERROR;
        }
        if (0 == num_prin_sa) {
            pr2serr(">> No service action given; assume Persistent Reserve "
                    "In command\n>> with Read Keys service action\n");
            op->prin_sa = 0;
            ++num_prin_sa;
        } else if (num_prin_sa > 1)  {
            pr2serr("Too many service actions given; choose one only\n");
            usage(1);
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    if ((op->param_unreg || op->param_rtp) &&
        (PROUT_REG_MOVE_SA != op->prout_sa)) {
        pr2serr("--unreg or --relative-target-port only useful with "
                "--register-move\n");
        usage(1);
        return SG_LIB_SYNTAX_ERROR;
    }
    if ((PROUT_REG_MOVE_SA == op->prout_sa) &&
        (1 != op->num_transportids)) {
        pr2serr("with --register-move one (and only one) --transport-id "
                "should be given\n");
        usage(1);
        return SG_LIB_SYNTAX_ERROR;
    }
    if (((PROUT_RES_SA == op->prout_sa) ||
         (PROUT_REL_SA == op->prout_sa) ||
         (PROUT_PREE_SA == op->prout_sa) ||
         (PROUT_PREE_AB_SA == op->prout_sa)) &&
        (0 == op->prout_type)) {
        pr2serr("warning>>> --prout-type probably needs to be given\n");
    }
    if ((op->verbose > 2) && op->num_transportids) {
        pr2serr("number of tranport-ids decoded from command line (or "
                "stdin): %d\n", op->num_transportids);
        pr2serr("  Decode given transport-ids:\n");
        decode_transport_id("      ", op->transportid_arr,
                            0, op->num_transportids);
    }

    if (op->inquiry) {
        if ((sg_fd = sg_cmds_open_device(device_name, 1 /* ro */,
                                         op->verbose)) < 0) {
            pr2serr("sg_persist: error opening file (ro): %s: %s\n",
                    device_name, safe_strerror(-sg_fd));
            return SG_LIB_FILE_ERROR;
        }
        if (0 == sg_simple_inquiry(sg_fd, &inq_resp, 1, op->verbose)) {
            printf("  %.8s  %.16s  %.4s\n", inq_resp.vendor, inq_resp.product,
                   inq_resp.revision);
            peri_type = inq_resp.peripheral_type;
            cp = sg_get_pdt_str(peri_type, sizeof(buff), buff);
            if (strlen(cp) > 0)
                printf("  Peripheral device type: %s\n", cp);
            else
                printf("  Peripheral device type: 0x%x\n", peri_type);
        } else {
            printf("sg_persist: %s doesn't respond to a SCSI INQUIRY\n",
                   device_name);
            return SG_LIB_CAT_OTHER;
        }
        sg_cmds_close_device(sg_fd);
    }

    if (0 == op->readonly) {
        cp = getenv(SG_PERSIST_IN_RDONLY);
        if (cp && op->prin)
            op->readonly = 1;
    } else if (op->readonly > 1)        /* -yy forces open(RW) */
        op->readonly = 0;
    if ((sg_fd = sg_cmds_open_device(device_name, op->readonly,
                                     op->verbose)) < 0) {
        pr2serr("sg_persist: error opening file (rw): %s: %s\n", device_name,
                safe_strerror(-sg_fd));
        return SG_LIB_FILE_ERROR;
    }

    if (op->prin)
        ret = prin_work(sg_fd, op);
    else if (PROUT_REG_MOVE_SA == op->prout_sa)
        ret = prout_reg_move_work(sg_fd, op);
    else /* PROUT commands other than 'register and move' */
        ret = prout_work(sg_fd, op);

    res = sg_cmds_close_device(sg_fd);
    if (res < 0) {
        pr2serr("close error: %s\n", safe_strerror(-res));
        if (0 == ret)
            return SG_LIB_FILE_ERROR;
    }
    return (ret >= 0) ? ret : SG_LIB_CAT_OTHER;
}
