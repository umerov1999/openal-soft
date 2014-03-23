#include "config.h"

#ifdef HAVE_XMMINTRIN_H
#ifdef IN_IDE_PARSER
/* KDevelop's parser won't recognize these defines that get added by the -msse
 * switch used to compile this source. Without them, xmmintrin.h fails to
 * declare anything. */
#define __MMX__
#define __SSE__
#endif
#include <xmmintrin.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"

#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "mixer_defs.h"


static inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*restrict Values)[2],
                                   const ALuint IrSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2],
                                   ALfloat left, ALfloat right)
{
    const __m128 lrlr = { left, right, left, right };
    __m128 coeffs, deltas, imp0, imp1;
    __m128 vals = _mm_setzero_ps();
    ALuint i;

    if((Offset&1))
    {
        const ALuint o0 = Offset&HRIR_MASK;
        const ALuint o1 = (Offset+IrSize-1)&HRIR_MASK;

        coeffs = _mm_load_ps(&Coeffs[0][0]);
        deltas = _mm_load_ps(&CoeffStep[0][0]);
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o0][0]);
        imp0 = _mm_mul_ps(lrlr, coeffs);
        coeffs = _mm_add_ps(coeffs, deltas);
        vals = _mm_add_ps(imp0, vals);
        _mm_store_ps(&Coeffs[0][0], coeffs);
        _mm_storel_pi((__m64*)&Values[o0][0], vals);
        for(i = 1;i < IrSize-1;i += 2)
        {
            const ALuint o2 = (Offset+i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i+1][0]);
            deltas = _mm_load_ps(&CoeffStep[i+1][0]);
            vals = _mm_load_ps(&Values[o2][0]);
            imp1 = _mm_mul_ps(lrlr, coeffs);
            coeffs = _mm_add_ps(coeffs, deltas);
            imp0 = _mm_shuffle_ps(imp0, imp1, _MM_SHUFFLE(1, 0, 3, 2));
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Coeffs[i+1][0], coeffs);
            _mm_store_ps(&Values[o2][0], vals);
            imp0 = imp1;
        }
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o1][0]);
        imp0 = _mm_movehl_ps(imp0, imp0);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi((__m64*)&Values[o1][0], vals);
    }
    else
    {
        for(i = 0;i < IrSize;i += 2)
        {
            const ALuint o = (Offset + i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i][0]);
            deltas = _mm_load_ps(&CoeffStep[i][0]);
            vals = _mm_load_ps(&Values[o][0]);
            imp0 = _mm_mul_ps(lrlr, coeffs);
            coeffs = _mm_add_ps(coeffs, deltas);
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Coeffs[i][0], coeffs);
            _mm_store_ps(&Values[o][0], vals);
        }
    }
}

static inline void ApplyCoeffs(ALuint Offset, ALfloat (*restrict Values)[2],
                               const ALuint IrSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right)
{
    const __m128 lrlr = { left, right, left, right };
    __m128 vals = _mm_setzero_ps();
    __m128 coeffs;
    ALuint i;

    if((Offset&1))
    {
        const ALuint o0 = Offset&HRIR_MASK;
        const ALuint o1 = (Offset+IrSize-1)&HRIR_MASK;
        __m128 imp0, imp1;

        coeffs = _mm_load_ps(&Coeffs[0][0]);
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o0][0]);
        imp0 = _mm_mul_ps(lrlr, coeffs);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi((__m64*)&Values[o0][0], vals);
        for(i = 1;i < IrSize-1;i += 2)
        {
            const ALuint o2 = (Offset+i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i+1][0]);
            vals = _mm_load_ps(&Values[o2][0]);
            imp1 = _mm_mul_ps(lrlr, coeffs);
            imp0 = _mm_shuffle_ps(imp0, imp1, _MM_SHUFFLE(1, 0, 3, 2));
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Values[o2][0], vals);
            imp0 = imp1;
        }
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o1][0]);
        imp0 = _mm_movehl_ps(imp0, imp0);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi((__m64*)&Values[o1][0], vals);
    }
    else
    {
        for(i = 0;i < IrSize;i += 2)
        {
            const ALuint o = (Offset + i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i][0]);
            vals = _mm_load_ps(&Values[o][0]);
            vals = _mm_add_ps(vals, _mm_mul_ps(lrlr, coeffs));
            _mm_store_ps(&Values[o][0], vals);
        }
    }
}

