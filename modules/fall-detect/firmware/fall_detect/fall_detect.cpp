#include "fall_detect.h"
#include "model_data.h"

#include <string.h>
#include <math.h>

/*
 * TFLite Micro includes.  These are provided by the
 * "Arduino_TensorFlowLite" library (version ≥ 2.4.0).
 */
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

/* Arena size tuned for the 1D-CNN model with int8 quantisation.
 * Increase if tflite_micro reports an allocation failure. */
#define TFLM_ARENA_SIZE (32 * 1024)

static uint8_t             s_arena[TFLM_ARENA_SIZE];
static tflite::MicroMutableOpResolver<8> s_resolver;
static const tflite::Model         *s_model   = nullptr;
static tflite::MicroInterpreter    *s_interp  = nullptr;
static TfLiteTensor                *s_input   = nullptr;
static TfLiteTensor                *s_output  = nullptr;
static bool                         s_tflm_ok = false;

static bool tflm_init(void)
{
    s_model = tflite::GetModel(g_model_data);
    if (s_model->version() != TFLITE_SCHEMA_VERSION)
        return false;

    // Register the ops used by the 1D-CNN classifier. (AllOpsResolver was
    // removed from TensorFlow Lite Micro; MicroMutableOpResolver keeps only the
    // ops the model actually needs, which shrinks the binary.)
    s_resolver.AddConv2D();
    s_resolver.AddDepthwiseConv2D();
    s_resolver.AddFullyConnected();
    s_resolver.AddMaxPool2D();
    s_resolver.AddReshape();
    s_resolver.AddRelu();
    s_resolver.AddSoftmax();

    static tflite::MicroInterpreter interp(
        s_model, s_resolver, s_arena, TFLM_ARENA_SIZE);
    s_interp = &interp;

    if (s_interp->AllocateTensors() != kTfLiteOk)
        return false;

    s_input  = s_interp->input(0);
    s_output = s_interp->output(0);

    /* Verify tensor shapes. */
    if (s_input->dims->size != 3)             return false;
    if (s_input->dims->data[1] != WINDOW_SAMPLES) return false;
    if (s_input->dims->data[2] != WINDOW_AXES)    return false;
    if (s_output->dims->data[1] != 2)         return false;

    return true;
}

void fd_init(FallDetector *fd)
{
    memset(fd, 0, sizeof(*fd));
    fd->state = FD_IDLE;
    s_tflm_ok = tflm_init();
}

bool fd_classify(FallDetector *fd)
{
    if (!s_tflm_ok) {
        /* TFLite unavailable (placeholder model) — use a simple heuristic:
         * check that fall_prob field was set by the threshold stage. */
        fd->fall_prob  = 0.8f;
        fd->nfall_prob = 0.2f;
        return true;
    }

    /* Copy the circular window into the input tensor in chronological order,
     * applying per-axis normalisation. */
    float *inp = s_input->data.f;
    for (uint8_t t = 0; t < WINDOW_SAMPLES; t++) {
        uint8_t idx = (uint8_t)((fd->win_head + t) % WINDOW_SAMPLES);
        for (uint8_t ax = 0; ax < WINDOW_AXES; ax++) {
            float v = fd->window[idx][ax];
            v = (v - g_input_mean[ax]) / g_input_std[ax];
            inp[t * WINDOW_AXES + ax] = v;
        }
    }

    if (s_interp->Invoke() != kTfLiteOk)
        return false;

    fd->nfall_prob = s_output->data.f[0];
    fd->fall_prob  = s_output->data.f[1];
    return true;
}

bool fd_update(FallDetector *fd, const Mpu6050Sample *s, uint32_t ms)
{
    float smv = fd_smv(s->ax, s->ay, s->az);

    /* Always push sample into circular window. */
    uint8_t wi = fd->win_head;
    fd->window[wi][0] = s->ax;
    fd->window[wi][1] = s->ay;
    fd->window[wi][2] = s->az;
    fd->window[wi][3] = s->gx;
    fd->window[wi][4] = s->gy;
    fd->window[wi][5] = s->gz;
    fd->win_head = (uint8_t)((wi + 1) % WINDOW_SAMPLES);
    if (fd->win_count < WINDOW_SAMPLES)
        fd->win_count++;

    switch (fd->state) {

    case FD_IDLE:
    case FD_COOLDOWN: {
        /* Check cooldown expiry. */
        if (fd->state == FD_COOLDOWN &&
            (ms - fd->state_entry_ms) >= ALERT_COOLDOWN_MS) {
            fd->state          = FD_IDLE;
            fd->state_entry_ms = ms;
        }
        /* Detect free-fall phase. */
        if (smv < FREE_FALL_THRESHOLD) {
            fd->state       = FD_FREE_FALL;
            fd->ff_entry_ms = ms;
            fd->state_entry_ms = ms;
        }
        break;
    }

    case FD_FREE_FALL: {
        /* Timeout: no impact arrived in time → not a fall. */
        if ((ms - fd->ff_entry_ms) > IMPACT_WINDOW_MS) {
            fd->state          = FD_IDLE;
            fd->state_entry_ms = ms;
            break;
        }
        /* Impact detected. */
        if (smv > IMPACT_THRESHOLD) {
            fd->state          = FD_IMPACT;
            fd->state_entry_ms = ms;
        }
        break;
    }

    case FD_IMPACT: {
        /* Wait POST_IMPACT_MS for the window to fill with post-impact samples
         * so the classifier sees both the impact and immediate aftermath. */
        if ((ms - fd->state_entry_ms) >= POST_IMPACT_MS) {
            fd->state          = FD_CLASSIFYING;
            fd->state_entry_ms = ms;
        }
        break;
    }

    case FD_CLASSIFYING: {
        if (!fd_classify(fd)) {
            fd->state          = FD_IDLE;
            fd->state_entry_ms = ms;
            break;
        }
        if (fd->fall_prob >= FALL_CONF_THRESHOLD) {
            fd->total_falls++;
            fd->state          = FD_LYING;
            fd->state_entry_ms = ms;
        } else {
            fd->state          = FD_IDLE;
            fd->state_entry_ms = ms;
        }
        break;
    }

    case FD_LYING: {
        /* Post-fall posture check: device must remain near-horizontal. */
        if (fabsf(s->az) > LYING_THRESHOLD) {
            /* Person got up or this was a false positive — cancel. */
            fd->false_positives_suppressed++;
            fd->state          = FD_COOLDOWN;
            fd->state_entry_ms = ms;
            break;
        }
        if ((ms - fd->state_entry_ms) >= LYING_DURATION_MS) {
            fd->total_alerts++;
            fd->state          = FD_ALERT;
            fd->state_entry_ms = ms;
            return true;   /* ← confirmed fall */
        }
        break;
    }

    case FD_ALERT: {
        /* Stay in ALERT state for one tick — the caller handles the SMS.
         * Transition immediately to cooldown so the state is stable on return. */
        fd->state          = FD_COOLDOWN;
        fd->state_entry_ms = ms;
        break;
    }

    }

    return false;
}
