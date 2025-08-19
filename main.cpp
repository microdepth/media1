#include <iostream>
#include <format>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION // i guess this library won't run without these
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"

extern "C" {
    #include <libavcodec/avcodec.h> // codec stuff
    #include <libavformat/avformat.h> // container format stuff
    #include <libavutil/avutil.h> // general util stuff
    #include <libavutil/pixdesc.h> // pixel format descriptions
    #include <libavutil/imgutils.h> // image util functions
    #include <libswscale/swscale.h> // scaling and color space conversion
    #include <libswresample/swresample.h> // audio resampling
    #include <libavdevice/avdevice.h> // device handling
}

using namespace std;
using namespace filesystem;

// todo add conversion to different filetypes
int save_image_as_bmp(const char* filename, AVFrame* pFrame, int width, int height) { // saves a frame to a bmp file
    if (stbi_write_bmp(filename, width, height, 3, pFrame->data[0]) == 0) {
        printf("failed to save image: %s\n", filename);
        return -1;
    }

    printf("saved image: %s\n", filename);
    return 0;
}
void clear_folder(string& folderPath) { // recursively clears a directory
    try { // try-catch block for file system errors
        if (exists(folderPath) && is_directory(folderPath)) {
            for (const auto& entry : directory_iterator(folderPath)) {
                remove_all(entry.path()); // recursive
            }
            printf("folder cleared");
        } else {
            printf("path doesn't exist/isn't a folder");
        }
    } catch (const filesystem_error& e) {
        printf("error clearing folder");
    }
};

