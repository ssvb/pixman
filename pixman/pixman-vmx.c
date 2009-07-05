/*
 * Copyright © 2007 Luca Barbato
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Luca Barbato not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Luca Barbato makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 * Author:  Luca Barbato (lu_zero@gentoo.org)
 *
 * Based on fbmmx.c by Owen Taylor, Søren Sandmann and Nicholas Miell
 */

#include <config.h>
#include "pixman-private.h"
#include "pixman-combine32.h"
#include <altivec.h>

#define AVV(x...) {x}

static force_inline vector unsigned int
splat_alpha (vector unsigned int pix) {
    return vec_perm (pix, pix,
    (vector unsigned char)AVV(0x00,0x00,0x00,0x00, 0x04,0x04,0x04,0x04,
                               0x08,0x08,0x08,0x08, 0x0C,0x0C,0x0C,0x0C));
}

static force_inline vector unsigned int
pix_multiply (vector unsigned int p, vector unsigned int a)
{
    vector unsigned short hi, lo, mod;
    /* unpack to short */
    hi = (vector unsigned short)
                    vec_mergeh ((vector unsigned char)AVV(0),
                                (vector unsigned char)p);
    mod = (vector unsigned short)
                    vec_mergeh ((vector unsigned char)AVV(0),
                                (vector unsigned char)a);

    hi = vec_mladd (hi, mod, (vector unsigned short)
                            AVV(0x0080,0x0080,0x0080,0x0080,
                                 0x0080,0x0080,0x0080,0x0080));

    hi = vec_adds (hi, vec_sr (hi, vec_splat_u16 (8)));

    hi = vec_sr (hi, vec_splat_u16 (8));

    /* unpack to short */
    lo = (vector unsigned short)
                    vec_mergel ((vector unsigned char)AVV(0),
                                (vector unsigned char)p);
    mod = (vector unsigned short)
                    vec_mergel ((vector unsigned char)AVV(0),
                                (vector unsigned char)a);

    lo = vec_mladd (lo, mod, (vector unsigned short)
                            AVV(0x0080,0x0080,0x0080,0x0080,
                                 0x0080,0x0080,0x0080,0x0080));

    lo = vec_adds (lo, vec_sr (lo, vec_splat_u16 (8)));

    lo = vec_sr (lo, vec_splat_u16 (8));

    return (vector unsigned int)vec_packsu (hi, lo);
}

static force_inline vector unsigned int
pix_add (vector unsigned int a, vector unsigned int b)
{
    return (vector unsigned int)vec_adds ((vector unsigned char)a,
                     (vector unsigned char)b);
}

static force_inline vector unsigned int
pix_add_mul (vector unsigned int x, vector unsigned int a,
             vector unsigned int y, vector unsigned int b)
{
    vector unsigned short hi, lo, mod, hiy, loy, mody;

    hi = (vector unsigned short)
                    vec_mergeh ((vector unsigned char)AVV(0),
                                (vector unsigned char)x);
    mod = (vector unsigned short)
                    vec_mergeh ((vector unsigned char)AVV(0),
                                (vector unsigned char)a);
    hiy = (vector unsigned short)
                    vec_mergeh ((vector unsigned char)AVV(0),
                                (vector unsigned char)y);
    mody = (vector unsigned short)
                    vec_mergeh ((vector unsigned char)AVV(0),
                                (vector unsigned char)b);

    hi = vec_mladd (hi, mod, (vector unsigned short)
                             AVV(0x0080,0x0080,0x0080,0x0080,
                                  0x0080,0x0080,0x0080,0x0080));

    hi = vec_mladd (hiy, mody, hi);

    hi = vec_adds (hi, vec_sr (hi, vec_splat_u16 (8)));

    hi = vec_sr (hi, vec_splat_u16 (8));

    lo = (vector unsigned short)
                    vec_mergel ((vector unsigned char)AVV(0),
                                (vector unsigned char)x);
    mod = (vector unsigned short)
                    vec_mergel ((vector unsigned char)AVV(0),
                                (vector unsigned char)a);

    loy = (vector unsigned short)
                    vec_mergel ((vector unsigned char)AVV(0),
                                (vector unsigned char)y);
    mody = (vector unsigned short)
                    vec_mergel ((vector unsigned char)AVV(0),
                                (vector unsigned char)b);

    lo = vec_mladd (lo, mod, (vector unsigned short)
                             AVV(0x0080,0x0080,0x0080,0x0080,
                                  0x0080,0x0080,0x0080,0x0080));

    lo = vec_mladd (loy, mody, lo);

    lo = vec_adds (lo, vec_sr (lo, vec_splat_u16 (8)));

    lo = vec_sr (lo, vec_splat_u16 (8));

    return (vector unsigned int)vec_packsu (hi, lo);
}

