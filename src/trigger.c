// a2h_trigger - Tiny AAudio trigger to force HAL loading in audioserver
#include <aaudio/AAudio.h>
#include <stdio.h>

int main(void) {
    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t r = AAudio_createStreamBuilder(&builder);
    if (r != AAUDIO_OK) {
        fprintf(stderr, "TRIGGER: builder create fail: %d\n", r);
        return 1;
    }

    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, 0);

    AAudioStream *stream = NULL;
    r = AAudioStreamBuilder_openStream(builder, &stream);
    if (r != AAUDIO_OK) {
        fprintf(stderr, "TRIGGER: openStream fail: %d\n", r);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }

    // Write a few silent frames to actually trigger the audio pipeline
    int16_t silence[480] = {0};
    int32_t framesPerBurst = AAudioStream_getFramesPerBurst(stream);
    if (framesPerBurst <= 0) framesPerBurst = 240;

    AAudioStream_requestStart(stream);
    AAudioStream_write(stream, silence, framesPerBurst, 1000000000LL);
    AAudioStream_requestPause(stream);
    AAudioStream_requestFlush(stream);

    AAudioStream_close(stream);
    AAudioStreamBuilder_delete(builder);
    fprintf(stderr, "TRIGGER: OK\n");
    return 0;
}
