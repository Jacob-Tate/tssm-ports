#include <stdio.h>
#include <switch.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
int main(int argc, char** argv) {
    consoleInit(NULL);
    printf("%s\n", av_version_info());
    printf("bink video: %p\n", (void*)avcodec_find_decoder(AV_CODEC_ID_BINKVIDEO));
    printf("bink audio: %p\n", (void*)avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_RDFT));
    AVFormatContext* fc = NULL;
    int r = avformat_open_input(&fc, "/switch/tssm/files/FMV/does_not_matter.bik", NULL, NULL);
    printf("open: %d\n", r);
    struct SwsContext* s = sws_getContext(64, 64, AV_PIX_FMT_YUV420P, 64, 64, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    SwrContext* w = swr_alloc();
    printf("sws %p swr %p\n", (void*)s, (void*)w);
    sws_freeContext(s); swr_free(&w);
    while (appletMainLoop()) consoleUpdate(NULL);
    consoleExit(NULL);
    return 0;
}