static force_inline vector unsigned int
negate (vector unsigned int src)
{
    return vec_nor (src, src);
}
/* dest*~srca + src */
static force_inline vector unsigned int
over (vector unsigned int src, vector unsigned int srca,
      vector unsigned int dest)
{
    vector unsigned char tmp = (vector unsigned char)
                                pix_multiply (dest, negate (srca));
    tmp = vec_adds ((vector unsigned char)src, tmp);
    return (vector unsigned int)tmp;
}

/* in == pix_multiply */
#define in_over(src, srca, mask, dest) over (pix_multiply (src, mask),\
                                             pix_multiply (srca, mask), dest)


#define COMPUTE_SHIFT_MASK(source) \
    source ## _mask = vec_lvsl (0, source);

#define COMPUTE_SHIFT_MASKS(dest, source) \
    dest ## _mask = vec_lvsl (0, dest); \
    source ## _mask = vec_lvsl (0, source); \
    store_mask = vec_lvsr (0, dest);

#define COMPUTE_SHIFT_MASKC(dest, source, mask) \
    mask ## _mask = vec_lvsl (0, mask); \
    dest ## _mask = vec_lvsl (0, dest); \
    source ## _mask = vec_lvsl (0, source); \
    store_mask = vec_lvsr (0, dest);

/* notice you have to declare temp vars...
 * Note: tmp3 and tmp4 must remain untouched!
 */

