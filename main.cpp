#include <iostream>
extern "C"
{
#include "SDL2/SDL.h"
};
using namespace std;

#include <stdio.h>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
};


//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit = 0;
int thread_stop = 0;

int sfp_refresh_thread(void *opaque){
    thread_exit = 0;
    while (!thread_exit) {
        cout<<"refresh"<<endl;
        SDL_Event event;
        event.type = SFM_REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(10);
        if(thread_stop) {
            while (thread_stop) {
                cout<<"sleep"<<endl;
                SDL_Delay(1);
            }
        }
    }
    thread_exit=0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}



int sfp_stop_thread(void *opaque){
    return 0;
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        cout<<"ERR: LACK OF INPUT FILE!"<<endl;
        return 0;
    }
    //------------FFmpeg----------------
    AVFormatContext	*pFormatCtx;
    int				videoindex;
    AVCodecContext	*pCodecCtx;
    AVCodec			*pCodec;
    AVFrame	*pFrame,*pFrameYUV;
    uint8_t *out_buffer;
    AVPacket *packet;
    int ret, got_picture;

    //------------SDL----------------
    int screen_w,screen_h;
    SDL_Window *screen;
    SDL_Renderer* sdlRenderer;
    SDL_Texture* sdlTexture;
    SDL_Rect sdlRect;
    SDL_Thread *video_tid, *video_stop_tid;
    SDL_Event event;

    struct SwsContext *img_convert_ctx;

    char filepath[500];
    strcpy(filepath, argv[1]);

    av_register_all();
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();

    if(avformat_open_input(&pFormatCtx, filepath,NULL,NULL) !=0 ) {
        printf("Couldn't open input stream.\n");
        return -1;
    }
    if(avformat_find_stream_info(pFormatCtx,NULL) < 0) {
        printf("Couldn't find stream information.\n");
        return -1;
    }

    videoindex = -1;
    for(int i = 0; i < pFormatCtx->nb_streams; i++) {
        if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            videoindex = i;
            break;
        }
    }

    if(videoindex == -1) {
        printf("Didn't find a video stream.\n");
        return -1;
    }

    pCodecCtx = pFormatCtx->streams[videoindex]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

    if(pCodec == NULL) {
        printf("Codec not found.\n");
        return -1;
    }

    if(avcodec_open2(pCodecCtx, pCodec,NULL) < 0) {
        printf("Could not open codec.\n");
        return -1;
    }

    pFrame=av_frame_alloc();
    pFrameYUV=av_frame_alloc();
    out_buffer=(uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
    // int avpicture_fill(AVPicture *picture, const uint8_t *ptr, AVPixelFormat pix_fmt, int width, int height)
    avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
    // SwsContext *sws_getContext(int srcW, int srcH, AVPixelFormat srcFormat,
    //                            int dstW, int dstH, AVPixelFormat dstFormat, int flags, SwsFilter *srcFilter, SwsFilter *dstFilter, const double *param)
    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                     pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        printf( "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL 2.0 Support for multiple windows
    // Get width & height from codec, give it to SDL
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h,SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if(!screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
    }

    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = screen_w;
    sdlRect.h = screen_h;

    packet = (AVPacket *)av_malloc(sizeof(AVPacket));

    video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
    video_stop_tid = SDL_CreateThread(sfp_stop_thread,NULL,NULL);
    //------------SDL End------------
    //Event Loop

    while(1) {
        //Wait
        SDL_WaitEvent(&event);
        if(event.type == SFM_REFRESH_EVENT) {
            // av_read_frame: Get AVpacket From AVFormatContext
            if(av_read_frame(pFormatCtx, packet) >= 0) {
                if(packet->stream_index == videoindex) {
                    // avcodec_decode_video2: Decode the video frame of size avpkt->size from avpkt->data into picture.
                    ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
                    if(ret < 0) {
                        printf("Decode Error.\n");
                        return -1;
                    }
                    if(got_picture) {
                        // int sws_scale(SwsContext *c, const uint8_t * const *srcSlice, const int *srcStride, int srcSliceY, int srcSliceH, uint8_t * const *dst, const int *dstStride)
                        // Scale the image slice in srcSlice and put the resulting scaled slice in the image in dst. A slice is a sequence of consecutive rows in an image.
                        // pFrame -> pFrameYUV
                        sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
                        //SDL---------------------------
                        SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );
                        SDL_RenderClear( sdlRenderer );
                        //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
                        SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);
                        SDL_RenderPresent( sdlRenderer );
                        //SDL End-----------------------
                    }
                }
                av_free_packet(packet);
            } else {
                //Exit Thread
                thread_exit=1;
            }
        } else if(event.type == SDL_WINDOWEVENT) {
            //If Resize
            SDL_GetWindowSize(screen, &screen_w, &screen_h);
        } else if(event.type == SDL_MOUSEBUTTONDOWN) {

        } else if(event.type == SDL_QUIT) {
            thread_exit = 1;
        } else if(event.type == SFM_BREAK_EVENT) {
            break;
        } else if(event.type == SDL_KEYDOWN) {
            if(event.key.keysym.sym == SDLK_s) {
                cout<<"Stop"<<endl;
                thread_stop = 1;
            } else if(event.key.keysym.sym == SDLK_r) {
                cout<<"Resume"<<endl;
                thread_stop = 0;
            }
        }
    }

    sws_freeContext(img_convert_ctx);

    SDL_Quit();
    //--------------
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;

}