#define SUFFIX SSE
#include "mixer_inc.c"
#undef SUFFIX


void MixDirect_SSE(DirectParams *params, const ALfloat *restrict data, ALuint srcchan,
  ALuint OutPos, ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALuint Counter = maxu(params->Counter, OutPos) - OutPos;
    ALfloat DrySend, Step;
    __m128 gain, step;
    ALuint c;

    for(c = 0;c < MaxChannels;c++)
    {
        ALuint pos = 0;
        Step = params->Mix.Gains.Step[srcchan][c];
        if(Step != 1.0f && Counter > 0)
        {
            DrySend = params->Mix.Gains.Current[srcchan][c];
            if(BufferSize-pos > 3 && Counter-pos > 3)
            {
                gain = _mm_set_ps(
                    DrySend,
                    DrySend * Step,
                    DrySend * Step * Step,
                    DrySend * Step * Step * Step
                );
                step = _mm_set1_ps(Step * Step * Step * Step);
                do {
                    const __m128 val4 = _mm_load_ps(&data[pos]);
                    __m128 dry4 = _mm_load_ps(&OutBuffer[c][OutPos+pos]);
                    dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain));
                    gain = _mm_mul_ps(gain, step);
                    _mm_store_ps(&OutBuffer[c][OutPos+pos], dry4);
                    pos += 4;
                } while(BufferSize-pos > 3 && Counter-pos > 3);
                DrySend = _mm_cvtss_f32(_mm_shuffle_ps(gain, gain, _MM_SHUFFLE(3, 3, 3, 3)));
            }
            if(!(BufferSize-pos > 3))
            {
                for(;pos < BufferSize && pos < Counter;pos++)
                {
                    OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
                    DrySend *= Step;
                }
            }
            params->Mix.Gains.Current[srcchan][c] = DrySend;
        }

        DrySend = params->Mix.Gains.Target[srcchan][c];
        if(!(DrySend > GAIN_SILENCE_THRESHOLD))
            continue;
        gain = _mm_set1_ps(DrySend);
        for(;BufferSize-pos > 3;pos += 4)
        {
            const __m128 val4 = _mm_load_ps(&data[pos]);
            __m128 dry4 = _mm_load_ps(&OutBuffer[c][OutPos+pos]);
            dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain));
            _mm_store_ps(&OutBuffer[c][OutPos+pos], dry4);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
    }
}


void MixSend_SSE(SendParams *params, const ALfloat *restrict data,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALfloat *restrict ClickRemoval = params->ClickRemoval;
    ALfloat *restrict PendingClicks = params->PendingClicks;
    ALfloat WetGain;
    __m128 gain;
    ALuint pos;

    WetGain = params->Gain;
    if(!(WetGain > GAIN_SILENCE_THRESHOLD))
        return;

    if(OutPos == 0)
        ClickRemoval[0] -= data[0] * WetGain;

    gain = _mm_set1_ps(WetGain);
    for(pos = 0;BufferSize-pos > 3;pos += 4)
    {
        const __m128 val4 = _mm_load_ps(&data[pos]);
        __m128 wet4 = _mm_load_ps(&OutBuffer[0][OutPos+pos]);
        wet4 = _mm_add_ps(wet4, _mm_mul_ps(val4, gain));
        _mm_store_ps(&OutBuffer[0][OutPos+pos], wet4);
    }
    for(;pos < BufferSize;pos++)
        OutBuffer[0][OutPos+pos] += data[pos] * WetGain;

    if(OutPos+pos == SamplesToDo)
        PendingClicks[0] += data[pos] * WetGain;
}