#define LOAD_VECTORS(dest, source) \
        tmp1 = (typeof(tmp1))vec_ld(0, source); \
        tmp2 = (typeof(tmp2))vec_ld(15, source); \
        tmp3 = (typeof(tmp3))vec_ld(0, dest); \
        v ## source = (typeof(v ## source)) \
                       vec_perm(tmp1, tmp2, source ## _mask); \
        tmp4 = (typeof(tmp4))vec_ld(15, dest); \
        v ## dest = (typeof(v ## dest)) \
                     vec_perm(tmp3, tmp4, dest ## _mask);

#define LOAD_VECTORSC(dest, source, mask) \
        tmp1 = (typeof(tmp1))vec_ld(0, source); \
        tmp2 = (typeof(tmp2))vec_ld(15, source); \
        tmp3 = (typeof(tmp3))vec_ld(0, dest); \
        v ## source = (typeof(v ## source)) \
                       vec_perm(tmp1, tmp2, source ## _mask); \
        tmp4 = (typeof(tmp4))vec_ld(15, dest); \
        tmp1 = (typeof(tmp1))vec_ld(0, mask); \
        v ## dest = (typeof(v ## dest)) \
                     vec_perm(tmp3, tmp4, dest ## _mask); \
        tmp2 = (typeof(tmp2))vec_ld(15, mask); \
        v ## mask = (typeof(v ## mask)) \
                     vec_perm(tmp1, tmp2, mask ## _mask);

#define LOAD_VECTORSM(dest, source, mask) \
        LOAD_VECTORSC(dest, source, mask) \
        v ## source = pix_multiply(v ## source, \
                                   splat_alpha (v ## mask));

#define STORE_VECTOR(dest) \
        edges = vec_perm (tmp4, tmp3, dest ## _mask); \
        tmp3 = vec_perm ((vector unsigned char)v ## dest, edges, store_mask); \
        tmp1 = vec_perm (edges, (vector unsigned char)v ## dest, store_mask); \
        vec_st ((vector unsigned int) tmp3, 15, dest ); \
        vec_st ((vector unsigned int) tmp1, 0, dest );

static void
vmxCombineOverUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = over (vsrc, splat_alpha (vsrc), vdest);

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t ia = Alpha (~s);

        UN8x4_MUL_UN8_ADD_UN8x4 (d, ia, s);
        dest[i] = d;
    }
}

static void
vmxCombineOverUmask (uint32_t *dest,
                     const uint32_t *src,
                     const uint32_t *mask,
                     int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask);

        vdest = over (vsrc, splat_alpha (vsrc), vdest);

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t ia;

        UN8x4_MUL_UN8 (s, m);

        ia = Alpha (~s);

        UN8x4_MUL_UN8_ADD_UN8x4 (d, ia, s);
        dest[i] = d;
    }
}

static void
vmxCombineOverU(pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest, const uint32_t *src, const uint32_t *mask,
                int width)
{
    if (mask)
        vmxCombineOverUmask(dest, src, mask, width);
    else
        vmxCombineOverUnomask(dest, src, width);
}

static void
vmxCombineOverReverseUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = over (vdest, splat_alpha (vdest) , vsrc);

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t ia = Alpha (~dest[i]);

        UN8x4_MUL_UN8_ADD_UN8x4 (s, ia, d);
        dest[i] = s;
    }
}

static void
vmxCombineOverReverseUmask (uint32_t *dest,
                            const uint32_t *src,
                            const uint32_t *mask,
                            int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = over (vdest, splat_alpha (vdest) , vsrc);

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t ia = Alpha (~dest[i]);

        UN8x4_MUL_UN8 (s, m);

        UN8x4_MUL_UN8_ADD_UN8x4 (s, ia, d);
        dest[i] = s;
    }
}

static void
vmxCombineOverReverseU (pixman_implementation_t *imp, pixman_op_t op,
			uint32_t *dest, const uint32_t *src,
                        const uint32_t *mask, int width)
{
    if (mask)
        vmxCombineOverReverseUmask(dest, src, mask, width);
    else
        vmxCombineOverReverseUnomask(dest, src, width);
}

static void
vmxCombineInUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = pix_multiply (vsrc, splat_alpha (vdest));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {

        uint32_t s = src[i];
        uint32_t a = Alpha (dest[i]);
        UN8x4_MUL_UN8 (s, a);
        dest[i] = s;
    }
}

static void
vmxCombineInUmask (uint32_t *dest,
                   const uint32_t *src,
                   const uint32_t *mask,
                   int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_multiply (vsrc, splat_alpha (vdest));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t a = Alpha (dest[i]);

        UN8x4_MUL_UN8 (s, m);

        UN8x4_MUL_UN8 (s, a);
        dest[i] = s;
    }
}

static void
vmxCombineInU (pixman_implementation_t *imp, pixman_op_t op,
	       uint32_t *dest, const uint32_t *src, const uint32_t *mask,
               int width)
{
    if (mask)
        vmxCombineInUmask(dest, src, mask, width);
    else
        vmxCombineInUnomask(dest, src, width);
}

static void
vmxCombineInReverseUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = pix_multiply (vdest, splat_alpha (vsrc));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t d = dest[i];
        uint32_t a = Alpha (src[i]);
        UN8x4_MUL_UN8 (d, a);
        dest[i] = d;
    }
}

static void
vmxCombineInReverseUmask (uint32_t *dest,
                          const uint32_t *src,
                          const uint32_t *mask,
                          int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_multiply (vdest, splat_alpha (vsrc));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t d = dest[i];
        uint32_t a = src[i];

        UN8x4_MUL_UN8 (a, m);

        a = Alpha (a);
        UN8x4_MUL_UN8 (d, a);
        dest[i] = d;
    }
}

static void
vmxCombineInReverseU (pixman_implementation_t *imp, pixman_op_t op,
		      uint32_t *dest, const uint32_t *src,
                      const uint32_t *mask, int width)
{
    if (mask)
        vmxCombineInReverseUmask(dest, src, mask, width);
    else
        vmxCombineInReverseUnomask(dest, src, width);
}

static void
vmxCombineOutUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = pix_multiply (vsrc, splat_alpha (negate (vdest)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t s = src[i];
        uint32_t a = Alpha (~dest[i]);
        UN8x4_MUL_UN8 (s, a);
        dest[i] = s;
    }
}

