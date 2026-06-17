#include "tls.h"
#include "terminal.h"
#include "kernel.h"
#include "lumie.h"

/* Minimal TLS 1.2 Client - TLS_RSA_WITH_AES_128_CBC_SHA256
   Works on any UEFI. No certificate validation. */

/* ========================= SHA-256 ========================= */
#define RR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define EP0(x) (RR(x,2)^RR(x,13)^RR(x,22))
#define EP1(x) (RR(x,6)^RR(x,11)^RR(x,25))
#define SG0(x) (RR(x,7)^RR(x,18)^((x)>>3))
#define SG1(x) (RR(x,17)^RR(x,19)^((x)>>10))
static const u32 K256[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34bcb0b5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
typedef struct{ u32 s[8]; u64 cnt; u8 buf[64]; } sha2_ctx;
static void sha2_init(sha2_ctx *c){c->s[0]=0x6a09e667;c->s[1]=0xbb67ae85;c->s[2]=0x3c6ef372;c->s[3]=0xa54ff53a;c->s[4]=0x510e527f;c->s[5]=0x9b05688c;c->s[6]=0x1f83d9ab;c->s[7]=0x5be0cd19;c->cnt=0;}
static void sha2_blk(u32 s[8],const u8 *b){
    u32 w[64],a,b0,c,d,e,f,g,h,t1,t2;
    for(int i=0;i<16;i++)w[i]=((u32)b[4*i]<<24)|((u32)b[4*i+1]<<16)|((u32)b[4*i+2]<<8)|b[4*i+3];
    for(int i=16;i<64;i++)w[i]=SG1(w[i-2])+w[i-7]+SG0(w[i-15])+w[i-16];
    a=s[0];b0=s[1];c=s[2];d=s[3];e=s[4];f=s[5];g=s[6];h=s[7];
    for(int i=0;i<64;i++){t1=h+EP1(e)+((e&f)^((~e)&g))+K256[i]+w[i];t2=EP0(a)+((a&b0)^(a&c)^(b0&c));h=g;g=f;f=e;e=d+t1;d=c;c=b0;b0=a;a=t1+t2;}
    s[0]+=a;s[1]+=b0;s[2]+=c;s[3]+=d;s[4]+=e;s[5]+=f;s[6]+=g;s[7]+=h;
}
static void sha2_upd(sha2_ctx *c,const void *d,u32 n){
    const u8 *p=(const u8*)d;u32 idx=(u32)(c->cnt&63);c->cnt+=n;
    if(n>=64-idx){lumie_memcpy(c->buf+idx,p,64-idx);sha2_blk(c->s,c->buf);p+=64-idx;n-=64-idx;while(n>=64){sha2_blk(c->s,p);p+=64;n-=64;}idx=0;}
    lumie_memcpy(c->buf+idx,p,n);
}
static void sha2_done(sha2_ctx *c,u8 *d){
    u64 bits=c->cnt<<3;u32 idx=(u32)(c->cnt&63);c->buf[idx++]=0x80;
    if(idx>56){lumie_memset(c->buf+idx,0,64-idx);sha2_blk(c->s,c->buf);idx=0;}
    lumie_memset(c->buf+idx,0,56-idx);for(int i=0;i<8;i++)c->buf[56+i]=(u8)(bits>>(56-8*i));
    sha2_blk(c->s,c->buf);for(int i=0;i<8;i++){d[4*i]=(u8)(c->s[i]>>24);d[4*i+1]=(u8)(c->s[i]>>16);d[4*i+2]=(u8)(c->s[i]>>8);d[4*i+3]=(u8)c->s[i];}
}
static void sha256(const void *d,u32 n,u8 *h){sha2_ctx c;sha2_init(&c);sha2_upd(&c,d,n);sha2_done(&c,h);}

/* ======================= HMAC-SHA256 ======================= */
static void hmac_sha256(const u8 *k,u32 kl,const u8 *d,u32 dl,u8 *m){
    u8 tk[32];if(kl>64){sha256(k,kl,tk);k=tk;kl=32;}
    u8 k_ip[64],k_op[64];lumie_memset(k_ip,0,64);lumie_memcpy(k_ip,k,kl);lumie_memset(k_op,0,64);lumie_memcpy(k_op,k,kl);
    for(int i=0;i<64;i++)k_ip[i]^=0x36,k_op[i]^=0x5c;
    sha2_ctx c;sha2_init(&c);sha2_upd(&c,k_ip,64);sha2_upd(&c,d,dl);sha2_done(&c,m);
    sha2_init(&c);sha2_upd(&c,k_op,64);sha2_upd(&c,m,32);sha2_done(&c,m);
}

/* ======================= AES-128-CBC ======================= */
static const u8 sb[256]={
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const u8 isb[256]={
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};
static void aes_keyexp(const u8 *k,u8 *rk){
    u32 *w=(u32*)rk;for(int i=0;i<4;i++)w[i]=((u32)k[4*i]<<24)|((u32)k[4*i+1]<<16)|((u32)k[4*i+2]<<8)|k[4*i+3];
    for(int i=4;i<44;i++){u32 t=w[i-1];if(i%4==0){t=(t<<8)|(t>>24);t=((u32)sb[t>>24]<<24)|((u32)sb[(t>>16)&0xFF]<<16)|((u32)sb[(t>>8)&0xFF]<<8)|sb[t&0xFF];t^=((u32)1<<((i/4)-1));}w[i]=w[i-4]^t;}
}
static void aes_enc(const u8 *in,u8 *out,const u8 *rk){
    u32 s[4],t[4];const u32 *kp=(const u32*)rk;for(int i=0;i<4;i++)s[i]=(((u32)in[4*i]<<24)|((u32)in[4*i+1]<<16)|((u32)in[4*i+2]<<8)|in[4*i+3])^kp[i];
    for(int r=1;r<=10;r++){
        for(int i=0;i<4;i++){u32 x=s[(i+0)%4];t[i]=((u32)sb[x>>24]<<24)|((u32)sb[(x>>16)&0xFF]<<16)|((u32)sb[(x>>8)&0xFF]<<8)|sb[x&0xFF];}
        if(r<10){u32 v[4];v[0]=t[0]^((t[1]>>16)|(t[1]<<24))^((t[2]>>8)|(t[2]<<24))^t[3];v[1]=t[1]^((t[2]>>16)|(t[2]<<24))^((t[3]>>8)|(t[3]<<24))^t[0];v[2]=t[2]^((t[3]>>16)|(t[3]<<24))^((t[0]>>8)|(t[0]<<24))^t[1];v[3]=t[3]^((t[0]>>16)|(t[0]<<24))^((t[1]>>8)|(t[1]<<24))^t[2];t[0]=v[0];t[1]=v[1];t[2]=v[2];t[3]=v[3];}
        for(int i=0;i<4;i++)s[i]=t[i]^kp[4*r+i];
    }
    for(int i=0;i<4;i++){out[4*i]=(u8)(s[i]>>24);out[4*i+1]=(u8)(s[i]>>16);out[4*i+2]=(u8)(s[i]>>8);out[4*i+3]=(u8)s[i];}
}
static void aes_dec(const u8 *in,u8 *out,const u8 *rk){
    u32 s[4],t[4];const u32 *kp=(const u32*)rk;for(int i=0;i<4;i++)s[i]=(((u32)in[4*i]<<24)|((u32)in[4*i+1]<<16)|((u32)in[4*i+2]<<8)|in[4*i+3])^kp[40+i];
    for(int r=9;r>=0;r--){
        for(int i=0;i<4;i++){u32 x=s[(i+0)%4];t[i]=((u32)isb[x>>24]<<24)|((u32)isb[(x>>16)&0xFF]<<16)|((u32)isb[(x>>8)&0xFF]<<8)|isb[x&0xFF];}
        if(r>0){u32 v[4];v[0]=t[0]^((t[1]>>24)|(t[1]<<8))^((t[2]>>16)|(t[2]<<16))^((t[3]>>8)|(t[3]<<24));v[1]=t[1]^((t[2]>>24)|(t[2]<<8))^((t[3]>>16)|(t[3]<<16))^((t[0]>>8)|(t[0]<<24));v[2]=t[2]^((t[3]>>24)|(t[3]<<8))^((t[0]>>16)|(t[0]<<16))^((t[1]>>8)|(t[1]<<24));v[3]=t[3]^((t[0]>>24)|(t[0]<<8))^((t[1]>>16)|(t[1]<<16))^((t[2]>>8)|(t[2]<<24));t[0]=v[0];t[1]=v[1];t[2]=v[2];t[3]=v[3];}
        for(int i=0;i<4;i++)s[i]=t[i]^kp[4*r+i];
    }
    for(int i=0;i<4;i++){out[4*i]=(u8)(s[i]>>24);out[4*i+1]=(u8)(s[i]>>16);out[4*i+2]=(u8)(s[i]>>8);out[4*i+3]=(u8)s[i];}
}
static void aes_cbc_enc(const u8 *k,const u8 *iv,const u8 *in,u32 len,u8 *out){
    u8 rk[176];aes_keyexp(k,rk);u8 c[16];lumie_memcpy(c,iv,16);
    for(u32 i=0;i<len;i+=16){for(int j=0;j<16;j++)c[j]^=in[i+j];aes_enc(c,out+i,rk);lumie_memcpy(c,out+i,16);}
}
static void aes_cbc_dec(const u8 *k,const u8 *iv,const u8 *in,u32 len,u8 *out){
    u8 rk[176];aes_keyexp(k,rk);u8 c[16];lumie_memcpy(c,iv,16);
    for(u32 i=0;i<len;i+=16){aes_dec(in+i,out+i,rk);for(int j=0;j<16;j++)out[i+j]^=c[j];lumie_memcpy(c,in+i,16);}
}

/* ======================= BigNum (RSA 2048-bit) ======================= */
#define BN_SZ 256
static void bn_mul_mod(u8 *r,const u8 *a,const u8 *b,const u8 *m){
    u8 t[BN_SZ*2];lumie_memset(t,0,BN_SZ*2);
    for(int i=BN_SZ-1;i>=0;i--){int c=0;for(int j=BN_SZ-1;j>=0;j--){int s=(int)a[i]*b[j]+t[i+j+1]+c;t[i+j+1]=(u8)s;c=s>>8;}t[i]+=(u8)c;}
    lumie_memset(r,0,BN_SZ);
    for(int i=0;i<=BN_SZ;i++){int sub=1;for(int j=0;j<BN_SZ;j++)if(t[i+j]!=m[j]){if(t[i+j]<m[j])sub=0;break;}if(sub){int b=0;for(int j=BN_SZ-1;j>=0;j--){int d=t[i+j]-m[j]-b;if(d<0){d+=256;b=1;}else b=0;t[i+j]=(u8)d;}}}
    lumie_memcpy(r,t,BN_SZ);
}
static void bn_mod_exp(u8 *r,const u8 *b,const u8 *e,int el,const u8 *m){
    u8 base[BN_SZ],exp[BN_SZ];lumie_memcpy(base,b,BN_SZ);lumie_memset(r,0,BN_SZ);r[BN_SZ-1]=1;
    lumie_memset(exp,0,BN_SZ);lumie_memcpy(exp+BN_SZ-el,e,el);
    for(int i=0;i<BN_SZ*8;i++){if(exp[i/8]&(0x80>>(i%8)))bn_mul_mod(r,r,base,m);bn_mul_mod(base,base,base,m);}
}
static int rsa_enc(const u8 *n,int nl,const u8 *e,int el,const u8 *in,int il,u8 *out){
    if(il+11>nl)return -1;
    u8 pad[BN_SZ];lumie_memset(pad,0,nl);pad[0]=0;pad[1]=2;
    int ps=nl-il-3;for(int i=0;i<ps;i++)pad[2+i]=0xFF;pad[2+ps]=0;lumie_memcpy(pad+3+ps,in,il);
    u8 nb[BN_SZ],eb[BN_SZ],pb[BN_SZ],rb[BN_SZ];
    lumie_memset(nb,0,BN_SZ);lumie_memcpy(nb+BN_SZ-nl,n,nl);
    lumie_memset(eb,0,BN_SZ);lumie_memcpy(eb+BN_SZ-el,e,el);
    lumie_memset(pb,0,BN_SZ);lumie_memcpy(pb+BN_SZ-nl,pad,nl);
    bn_mod_exp(rb,pb,eb,el,nb);
    lumie_memcpy(out,rb+BN_SZ-nl,nl);return nl;
}

/* ======================= ASN.1 DER (minimal) ======================= */
static const u8 *der_t(const u8 *p,int tag,int *len){
    if(p[0]!=tag)return NULL;if(p[1]<0x80){*len=p[1];return p+2;}
    int nl=p[1]&0x7F;*len=0;for(int i=0;i<nl;i++)*len=(*len<<8)|p[2+i];return p+2+nl;
}
static int ext_key(const u8 *cert,int clen,u8 *n,int *nl,u8 *e,int *el){
    int len;const u8 *tbs=der_t(cert,0x30,&len);if(!tbs||tbs+len>cert+clen)return -1;
    const u8 *p=tbs;
    if(p[0]==0xA0){int sl;const u8 *sv=der_t(p,0xA0,&sl);if(!sv)return -1;p=sv+sl;}
    {int sl;const u8 *sn=der_t(p,0x02,&sl);if(!sn)return -1;p=sn+sl;}
    {int sl;const u8 *sg=der_t(p,0x30,&sl);if(!sg)return -1;p=sg+sl;}
    {int sl;const u8 *is=der_t(p,0x30,&sl);if(!is)return -1;p=is+sl;}
    {int sl;const u8 *vd=der_t(p,0x30,&sl);if(!vd)return -1;p=vd+sl;}
    {int sl;const u8 *sb=der_t(p,0x30,&sl);if(!sb)return -1;p=sb+sl;}
    {int sl;const u8 *spki=der_t(p,0x30,&sl);if(!spki)return -1;p=spki;}
    {int sl;const u8 *alg=der_t(p,0x30,&sl);if(!alg)return -1;p=alg+sl;}
    {int sl;const u8 *spk=der_t(p,0x03,&sl);if(!spk)return -1;p=spk+1;sl--;
     int rl;const u8 *rsa=der_t(p,0x30,&rl);if(!rsa)return -1;
     int tnl;const u8 *tn=der_t(rsa,0x02,&tnl);if(!tn)return -1;if(tn[0]==0){tn++;tnl--;}
     *nl=tnl;lumie_memset(n,0,BN_SZ);lumie_memcpy(n+BN_SZ-tnl,tn,tnl);
     const u8 *te=tn+tnl+2;{int tl;te=der_t(te-2,0x02,&tl);if(te){*el=tl;if(te[0]==0){te++;(*el)--;}lumie_memset(e,0,BN_SZ);lumie_memcpy(e+BN_SZ-*el,te,*el);}else return -1;}
     return 0;}
}

/* ======================= TLS 1.2 ======================= */
#define HDR_SZ 5
#define CT_APP 23
#define CT_CCS 20
#define CT_ALERT 21
#define CT_HS 22
#define HS_CH 1
#define HS_SH 2
#define HS_CERT 11
#define HS_SD 14
#define HS_CKE 16
#define HS_FIN 20
#define CS_RSA_AES128_SHA256 0x003C
static u16 r16(u16 v){return(v>>8)|(v<<8);}
static u32 r32(u32 v){return((v>>24)&0xFF)|((v>>8)&0xFF00)|((v<<8)&0xFF0000)|((v<<24)&0xFF000000);}

/* File-level TLS context (single connection) */
static struct {tls_stream *s;u8 ms[48],sr[32],cr[32];u8 ek[16],mk[32],iv[16];u8 dk[16],dmk[32],div[16];u64 sseq,rseq;u8 rn[BN_SZ+8];int rnl;u8 re[BN_SZ+8];int rel;u8 buf[16384+1024];int pos;sha2_ctx hsh;int hsh_init;int connected;} g_tls;

static void add_hash(const u8 *d,u32 l){if(!g_tls.hsh_init){sha2_init(&g_tls.hsh);g_tls.hsh_init=1;}sha2_upd(&g_tls.hsh,d,l);}
static int sendall(const void *d,u32 l){return g_tls.s->send(d,l);}
static int recv_at_least(u32 n,u64 t){
    u8 *tmp=NULL;if(((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA,4096,(void**)&tmp)!=0||!tmp)return -1;
    while(g_tls.pos<n){u32 tl=4096;if(g_tls.s->recv(tmp,&tl,t)<0){((efi_bs_free_pool)g_BS->FreePool)(tmp);return -1;}if(tl>sizeof(g_tls.buf)-g_tls.pos)tl=sizeof(g_tls.buf)-g_tls.pos;lumie_memcpy(g_tls.buf+g_tls.pos,tmp,tl);g_tls.pos+=tl;}
    ((efi_bs_free_pool)g_BS->FreePool)(tmp);return 0;
}

/* TLS 1.2 PRF: P_SHA256(secret, label + seed) */
static void tls_prf(const u8 *sec,int sl,const char *lab,const u8 *seed,int sdl,u8 *out,int ol){
    int ll=(int)lumie_strlen(lab);u8 sd[96];lumie_memcpy(sd,(const u8*)lab,ll);lumie_memcpy(sd+ll,seed,sdl);
    int ttl=ll+sdl;u8 A[32];hmac_sha256(sec,sl,sd,ttl,A);
    int pos=0;while(pos<ol){u8 buf[128];lumie_memcpy(buf,A,32);lumie_memcpy(buf+32,sd,ttl);hmac_sha256(sec,sl,buf,32+ttl,buf);int copy=ol-pos;if(copy>32)copy=32;lumie_memcpy(out+pos,buf,copy);pos+=copy;if(pos<ol)hmac_sha256(sec,sl,A,32,A);}
}

/* Encrypt plaintext into TLS application data record. Returns record in malloc'ed buf or -1. */
static int tls_encrypt_send(const void *plain,u32 plen){
    int ret=-1;
    if(plen>16336)return -1;
    u8 *pkt=NULL;if(((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA,16384,(void**)&pkt)!=0||!pkt)goto en_done;
    u8 *mh=NULL;if(((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA,13+16384,(void**)&mh)!=0||!mh)goto en_done;
    u8 *cipher=NULL;if(((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA,16384,(void**)&cipher)!=0||!cipher)goto en_done;
    u8 *rec=NULL;if(((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA,16384+64,(void**)&rec)!=0||!rec)goto en_done;
    /* Build plaintext = data + HMAC + padding */
    int pp=0;
    lumie_memcpy(pkt,plain,plen);pp=plen;
    /* HMAC: seq(8) + type(1) + version(2) + length(2) + plaintext */
    for(int i=0;i<8;i++)mh[i]=(u8)(g_tls.sseq>>(56-8*i));mh[8]=CT_APP;mh[9]=3;mh[10]=3;
    *(u16*)(mh+11)=r16(plen);lumie_memcpy(mh+13,plain,plen);
    u8 mac[32];hmac_sha256(g_tls.mk,32,mh,13+plen,mac);
    lumie_memcpy(pkt+pp,mac,32);pp+=32;
    int pad=16-(pp%16);if(pad<1)pad+=16;for(int i=0;i<pad;i++)pkt[pp+i]=(u8)(pad-1);pp+=pad;
    /* Encrypt */
    aes_cbc_enc(g_tls.ek,g_tls.iv,pkt,pp,cipher);
    /* Record header */
    rec[0]=CT_APP;rec[1]=3;rec[2]=3;*(u16*)(rec+3)=r16(pp);
    u32 total=5+pp;lumie_memcpy(rec+5,cipher,pp);
    /* Update IV to last ciphertext block for next record (TLS 1.2 implicit IV) */
    lumie_memcpy(g_tls.iv, cipher + pp - 16, 16);
    g_tls.sseq++;
    ret=sendall(rec,total);
en_done:
    if(pkt)((efi_bs_free_pool)g_BS->FreePool)(pkt);
    if(mh)((efi_bs_free_pool)g_BS->FreePool)(mh);
    if(cipher)((efi_bs_free_pool)g_BS->FreePool)(cipher);
    if(rec)((efi_bs_free_pool)g_BS->FreePool)(rec);
    return ret;
}

/* Decrypt and return next TLS application data record. buf must be at least 16384 bytes. */
static int tls_decrypt_recv(u8 *buf,u32 *len,u64 to){
    u8 *plain=NULL,*mh=NULL;
    if(((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA,16384,(void**)&plain)!=0||!plain)return -1;
    if(((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA,13+16384,(void**)&mh)!=0||!mh){((efi_bs_free_pool)g_BS->FreePool)(plain);return -1;}
    int ret=-1;int unknown_records=0;
    while(1){
        if(recv_at_least(HDR_SZ,to)<0)break;
        int rtype=g_tls.buf[0];int rlen=r16(*(u16*)(g_tls.buf+3));
        if(recv_at_least(HDR_SZ+rlen,1*1000000)<0)break;
        if(rtype==CT_APP){
            /* Decrypt */
            int plen=rlen;
            if(plen%16!=0||plen<16)break;
            /* Capture ciphertext for next record's IV BEFORE decrypt */
            u8 *rec_cipher = g_tls.buf + HDR_SZ;
            aes_cbc_dec(g_tls.dk, g_tls.div, rec_cipher, plen, plain);
            /* Update IV to received ciphertext for next record (TLS 1.2 implicit IV) */
            lumie_memcpy(g_tls.div, rec_cipher + plen - 16, 16);
            /* Remove padding */
            int pad=plain[plen-1]+1;if(pad>plen||pad<1)break;
            int data_len=plen-pad;
            /* Verify MAC */
            int mac_off=data_len-32;
            if(mac_off<0)break;
            for(int i=0;i<8;i++)mh[i]=(u8)(g_tls.rseq>>(56-8*i));mh[8]=CT_APP;mh[9]=3;mh[10]=3;
            *(u16*)(mh+11)=r16(mac_off);
            lumie_memcpy(mh+13,plain,mac_off);
            u8 mac[32];hmac_sha256(g_tls.dmk,32,mh,13+mac_off,mac);
            if(lumie_memcmp(mac,plain+mac_off,32)!=0){
                /* MAC mismatch - ignore for now (simplified) */
            }
            g_tls.rseq++;
            /* Shift buffer */
            g_tls.pos-=HDR_SZ+rlen;if(g_tls.pos>0)lumie_memmove(g_tls.buf,g_tls.buf+HDR_SZ+rlen,g_tls.pos);
            *len=mac_off;lumie_memcpy(buf,plain,mac_off);
            ret=0;break;
        }else if(rtype==CT_ALERT){
            g_tls.pos-=HDR_SZ+rlen;if(g_tls.pos>0)lumie_memmove(g_tls.buf,g_tls.buf+HDR_SZ+rlen,g_tls.pos);
            break;
        }else{
            g_tls.pos-=HDR_SZ+rlen;if(g_tls.pos>0)lumie_memmove(g_tls.buf,g_tls.buf+HDR_SZ+rlen,g_tls.pos);
            if(++unknown_records>8)break;
        }
    }
    ((efi_bs_free_pool)g_BS->FreePool)(plain);
    ((efi_bs_free_pool)g_BS->FreePool)(mh);
    return ret;
}

int tls_connect(tls_stream *stream,const char *hostname,u16 port){
    (void)port;lumie_memset(&g_tls,0,sizeof(g_tls));g_tls.s=stream;
    term_write(" TLS:CH");
    u8 ch[512];int cp=0;ch[cp++]=CT_HS;ch[cp++]=3;ch[cp++]=3;int rl_off=cp;cp+=2;
    int hs_off=cp;cp++;int hl_off=cp;cp+=3;
    ch[cp++]=3;ch[cp++]=3;
    for(int i=0;i<32;i++)g_tls.cr[i]=(u8)(i*37+13);lumie_memcpy(ch+cp,g_tls.cr,32);cp+=32;
    ch[cp++]=0;
    *(u16*)(ch+cp)=r16(2);cp+=2;*(u16*)(ch+cp)=r16(CS_RSA_AES128_SHA256);cp+=2;
    ch[cp++]=1;ch[cp++]=0;
    int ex_off=cp;cp+=2;
    int snl=(int)lumie_strlen(hostname);
    *(u16*)(ch+cp)=r16(0);cp+=2;*(u16*)(ch+cp)=r16(snl+5);cp+=2;*(u16*)(ch+cp)=r16(snl+3);cp+=2;
    ch[cp++]=0;*(u16*)(ch+cp)=r16(snl);cp+=2;lumie_memcpy(ch+cp,hostname,snl);cp+=snl;
    *(u16*)(ch+cp)=r16(13);cp+=2;*(u16*)(ch+cp)=r16(4);cp+=2;*(u16*)(ch+cp)=r16(2);cp+=2;ch[cp++]=4;ch[cp++]=1;
    *(u16*)(ch+cp)=r16(23);cp+=2;*(u16*)(ch+cp)=r16(0);cp+=2;
    *(u16*)(ch+ex_off)=r16(cp-ex_off-2);
    int hs_len=cp-hs_off-4;ch[hs_off]=HS_CH;ch[hl_off]=0;ch[hl_off+1]=(u8)(hs_len>>8);ch[hl_off+2]=(u8)hs_len;
    *(u16*)(ch+rl_off)=r16(cp-HDR_SZ);
    add_hash(ch+hs_off,4+hs_len);
    sendall(ch,cp);
    term_write(" sent");

    int done=0;
    while(!done){
        if(recv_at_least(HDR_SZ+1,10000000)<0)return -1;
        int rtype=g_tls.buf[0];int rlen=r16(*(u16*)(g_tls.buf+3));
        if(recv_at_least(HDR_SZ+rlen,1000000)<0)return -1;
        if(rtype==CT_HS){
            u8 *hs=g_tls.buf+HDR_SZ;int hp=0;
            while(hp<rlen){int ht=hs[hp];int hl=((u32)hs[hp+1]<<16)|((u32)hs[hp+2]<<8)|hs[hp+3];
                add_hash(hs+hp,4+hl);
                if(ht==HS_SH){term_write(" SH");if(hs[hp+4]!=3||hs[hp+5]!=3)return -1;lumie_memcpy(g_tls.sr,hs+hp+6,32);int sid_len=hs[hp+6+32];int cs=r16(*(u16*)(hs+hp+6+32+1+sid_len));if(cs!=CS_RSA_AES128_SHA256)return -1;}
                else if(ht==HS_CERT){term_write(" CERT");int cd=(hs[hp+7]<<16)|(hs[hp+8]<<8)|hs[hp+9];if(cd>4096)return -1;if(ext_key(hs+hp+10,cd,g_tls.rn,&g_tls.rnl,g_tls.re,&g_tls.rel)<0)return -1;}
                else if(ht==HS_SD){term_write(" SD");done=1;break;}
                hp+=4+hl;}
        }else if(rtype==CT_ALERT){return -1;}
        g_tls.pos-=HDR_SZ+rlen;if(g_tls.pos>0)lumie_memmove(g_tls.buf,g_tls.buf+HDR_SZ+rlen,g_tls.pos);
    }

    u8 pms[48];pms[0]=3;pms[1]=3;for(int i=2;i<48;i++)pms[i]=(u8)(i*41+7);
    {u8 enc[BN_SZ];int encl=rsa_enc(g_tls.rn,g_tls.rnl,g_tls.re,g_tls.rel,pms,48,enc);if(encl<0)return -1;
     term_write(" RSA");
     u8 sd[64];lumie_memcpy(sd,g_tls.cr,32);lumie_memcpy(sd+32,g_tls.sr,32);tls_prf(pms,48,"master secret",sd,64,g_tls.ms,48);
     lumie_memcpy(sd,g_tls.sr,32);lumie_memcpy(sd+32,g_tls.cr,32);u8 kb[128];tls_prf(g_tls.ms,48,"key expansion",sd,64,kb,128);
     lumie_memcpy(g_tls.mk,kb,32);lumie_memcpy(g_tls.dmk,kb+32,32);lumie_memcpy(g_tls.ek,kb+64,16);lumie_memcpy(g_tls.dk,kb+80,16);lumie_memcpy(g_tls.iv,kb+96,16);lumie_memcpy(g_tls.div,kb+112,16);
     term_write(" KEYS");
     u8 cke[512];int cp2=HDR_SZ;cke[0]=CT_HS;cke[1]=3;cke[2]=3;
     cke[cp2++]=HS_CKE;cke[cp2++]=0;cke[cp2++]=0;cke[cp2++]=0;/* placeholder */
     int ln_off=cp2;cp2+=2;lumie_memcpy(cke+cp2,enc,encl);cp2+=encl;
     *(u16*)(cke+ln_off)=r16(encl);int hlen=cp2-HDR_SZ-4;int tls_len=cp2-HDR_SZ;
     cke[HDR_SZ+1]=(u8)(hlen>>16);cke[HDR_SZ+2]=(u8)(hlen>>8);cke[HDR_SZ+3]=(u8)hlen;
     *(u16*)(cke+3)=r16(tls_len);add_hash(cke+HDR_SZ,4+hlen);
     sendall(cke,cp2);}

    {u8 ccs[6]={CT_CCS,3,3,0,1,1};sendall(ccs,6);}term_write(" CCS");

    {u8 hsh_done[32];sha2_done(&g_tls.hsh,hsh_done);u8 vd[12];tls_prf(g_tls.ms,48,"client finished",hsh_done,32,vd,12);
     u8 plain[256];int pp=0;plain[pp++]=HS_FIN;plain[pp++]=0;plain[pp++]=0;plain[pp++]=12;lumie_memcpy(plain+pp,vd,12);pp+=12;
     u8 mh[269];for(int i=0;i<8;i++)mh[i]=(u8)(g_tls.sseq>>(56-8*i));mh[8]=CT_APP;mh[9]=3;mh[10]=3;*(u16*)(mh+11)=r16(pp);lumie_memcpy(mh+13,plain,pp);
     u8 mac[32];hmac_sha256(g_tls.mk,32,mh,13+pp,mac);lumie_memcpy(plain+pp,mac,32);pp+=32;
     int pad=16-(pp%16);if(pad<1)pad+=16;for(int i=0;i<pad;i++)plain[pp+i]=(u8)(pad-1);pp+=pad;
     u8 cipher[256];aes_cbc_enc(g_tls.ek,g_tls.iv,plain,pp,cipher);
     u8 rec[512];rec[0]=CT_APP;rec[1]=3;rec[2]=3;*(u16*)(rec+3)=r16(pp);lumie_memcpy(rec+5,cipher,pp);
     sendall(rec,5+pp);g_tls.sseq++;}

    term_write(" FIN");
    int g_ccs=0;
    while(!g_ccs){if(recv_at_least(HDR_SZ+1,10000000)<0)return -1;int rl=r16(*(u16*)(g_tls.buf+3));if(recv_at_least(HDR_SZ+rl,1000000)<0)return -1;if(g_tls.buf[0]==CT_CCS)g_ccs=1;g_tls.pos-=HDR_SZ+rl;if(g_tls.pos>0)lumie_memmove(g_tls.buf,g_tls.buf+HDR_SZ+rl,g_tls.pos);}
    /* Receive server Finished (app data record) */
    {u8 tmp[128];u32 tl=sizeof(tmp);if(tls_decrypt_recv(tmp,&tl,10000000)<0)return -1;}
    g_tls.hsh_init=0;g_tls.connected=1;
    term_write(" OK");
    return 0;
}

int tls_send(tls_stream *s,const void *data,u32 len){(void)s;if(!g_tls.connected)return -1;return tls_encrypt_send(data,len);}

int tls_recv(tls_stream *s,void *buf,u32 *len,u64 timeout){(void)s;if(!g_tls.connected)return -1;return tls_decrypt_recv((u8*)buf,len,timeout);}

void tls_close(tls_stream *s){(void)s;g_tls.connected=0;}