int main() {
    // todo add config/input/customization
    // setup

    avdevice_register_all(); // register all devices

    AVFormatContext* pFormatContext = avformat_alloc_context(); // format context basically stores all the important stuff for the video itself

    const AVInputFormat* inputFormat = av_find_input_format("dshow"); // windows multimedia stream system format
    const char* deviceName = "video=Brio 100"; // my external webcam name according to windows

    const char* fileName = "test.mov";

    string folder = "frames";
    clear_folder(folder); // recursively clear any file in the working directory

    int ret = avformat_open_input(&pFormatContext, fileName, nullptr, nullptr); // initializes avformatcontext to read data from file/device using the specified format and options
    if (ret != 0) { // if it returns 0 then the function went well
        char errorbuf[1024];
        av_strerror(ret, errorbuf, sizeof(errorbuf)); // av_strerror puts an error message inside of errorbuf to be printed
        printf("error opening device: %s\n", errorbuf);
        return -1;
    }

    AVCodecParameters* pCodecParameters = nullptr; // holds properties of a codec stream
    int videoStreamIndex = -1; // a media file can have different streams like audio and video. for this program we want the video stream
    for (int i = 0; i < pFormatContext->nb_streams; ++i) { // loop through all the streams
        if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { // check to see if the codec type of the stream is video
            printf("found video stream\n");
            videoStreamIndex = i; // set the index of the video stream
            pCodecParameters = pFormatContext->streams[i]->codecpar; // store codec parameters too (stream data)
            break;
        }
    }

    if (videoStreamIndex == -1) { // error checking video stream
        printf("failed to find video stream\n");
        return -1;
    }

    const AVCodec* decoder = avcodec_find_decoder(pCodecParameters->codec_id); // find the decoder from the codec id in the streams parameters, so we can decode our frames
    if (!decoder) {
        printf("failed to find decoder\n");
        return -1;
    }

    AVCodecContext* pCodecContext = avcodec_alloc_context3(decoder); // this holds information specific to encoding/decoding
    if (!pCodecContext) {
        printf("failed to allocate codec context\n");
        return -1;
    }

    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) { // copy codec parameters into codec context
        printf("failed to copy codec parameters into codec context\n");
        return -1;
    }

    if (avcodec_open2(pCodecContext, decoder, nullptr) < 0) { // open the codec
        printf("failed to open codec\n");
        return -1;
    }

    // processing setup
    AVPacket* pPacket = av_packet_alloc(); // allocating memory for a packet
    AVFrame* pFrame = av_frame_alloc(); // allocating memory for a frame which is going to hold the original decoded packet before transformations
    AVFrame* pFrameRGB = av_frame_alloc(); // frame storage buffer for post processing frame

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecContext->width, pCodecContext->height, 1); // number of bytes to contain a frame in a specific format. here its AV_PIX_FMT_RGB24
    uint8_t* buffer = (uint8_t*) av_malloc(numBytes * sizeof(uint8_t)); // allocated buffer based on the number of bytes in the frame
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, pCodecContext->width, pCodecContext->height, 1); // sets up pframergb pointers to reference to the buffer we just created based on the dimensions and pixel format

    char filename2[1024]; // filename
    int frame_number = 0; // frame number to print

    // processing loop
    while (av_read_frame(pFormatContext, pPacket) >= 0) { // reading a packet from the stream
        if (pPacket->stream_index == videoStreamIndex) { // checking if the packet we read is from the desired stream (video)
            if (avcodec_send_packet(pCodecContext, pPacket) < 0) { // sending the packet to be decoded into frames
                printf("error sending packet for decoding\n");
                continue;
            }
            while (true) { // while loop can process multiple frames from a single packet at once
                int ret = avcodec_receive_frame(pCodecContext, pFrame); // decoding and taking one frame from the packet to process
                if (ret == AVERROR(EAGAIN)) { // AVERROR(EAGAIN) means that the packet did not contain enough data to give me a frame, so I have to try again
                    break; // pop the while and grab some more information
                } else if (ret < 0) { // eof/genuine error
                    printf("error receiving decoded frame\n");
                    break;
                }
                printf("frame decoded\n");

                // luminance loop
                for (int y = 0; y < pCodecContext->height; ++y) { // loop through every row in the frame
                    for (int x = 0; x < pCodecContext->width; ++x) { // loop through every element in the row
                        // here we should be dealing with rgb pixels
                        uint8_t luminance = pFrame->data[0][y * pFrame->linesize[0] + x]; // how bright or dark each pixel is

                        int y_chr = (y + 1) / 2;
                        int x_chr = (x + 1) / 2;

                        uint8_t u_chrominance = pFrame->data[1][(y_chr * pFrame->linesize[1]) + x_chr]; // the blue color difference component
                        uint8_t v_chrominance = pFrame->data[2][(y_chr * pFrame->linesize[2]) + x_chr]; // the red color difference component

                        int rgb_offset = y * pFrameRGB->linesize[0] + x * 3; // rgb offset is where we are in the rgb frame. the reason we multiply our x value by 3 is because each pixel in rgb is 3 times the size, and we want to count by pixels, so we multiply by 3 to skip the entire pixel each time

                        pFrameRGB->data[0][rgb_offset] = luminance + 1.140 * (v_chrominance - 128); // setting red value in the pixel
                        pFrameRGB->data[0][rgb_offset + 1] = luminance - 0.395 * (u_chrominance - 128) - 0.581 * (v_chrominance - 128); // setting green value in the pixel
                        pFrameRGB->data[0][rgb_offset + 2] = luminance + 2.032 * (u_chrominance - 128); // setting blue value in the pixel
                    }
                }

                // todo reconstruct frames into mov
                // saving to file
                snprintf(filename2, sizeof(filename2), "frames/frame_%d.bmp", frame_number); // building a filename for our frame to save into our frames folder as a bmp
                save_image_as_bmp(filename2, pFrameRGB, pCodecContext->width, pCodecContext->height); // saving our frame as a bmp image

                frame_number++; // increment frame number
            }
        }
        av_packet_unref(pPacket);
    }

    // todo comment everything
    // building mov
    AVFormatContext* pOutputFormatContext = nullptr;

    avformat_alloc_output_context2(&pOutputFormatContext, nullptr, "mov", "output.mov");
    if (!pOutputFormatContext) {
        printf("error allocating format context\n");
        return -1;
    }

    AVStream* outStream = avformat_new_stream(pOutputFormatContext, nullptr);
    if (!outStream) {
        printf("failed to create output stream\n");
        return -1;
    }

    const AVCodec* encoder = avcodec_find_encoder(pCodecParameters->codec_id); // find the encoder from the codec id in the streams parameters, so we can encode our frames [UNUSED]
    if (!encoder) {
        printf("failed to find codec\n");
        return -1;
    }

    AVCodecContext* pOutputCodecContext = avcodec_alloc_context3(encoder);
    pOutputCodecContext->codec_id = encoder->id;
    pOutputCodecContext->height = pCodecContext->height;
    pOutputCodecContext->width = pCodecContext->width;
    pOutputCodecContext->time_base = {1, 30};
    pOutputCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avcodec_open2(pOutputCodecContext, encoder, nullptr) < 0) {
        printf("failed to open encoder\n");
        return -1;
    }

    avcodec_parameters_from_context(outStream->codecpar, pOutputCodecContext);

    AVFrame* outputFrameRGB = av_frame_alloc();
    outputFrameRGB->format = AV_PIX_FMT_RGB24;
    outputFrameRGB->width = pCodecContext->width;
    outputFrameRGB->height = pCodecContext->height;
    av_frame_get_buffer(outputFrameRGB, 0);

    AVFrame* outputFrameYUV = av_frame_alloc();
    outputFrameYUV->format = AV_PIX_FMT_YUV420P;
    outputFrameYUV->width = pCodecContext->width;
    outputFrameYUV->height = pCodecContext->height;
    av_frame_get_buffer(outputFrameYUV, 0);

    struct SwsContext* sws_ctx = sws_getContext(
            pCodecContext->width, pCodecContext->height, AV_PIX_FMT_RGB24,
            pCodecContext->width, pCodecContext->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR,nullptr, nullptr, nullptr);

    uint8_t* bmpData = nullptr;
    AVPacket* pOutputPacket = av_packet_alloc();
    int64_t pts = 0;

    if (avio_open(&pOutputFormatContext->pb, "output.mov", AVIO_FLAG_WRITE) < 0) {
        printf("Failed to open output file\n");
        return -1;
    }

    int output_frame_number = 0;
    for (const auto& frame : directory_iterator("frames")) { // loops over the frames folder
        int width, height, channels;
        bmpData = stbi_load(frame.path().string().c_str(), &width, &height, &channels, 3);
        if (!bmpData) {
            printf("failed to load bmp file %s\n", frame.path().string().c_str());
            continue;
        }

        for (int y = 0; y < height; ++y) {
            uint8_t* src_row = bmpData + y * width * 3;
            uint8_t* dst_row = outputFrameRGB->data[0] + y * outputFrameRGB->linesize[0];
            memcpy(dst_row, src_row, width * 3);
        }

        sws_scale(sws_ctx, outputFrameRGB->data, outputFrameRGB->linesize, 0, height, outputFrameYUV->data, outputFrameYUV->linesize);

        outputFrameYUV->pts = pts++;

        printf("1\n");

        if (avcodec_send_frame(pOutputCodecContext, outputFrameYUV) >= 0) {
            while (true) {
                printf("2\n");
                int ret = avcodec_receive_packet(pOutputCodecContext, pOutputPacket);
                printf("3\n");
                if (ret == AVERROR(EAGAIN)) {
                    printf("4\n");
                    break;
                }  else if (ret == AVERROR_EOF) {
                    printf("5\n");
                    break;
                } else if (ret < 0) {
                    printf("error receiving packet\n");
                    printf("6\n");
                    break; // geniune error
                }

                printf("7\n");

                if (av_interleaved_write_frame(pOutputFormatContext, pOutputPacket) < 0) {
                    printf("error writing packets to output file\n");
                    av_packet_unref(pOutputPacket);
                    printf("8\n");
                    break;
                }

                printf("9\n");

                av_packet_unref(pOutputPacket);
                printf("10\n");
            }
            printf("11\n");
        } else {
            printf("error sending frame\n");
            printf("12\n");
            break;
        }

        printf("13\n");
        printf("frame %d\n", ++output_frame_number);
    }
    printf("14\n");

    // freeing memory and finalizing file
    stbi_image_free(bmpData);
    sws_freeContext(sws_ctx);
    av_frame_free(&outputFrameRGB);
    av_write_trailer(pOutputFormatContext);
    avcodec_free_context(&pOutputCodecContext);
    avio_close(pOutputFormatContext->pb);
    avformat_free_context(pOutputFormatContext);
    av_frame_free(&pFrame);
    av_frame_free(&pFrameRGB);
    avcodec_free_context(&pCodecContext);
    avformat_close_input(&pFormatContext);

    return 0;
}