static void
vmxCombineOutUmask (uint32_t *dest,
                    const uint32_t *src,
                    const uint32_t *mask,
                    int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_multiply (vsrc, splat_alpha (negate (vdest)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t a = Alpha (~dest[i]);

        UN8x4_MUL_UN8 (s, m);

        UN8x4_MUL_UN8 (s, a);
        dest[i] = s;
    }
}

static void
vmxCombineOutU (pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest, const uint32_t *src, const uint32_t *mask,
                int width)
{
    if (mask)
        vmxCombineOutUmask(dest, src, mask, width);
    else
        vmxCombineOutUnomask(dest, src, width);
}

static void
vmxCombineOutReverseUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = pix_multiply (vdest, splat_alpha (negate (vsrc)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t d = dest[i];
        uint32_t a = Alpha (~src[i]);
        UN8x4_MUL_UN8 (d, a);
        dest[i] = d;
    }
}

static void
vmxCombineOutReverseUmask (uint32_t *dest,
                           const uint32_t *src,
                           const uint32_t *mask,
                           int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_multiply (vdest, splat_alpha (negate (vsrc)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t d = dest[i];
        uint32_t a = src[i];

        UN8x4_MUL_UN8 (a, m);

        a = Alpha (~a);
        UN8x4_MUL_UN8 (d, a);
        dest[i] = d;
    }
}

static void
vmxCombineOutReverseU (pixman_implementation_t *imp, pixman_op_t op,
		       uint32_t *dest,
                       const uint32_t *src,
                       const uint32_t *mask,
                       int width)
{
    if (mask)
        vmxCombineOutReverseUmask(dest, src, mask, width);
    else
        vmxCombineOutReverseUnomask(dest, src, width);
}

static void
vmxCombineAtopUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = pix_add_mul (vsrc, splat_alpha (vdest),
                            vdest, splat_alpha (negate (vsrc)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t dest_a = Alpha (d);
        uint32_t src_ia = Alpha (~s);

        UN8x4_MUL_UN8_ADD_UN8x4_MUL_UN8 (s, dest_a, d, src_ia);
        dest[i] = s;
    }
}

static void
vmxCombineAtopUmask (uint32_t *dest,
                     const uint32_t *src,
                     const uint32_t *mask,
                     int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_add_mul (vsrc, splat_alpha (vdest),
                            vdest, splat_alpha (negate (vsrc)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t dest_a = Alpha (d);
        uint32_t src_ia;

        UN8x4_MUL_UN8 (s, m);

        src_ia = Alpha (~s);

        UN8x4_MUL_UN8_ADD_UN8x4_MUL_UN8 (s, dest_a, d, src_ia);
        dest[i] = s;
    }
}

static void
vmxCombineAtopU (pixman_implementation_t *imp, pixman_op_t op,
		 uint32_t *dest,
                 const uint32_t *src,
                 const uint32_t *mask,
                 int width)
{
    if (mask)
        vmxCombineAtopUmask(dest, src, mask, width);
    else
        vmxCombineAtopUnomask(dest, src, width);
}

static void
vmxCombineAtopReverseUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = pix_add_mul (vdest, splat_alpha (vsrc),
                            vsrc, splat_alpha (negate (vdest)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t src_a = Alpha (s);
        uint32_t dest_ia = Alpha (~d);

        UN8x4_MUL_UN8_ADD_UN8x4_MUL_UN8 (s, dest_ia, d, src_a);
        dest[i] = s;
    }
}

static void
vmxCombineAtopReverseUmask (uint32_t *dest,
                            const uint32_t *src,
                            const uint32_t *mask,
                            int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_add_mul (vdest, splat_alpha (vsrc),
                            vsrc, splat_alpha (negate (vdest)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t src_a;
        uint32_t dest_ia = Alpha (~d);

        UN8x4_MUL_UN8 (s, m);

        src_a = Alpha (s);

        UN8x4_MUL_UN8_ADD_UN8x4_MUL_UN8 (s, dest_ia, d, src_a);
        dest[i] = s;
    }
}

static void
vmxCombineAtopReverseU (pixman_implementation_t *imp, pixman_op_t op,
			uint32_t *dest,
                        const uint32_t *src,
                        const uint32_t *mask,
                        int width)
{
    if (mask)
        vmxCombineAtopReverseUmask(dest, src, mask, width);
    else
        vmxCombineAtopReverseUnomask(dest, src, width);
}

static void
vmxCombineXorUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS (dest, src)

        vdest = pix_add_mul (vsrc, splat_alpha (negate (vdest)),
                            vdest, splat_alpha (negate (vsrc)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t src_ia = Alpha (~s);
        uint32_t dest_ia = Alpha (~d);

        UN8x4_MUL_UN8_ADD_UN8x4_MUL_UN8 (s, dest_ia, d, src_ia);
        dest[i] = s;
    }
}

static void
vmxCombineXorUmask (uint32_t *dest,
                    const uint32_t *src,
                    const uint32_t *mask,
                    int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_add_mul (vsrc, splat_alpha (negate (vdest)),
                            vdest, splat_alpha (negate (vsrc)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t src_ia;
        uint32_t dest_ia = Alpha (~d);

        UN8x4_MUL_UN8 (s, m);

        src_ia = Alpha (~s);

        UN8x4_MUL_UN8_ADD_UN8x4_MUL_UN8 (s, dest_ia, d, src_ia);
        dest[i] = s;
    }
}

static void
vmxCombineXorU (pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest,
                const uint32_t *src,
                const uint32_t *mask,
                int width)
{
    if (mask)
        vmxCombineXorUmask(dest, src, mask, width);
    else
        vmxCombineXorUnomask(dest, src, width);
}

static void
vmxCombineAddUnomask (uint32_t *dest, const uint32_t *src, int width)
{
    int i;
    vector unsigned int  vdest, vsrc;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKS(dest, src)
    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORS(dest, src)

        vdest = pix_add (vsrc, vdest);

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t s = src[i];
        uint32_t d = dest[i];
        UN8x4_ADD_UN8x4 (d, s);
        dest[i] = d;
    }
}

static void
vmxCombineAddUmask (uint32_t *dest,
                    const uint32_t *src,
                    const uint32_t *mask,
                    int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, src_mask, mask_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSM(dest, src, mask)

        vdest = pix_add (vsrc, vdest);

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t m = Alpha (mask[i]);
        uint32_t s = src[i];
        uint32_t d = dest[i];

        UN8x4_MUL_UN8 (s, m);

        UN8x4_ADD_UN8x4 (d, s);
        dest[i] = d;
    }
}

static void
vmxCombineAddU (pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest,
                const uint32_t *src,
                const uint32_t *mask,
                int width)
{
    if (mask)
        vmxCombineAddUmask(dest, src, mask, width);
    else
        vmxCombineAddUnomask(dest, src, width);
}

static void
vmxCombineSrcC (pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask);
    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_multiply (vsrc, vmask);

        STORE_VECTOR(dest)

        mask+=4;
        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        UN8x4_MUL_UN8x4 (s, a);
        dest[i] = s;
    }
}

static void
vmxCombineOverC (pixman_implementation_t *imp, pixman_op_t op,
		 uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask);
    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = in_over (vsrc, splat_alpha (vsrc), vmask, vdest);

        STORE_VECTOR(dest)

        mask+=4;
        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];
        UN8x4_MUL_UN8x4 (s, a);
        UN8x4_MUL_UN8x4_ADD_UN8x4 (d, ~a, s);
        dest[i] = d;
    }
}

