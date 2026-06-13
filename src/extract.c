#include "extract.h"
#include "terminal.h"
#include "fat.h"
#include "lumie.h"
#include "kernel.h"

/* ===== Bit reader ===== */
typedef struct {
    const u8 *data;
    u32  size;
    u32  buf;
    int  bits;
    u32  pos;
} bit_reader;

static void br_init(bit_reader *br, const u8 *data, u32 size) {
    br->data = data;
    br->size = size;
    br->buf = 0;
    br->bits = 0;
    br->pos = 0;
}

static u32 br_read(bit_reader *br, int n) {
    while (br->bits < n) {
        if (br->pos >= br->size) return 0;
        br->buf |= (u32)br->data[br->pos] << br->bits;
        br->pos++;
        br->bits += 8;
    }
    u32 r = br->buf & ((1u << n) - 1);
    br->buf >>= n;
    br->bits -= n;
    return r;
}

static void br_align(bit_reader *br) {
    br->bits = 0;
    br->buf = 0;
}

/* ===== Huffman tree ===== */
#define HT_NODES 1024

typedef struct {
    u16 left, right;
    u16 symbol;
    u8  is_leaf;
} ht_node;

typedef struct {
    ht_node n[HT_NODES];
    int     num;
} huffman_tree;

static int ht_new(huffman_tree *t) {
    if (t->num >= HT_NODES) return -1;
    int idx = t->num++;
    t->n[idx].left = t->n[idx].right = 0xFFFF;
    t->n[idx].is_leaf = 0;
    return idx;
}

static int ht_build(huffman_tree *t, const u16 *lens, int nsym) {
    t->num = 0;
    int root = ht_new(t);
    if (root < 0) return -1;

    u16 cnt[16] = {0};
    for (int i = 0; i < nsym; i++)
        if (lens[i] > 0 && lens[i] < 16) cnt[lens[i]]++;

    u16 code[16], next = 0;
    for (int b = 1; b < 16; b++) {
        code[b] = next;
        next = (next + cnt[b]) << 1;
    }

    for (int sym = 0; sym < nsym; sym++) {
        int len = lens[sym];
        if (len == 0) continue;
        u16 c = code[len]++;
        int node = root;
        for (int b = len - 1; b >= 0; b--) {
            if ((c >> b) & 1) {
                if (t->n[node].right == 0xFFFF) {
                    int n2 = ht_new(t);
                    if (n2 < 0) return -1;
                    t->n[node].right = n2;
                }
                node = t->n[node].right;
            } else {
                if (t->n[node].left == 0xFFFF) {
                    int n2 = ht_new(t);
                    if (n2 < 0) return -1;
                    t->n[node].left = n2;
                }
                node = t->n[node].left;
            }
        }
        t->n[node].is_leaf = 1;
        t->n[node].symbol = sym;
    }
    return root;
}

static int ht_decode(huffman_tree *t, bit_reader *br, int root) {
    int node = root;
    while (!t->n[node].is_leaf) {
        int bit = (int)br_read(br, 1);
        if (bit == 0) {
            if (t->n[node].left == 0xFFFF) return -1;
            node = t->n[node].left;
        } else {
            if (t->n[node].right == 0xFFFF) return -1;
            node = t->n[node].right;
        }
    }
    return t->n[node].symbol;
}

/* ===== Length/distance tables ===== */
static const u16 len_base[]  = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const u16 len_extra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const u16 dst_base[]  = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const u16 dst_extra[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

/* ===== CRC32 ===== */
static u32 crc32_tab[256];
static int crc32_ok = 0;

static void crc32_init(void) {
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int j = 0; j < 8; j++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : (c >> 1);
        crc32_tab[i] = c;
    }
    crc32_ok = 1;
}

