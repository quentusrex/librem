#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem_fir.h>
#include <rem_auresamp.h>


struct auresamp {
	struct fir fir;
	const int16_t *coeffv;
	int coeffn;
	double ratio;
};


/*
 * FIR filter with cutoff 8000Hz, samplerate 16000Hz
 */
static const int16_t fir_lowpass[31] = {
	-55,      0,     96,      0,   -220,      0,    461,      0,
	-877,      0,   1608,      0,  -3176,      0,  10342,  16410,
	10342,      0,  -3176,      0,   1608,      0,   -877,      0,
	461,      0,   -220,      0,     96,      0,    -55,
};


static inline void resample(struct auresamp *ar, int16_t *dst,
			    const int16_t *src, size_t nsamp_dst)
{
	double p = 0;

	while (nsamp_dst--) {

		*dst++ = src[(int)p];
		p += 1/ar->ratio;
	}
}


/* todo: handle channel resampling */
int auresamp_alloc(struct auresamp **arp, uint32_t srate_in,
		   uint32_t srate_out)
{
	struct auresamp *ar;

	if (!arp || !srate_in || !srate_out)
		return EINVAL;

	ar = mem_zalloc(sizeof(*ar), NULL);
	if (!ar)
		return ENOMEM;

	ar->ratio = 1.0 * srate_out / srate_in;

	re_printf("allocate resampler: %uHz ---> %uHz (ratio=%f)\n",
		  srate_in, srate_out, ar->ratio);

	fir_init(&ar->fir);

	if (ar->ratio < 1) {

		ar->coeffv = fir_lowpass;
		ar->coeffn = (int)ARRAY_SIZE(fir_lowpass);
	}
	else if (ar->ratio > 1) {

		ar->coeffv = fir_lowpass;
		ar->coeffn = (int)ARRAY_SIZE(fir_lowpass);
	}

	*arp = ar;

	return 0;
}


int auresamp_process(struct auresamp *ar, struct mbuf *dst, struct mbuf *src)
{
	size_t ns, nd, sz;
	int16_t *s, *d;

	if (!ar || !dst || !src)
		return EINVAL;

	ns = mbuf_get_left(src) / 2;
	nd = ns * ar->ratio;
	sz = nd * 2;

	if (mbuf_get_space(dst) < sz) {

		int err;

		re_printf("resamp: dst resize %u -> %u\n",
			  dst->size, dst->pos + sz);

		err = mbuf_resize(dst, dst->pos + sz);
		if (err)
			return err;
	}

	s = (void *)mbuf_buf(src);
	d = (void *)mbuf_buf(dst);

	if (ar->ratio > 1) {

		resample(ar, d, s, nd);
		auresamp_lowpass(ar, d, nd);
	}
	else {
		/* decimation: low-pass filter, then downsample */

		auresamp_lowpass(ar, s, ns);
		resample(ar, d, s, nd);
	}

	dst->end = dst->pos += sz;

	return 0;
}


int auresamp_lowpass(struct auresamp *ar, int16_t *buf, size_t nsamp)
{
	while (nsamp > 0) {

		size_t len = min(nsamp, FIR_MAX_INPUT_LEN);

		fir_process(&ar->fir, ar->coeffv, buf, buf, len, ar->coeffn);

		buf   += len;
		nsamp -= len;
	}

	return 0;
}