static void
vmxCombineOverReverseC (pixman_implementation_t *imp, pixman_op_t op,
			uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask);
    /* printf("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC (dest, src, mask)

        vdest = over (vdest, splat_alpha (vdest), pix_multiply (vsrc, vmask));

        STORE_VECTOR(dest)

        mask+=4;
        src+=4;
        dest+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t da = Alpha (d);
        UN8x4_MUL_UN8x4 (s, a);
        UN8x4_MUL_UN8x4_ADD_UN8x4 (s, ~da, d);
        dest[i] = s;
    }
}

static void
vmxCombineInC (pixman_implementation_t *imp, pixman_op_t op,
	       uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_multiply (pix_multiply (vsrc, vmask), splat_alpha (vdest));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t da = Alpha (dest[i]);
        UN8x4_MUL_UN8 (s, a);
        UN8x4_MUL_UN8 (s, da);
        dest[i] = s;
    }
}

static void
vmxCombineInReverseC (pixman_implementation_t *imp, pixman_op_t op,
		      uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_multiply (vdest, pix_multiply (vmask, splat_alpha (vsrc)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t d = dest[i];
        uint32_t sa = Alpha (src[i]);
        UN8x4_MUL_UN8 (a, sa);
        UN8x4_MUL_UN8x4 (d, a);
        dest[i] = d;
    }
}

static void
vmxCombineOutC (pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_multiply (pix_multiply (vsrc, vmask), splat_alpha (vdest));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t da = Alpha (~d);
        UN8x4_MUL_UN8x4 (s, a);
        UN8x4_MUL_UN8x4 (s, da);
        dest[i] = s;
    }
}

static void
vmxCombineOutReverseC (pixman_implementation_t *imp, pixman_op_t op,
		       uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_multiply (vdest,
                             negate (pix_multiply (vmask, splat_alpha (vsrc))));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t sa = Alpha (s);
        UN8x4_MUL_UN8x4 (a, sa);
        UN8x4_MUL_UN8x4 (d, ~a);
        dest[i] = d;
    }
}

static void
vmxCombineAtopC (pixman_implementation_t *imp, pixman_op_t op,
		 uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_add_mul (pix_multiply (vsrc, vmask), splat_alpha (vdest),
                            vdest,
                            negate (pix_multiply (vmask,
                                                splat_alpha (vmask))));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t sa = Alpha (s);
        uint32_t da = Alpha (d);

        UN8x4_MUL_UN8x4 (s, a);
        UN8x4_MUL_UN8 (a, sa);
        UN8x4_MUL_UN8x4_ADD_UN8x4_MUL_UN8 (d, ~a, s, da);
        dest[i] = d;
    }
}

static void
vmxCombineAtopReverseC (pixman_implementation_t *imp, pixman_op_t op,
			uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_add_mul (vdest,
                            pix_multiply (vmask, splat_alpha (vsrc)),
                            pix_multiply (vsrc, vmask),
                            negate (splat_alpha (vdest)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t sa = Alpha (s);
        uint32_t da = Alpha (d);

        UN8x4_MUL_UN8x4 (s, a);
        UN8x4_MUL_UN8 (a, sa);
        UN8x4_MUL_UN8x4_ADD_UN8x4_MUL_UN8 (d, a, s, ~da);
        dest[i] = d;
    }
}

static void
vmxCombineXorC (pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_add_mul (vdest,
                            negate (pix_multiply (vmask, splat_alpha (vsrc))),
                            pix_multiply (vsrc, vmask),
                            negate (splat_alpha (vdest)));

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];
        uint32_t sa = Alpha (s);
        uint32_t da = Alpha (d);

        UN8x4_MUL_UN8x4 (s, a);
        UN8x4_MUL_UN8 (a, sa);
        UN8x4_MUL_UN8x4_ADD_UN8x4_MUL_UN8 (d, ~a, s, ~da);
        dest[i] = d;
    }
}

static void
vmxCombineAddC (pixman_implementation_t *imp, pixman_op_t op,
		uint32_t *dest, const uint32_t *src, const uint32_t *mask, int width)
{
    int i;
    vector unsigned int  vdest, vsrc, vmask;
    vector unsigned char tmp1, tmp2, tmp3, tmp4, edges,
                         dest_mask, mask_mask, src_mask, store_mask;

    COMPUTE_SHIFT_MASKC(dest, src, mask)

    /* printf ("%s\n",__PRETTY_FUNCTION__); */
    for (i = width/4; i > 0; i--) {

        LOAD_VECTORSC(dest, src, mask)

        vdest = pix_add (pix_multiply (vsrc, vmask), vdest);

        STORE_VECTOR(dest)

        src+=4;
        dest+=4;
        mask+=4;
    }

    for (i = width%4; --i >=0;) {
        uint32_t a = mask[i];
        uint32_t s = src[i];
        uint32_t d = dest[i];

        UN8x4_MUL_UN8x4 (s, a);
        UN8x4_ADD_UN8x4 (s, d);
        dest[i] = s;
    }
}