static u32 crc32_calc(u32 crc, const u8 *d, u32 sz) {
    if (!crc32_ok) crc32_init();
    for (u32 i = 0; i < sz; i++) crc = crc32_tab[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

/* ===== DEFLATE ===== */
static int deflate_decompress(bit_reader *br, u8 *out, u32 out_max, u32 *out_size) {
    u32 pos = 0;
    int bfinal = 0;

    while (!bfinal) {
        bfinal = (int)br_read(br, 1);
        int btype = (int)br_read(br, 2);

        if (btype == 0) { /* stored */
            br_align(br);
            u16 len = (u16)br_read(br, 16);
            br_read(br, 16); /* skip NLEN */
            for (u16 i = 0; i < len && br->pos < br->size && pos < out_max; i++)
                out[pos++] = br->data[br->pos++];
        } else if (btype == 1 || btype == 2) {
            huffman_tree tl, td;
            u16 ll[288], dl[32];

            if (btype == 1) { /* fixed */
                for (int i = 0; i < 288; i++)
                    ll[i] = (i <= 143) ? 8 : (i <= 255) ? 9 : (i <= 279) ? 7 : 8;
                for (int i = 0; i < 32; i++) dl[i] = 5;
            } else { /* dynamic */
                int hlit  = (int)br_read(br, 5) + 257;
                int hdist = (int)br_read(br, 5) + 1;
                int hclen = (int)br_read(br, 4) + 4;
                static const u8 co[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                u16 cl[19] = {0};
                huffman_tree hcl;
                for (int i = 0; i < hclen; i++) cl[co[i]] = (u16)br_read(br, 3);
                int cl_root = ht_build(&hcl, cl, 19);
                if (cl_root < 0) return -1;

                u16 buf[320];
                int bi = 0, total = hlit + hdist;
                while (bi < total) {
                    int s = ht_decode(&hcl, br, cl_root);
                    if (s < 0) return -1;
                    if (s < 16) { buf[bi++] = (u16)s; }
                    else if (s == 16) {
                        int r = (int)br_read(br, 2) + 3;
                        u16 p = bi > 0 ? buf[bi-1] : 0;
                        while (r-- && bi < total) buf[bi++] = p;
                    } else if (s == 17) {
                        int r = (int)br_read(br, 3) + 3;
                        while (r-- && bi < total) buf[bi++] = 0;
                    } else if (s == 18) {
                        int r = (int)br_read(br, 7) + 11;
                        while (r-- && bi < total) buf[bi++] = 0;
                    }
                }
                lumie_memset(ll, 0, sizeof(ll));
                lumie_memset(dl, 0, sizeof(dl));
                for (int i = 0; i < hlit;  i++) ll[i] = buf[i];
                for (int i = 0; i < hdist; i++) dl[i] = buf[hlit + i];
            }

            int lr = ht_build(&tl, ll, 288);
            int dr = ht_build(&td, dl, 32);
            if (lr < 0 || dr < 0) return -1;

            while (1) {
                int sym = ht_decode(&tl, br, lr);
                if (sym < 0) return -1;
                if (sym < 256) {
                    if (pos >= out_max) return -1;
                    out[pos++] = (u8)sym;
                } else if (sym == 256) break;
                else {
                    int li = sym - 257;
                    if (li < 0 || li >= 29) return -1;
                    u32 len = len_base[li] + br_read(br, len_extra[li]);
                    int ds = ht_decode(&td, br, dr);
                    if (ds < 0 || ds >= 30) return -1;
                    u32 dist = dst_base[ds] + br_read(br, dst_extra[ds]);
                    for (u32 i = 0; i < len; i++) {
                        if (pos >= out_max || pos < dist) return -1;
                        out[pos] = out[pos - dist];
                        pos++;
                    }
                }
            }
        } else return -1;
    }
    *out_size = pos;
    return 0;
}

/* ===== Gzip ===== */
static int gzip_decompress(const u8 *in, u32 in_sz, u8 **out, u32 *out_sz) {
    if (in_sz < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8) return -1;
    u8 flg = in[3];
    u32 hdr = 10;
    if (flg & 0x04) { if (hdr + 2 > in_sz) return -1; hdr += 2 + (in[hdr] | (in[hdr+1] << 8)); }
    if (flg & 0x08) { while (hdr < in_sz && in[hdr]) hdr++; hdr++; }
    if (flg & 0x10) { while (hdr < in_sz && in[hdr]) hdr++; hdr++; }
    if (flg & 0x02) hdr++;
    if (hdr >= in_sz) return -1;

    u32 max_out = in_sz * 4;
    if (max_out < 65536) max_out = 65536;
    if (max_out > 256 * 1024 * 1024) max_out = 256 * 1024 * 1024;

    *out = NULL;
    efi_status st = ((efi_bs_allocate_pool)g_BS->AllocatePool)(4, max_out, (void**)out);
    if (EFI_ERROR(st) || !*out) return -1;

    bit_reader br;
    br_init(&br, in + hdr, in_sz - hdr - 8);
    u32 dec = 0;
    int ret = deflate_decompress(&br, *out, max_out, &dec);
    if (ret < 0) { ((efi_bs_free_pool)g_BS->FreePool)(*out); *out = NULL; return -1; }

    int tr = in_sz - 8;
    if (tr >= (int)hdr) {
        u32 ecrc = (u32)in[tr] | ((u32)in[tr+1]<<8) | ((u32)in[tr+2]<<16) | ((u32)in[tr+3]<<24);
        u32 acrc = crc32_calc(0xFFFFFFFF, *out, dec) ^ 0xFFFFFFFF;
        if (acrc != ecrc) term_writeln("  CRC mismatch (ignored)");
    }
    *out_sz = dec;
    return 0;
}

/* ===== Tar ===== */
#pragma pack(push, 1)
typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_hdr;
#pragma pack(pop)

static u32 parse_oct(const char *s, int len) {
    u32 v = 0;
    for (int i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + (s[i] - '0');
    return v;
}

static int tar_extract(const u8 *data, u32 size) {
    u32 off = 0;
    int cnt = 0;
    while (off + 512 <= size) {
        tar_hdr *h = (tar_hdr*)(data + off);
        if (h->name[0] == 0) break;
        int valid = (lumie_memcmp(h->magic, "ustar", 5) == 0);
        if (!valid) {
            if (h->name[0] >= 0x20 && h->name[0] < 0x7F) valid = 1;
            else break;
        }
        u32 fsz = parse_oct(h->size, 12);
        char path[256];
        if (h->prefix[0]) {
            int pl = 0; while (pl < 155 && h->prefix[pl]) pl++;
            int nl = 0; while (nl < 100 && h->name[nl]) nl++;
            if (pl + 1 + nl > 255) break;
            lumie_memcpy(path, h->prefix, pl);
            path[pl] = '/';
            lumie_memcpy(path + pl + 1, h->name, nl);
            path[pl + 1 + nl] = 0;
        } else {
            int nl = 0; while (nl < 100 && h->name[nl]) nl++;
            lumie_memcpy(path, h->name, nl);
            path[nl] = 0;
        }

        term_set_fg(h->typeflag == '5' ? LUMIE_LIGHTCYAN : LUMIE_LIGHTGREEN);
        term_write(h->typeflag == '5' ? "  [DIR]  " : "  [FILE] ");
        term_set_fg(LUMIE_WHITE);
        term_writeln(path);

        u32 doff = off + 512;
        if ((h->typeflag == '0' || h->typeflag == '\0') && fsz > 0) {
            if (doff + fsz <= size)
                fat_write_file(path, data + doff, fsz);
        }
        off += 512 + ((fsz + 511) / 512) * 512;
        cnt++;
    }
    return cnt;
}

/* ================================================================
 * LZMA2 / XZ decompression
 * ================================================================ */

/* ---------- Range Decoder ---------- */
typedef struct {
    u32   range, code;
    const u8 *buf;
    u32   pos, size;
} rc_t;

static void rc_init(rc_t *rc, const u8 *buf, u32 size) {
    rc->buf = buf; rc->pos = 0; rc->size = size;
    rc->range = 0xFFFFFFFF; rc->code = 0;
    for (int i = 0; i < 5; i++)
        rc->code = (rc->code << 8) | (rc->pos < rc->size ? rc->buf[rc->pos++] : 0);
}

static u32 rc_byte(rc_t *rc) {
    return rc->pos < rc->size ? rc->buf[rc->pos++] : 0;
}

static int rc_bit(rc_t *rc, u16 *prob) {
    u32 bound = (rc->range >> 11) * (*prob);
    int bit;
    if (rc->code < bound) {
        rc->range = bound;
        *prob += (2048 - *prob) >> 5; bit = 0;
    } else {
        rc->range -= bound; rc->code -= bound;
        *prob -= *prob >> 5; bit = 1;
    }
    if (rc->range < 0x01000000) {
        rc->range <<= 8;
        rc->code = (rc->code << 8) | rc_byte(rc);
    }
    return bit;
}

static u32 rc_bits(rc_t *rc, int n) {
    u32 v = 0;
    for (int i = 0; i < n; i++) {
        u16 p = 1024;
        v |= (u32)rc_bit(rc, &p) << i;
    }
    return v;
}

static u32 rc_tree(rc_t *rc, u16 *probs, int n) {
    u32 m = 1;
    for (int i = 0; i < n; i++)
        m = (m << 1) + rc_bit(rc, &probs[m]);
    return m - (1 << n);
}

static u32 rc_rev_tree(rc_t *rc, u16 *probs, int n) {
    u32 m = 1, v = 0;
    for (int i = 0; i < n; i++) {
        int b = rc_bit(rc, &probs[m]);
        m = (m << 1) + b; v |= (u32)b << i;
    }
    return v;
}

/* ---------- LZMA constants ---------- */
#define LZMA_STATES  12
#define LZMA_POS_MAX  4
#define LZMA_ALIGN    4
#define LZMA_NUM_PROBS 9000

/* lzma_next_state[state][type:0=lit,1=match,2=rep,3=shortrep] */
static const u8 lzma_ns[LZMA_STATES][4] = {
    {0,7,8,9},{0,7,8,9},{0,7,8,9},{0,7,8,9},
    {1,7,8,9},{2,7,8,9},{3,7,8,9},
    {4,10,10,10},{5,10,10,10},{6,10,10,10},
    {4,10,10,10},{5,10,10,10}
};

/* Length encoding: choice → low(4) / mid(4) / high(8) */
/* Distance slot info: [slot] = (base, extra_bits) encoded in two halves */
static const u16 lzma_db[30] = {
    0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,
    1024,1536,2048,3072,4096,6144,8192,12288,16384,24576
};
static const u8 lzma_de[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* ---------- Probability layout (flat array) ---------- */
#define P_IS_MATCH(st,ps)     (0 + (st)*LZMA_POS_MAX + (ps))
#define P_IS_REP(st)          (48 + (st))
#define P_IS_REP_G0(st)       (60 + (st))
#define P_IS_REP_G1(st)       (72 + (st))
#define P_IS_REP_G2(st)       (84 + (st))
#define P_IS_REP0_LONG(st,ps) (96 + (st)*LZMA_POS_MAX + (ps))
#define P_LIT(coder,sub)      (144 + (coder)*4096 + (sub)*256)  /* sub-coder: prevByte context, 256 bits per sub */
/* Actually each literal sub-coder uses a binary tree with 255 internal nodes.
   So we need: 2 coders * 16 sub-coders * 255 = 8160, but max context is 1<<(lc+lp)=16.
   But we use a simpler flat allocation: */
#define LIT_CODER_SIZE        (16 * 255)  /* 4080 per coder */
#define P_LIT2(coder,sub,bit) (144 + (coder)*LIT_CODER_SIZE + (sub)*255 + (bit))
#define P_LEN_CHOICE(c)       (144 + 2*LIT_CODER_SIZE + (c))    /* offset ~8304 */
#define P_LEN_LOW(c,ps,bit)   (8306 + (c)*4*4 + (ps)*4 + (bit))
#define P_LEN_MID(c,ps,bit)   (8338 + (c)*4*4 + (ps)*4 + (bit))
#define P_LEN_HIGH(c,bit)     (8370 + (c)*8 + (bit))
#define P_DIST_SLOT(lp,b)     (8386 + (lp)*64 + (b)) /* 4*64*/  /* each slot bit uses 4 contexts per lenToPosState */
/* Actually distSlot is decoded using a binary tree per lenToPosState.
   We have 4 lenToPosStates × 63 probs = 252 probs starting at 8386 */
#define P_ALIGN(b)            (8638 + (b))

/* ---------- LZMA decode context ---------- */
typedef struct {
    rc_t  rc;
    u16   probs[LZMA_NUM_PROBS];
    u8   *dict;
    u32   dict_size, dict_pos;
    int   state;
    u32   rep0, rep1, rep2, rep3;
    u8    prev_byte;
    int   lc, lp, pb;
} lzma_t;

/* ---------- Helper: decode a match length ---------- */
static u32 lzma_len(lzma_t *lz, u32 pos_state, u16 *choice, u16 *low, u16 *mid, u16 *high) {
    if (!rc_bit(&lz->rc, choice)) {
        return rc_tree(&lz->rc, low + pos_state * 4, 4);
    }
    if (!rc_bit(&lz->rc, choice + 1)) {
        return 8 + rc_tree(&lz->rc, mid + pos_state * 4, 4);
    }
    return 16 + rc_tree(&lz->rc, high, 8);
}

/* ---------- Decode one LZMA sequence ---------- */
/* Returns 0 on success, -1 on error. Updates dict_pos, state, etc. */
static int lzma_seq(lzma_t *lz) {
    u32 pos_state = lz->dict_pos & ((1 << lz->pb) - 1);
    u32 reps[4] = {lz->rep0, lz->rep1, lz->rep2, lz->rep3};

    if (!rc_bit(&lz->rc, &lz->probs[P_IS_MATCH(lz->state, pos_state)])) {
        /* ----- Literal ----- */
        u32 lit_state = lz->prev_byte >> (8 - lz->lc);
        u32 lit_pos   = (lz->dict_pos >> lz->lp) & ((1 << lz->lp) - 1);
        u32 sub = lit_state * (1 << lz->lp) + lit_pos;
        u16 *probs;
        if (lz->state < 7 && lz->rep0 == lz->dict_pos + 1) {
            /* match literal (previous was a match at distance 1) */
            probs = lz->probs + 144 + LIT_CODER_SIZE + sub * 255;
        } else {
            probs = lz->probs + 144 + sub * 255;
        }
        u32 sym = 0;
        u32 m = 1;
        /* Wait — the literal tree has 256 leaves (8-bit byte). The tree
           indexes are 1 to 255 for internal nodes. Leaves are positions
           256-511. But rc_tree returns (m - (1<<n)). */
        for (int i = 0; i < 8; i++) {
            m = (m << 1) + rc_bit(&lz->rc, &probs[m]);
        }
        sym = m - 256;
        if (lz->dict_pos < lz->dict_size) {
            lz->dict[lz->dict_pos++] = (u8)sym;
            lz->prev_byte = (u8)sym;
        }
        lz->state = lzma_ns[lz->state][0];
        reps[0] = lz->rep0;
        return 0;
    }

    /* ----- Match / Rep match ----- */
    if (rc_bit(&lz->rc, &lz->probs[P_IS_REP(lz->state)])) {
        /* Rep match */
        if (rc_bit(&lz->rc, &lz->probs[P_IS_REP_G0(lz->state)])) {
            if (rc_bit(&lz->rc, &lz->probs[P_IS_REP_G1(lz->state)])) {
                u32 t = reps[3]; reps[3] = reps[2]; reps[2] = reps[1]; reps[1] = reps[0]; reps[0] = t;
            } else {
                u32 t = reps[2]; reps[2] = reps[1]; reps[1] = reps[0]; reps[0] = t;
            }
        } else {
            if (rc_bit(&lz->rc, &lz->probs[P_IS_REP0_LONG(lz->state, pos_state)])) {
                reps[0] = reps[0]; /* keep same */
                goto do_rep_match;
            } else {
                /* Short rep match: copy 1 byte */
                if (lz->dict_pos >= lz->dict_size) return -1;
                if (lz->dict_pos < reps[0]) return -1;
                lz->dict[lz->dict_pos] = lz->dict[lz->dict_pos - reps[0]];
                lz->prev_byte = lz->dict[lz->dict_pos];
                lz->dict_pos++;
                lz->state = lzma_ns[lz->state][3];
                lz->rep0 = reps[0];
                return 0;
            }
        }
do_rep_match:
        {
            u32 len = lzma_len(lz, pos_state,
                &lz->probs[P_LEN_CHOICE(0)], /* use coder 0 for rep */
                &lz->probs[P_LEN_LOW(0,0,0)],
                &lz->probs[P_LEN_MID(0,0,0)],
                &lz->probs[P_LEN_HIGH(0,0)]);
            len += 2;
            if (lz->dict_pos + len > lz->dict_size) return -1;
            for (u32 i = 0; i < len; i++) {
                if (lz->dict_pos < reps[0]) return -1;
                lz->dict[lz->dict_pos] = lz->dict[lz->dict_pos - reps[0]];
                lz->dict_pos++;
            }
            lz->prev_byte = lz->dict[lz->dict_pos - 1];
            lz->state = lzma_ns[lz->state][2];
            lz->rep0 = reps[0];
        }
        return 0;
    }

    /* ----- Simple match ----- */
    {
        reps[3] = reps[2]; reps[2] = reps[1]; reps[1] = reps[0];

        u32 len = lzma_len(lz, pos_state,
            &lz->probs[P_LEN_CHOICE(1)], /* coder 1 for match */
            &lz->probs[P_LEN_LOW(1,0,0)],
            &lz->probs[P_LEN_MID(1,0,0)],
            &lz->probs[P_LEN_HIGH(1,0)]);
        len += 2;

        /* Decode distance */
        u32 len_to_pos_state = len < 4 ? len - 2 : 3;
        if (len_to_pos_state > 3) len_to_pos_state = 3;

        u16 *dprobs = &lz->probs[P_DIST_SLOT(len_to_pos_state, 0)];
        u32 ds = rc_tree(&lz->rc, dprobs, 6);

        u32 dist;
        if (ds < 4) {
            dist = ds;
        } else if (ds < 14) {
            /* Simple extra bits per slot */
            int extra = lzma_de[ds];
            u32 low = rc_bits(&lz->rc, extra);
            dist = lzma_db[ds] + low;
        } else {
            /* Complex with align */
            int extra = lzma_de[ds];
            u32 low = rc_bits(&lz->rc, extra - LZMA_ALIGN);
            u32 align = rc_rev_tree(&lz->rc, &lz->probs[P_ALIGN(0)], LZMA_ALIGN);
            dist = lzma_db[ds] + (low << LZMA_ALIGN) + align;
        }

        if (dist >= lz->dict_pos && dist != 0xFFFFFFFF) return -1;
        reps[0] = dist + 1;

        if (lz->dict_pos + len > lz->dict_size) return -1;
        for (u32 i = 0; i < len; i++) {
            if (lz->dict_pos < reps[0]) return -1;
            lz->dict[lz->dict_pos] = lz->dict[lz->dict_pos - reps[0]];
            lz->dict_pos++;
        }
        lz->prev_byte = lz->dict[lz->dict_pos - 1];
        lz->state = lzma_ns[lz->state][1];
    }
    return 0;
}

/* ---------- LZMA2 decoder ---------- */
/* Allocates output via AllocatePool.
   Returns decompressed size (>0) or <0 on error. */
static int lzma2_decompress(const u8 *in, u32 in_sz, u8 **out, u32 *out_sz) {
    lzma_t lz;
    u32 out_max = 256 * 1024 * 1024; /* 256 MB max */

    /* Allocate dictionary + output buffer.
       The output IS the dictionary in LZMA2 — it writes to the sliding window. */
    u8 *buf = NULL;
    efi_status st = ((efi_bs_allocate_pool)g_BS->AllocatePool)(4, out_max, (void**)&buf);
    if (EFI_ERROR(st) || !buf) return -1;

    lumie_memset(&lz, 0, sizeof(lz));
    lz.dict = buf;
    lz.dict_size = out_max;
    lz.dict_pos = 0;
    lz.state = 0;
    lz.rep0 = lz.rep1 = lz.rep2 = lz.rep3 = 0;
    lz.prev_byte = 0;
    lz.lc = 3; lz.lp = 0; lz.pb = 2; /* defaults, overwritten by properties */

    /* Reset all probabilities to 1024 */
    for (int i = 0; i < LZMA_NUM_PROBS; i++) lz.probs[i] = 1024;

    u32 in_pos = 0;

    while (in_pos < in_sz) {
        u8 ctrl = in[in_pos++];

        if (ctrl == 0x00) {
            break;
        }

        if (ctrl == 0x01) {
            if (in_pos + 5 > in_sz) goto err;
            u8 props = in[in_pos];
            u32 uncomp_size = ((u32)in[in_pos+2] << 16) | ((u32)in[in_pos+3] << 8) | in[in_pos+4];
            in_pos += 5;

            lz.lc = props % 9; props /= 9;
            lz.lp = props % 5;
            lz.pb = props / 5;
            lz.dict_pos = 0;
            lz.state = 0;
            lz.rep0 = lz.rep1 = lz.rep2 = lz.rep3 = 0;
            lz.prev_byte = 0;
            for (int i = 0; i < LZMA_NUM_PROBS; i++) lz.probs[i] = 1024;

            if (uncomp_size > out_max - lz.dict_pos) goto err;
            if (lz.dict_pos + uncomp_size > lz.dict_size) goto err;

            if (in_pos + 5 > in_sz) goto err;
            rc_init(&lz.rc, in + in_pos, in_sz - in_pos);
            if (uncomp_size == 0) continue;

            u32 target = lz.dict_pos + uncomp_size;
            while (lz.dict_pos < target) {
                if (lz.rc.pos >= lz.rc.size) break;
                if (lzma_seq(&lz) < 0) goto err;
            }
            in_pos = (u32)((u64)(lz.rc.buf - in) + lz.rc.pos);
            continue;
        }

        if (ctrl == 0x02) {
            if (in_pos + 5 > in_sz) goto err;
            u8 props = in[in_pos];
            u32 uncomp_size = ((u32)in[in_pos+2] << 16) | ((u32)in[in_pos+3] << 8) | in[in_pos+4];
            in_pos += 5;

            lz.lc = props % 9; props /= 9;
            lz.lp = props % 5;
            lz.pb = props / 5;
            lz.state = 0;
            lz.rep0 = lz.rep1 = lz.rep2 = lz.rep3 = 0;
            lz.prev_byte = 0;
            for (int i = 0; i < LZMA_NUM_PROBS; i++) lz.probs[i] = 1024;

            if (uncomp_size > out_max - lz.dict_pos) goto err;
            if (lz.dict_pos + uncomp_size > lz.dict_size) goto err;

            if (in_pos + 5 > in_sz) goto err;
            rc_init(&lz.rc, in + in_pos, in_sz - in_pos);
            if (uncomp_size == 0) continue;

            u32 target = lz.dict_pos + uncomp_size;
            while (lz.dict_pos < target) {
                if (lz.rc.pos >= lz.rc.size) break;
                if (lzma_seq(&lz) < 0) goto err;
            }
            in_pos = (u32)((u64)(lz.rc.buf - in) + lz.rc.pos);
            continue;
        }

        if (ctrl >= 0x03 && ctrl <= 0x7F) {
            if (in_pos + 2 > in_sz) goto err;
            u32 sz = ((u32)ctrl * 256 * 16) | ((u32)in[in_pos] << 8) | in[in_pos + 1];
            in_pos += 2;
            if (sz > out_max - lz.dict_pos) goto err;
            if (in_pos + sz > in_sz) goto err;
            lumie_memcpy(lz.dict + lz.dict_pos, in + in_pos, sz);
            lz.dict_pos += sz;
            in_pos += sz;
            continue;
        }

        if (ctrl >= 0x80) {
            if (in_pos + 2 > in_sz) goto err;
            u32 sz = ((u32)(ctrl & 0x1F) << 16) | ((u32)in[in_pos] << 8) | in[in_pos + 1];
            in_pos += 2;

            lz.state = 0;
            lz.rep0 = lz.rep1 = lz.rep2 = lz.rep3 = 0;
            lz.prev_byte = lz.dict_pos > 0 ? lz.dict[lz.dict_pos - 1] : 0;
            for (int i = 0; i < 144; i++) lz.probs[i] = 1024;
            for (int i = 144 + 2*LIT_CODER_SIZE; i < LZMA_NUM_PROBS; i++) lz.probs[i] = 1024;

            if (sz > in_sz - in_pos) goto err;
            if (in_pos + 5 > in_sz) goto err;
            rc_init(&lz.rc, in + in_pos, sz);
            in_pos += 5;

            while (lz.rc.pos < lz.rc.size) {
                if (lz.dict_pos >= lz.dict_size) break;
                if (lzma_seq(&lz) < 0) break;
            }
            in_pos += sz;
            continue;
        }
    }

    if (lz.dict_pos > 0) {
        /* Reallocate to exact size */
        u8 *exact = NULL;
        st = ((efi_bs_allocate_pool)g_BS->AllocatePool)(4, lz.dict_pos, (void**)&exact);
        if (!EFI_ERROR(st) && exact) {
            lumie_memcpy(exact, lz.dict, lz.dict_pos);
            ((efi_bs_free_pool)g_BS->FreePool)(buf);
            *out = exact;
        } else {
            *out = buf;
        }
        *out_sz = lz.dict_pos;
        return (int)lz.dict_pos;
    }

err:
    ((efi_bs_free_pool)g_BS->FreePool)(buf);
    return -1;
}

/* ---------- XZ container parser ---------- */
static int xz_decompress(const u8 *in, u32 in_sz, u8 **out, u32 *out_sz) {
    /* XZ Stream Header: FD 37 7A 58 5A 00 */
    if (in_sz < 12) return -1;
    if (in[0] != 0xFD || in[1] != '7' || in[2] != 'z' ||
        in[3] != 'X' || in[4] != 'Z' || in[5] != 0x00) return -1;
    if (in[6] != 0x00) return -1;

    /* Skip Stream Header (6) + CRC32 (4) */
    u32 off = 12;
    /* Block Header: variable size, first byte = (size/4 - 1) */
    if (off >= in_sz) return -1;
    u32 bh_size = (in[off] + 1) * 4;
    off += bh_size;
    if (off >= in_sz) return -1;

    /* Remaining data is LZMA2 + Block Padding + Check + Index + Footer.
       For simplicity, extract just the LZMA2 data.
       XZ block contains: Block Header + Block Data (LZMA2) + Padding + Check.
       We skip the Block Header and decode LZMA2 until we hit the Index. */

    /* The block data is LZMA2, and we just pass it to lzma2_decompress.
       But we need to know where the LZMA2 data ends (before block padding).
       For now, pass the rest of the data and let lzma2_decompress stop at 0x00. */

    term_writeln("  Format: XZ container");

    u32 lzma2_sz = in_sz - off;
    u8 *dec = NULL;
    u32 dec_sz = 0;
    int ret = lzma2_decompress(in + off, lzma2_sz, &dec, &dec_sz);

    if (ret < 0 || !dec) {
        term_writeln("  LZMA2 decompression failed");
        return -1;
    }

    *out = dec;
    *out_sz = dec_sz;
    return 0;
}

/* ===== Main entry ===== */
int extract_gzip_tar(const char *filename) {
    if (!filename) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: extract <file.tar.gz>");
        term_set_fg(LUMIE_LIGHTGRAY);
        return -1;
    }

    int fsz = fat_get_file_size(filename);
    if (fsz <= 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("File not found: "); term_writeln(filename);
        term_set_fg(LUMIE_LIGHTGRAY);
        return -1;
    }

    char buf[64];
    lumie_itoa(fsz, buf, 10);
    term_write("Reading "); term_write(buf); term_writeln(" bytes");

    u8 *data = NULL;
    efi_status st = ((efi_bs_allocate_pool)g_BS->AllocatePool)(4, fsz, (void**)&data);
    if (EFI_ERROR(st) || !data) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Out of memory");
        term_set_fg(LUMIE_LIGHTGRAY);
        return -1;
    }

    if (fat_read_file(filename, data, fsz) != fsz) {
        ((efi_bs_free_pool)g_BS->FreePool)(data);
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Read error");
        term_set_fg(LUMIE_LIGHTGRAY);
        return -1;
    }

#define XZ_MAGIC "\xFD\x37\x7A\x58\x5A\x00"

    if (data[0] == 0x1F && data[1] == 0x8B) {
        term_writeln("Format: gzip");
        u8 *dec = NULL; u32 dec_sz = 0;
        int ret = gzip_decompress(data, fsz, &dec, &dec_sz);
        ((efi_bs_free_pool)g_BS->FreePool)(data);
        if (ret < 0 || !dec) {
            term_set_fg(LUMIE_LIGHTRED);
            term_writeln("Decompression failed");
            term_set_fg(LUMIE_LIGHTGRAY);
            return -1;
        }
        lumie_itoa(dec_sz, buf, 10);
        term_write("Decompressed: "); term_write(buf); term_writeln(" bytes");

        int cnt = tar_extract(dec, dec_sz);
        ((efi_bs_free_pool)g_BS->FreePool)(dec);
        lumie_itoa(cnt, buf, 10);
        term_write(buf); term_writeln(" entries extracted");
        return 0;
    }

    if (lumie_memcmp(data, XZ_MAGIC, 6) == 0) {
        term_writeln("Format: XZ");
        u8 *dec = NULL; u32 dec_sz = 0;
        int ret = xz_decompress(data, fsz, &dec, &dec_sz);
        ((efi_bs_free_pool)g_BS->FreePool)(data);
        if (ret < 0 || !dec) {
            term_set_fg(LUMIE_LIGHTRED);
            term_writeln("XZ decompression failed");
            term_set_fg(LUMIE_LIGHTGRAY);
            return -1;
        }
        lumie_itoa(dec_sz, buf, 10);
        term_write("Decompressed: "); term_write(buf); term_writeln(" bytes");

        int cnt = tar_extract(dec, dec_sz);
        ((efi_bs_free_pool)g_BS->FreePool)(dec);
        lumie_itoa(cnt, buf, 10);
        term_write(buf); term_writeln(" entries extracted");
        return 0;
    }

    if (lumie_strstr(filename, ".tar")) {
        term_writeln("Format: tar");
        int cnt = tar_extract(data, fsz);
        ((efi_bs_free_pool)g_BS->FreePool)(data);
        lumie_itoa(cnt, buf, 10);
        term_write(buf); term_writeln(" entries extracted");
        return 0;
    }

    ((efi_bs_free_pool)g_BS->FreePool)(data);
    term_set_fg(LUMIE_LIGHTRED);
    term_writeln("Unknown format");
    term_set_fg(LUMIE_LIGHTGRAY);
    return -1;
}