#if 0
void
vmx_CompositeOver_n_8888 (pixman_operator_t	op,
			    pixman_image_t * src_image,
			    pixman_image_t * mask_image,
			    pixman_image_t * dst_image,
			    int16_t	src_x,
			    int16_t	src_y,
			    int16_t	mask_x,
			    int16_t	mask_y,
			    int16_t	dest_x,
			    int16_t	dest_y,
			    uint16_t	width,
			    uint16_t	height)
{
    uint32_t	src;
    uint32_t	*dstLine, *dst;
    int	dstStride;

    _pixman_image_get_solid (src_image, dst_image, src);

    if (src >> 24 == 0)
	return;

    PIXMAN_IMAGE_GET_LINE (dst_image, dest_x, dest_y, uint32_t, dstStride, dstLine, 1);

    while (height--)
    {
	dst = dstLine;
	dstLine += dstStride;
	/* XXX vmxCombineOverU (dst, src, width); */
    }
}

void
vmx_CompositeOver_n_0565 (pixman_operator_t	op,
			    pixman_image_t * src_image,
			    pixman_image_t * mask_image,
			    pixman_image_t * dst_image,
			    int16_t	src_x,
			    int16_t	src_y,
			    int16_t	mask_x,
			    int16_t	mask_y,
			    int16_t	dest_x,
			    int16_t	dest_y,
			    uint16_t	width,
			    uint16_t	height)
{
    uint32_t	src;
    uint16_t	*dstLine, *dst;
    uint16_t	w;
    int	dstStride;

    _pixman_image_get_solid (src_image, dst_image, src);

    if (src >> 24 == 0)
	return;

    PIXMAN_IMAGE_GET_LINE (dst_image, dest_x, dest_y, uint16_t, dstStride, dstLine, 1);

    while (height--)
    {
	dst = dstLine;
	dstLine += dstStride;
       vmxCombineOverU565(dst, src, width);
    }
}

static const pixman_fast_path_t vmx_fast_path_array[] =
{
    { PIXMAN_OP_NONE },
};

const pixman_fast_path_t *const vmx_fast_paths = vmx_fast_path_array;

#endif

pixman_implementation_t *
_pixman_implementation_create_vmx (void)
{
    pixman_implementation_t *fast = _pixman_implementation_create_fast_path ();
    pixman_implementation_t *imp = _pixman_implementation_create (fast);

    /* Set up function pointers */
    
    /* SSE code patch for fbcompose.c */
    imp->combine_32[PIXMAN_OP_OVER] = vmxCombineOverU;
    imp->combine_32[PIXMAN_OP_OVER_REVERSE] = vmxCombineOverReverseU;
    imp->combine_32[PIXMAN_OP_IN] = vmxCombineInU;
    imp->combine_32[PIXMAN_OP_IN_REVERSE] = vmxCombineInReverseU;
    imp->combine_32[PIXMAN_OP_OUT] = vmxCombineOutU;
    imp->combine_32[PIXMAN_OP_OUT_REVERSE] = vmxCombineOutReverseU;
    imp->combine_32[PIXMAN_OP_ATOP] = vmxCombineAtopU;
    imp->combine_32[PIXMAN_OP_ATOP_REVERSE] = vmxCombineAtopReverseU;
    imp->combine_32[PIXMAN_OP_XOR] = vmxCombineXorU;

    imp->combine_32[PIXMAN_OP_ADD] = vmxCombineAddU;

    imp->combine_32_ca[PIXMAN_OP_SRC] = vmxCombineSrcC;
    imp->combine_32_ca[PIXMAN_OP_OVER] = vmxCombineOverC;
    imp->combine_32_ca[PIXMAN_OP_OVER_REVERSE] = vmxCombineOverReverseC;
    imp->combine_32_ca[PIXMAN_OP_IN] = vmxCombineInC;
    imp->combine_32_ca[PIXMAN_OP_IN_REVERSE] = vmxCombineInReverseC;
    imp->combine_32_ca[PIXMAN_OP_OUT] = vmxCombineOutC;
    imp->combine_32_ca[PIXMAN_OP_OUT_REVERSE] = vmxCombineOutReverseC;
    imp->combine_32_ca[PIXMAN_OP_ATOP] = vmxCombineAtopC;
    imp->combine_32_ca[PIXMAN_OP_ATOP_REVERSE] = vmxCombineAtopReverseC;
    imp->combine_32_ca[PIXMAN_OP_XOR] = vmxCombineXorC;
    imp->combine_32_ca[PIXMAN_OP_ADD] = vmxCombineAddC;
    
    return imp;
}

